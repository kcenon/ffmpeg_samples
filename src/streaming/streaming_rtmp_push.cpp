/**
 * RTMP Streamer
 *
 * This sample demonstrates how to stream video to an RTMP server (e.g., YouTube
 * Live, Twitch, NGINX RTMP) using FFmpeg. It reads a local file and pushes it
 * to the server.
 */

#include "ffmpeg_wrappers.hpp"

#include <chrono>
#include <format>
#include <iostream>
#include <thread>

namespace {

class RtmpStreamer {
public:
  RtmpStreamer(std::string_view input_file, std::string_view rtmp_url)
      : rtmp_url_(rtmp_url),
        input_format_ctx_(ffmpeg::open_input_format(input_file.data())),
        packet_(ffmpeg::create_packet()) {

    initialize();
  }

  void stream() {
    std::cout << std::format("Streaming to {}\n", rtmp_url_);

    auto start_time = std::chrono::steady_clock::now();
    int64_t start_pts = 0;
    bool first_packet = true;

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

      // Basic synchronization to simulate real-time streaming
      if (packet_->pts != AV_NOPTS_VALUE) {
        if (first_packet) {
          start_pts = packet_->pts;
          first_packet = false;
        }

        // Calculate how long we should wait
        double pts_time =
            (packet_->pts - start_pts) * av_q2d(in_stream->time_base);
        auto now = std::chrono::steady_clock::now();
        double elapsed =
            std::chrono::duration<double>(now - start_time).count();

        if (pts_time > elapsed) {
          std::this_thread::sleep_for(
              std::chrono::duration<double>(pts_time - elapsed));
        }
      }

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
    }

    av_write_trailer(output_format_ctx_.get());
    std::cout << "Streaming finished.\n";
  }

private:
  void initialize() {
    // Output context for FLV/RTMP
    AVFormatContext *raw_out = nullptr;
    ffmpeg::check_error(avformat_alloc_output_context2(&raw_out, nullptr, "flv",
                                                       rtmp_url_.c_str()),
                        "create output context");
    output_format_ctx_.reset(raw_out);

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
      ffmpeg::check_error(avio_open(&output_format_ctx_->pb, rtmp_url_.c_str(),
                                    AVIO_FLAG_WRITE),
                          "open output url");
    }

    ffmpeg::check_error(
        avformat_write_header(output_format_ctx_.get(), nullptr),
        "write header");
  }

  std::string rtmp_url_;
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
    std::cerr << std::format("Usage: {} <input_file> <rtmp_url>\n", argv[0]);
    std::cerr << "Example: ... input.mp4 rtmp://localhost/live/stream\n";
    return 1;
  }

  try {
    RtmpStreamer streamer(argv[1], argv[2]);
    streamer.stream();
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
