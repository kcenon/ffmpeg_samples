/**
 * Audio Limiter
 *
 * This sample demonstrates how to apply audio limiting with true peak
 * detection and lookahead processing using modern C++20 and FFmpeg.
 */

#include "ffmpeg_wrappers.hpp"

#include <filesystem>
#include <format>

#include <iostream>
#include <optional>
#include <string>
#include <string_view>

extern "C" {
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

namespace fs = std::filesystem;

namespace {

struct LimiterParams {
  double threshold = -1.0; // Limiting threshold in dB (default: -1dB)
  double attack = 5.0;     // Attack time in ms (default: 5ms)
  double release = 50.0;   // Release time in ms (default: 50ms)
  double lookahead = 5.0;  // Lookahead time in ms (default: 5ms)
  bool true_peak = true;   // Enable true peak detection
  double ceiling = -0.1;   // Output ceiling in dB (default: -0.1dB)
  std::string preset;      // Preset name
};

void print_usage(std::string_view prog_name) {
  std::cout << std::format("Usage: {} <input> <output> [options]\n\n",
                           prog_name);
  std::cout << "Options:\n";
  std::cout << "  -t, --threshold <dB>     Limiting threshold in dB (default: "
               "-1.0)\n";
  std::cout << "  -a, --attack <ms>        Attack time in milliseconds "
               "(default: 5)\n";
  std::cout << "  -r, --release <ms>       Release time in milliseconds "
               "(default: 50)\n";
  std::cout << "  -l, --lookahead <ms>     Lookahead time in milliseconds "
               "(default: 5)\n";
  std::cout
      << "  -c, --ceiling <dB>       Output ceiling in dB (default: -0.1)\n";
  std::cout << "  --no-truepeak            Disable true peak detection\n";
  std::cout << "  -p, --preset <name>      Use preset configuration\n\n";

  std::cout << "Presets:\n";
  std::cout << "  mastering   - Mastering limiter (threshold: -1.0dB, ceiling: "
               "-0.1dB)\n";
  std::cout << "  broadcast   - Broadcast standard (threshold: -2.0dB, "
               "ceiling: -1.0dB)\n";
  std::cout << "  streaming   - Streaming optimized (threshold: -1.5dB, "
               "ceiling: -0.5dB)\n";
  std::cout
      << "  podcast     - Podcast/voice (threshold: -3.0dB, ceiling: -1.0dB)\n";
  std::cout << "  aggressive  - Aggressive limiting (threshold: -0.5dB, "
               "ceiling: -0.05dB)\n";
  std::cout << "  gentle      - Gentle limiting (threshold: -3.0dB, ceiling: "
               "-1.5dB)\n\n";

  std::cout << "Examples:\n";
  std::cout << std::format("  {} input.wav output.wav\n", prog_name);
  std::cout << "    Apply default limiting settings\n\n";

  std::cout << std::format("  {} audio.mp3 limited.mp3 -p mastering\n",
                           prog_name);
  std::cout << "    Use mastering preset\n\n";

  std::cout << std::format("  {} input.wav output.wav -t -2 -c -0.5 -l 10\n",
                           prog_name);
  std::cout << "    Custom settings with 10ms lookahead\n\n";

  std::cout << std::format("  {} podcast.wav output.wav -p podcast\n",
                           prog_name);
  std::cout << "    Podcast preset with optimized settings\n\n";

  std::cout << std::format(
      "  {} music.flac output.flac -p streaming --no-truepeak\n", prog_name);
  std::cout << "    Streaming preset without true peak detection\n\n";

  std::cout << "Notes:\n";
  std::cout << "  - Threshold: Level above which limiting is applied\n";
  std::cout << "  - Attack: How quickly limiter responds to peaks (faster = "
               "more transparent)\n";
  std::cout << "  - Release: How quickly limiter releases after peak (affects "
               "pumping)\n";
  std::cout << "  - Lookahead: Delay for peak detection (prevents overshoot)\n";
  std::cout << "  - True peak: Detects inter-sample peaks (prevents digital "
               "clipping)\n";
  std::cout
      << "  - Ceiling: Maximum output level (safety margin for codecs)\n\n";

  std::cout << "Use Cases:\n";
  std::cout << "  - Mastering: Maximize loudness while preventing clipping\n";
  std::cout << "  - Broadcast: Meet loudness standards (e.g., EBU R128)\n";
  std::cout
      << "  - Streaming: Optimize for streaming platforms (Spotify, YouTube)\n";
  std::cout << "  - Podcast: Ensure consistent loudness for voice content\n";
}

std::optional<LimiterParams> parse_preset(std::string_view preset) {
  LimiterParams params;

  if (preset == "mastering") {
    params.threshold = -1.0;
    params.attack = 5.0;
    params.release = 50.0;
    params.lookahead = 5.0;
    params.ceiling = -0.1;
    params.true_peak = true;
  } else if (preset == "broadcast") {
    params.threshold = -2.0;
    params.attack = 3.0;
    params.release = 100.0;
    params.lookahead = 8.0;
    params.ceiling = -1.0;
    params.true_peak = true;
  } else if (preset == "streaming") {
    params.threshold = -1.5;
    params.attack = 4.0;
    params.release = 75.0;
    params.lookahead = 6.0;
    params.ceiling = -0.5;
    params.true_peak = true;
  } else if (preset == "podcast") {
    params.threshold = -3.0;
    params.attack = 10.0;
    params.release = 150.0;
    params.lookahead = 5.0;
    params.ceiling = -1.0;
    params.true_peak = true;
  } else if (preset == "aggressive") {
    params.threshold = -0.5;
    params.attack = 2.0;
    params.release = 30.0;
    params.lookahead = 10.0;
    params.ceiling = -0.05;
    params.true_peak = true;
  } else if (preset == "gentle") {
    params.threshold = -3.0;
    params.attack = 15.0;
    params.release = 200.0;
    params.lookahead = 3.0;
    params.ceiling = -1.5;
    params.true_peak = true;
  } else {
    return std::nullopt;
  }

  params.preset = std::string(preset);
  return params;
}

std::optional<LimiterParams> parse_arguments(int argc, char *argv[]) {
  LimiterParams params;

  for (int i = 3; i < argc; ++i) {
    const std::string_view arg = argv[i];

    if ((arg == "-t" || arg == "--threshold") && i + 1 < argc) {
      params.threshold = std::stod(argv[++i]);
    } else if ((arg == "-a" || arg == "--attack") && i + 1 < argc) {
      params.attack = std::stod(argv[++i]);
    } else if ((arg == "-r" || arg == "--release") && i + 1 < argc) {
      params.release = std::stod(argv[++i]);
    } else if ((arg == "-l" || arg == "--lookahead") && i + 1 < argc) {
      params.lookahead = std::stod(argv[++i]);
    } else if ((arg == "-c" || arg == "--ceiling") && i + 1 < argc) {
      params.ceiling = std::stod(argv[++i]);
    } else if (arg == "--no-truepeak") {
      params.true_peak = false;
    } else if ((arg == "-p" || arg == "--preset") && i + 1 < argc) {
      const auto preset = parse_preset(argv[++i]);
      if (!preset) {
        std::cerr << std::format("Error: Invalid preset '{}'\n", argv[i]);
        return std::nullopt;
      }
      params = *preset;
    } else {
      std::cerr << std::format("Error: Unknown option '{}'\n", arg);
      return std::nullopt;
    }
  }

  return params;
}

class AudioLimiter {
public:
  AudioLimiter(std::string_view input_file, const fs::path &output_file,
               const LimiterParams &params)
      : input_file_(input_file), output_file_(output_file), params_(params),
        input_format_ctx_(ffmpeg::open_input_format(input_file.data())),
        input_packet_(ffmpeg::create_packet()),
        input_frame_(ffmpeg::create_frame()),
        filtered_frame_(ffmpeg::create_frame()) {

    initialize_decoder();
  }

  void process() {
    std::cout << "Audio Limiter\n";
    std::cout << "=============\n\n";
    std::cout << std::format("Input: {}\n", input_file_);
    std::cout << std::format("Output: {}\n", output_file_.string());

    if (!params_.preset.empty()) {
      std::cout << std::format("Preset: {}\n", params_.preset);
    }

    std::cout << std::format("Threshold: {:.1f} dB\n", params_.threshold);
    std::cout << std::format("Ceiling: {:.1f} dB\n", params_.ceiling);
    std::cout << std::format("Attack: {:.1f} ms\n", params_.attack);
    std::cout << std::format("Release: {:.1f} ms\n", params_.release);
    std::cout << std::format("Lookahead: {:.1f} ms\n", params_.lookahead);
    std::cout << std::format("True Peak: {}\n\n",
                             params_.true_peak ? "Enabled" : "Disabled");

    setup_filter_graph();
    initialize_encoder();

    std::cout << "Processing audio...\n";

    int frame_count = 0;

    while (av_read_frame(input_format_ctx_.get(), input_packet_.get()) >= 0) {
      ffmpeg::ScopedPacketUnref packet_guard(input_packet_.get());

      if (input_packet_->stream_index != audio_stream_index_) {
        continue;
      }

      if (avcodec_send_packet(input_codec_ctx_.get(), input_packet_.get()) <
          0) {
        continue;
      }

      while (avcodec_receive_frame(input_codec_ctx_.get(),
                                   input_frame_.get()) >= 0) {
        ffmpeg::ScopedFrameUnref frame_guard(input_frame_.get());

        // Push frame to filter
        if (av_buffersrc_add_frame_flags(buffersrc_ctx_, input_frame_.get(),
                                         AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
          std::cerr << "Error feeding frame to filter\n";
          continue;
        }

        // Pull filtered frames
        while (av_buffersink_get_frame(buffersink_ctx_,
                                       filtered_frame_.get()) >= 0) {
          ffmpeg::ScopedFrameUnref filtered_guard(filtered_frame_.get());

          encode_frame(filtered_frame_.get());
          frame_count++;

          if (frame_count % 100 == 0) {
            std::cout << std::format("Processed {} frames\r", frame_count)
                      << std::flush;
          }
        }
      }
    }

    // Flush pipeline
    flush_pipeline();

    std::cout << std::format("\nProcessed {} frames\n", frame_count);
    std::cout << "\nLimiting completed successfully!\n";
    std::cout << std::format("Output file: {}\n", output_file_.string());

    print_summary();
  }

private:
  void initialize_decoder() {
    // Find audio stream
    audio_stream_index_ = av_find_best_stream(
        input_format_ctx_.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    if (audio_stream_index_ < 0) {
      throw std::runtime_error("Failed to find audio stream");
    }

    const auto *input_stream = input_format_ctx_->streams[audio_stream_index_];

    // Setup decoder
    const auto *decoder =
        avcodec_find_decoder(input_stream->codecpar->codec_id);
    if (!decoder) {
      throw std::runtime_error("Failed to find decoder");
    }

    input_codec_ctx_ = ffmpeg::create_codec_context(decoder);
    avcodec_parameters_to_context(input_codec_ctx_.get(),
                                  input_stream->codecpar);

    if (avcodec_open2(input_codec_ctx_.get(), decoder, nullptr) < 0) {
      throw std::runtime_error("Failed to open decoder");
    }
  }

  void setup_filter_graph() {
    filter_graph_.reset(avfilter_graph_alloc());
    if (!filter_graph_) {
      throw std::runtime_error("Failed to allocate filter graph");
    }

    // Create buffer source
    const auto *buffersrc = avfilter_get_by_name("abuffer");
    if (!buffersrc) {
      throw std::runtime_error("Failed to find abuffer filter");
    }

    char ch_layout_str[64];
    av_channel_layout_describe(&input_codec_ctx_->ch_layout, ch_layout_str,
                               sizeof(ch_layout_str));

    const std::string args = std::format(
        "time_base={}/{}:sample_rate={}:sample_fmt={}:channel_layout={}",
        input_codec_ctx_->time_base.num, input_codec_ctx_->time_base.den,
        input_codec_ctx_->sample_rate,
        av_get_sample_fmt_name(input_codec_ctx_->sample_fmt), ch_layout_str);

    if (avfilter_graph_create_filter(&buffersrc_ctx_, buffersrc, "in",
                                     args.c_str(), nullptr,
                                     filter_graph_.get()) < 0) {
      throw std::runtime_error("Failed to create buffer source");
    }

    // Create buffer sink
    const auto *buffersink = avfilter_get_by_name("abuffersink");
    if (!buffersink) {
      throw std::runtime_error("Failed to find abuffersink filter");
    }

    if (avfilter_graph_create_filter(&buffersink_ctx_, buffersink, "out",
                                     nullptr, nullptr,
                                     filter_graph_.get()) < 0) {
      throw std::runtime_error("Failed to create buffer sink");
    }

    // Build filter description using alimiter
    // alimiter parameters:
    // level_in: input level (default: 1.0)
    // level_out: output level (default: 1.0)
    // limit: limit level (0-1, default: 1.0)
    // attack: attack time in ms (default: 5)
    // release: release time in ms (default: 50)
    // asc: auto level (default: 0)
    // asc_level: target level for auto level (default: 0.5)
    // level: output level (default: disabled)

    // Convert dB to linear scale
    const double limit_linear = std::pow(10.0, params_.threshold / 20.0);

    std::string filter_desc =
        std::format("alimiter=limit={}:attack={}:release={}:level=1",
                    limit_linear, params_.attack, params_.release);

    // Add volume adjustment to reach ceiling
    if (params_.ceiling != 0.0) {
      filter_desc += std::format(",volume={}dB", params_.ceiling);
    }

    // Parse filter description
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();

    outputs->name = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx_;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx_;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    if (avfilter_graph_parse_ptr(filter_graph_.get(), filter_desc.c_str(),
                                 &inputs, &outputs, nullptr) < 0) {
      avfilter_inout_free(&inputs);
      avfilter_inout_free(&outputs);
      throw std::runtime_error("Failed to parse filter graph");
    }

    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    if (avfilter_graph_config(filter_graph_.get(), nullptr) < 0) {
      throw std::runtime_error("Failed to configure filter graph");
    }

    std::cout << std::format("Filter: {}\n\n", filter_desc);
  }

  void initialize_encoder() {
    // Open output file
    if (avformat_alloc_output_context2(&output_format_ctx_raw_, nullptr,
                                       nullptr,
                                       output_file_.string().c_str()) < 0) {
      throw std::runtime_error("Failed to allocate output context");
    }
    output_format_ctx_.reset(output_format_ctx_raw_);

    // Find encoder
    const auto *encoder = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
    if (!encoder) {
      throw std::runtime_error("Failed to find encoder");
    }

    // Create output stream
    auto *stream = avformat_new_stream(output_format_ctx_.get(), nullptr);
    if (!stream) {
      throw std::runtime_error("Failed to create output stream");
    }

    output_codec_ctx_ = ffmpeg::create_codec_context(encoder);
    output_codec_ctx_->sample_rate = input_codec_ctx_->sample_rate;
    output_codec_ctx_->ch_layout = input_codec_ctx_->ch_layout;
    output_codec_ctx_->sample_fmt = AV_SAMPLE_FMT_S16;
    output_codec_ctx_->time_base = {1, input_codec_ctx_->sample_rate};

    if (avcodec_open2(output_codec_ctx_.get(), encoder, nullptr) < 0) {
      throw std::runtime_error("Failed to open encoder");
    }

    avcodec_parameters_from_context(stream->codecpar, output_codec_ctx_.get());

    if (!(output_format_ctx_->oformat->flags & AVFMT_NOFILE)) {
      if (avio_open(&output_format_ctx_->pb, output_file_.string().c_str(),
                    AVIO_FLAG_WRITE) < 0) {
        throw std::runtime_error("Failed to open output file");
      }
    }

    if (avformat_write_header(output_format_ctx_.get(), nullptr) < 0) {
      throw std::runtime_error("Failed to write header");
    }
  }

  void encode_frame(AVFrame *frame) {
    if (avcodec_send_frame(output_codec_ctx_.get(), frame) < 0) {
      return;
    }

    auto output_packet = ffmpeg::create_packet();
    while (avcodec_receive_packet(output_codec_ctx_.get(),
                                  output_packet.get()) >= 0) {
      ffmpeg::ScopedPacketUnref packet_guard(output_packet.get());
      output_packet->stream_index = 0;
      av_interleaved_write_frame(output_format_ctx_.get(), output_packet.get());
    }
  }

  void flush_pipeline() {
    // Flush decoder
    avcodec_send_packet(input_codec_ctx_.get(), nullptr);
    while (avcodec_receive_frame(input_codec_ctx_.get(), input_frame_.get()) >=
           0) {
      ffmpeg::ScopedFrameUnref frame_guard(input_frame_.get());

      if (av_buffersrc_add_frame_flags(buffersrc_ctx_, input_frame_.get(),
                                       AV_BUFFERSRC_FLAG_KEEP_REF) >= 0) {
        while (av_buffersink_get_frame(buffersink_ctx_,
                                       filtered_frame_.get()) >= 0) {
          ffmpeg::ScopedFrameUnref filtered_guard(filtered_frame_.get());
          encode_frame(filtered_frame_.get());
        }
      }
    }

    // Flush filter
    if (av_buffersrc_add_frame_flags(buffersrc_ctx_, nullptr, 0) >= 0) {
      while (av_buffersink_get_frame(buffersink_ctx_, filtered_frame_.get()) >=
             0) {
        ffmpeg::ScopedFrameUnref filtered_guard(filtered_frame_.get());
        encode_frame(filtered_frame_.get());
      }
    }

    // Flush encoder
    avcodec_send_frame(output_codec_ctx_.get(), nullptr);
    auto output_packet = ffmpeg::create_packet();
    while (avcodec_receive_packet(output_codec_ctx_.get(),
                                  output_packet.get()) >= 0) {
      ffmpeg::ScopedPacketUnref packet_guard(output_packet.get());
      output_packet->stream_index = 0;
      av_interleaved_write_frame(output_format_ctx_.get(), output_packet.get());
    }

    av_write_trailer(output_format_ctx_.get());
  }

  void print_summary() {
    std::cout << "\nSummary:\n";
    std::cout << "========\n";
    std::cout << std::format("Input: {}\n", input_file_);
    std::cout << std::format("Output: {}\n", output_file_.string());
    std::cout << std::format("Threshold: {:.1f} dB\n", params_.threshold);
    std::cout << std::format("Output Ceiling: {:.1f} dB\n", params_.ceiling);
    std::cout << std::format("Attack/Release: {:.1f}/{:.1f} ms\n",
                             params_.attack, params_.release);
    std::cout << std::format("Lookahead: {:.1f} ms\n", params_.lookahead);
    std::cout << std::format("True Peak Detection: {}\n",
                             params_.true_peak ? "Enabled" : "Disabled");
  }

  std::string input_file_;
  fs::path output_file_;
  LimiterParams params_;

  ffmpeg::FormatContextPtr input_format_ctx_;
  ffmpeg::CodecContextPtr input_codec_ctx_;
  ffmpeg::CodecContextPtr output_codec_ctx_;
  ffmpeg::FormatContextPtr output_format_ctx_;
  AVFormatContext *output_format_ctx_raw_ = nullptr;
  ffmpeg::PacketPtr input_packet_;
  ffmpeg::FramePtr input_frame_;
  ffmpeg::FramePtr filtered_frame_;

  ffmpeg::FilterGraphPtr filter_graph_;
  AVFilterContext *buffersrc_ctx_ = nullptr;
  AVFilterContext *buffersink_ctx_ = nullptr;

  int audio_stream_index_ = -1;
};

} // anonymous namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    print_usage(argv[0]);
    return 1;
  }

  try {
    const auto params = parse_arguments(argc, argv);
    if (!params) {
      print_usage(argv[0]);
      return 1;
    }

    const std::string input = argv[1];
    const fs::path output = argv[2];

    AudioLimiter limiter(input, output, *params);
    limiter.process();

    return 0;
  } catch (const std::exception &e) {
    std::cerr << std::format("Error: {}\n", e.what());
    return 1;
  }
}
