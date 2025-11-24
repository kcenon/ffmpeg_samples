/**
 * Audio Spectrum Visualizer
 *
 * This sample demonstrates how to create audio spectrum visualization videos
 * using modern C++20 and FFmpeg libraries.
 */

#include "ffmpeg_wrappers.hpp"

#include <filesystem>
#include <format>
#include <iostream>
#include <string_view>
#include <unordered_map>

namespace fs = std::filesystem;

namespace {

enum class VisualizationMode {
  SPECTRUM,
  WAVEFORM,
  SHOWCQT,
  SHOWFREQS,
  SHOWWAVES
};

VisualizationMode parse_mode(std::string_view mode_str) {
  static const std::unordered_map<std::string_view, VisualizationMode> modes = {
      {"spectrum", VisualizationMode::SPECTRUM},
      {"waveform", VisualizationMode::WAVEFORM},
      {"showcqt", VisualizationMode::SHOWCQT},
      {"showfreqs", VisualizationMode::SHOWFREQS},
      {"showwaves", VisualizationMode::SHOWWAVES}};

  const auto it = modes.find(mode_str);
  if (it == modes.end()) {
    throw std::invalid_argument(std::format("Invalid mode: {}", mode_str));
  }
  return it->second;
}

std::string get_filter_description(VisualizationMode mode, int width,
                                   int height) {
  switch (mode) {
  case VisualizationMode::SPECTRUM:
    return std::format(
        "showspectrum=s={}x{}:mode=combined:color=channel:scale=cbrt", width,
        height);
  case VisualizationMode::WAVEFORM:
    return std::format(
        "showwaves=s={}x{}:mode=cline:colors=red|green|blue|yellow", width,
        height);
  case VisualizationMode::SHOWCQT:
    return std::format(
        "showcqt=s={}x{}:fps=30:sono_h=0:bar_h=16:axis_h=0:font=''", width,
        height);
  case VisualizationMode::SHOWFREQS:
    return std::format("showfreqs=s={}x{}:mode=bar:cmode=combined:minamp=1e-6",
                       width, height);
  case VisualizationMode::SHOWWAVES:
    return std::format(
        "showwaves=s={}x{}:mode=p2p:colors=0xff0000|0x00ff00|0x0000ff", width,
        height);
  default:
    return std::format("showspectrum=s={}x{}", width, height);
  }
}

class AudioSpectrumVisualizer {
public:
  AudioSpectrumVisualizer(std::string_view input_audio,
                          const fs::path &output_video, VisualizationMode mode,
                          int width, int height, int fps)
      : input_audio_(input_audio), output_video_(output_video), mode_(mode),
        width_(width), height_(height), fps_(fps),
        format_ctx_(ffmpeg::open_input_format(input_audio.data())),
        packet_(ffmpeg::create_packet()) {

    initialize();
  }

  void generate() {
    std::cout << "Audio Spectrum Visualization\n";
    std::cout << "============================\n\n";
    std::cout << std::format("Input audio: {}\n", input_audio_);
    std::cout << std::format("Output video: {}\n", output_video_.string());
    std::cout << std::format("Resolution: {}x{}\n", width_, height_);
    std::cout << std::format("FPS: {}\n", fps_);
    std::cout << std::format("Sample rate: {} Hz\n\n", codec_ctx_->sample_rate);

    // Create output context
    AVFormatContext *output_ctx_raw = nullptr;
    ffmpeg::check_error(
        avformat_alloc_output_context2(&output_ctx_raw, nullptr, nullptr,
                                       output_video_.string().c_str()),
        "allocate output context");
    auto output_ctx = ffmpeg::FormatContextPtr(output_ctx_raw);

    // Create video stream
    auto *out_stream = avformat_new_stream(output_ctx.get(), nullptr);
    if (!out_stream) {
      throw ffmpeg::FFmpegError("Failed to create output stream");
    }

    // Setup encoder
    const auto *encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!encoder) {
      throw ffmpeg::FFmpegError("H.264 encoder not found");
    }

    encoder_ctx_ = ffmpeg::create_codec_context(encoder);
    encoder_ctx_->width = width_;
    encoder_ctx_->height = height_;
    encoder_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
    encoder_ctx_->time_base = AVRational{1, fps_};
    encoder_ctx_->framerate = AVRational{fps_, 1};
    encoder_ctx_->bit_rate = 2000000;

    if (output_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
      encoder_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    ffmpeg::check_error(avcodec_open2(encoder_ctx_.get(), encoder, nullptr),
                        "open encoder");

    ffmpeg::check_error(avcodec_parameters_from_context(out_stream->codecpar,
                                                        encoder_ctx_.get()),
                        "copy encoder parameters");

    out_stream->time_base = encoder_ctx_->time_base;

    // Open output file
    if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
      ffmpeg::check_error(avio_open(&output_ctx->pb,
                                    output_video_.string().c_str(),
                                    AVIO_FLAG_WRITE),
                          "open output file");
    }

    // Write header
    ffmpeg::check_error(avformat_write_header(output_ctx.get(), nullptr),
                        "write header");

    // Process audio and generate visualization
    std::cout << "Generating visualization...\n";

    int frame_count = 0;
    auto video_frame = ffmpeg::create_frame();

    while (av_read_frame(format_ctx_.get(), packet_.get()) >= 0) {
      ffmpeg::ScopedPacketUnref packet_guard(packet_.get());

      if (packet_->stream_index != audio_stream_index_) {
        continue;
      }

      const auto ret = avcodec_send_packet(codec_ctx_.get(), packet_.get());
      if (ret < 0) {
        continue;
      }

      while (true) {
        auto audio_frame = ffmpeg::create_frame();
        const auto recv_ret =
            avcodec_receive_frame(codec_ctx_.get(), audio_frame.get());

        if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF) {
          break;
        }

        if (recv_ret < 0) {
          break;
        }

        ffmpeg::ScopedFrameUnref audio_guard(audio_frame.get());

        // Push audio frame through filter
        ffmpeg::check_error(
            av_buffersrc_add_frame_flags(buffersrc_ctx_, audio_frame.get(),
                                         AV_BUFFERSRC_FLAG_KEEP_REF),
            "feed audio frame to filter");

        // Get video frames from filter
        while (true) {
          const auto filter_ret =
              av_buffersink_get_frame(buffersink_ctx_, video_frame.get());

          if (filter_ret == AVERROR(EAGAIN) || filter_ret == AVERROR_EOF) {
            break;
          }

          if (filter_ret < 0) {
            break;
          }

          ffmpeg::ScopedFrameUnref video_guard(video_frame.get());

          // Set correct PTS
          video_frame->pts = frame_count++;

          // Encode and write frame
          encode_write_frame(output_ctx.get(), out_stream, video_frame.get());

          if (frame_count % 30 == 0) {
            const auto seconds = frame_count / static_cast<double>(fps_);
            std::cout << std::format("Generated {:.2f} seconds\r", seconds)
                      << std::flush;
          }
        }
      }
    }

    // Flush filter
    if (av_buffersrc_add_frame_flags(buffersrc_ctx_, nullptr, 0) < 0) {
      std::cerr << "Error flushing filter graph\n";
    }

    while (true) {
      const auto filter_ret =
          av_buffersink_get_frame(buffersink_ctx_, video_frame.get());

      if (filter_ret == AVERROR(EAGAIN) || filter_ret == AVERROR_EOF) {
        break;
      }

      if (filter_ret < 0) {
        break;
      }

      ffmpeg::ScopedFrameUnref video_guard(video_frame.get());

      video_frame->pts = frame_count++;
      encode_write_frame(output_ctx.get(), out_stream, video_frame.get());
    }

    // Flush encoder
    flush_encoder(output_ctx.get(), out_stream);

    // Write trailer
    ffmpeg::check_error(av_write_trailer(output_ctx.get()), "write trailer");

    const auto duration = frame_count / static_cast<double>(fps_);
    std::cout << std::format("\n\nTotal frames: {}\n", frame_count);
    std::cout << std::format("Duration: {:.2f} seconds\n", duration);
    std::cout << std::format("✓ Visualization generated successfully\n");
    std::cout << std::format("Output file: {}\n", output_video_.string());
  }

private:
  void initialize() {
    // Find audio stream
    const auto stream_idx =
        ffmpeg::find_stream_index(format_ctx_.get(), AVMEDIA_TYPE_AUDIO);
    if (!stream_idx) {
      throw ffmpeg::FFmpegError("No audio stream found");
    }
    audio_stream_index_ = *stream_idx;

    // Setup decoder
    const auto *codecpar = format_ctx_->streams[audio_stream_index_]->codecpar;
    const auto *decoder = avcodec_find_decoder(codecpar->codec_id);
    if (!decoder) {
      throw ffmpeg::FFmpegError("Audio decoder not found");
    }

    codec_ctx_ = ffmpeg::create_codec_context(decoder);
    ffmpeg::check_error(
        avcodec_parameters_to_context(codec_ctx_.get(), codecpar),
        "copy decoder parameters");
    ffmpeg::check_error(avcodec_open2(codec_ctx_.get(), decoder, nullptr),
                        "open decoder");

    // Initialize visualization filter
    initialize_filter();
  }

  void initialize_filter() {
    const auto *abuffersrc = avfilter_get_by_name("abuffer");
    const auto *buffersink = avfilter_get_by_name("buffersink");

    filter_graph_.reset(avfilter_graph_alloc());
    if (!filter_graph_) {
      throw ffmpeg::FFmpegError("Failed to allocate filter graph");
    }

    // Create audio buffer source
    char ch_layout[64];
    av_channel_layout_describe(&codec_ctx_->ch_layout, ch_layout,
                               sizeof(ch_layout));

    const auto args = std::format(
        "sample_rate={}:sample_fmt={}:channel_layout={}:time_base={}/{}",
        codec_ctx_->sample_rate, av_get_sample_fmt_name(codec_ctx_->sample_fmt),
        ch_layout, 1, codec_ctx_->sample_rate);

    ffmpeg::check_error(avfilter_graph_create_filter(
                            &buffersrc_ctx_, abuffersrc, "in", args.c_str(),
                            nullptr, filter_graph_.get()),
                        "create audio buffer source");

    // Create video buffer sink
    ffmpeg::check_error(
        avfilter_graph_create_filter(&buffersink_ctx_, buffersink, "out",
                                     nullptr, nullptr, filter_graph_.get()),
        "create buffer sink");

    // Setup filter graph
    const auto filter_desc = get_filter_description(mode_, width_, height_);

    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();

    if (!outputs || !inputs) {
      avfilter_inout_free(&inputs);
      avfilter_inout_free(&outputs);
      throw ffmpeg::FFmpegError("Failed to allocate filter I/O");
    }

    outputs->name = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx_;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx_;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    const auto ret = avfilter_graph_parse_ptr(
        filter_graph_.get(), filter_desc.c_str(), &inputs, &outputs, nullptr);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    ffmpeg::check_error(ret, "parse filter graph");

    ffmpeg::check_error(avfilter_graph_config(filter_graph_.get(), nullptr),
                        "configure filter graph");
  }

  void encode_write_frame(AVFormatContext *output_ctx, AVStream *out_stream,
                          AVFrame *frame) {
    auto encoded_packet = ffmpeg::create_packet();

    const auto ret = avcodec_send_frame(encoder_ctx_.get(), frame);
    if (ret < 0) {
      return;
    }

    while (avcodec_receive_packet(encoder_ctx_.get(), encoded_packet.get()) >=
           0) {
      ffmpeg::ScopedPacketUnref packet_guard(encoded_packet.get());

      av_packet_rescale_ts(encoded_packet.get(), encoder_ctx_->time_base,
                           out_stream->time_base);
      encoded_packet->stream_index = out_stream->index;

      ffmpeg::check_error(
          av_interleaved_write_frame(output_ctx, encoded_packet.get()),
          "write frame");
    }
  }

  void flush_encoder(AVFormatContext *output_ctx, AVStream *out_stream) {
    avcodec_send_frame(encoder_ctx_.get(), nullptr);

    auto encoded_packet = ffmpeg::create_packet();
    while (avcodec_receive_packet(encoder_ctx_.get(), encoded_packet.get()) >=
           0) {
      ffmpeg::ScopedPacketUnref packet_guard(encoded_packet.get());

      av_packet_rescale_ts(encoded_packet.get(), encoder_ctx_->time_base,
                           out_stream->time_base);
      encoded_packet->stream_index = out_stream->index;

      av_interleaved_write_frame(output_ctx, encoded_packet.get());
    }
  }

  std::string input_audio_;
  fs::path output_video_;
  VisualizationMode mode_;
  int width_;
  int height_;
  int fps_;
  int audio_stream_index_ = -1;

  ffmpeg::FormatContextPtr format_ctx_;
  ffmpeg::CodecContextPtr codec_ctx_;
  ffmpeg::CodecContextPtr encoder_ctx_;
  ffmpeg::FilterGraphPtr filter_graph_;
  ffmpeg::PacketPtr packet_;

  AVFilterContext *buffersrc_ctx_ = nullptr;
  AVFilterContext *buffersink_ctx_ = nullptr;
};

void print_usage(std::string_view prog_name) {
  std::cout << std::format("Usage: {} <input_audio> <output_video> <mode> "
                           "[width] [height] [fps]\n\n",
                           prog_name);
  std::cout << "Visualization Modes:\n";
  std::cout << "  spectrum    - Frequency spectrum (default)\n";
  std::cout << "  waveform    - Waveform display\n";
  std::cout << "  showcqt     - Constant Q Transform spectrum\n";
  std::cout << "  showfreqs   - Frequency bars\n";
  std::cout << "  showwaves   - Waveform with multiple styles\n\n";
  std::cout << "Parameters:\n";
  std::cout << "  width       - Video width (default: 1280)\n";
  std::cout << "  height      - Video height (default: 720)\n";
  std::cout << "  fps         - Frame rate (default: 30)\n\n";
  std::cout << "Examples:\n";
  std::cout << std::format("  {} music.mp3 spectrum.mp4 spectrum\n", prog_name);
  std::cout << std::format(
      "  {} audio.wav waveform.mp4 waveform 1920 1080 60\n", prog_name);
  std::cout << std::format("  {} song.flac visual.mp4 showcqt 1280 720\n",
                           prog_name);
}

} // anonymous namespace

int main(int argc, char *argv[]) {
  if (argc < 4) {
    print_usage(argv[0]);
    return 1;
  }

  try {
    const std::string_view input_audio{argv[1]};
    const fs::path output_video{argv[2]};
    const auto mode = parse_mode(argv[3]);
    const int width = argc > 4 ? std::atoi(argv[4]) : 1280;
    const int height = argc > 5 ? std::atoi(argv[5]) : 720;
    const int fps = argc > 6 ? std::atoi(argv[6]) : 30;

    AudioSpectrumVisualizer visualizer(input_audio, output_video, mode, width,
                                       height, fps);
    visualizer.generate();

  } catch (const ffmpeg::FFmpegError &e) {
    std::cerr << std::format("FFmpeg error: {}\n", e.what());
    return 1;
  } catch (const std::exception &e) {
    std::cerr << std::format("Error: {}\n", e.what());
    return 1;
  }

  return 0;
}
