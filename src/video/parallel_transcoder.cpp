/**
 * Parallel Video Transcoder
 *
 * This sample demonstrates how to transcode multiple video files concurrently
 * using std::thread and independent FFmpeg contexts.
 */

#include "ffmpeg_wrappers.hpp"

#include <filesystem>
#include <format>
#include <iostream>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::mutex print_mutex;

void safe_print(std::string_view message) {
  std::lock_guard<std::mutex> lock(print_mutex);
  std::cout << message << std::flush;
}

class Transcoder {
public:
  Transcoder(const fs::path &input_file, const fs::path &output_file)
      : input_file_(input_file), output_file_(output_file),
        input_format_ctx_(ffmpeg::open_input_format(input_file.c_str())),
        packet_(ffmpeg::create_packet()), frame_(ffmpeg::create_frame()) {

    initialize();
  }

  void run() {
    safe_print(std::format("[{}] Starting transcoding...\n",
                           input_file_.filename().string()));

    int frame_count = 0;

    while (av_read_frame(input_format_ctx_.get(), packet_.get()) >= 0) {
      ffmpeg::ScopedPacketUnref packet_guard(packet_.get());

      if (packet_->stream_index != video_stream_index_) {
        continue;
      }

      if (avcodec_send_packet(decoder_ctx_.get(), packet_.get()) < 0) {
        continue;
      }

      while (true) {
        int ret = avcodec_receive_frame(decoder_ctx_.get(), frame_.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
          break;
        if (ret < 0)
          break;

        ffmpeg::ScopedFrameUnref frame_guard(frame_.get());

        // Encode
        encode_frame(frame_.get());
        frame_count++;
      }
    }

    flush_encoder();
    av_write_trailer(output_format_ctx_.get());

    safe_print(std::format("[{}] Finished! Processed {} frames.\n",
                           input_file_.filename().string(), frame_count));
  }

private:
  void initialize() {
    // Input
    auto stream_idx =
        ffmpeg::find_stream_index(input_format_ctx_.get(), AVMEDIA_TYPE_VIDEO);
    if (!stream_idx)
      throw std::runtime_error("No video stream");
    video_stream_index_ = *stream_idx;

    auto *in_codecpar =
        input_format_ctx_->streams[video_stream_index_]->codecpar;
    auto *decoder = avcodec_find_decoder(in_codecpar->codec_id);

    decoder_ctx_ = ffmpeg::create_codec_context(decoder);
    avcodec_parameters_to_context(decoder_ctx_.get(), in_codecpar);
    avcodec_open2(decoder_ctx_.get(), decoder, nullptr);

    // Output
    AVFormatContext *raw_out = nullptr;
    avformat_alloc_output_context2(&raw_out, nullptr, nullptr,
                                   output_file_.c_str());
    output_format_ctx_.reset(raw_out);

    auto *encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    output_stream_ = avformat_new_stream(output_format_ctx_.get(), nullptr);

    encoder_ctx_ = ffmpeg::create_codec_context(encoder);
    encoder_ctx_->width = decoder_ctx_->width;
    encoder_ctx_->height = decoder_ctx_->height;
    encoder_ctx_->time_base = {1, 30};
    encoder_ctx_->framerate = {30, 1};
    encoder_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
    encoder_ctx_->bit_rate = 1000000;

    if (output_format_ctx_->oformat->flags & AVFMT_GLOBALHEADER)
      encoder_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    avcodec_open2(encoder_ctx_.get(), encoder, nullptr);
    avcodec_parameters_from_context(output_stream_->codecpar,
                                    encoder_ctx_.get());
    output_stream_->time_base = encoder_ctx_->time_base;

    if (!(output_format_ctx_->oformat->flags & AVFMT_NOFILE)) {
      avio_open(&output_format_ctx_->pb, output_file_.c_str(), AVIO_FLAG_WRITE);
    }
    ffmpeg::check_error(
        avformat_write_header(output_format_ctx_.get(), nullptr),
        "write header");
  }

  void encode_frame(AVFrame *frame) {
    auto packet = ffmpeg::create_packet();
    if (avcodec_send_frame(encoder_ctx_.get(), frame) < 0)
      return;

    while (true) {
      int ret = avcodec_receive_packet(encoder_ctx_.get(), packet.get());
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        break;
      if (ret < 0)
        break;

      ffmpeg::ScopedPacketUnref packet_guard(packet.get());
      av_packet_rescale_ts(packet.get(), encoder_ctx_->time_base,
                           output_stream_->time_base);
      packet->stream_index = 0;
      av_interleaved_write_frame(output_format_ctx_.get(), packet.get());
    }
  }

  void flush_encoder() { encode_frame(nullptr); }

  fs::path input_file_;
  fs::path output_file_;
  int video_stream_index_ = -1;

  ffmpeg::FormatContextPtr input_format_ctx_;
  ffmpeg::FormatContextPtr output_format_ctx_;
  ffmpeg::CodecContextPtr decoder_ctx_;
  ffmpeg::CodecContextPtr encoder_ctx_;
  ffmpeg::PacketPtr packet_;
  ffmpeg::FramePtr frame_;
  AVStream *output_stream_ = nullptr;
};

void worker(fs::path input, fs::path output) {
  try {
    Transcoder transcoder(input, output);
    transcoder.run();
  } catch (const std::exception &e) {
    safe_print(
        std::format("[{}] Error: {}\n", input.filename().string(), e.what()));
  }
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::cout << "Usage: " << argv[0]
              << " <input_file1> [input_file2 ...] <output_dir>\n";
    return 1;
  }

  fs::path output_dir = argv[argc - 1];
  fs::create_directories(output_dir);

  std::vector<std::thread> threads;

  // Launch threads for each input file
  for (int i = 1; i < argc - 1; ++i) {
    fs::path input_path = argv[i];
    fs::path output_path =
        output_dir / ("transcoded_" + input_path.filename().string());

    threads.emplace_back(worker, input_path, output_path);
  }

  std::cout << "Launched " << threads.size() << " transcoding jobs...\n";

  for (auto &t : threads) {
    if (t.joinable())
      t.join();
  }

  std::cout << "All jobs completed.\n";
  return 0;
}
