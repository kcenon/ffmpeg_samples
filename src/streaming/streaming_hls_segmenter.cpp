/**
 * HLS Segmenter
 *
 * This sample demonstrates how to segment a video file into HLS (HTTP Live
 * Streaming) format (.m3u8 playlist + .ts segments) for web playback.
 */

#include "ffmpeg_wrappers.hpp"

#include <format>
#include <iostream>
#include <string_view>

namespace {

class HlsSegmenter {
public:
  HlsSegmenter(std::string_view input_file, std::string_view output_playlist,
               int segment_duration)
      : output_playlist_(output_playlist), segment_duration_(segment_duration),
        input_format_ctx_(ffmpeg::open_input_format(input_file.data())),
        packet_(ffmpeg::create_packet()) {

    initialize();
  }

  void segment() {
    std::cout << std::format("Segmenting {} to HLS...\n",
                             input_format_ctx_->url);
    std::cout << std::format("Output playlist: {}\n", output_playlist_);
    std::cout << std::format("Segment duration: {} seconds\n",
                             segment_duration_);

    int frame_count = 0;

    while (av_read_frame(input_format_ctx_.get(), packet_.get()) >= 0) {
      ffmpeg::ScopedPacketUnref packet_guard(packet_.get());

      // Check if packet belongs to a stream we are forwarding
      int out_stream_idx = -1;
      if (packet_->stream_index == video_stream_index_) {
        out_stream_idx = out_video_stream_index_;
      } else if (packet_->stream_index == audio_stream_index_) {
        out_stream_idx = out_audio_stream_index_;
      } else {
        continue;
      }

      AVStream *in_stream = input_format_ctx_->streams[packet_->stream_index];
      AVStream *out_stream = output_format_ctx_->streams[out_stream_idx];

      // Rescale timestamps
      av_packet_rescale_ts(packet_.get(), in_stream->time_base,
                           out_stream->time_base);
      packet_->pos = -1;
      packet_->stream_index = out_stream_idx;

      // Send packet
      if (av_interleaved_write_frame(output_format_ctx_.get(), packet_.get()) <
          0) {
        std::cerr << "Error sending packet\n";
        break;
      }

      frame_count++;
      if (frame_count % 100 == 0) {
        std::cout << std::format("Processed {} packets\r", frame_count)
                  << std::flush;
      }
    }

    av_write_trailer(output_format_ctx_.get());
    std::cout << std::format(
        "\nSegmentation finished. Playlist created at {}\n", output_playlist_);
  }

private:
  void initialize() {
    // Output context for HLS
    AVFormatContext *raw_out = nullptr;
    ffmpeg::check_error(avformat_alloc_output_context2(
                            &raw_out, nullptr, "hls", output_playlist_.c_str()),
                        "create output context");
    output_format_ctx_.reset(raw_out);

    // Set HLS options
    av_opt_set_int(output_format_ctx_->priv_data, "hls_time", segment_duration_,
                   0);
    av_opt_set_int(output_format_ctx_->priv_data, "hls_list_size", 0,
                   0); // 0 means keep all segments
    av_opt_set(output_format_ctx_->priv_data, "hls_segment_filename",
               (output_playlist_ + "_%03d.ts").c_str(), 0);

    // Copy streams
    for (unsigned int i = 0; i < input_format_ctx_->nb_streams; i++) {
      AVStream *in_stream = input_format_ctx_->streams[i];
      AVCodecParameters *in_codecpar = in_stream->codecpar;

      if (in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
          in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
        continue;
      }

      AVStream *out_stream =
          avformat_new_stream(output_format_ctx_.get(), nullptr);
      if (!out_stream)
        throw std::runtime_error("Failed to create output stream");

      ffmpeg::check_error(
          avcodec_parameters_copy(out_stream->codecpar, in_codecpar),
          "copy codec params");

      out_stream->codecpar->codec_tag = 0;

      if (in_codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        video_stream_index_ = i;
        out_video_stream_index_ = out_stream->index;
      } else if (in_codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        audio_stream_index_ = i;
        out_audio_stream_index_ = out_stream->index;
      }
    }

    if (!(output_format_ctx_->oformat->flags & AVFMT_NOFILE)) {
      ffmpeg::check_error(avio_open(&output_format_ctx_->pb,
                                    output_playlist_.c_str(), AVIO_FLAG_WRITE),
                          "open output file");
    }

    ffmpeg::check_error(
        avformat_write_header(output_format_ctx_.get(), nullptr),
        "write header");
  }

  std::string output_playlist_;
  int segment_duration_;
  int video_stream_index_ = -1;
  int audio_stream_index_ = -1;
  int out_video_stream_index_ = -1;
  int out_audio_stream_index_ = -1;

  ffmpeg::FormatContextPtr input_format_ctx_;
  ffmpeg::FormatContextPtr output_format_ctx_;
  ffmpeg::PacketPtr packet_;
};

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::cerr << std::format(
        "Usage: {} <input_file> <output_playlist.m3u8> [segment_duration]\n",
        argv[0]);
    return 1;
  }

  try {
    const int duration = argc > 3 ? std::atoi(argv[3]) : 4;
    HlsSegmenter segmenter(argv[1], argv[2], duration);
    segmenter.segment();
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
