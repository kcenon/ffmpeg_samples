/**
 * Microphone Capture
 *
 * This sample demonstrates how to capture audio from a microphone using
 * AVDevice.
 */

#include "ffmpeg_wrappers.hpp"

#include <format>
#include <iostream>
#include <string_view>

extern "C" {
#include <libavdevice/avdevice.h>
}

namespace {

class MicCapture {
public:
  MicCapture(std::string_view format_name, std::string_view device_name,
             std::string_view output_file)
      : output_file_(output_file) {

    avdevice_register_all();

    const AVInputFormat *ifmt = av_find_input_format(format_name.data());
    if (!ifmt) {
      throw ffmpeg::FFmpegError(
          std::format("Input format '{}' not found", format_name));
    }

    AVFormatContext *raw_ctx = nullptr;
    ffmpeg::check_error(
        avformat_open_input(&raw_ctx, device_name.data(), ifmt, nullptr),
        "open input device");
    input_format_ctx_.reset(raw_ctx);

    if (avformat_find_stream_info(input_format_ctx_.get(), nullptr) < 0) {
      throw ffmpeg::FFmpegError("Failed to find stream info");
    }

    initialize_output();
  }

  void capture(int duration_seconds) {
    std::cout << std::format(
        "Capturing audio from {} to {} for {} seconds...\n",
        input_format_ctx_->url, output_file_, duration_seconds);

    int frame_count = 0;
    auto start_time = std::chrono::steady_clock::now();

    while (true) {
      auto now = std::chrono::steady_clock::now();
      if (std::chrono::duration_cast<std::chrono::seconds>(now - start_time)
              .count() >= duration_seconds) {
        break;
      }

      if (av_read_frame(input_format_ctx_.get(), packet_.get()) < 0) {
        break;
      }

      ffmpeg::ScopedPacketUnref packet_guard(packet_.get());

      if (packet_->stream_index == audio_stream_index_) {
        av_packet_rescale_ts(
            packet_.get(),
            input_format_ctx_->streams[audio_stream_index_]->time_base,
            output_stream_->time_base);

        packet_->stream_index = output_stream_->index;

        if (av_interleaved_write_frame(output_format_ctx_.get(),
                                       packet_.get()) < 0) {
          std::cerr << "Error writing frame\n";
          break;
        }

        frame_count++;
        if (frame_count % 50 == 0) {
          std::cout << std::format("Captured {} frames\r", frame_count)
                    << std::flush;
        }
      }
    }

    av_write_trailer(output_format_ctx_.get());
    std::cout << std::format("\nCapture finished. Total frames: {}\n",
                             frame_count);
  }

private:
  void initialize_output() {
    const auto stream_idx =
        ffmpeg::find_stream_index(input_format_ctx_.get(), AVMEDIA_TYPE_AUDIO);
    if (!stream_idx)
      throw ffmpeg::FFmpegError("No audio stream found");
    audio_stream_index_ = *stream_idx;

    AVFormatContext *raw_out = nullptr;
    ffmpeg::check_error(avformat_alloc_output_context2(
                            &raw_out, nullptr, nullptr, output_file_.c_str()),
                        "create output context");
    output_format_ctx_.reset(raw_out);

    output_stream_ = avformat_new_stream(output_format_ctx_.get(), nullptr);

    AVCodecParameters *in_par =
        input_format_ctx_->streams[audio_stream_index_]->codecpar;
    avcodec_parameters_copy(output_stream_->codecpar, in_par);

    if (!(output_format_ctx_->oformat->flags & AVFMT_NOFILE)) {
      avio_open(&output_format_ctx_->pb, output_file_.c_str(), AVIO_FLAG_WRITE);
    }
    ffmpeg::check_error(
        avformat_write_header(output_format_ctx_.get(), nullptr),
        "write header");
  }

  std::string output_file_;
  int audio_stream_index_ = -1;

  ffmpeg::FormatContextPtr input_format_ctx_;
  ffmpeg::FormatContextPtr output_format_ctx_;
  ffmpeg::PacketPtr packet_ = ffmpeg::create_packet();
  AVStream *output_stream_ = nullptr;
};

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 4) {
    std::cerr << std::format(
        "Usage: {} <format> <device_name> <output_file> [duration]\n", argv[0]);
    std::cerr << "Examples:\n";
    std::cerr << std::format("  macOS:   {} avfoundation \":0\" output.wav\n",
                             argv[0]);
    std::cerr << std::format("  Linux:   {} alsa hw:0 output.wav\n", argv[0]);
    return 1;
  }

  try {
    int duration = argc > 4 ? std::atoi(argv[4]) : 10;
    MicCapture capture(argv[1], argv[2], argv[3]);
    capture.capture(duration);
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
