/**
 * Audio Pitch Shift
 *
 * This sample demonstrates how to shift the pitch of audio while
 * maintaining tempo using modern C++20 and FFmpeg libraries.
 */

#include "ffmpeg_wrappers.hpp"

#include <cmath>
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

struct PitchShiftParams {
  double semitones = 0.0;     // Pitch shift in semitones (default: 0)
  bool preserve_tempo = true; // Preserve original tempo (default: true)
  int sample_rate = 0;        // Override sample rate (0 = auto)
  std::string preset;         // Preset name
};

void print_usage(std::string_view prog_name) {
  std::cout << std::format("Usage: {} <input> <output> [options]\n\n",
                           prog_name);
  std::cout << "Options:\n";
  std::cout
      << "  -s, --semitones <value>   Pitch shift in semitones (default: 0)\n";
  std::cout << "                              Positive = higher pitch\n";
  std::cout << "                              Negative = lower pitch\n";
  std::cout << "  -t, --no-tempo            Don't preserve tempo (speed will "
               "change)\n";
  std::cout << "  -r, --rate <hz>           Override sample rate\n";
  std::cout << "  -p, --preset <name>       Use preset configuration\n\n";

  std::cout << "Presets:\n";
  std::cout << "  octave_up     - Shift up one octave (+12 semitones)\n";
  std::cout << "  octave_down   - Shift down one octave (-12 semitones)\n";
  std::cout << "  fifth_up      - Perfect fifth up (+7 semitones)\n";
  std::cout << "  fourth_up     - Perfect fourth up (+5 semitones)\n";
  std::cout << "  male_female   - Male to female voice (+5 semitones)\n";
  std::cout << "  female_male   - Female to male voice (-5 semitones)\n";
  std::cout << "  chipmunk      - Chipmunk effect (+12 semitones, no tempo "
               "preserve)\n";
  std::cout << "  deep          - Deep voice (-7 semitones)\n\n";

  std::cout << "Examples:\n";
  std::cout << std::format("  {} input.wav output.wav -s 2\n", prog_name);
  std::cout << "    Shift pitch up 2 semitones (whole step)\n\n";

  std::cout << std::format("  {} audio.mp3 shifted.mp3 -s -3\n", prog_name);
  std::cout << "    Shift pitch down 3 semitones (minor third)\n\n";

  std::cout << std::format("  {} vocal.wav higher.wav -p octave_up\n",
                           prog_name);
  std::cout << "    Shift up one octave\n\n";

  std::cout << std::format("  {} voice.wav deep.wav -p deep\n", prog_name);
  std::cout << "    Apply deep voice effect\n\n";

  std::cout << std::format("  {} music.flac pitched.flac -s 5 -t\n", prog_name);
  std::cout
      << "    Shift 5 semitones without preserving tempo (speed changes)\n\n";

  std::cout << "Notes:\n";
  std::cout << "  - Semitones: Musical interval (12 semitones = 1 octave)\n";
  std::cout
      << "  - Tempo preservation: Keeps duration same, only changes pitch\n";
  std::cout << "  - Without tempo preservation: Both pitch and speed change\n";
  std::cout << "  - Common intervals:\n";
  std::cout << "      +1 = Minor second\n";
  std::cout << "      +2 = Major second (whole step)\n";
  std::cout << "      +3 = Minor third\n";
  std::cout << "      +4 = Major third\n";
  std::cout << "      +5 = Perfect fourth\n";
  std::cout << "      +7 = Perfect fifth\n";
  std::cout << "      +12 = Octave\n\n";

  std::cout << "Use Cases:\n";
  std::cout << "  - Vocal tuning and correction\n";
  std::cout << "  - Musical transposition\n";
  std::cout << "  - Voice character modification\n";
  std::cout << "  - Audio restoration and matching\n";
  std::cout << "  - Special effects (chipmunk, deep voice)\n";
}

std::optional<PitchShiftParams> parse_preset(std::string_view preset) {
  PitchShiftParams params;

  if (preset == "octave_up") {
    params.semitones = 12.0;
    params.preserve_tempo = true;
  } else if (preset == "octave_down") {
    params.semitones = -12.0;
    params.preserve_tempo = true;
  } else if (preset == "fifth_up") {
    params.semitones = 7.0;
    params.preserve_tempo = true;
  } else if (preset == "fourth_up") {
    params.semitones = 5.0;
    params.preserve_tempo = true;
  } else if (preset == "male_female") {
    params.semitones = 5.0;
    params.preserve_tempo = true;
  } else if (preset == "female_male") {
    params.semitones = -5.0;
    params.preserve_tempo = true;
  } else if (preset == "chipmunk") {
    params.semitones = 12.0;
    params.preserve_tempo = false;
  } else if (preset == "deep") {
    params.semitones = -7.0;
    params.preserve_tempo = true;
  } else {
    return std::nullopt;
  }

  params.preset = std::string(preset);
  return params;
}

std::optional<PitchShiftParams> parse_arguments(int argc, char *argv[]) {
  PitchShiftParams params;

  for (int i = 3; i < argc; ++i) {
    const std::string_view arg = argv[i];

    if ((arg == "-s" || arg == "--semitones") && i + 1 < argc) {
      params.semitones = std::stod(argv[++i]);
    } else if (arg == "-t" || arg == "--no-tempo") {
      params.preserve_tempo = false;
    } else if ((arg == "-r" || arg == "--rate") && i + 1 < argc) {
      params.sample_rate = std::stoi(argv[++i]);
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

  // Clamp semitones to reasonable range
  params.semitones = std::clamp(params.semitones, -24.0, 24.0);

  return params;
}

class AudioPitchShift {
public:
  AudioPitchShift(std::string_view input_file, const fs::path &output_file,
                  const PitchShiftParams &params)
      : input_file_(input_file), output_file_(output_file), params_(params),
        input_format_ctx_(ffmpeg::open_input_format(input_file.data())),
        input_packet_(ffmpeg::create_packet()),
        input_frame_(ffmpeg::create_frame()),
        filtered_frame_(ffmpeg::create_frame()) {

    initialize_decoder();
  }

  void process() {
    std::cout << "Audio Pitch Shift\n";
    std::cout << "=================\n\n";
    std::cout << std::format("Input: {}\n", input_file_);
    std::cout << std::format("Output: {}\n", output_file_.string());

    if (!params_.preset.empty()) {
      std::cout << std::format("Preset: {}\n", params_.preset);
    }

    std::cout << std::format("Pitch Shift: {:.1f} semitones",
                             params_.semitones);

    if (params_.semitones > 0) {
      std::cout << " (higher)";
    } else if (params_.semitones < 0) {
      std::cout << " (lower)";
    }
    std::cout << "\n";

    std::cout << std::format("Tempo Preservation: {}\n",
                             params_.preserve_tempo ? "Enabled" : "Disabled");

    if (params_.preserve_tempo) {
      std::cout << "  (Duration remains the same)\n";
    } else {
      const double speed_factor = std::pow(2.0, params_.semitones / 12.0);
      std::cout << std::format("  (Speed factor: {:.2f}x)\n", speed_factor);
    }

    std::cout << "\n";

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
    std::cout << "\nPitch shift applied successfully!\n";
    std::cout << std::format("Output file: {}\n", output_file_.string());
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

    // Build filter description
    std::string filter_desc;

    if (params_.preserve_tempo) {
      // Use asetrate + atempo to preserve tempo while changing pitch
      // Calculate the pitch ratio
      const double pitch_ratio = std::pow(2.0, params_.semitones / 12.0);

      // New sample rate to achieve pitch shift

      // Tempo adjustment to compensate (inverse of pitch ratio)
      const double tempo = 1.0 / pitch_ratio;

      // atempo has limits (0.5 to 100.0), so we may need to chain multiple
      if (tempo >= 0.5 && tempo <= 2.0) {
        filter_desc = std::format("asetrate={}*{},aresample={},atempo={}",
                                  input_codec_ctx_->sample_rate, pitch_ratio,
                                  input_codec_ctx_->sample_rate, tempo);
      } else if (tempo > 2.0) {
        // Need to chain multiple atempo filters
        double remaining_tempo = tempo;
        filter_desc = std::format("asetrate={}*{},aresample={}",
                                  input_codec_ctx_->sample_rate, pitch_ratio,
                                  input_codec_ctx_->sample_rate);

        while (remaining_tempo > 2.0) {
          filter_desc += ",atempo=2.0";
          remaining_tempo /= 2.0;
        }
        filter_desc += std::format(",atempo={}", remaining_tempo);
      } else {
        // tempo < 0.5, need to chain multiple
        double remaining_tempo = tempo;
        filter_desc = std::format("asetrate={}*{},aresample={}",
                                  input_codec_ctx_->sample_rate, pitch_ratio,
                                  input_codec_ctx_->sample_rate);

        while (remaining_tempo < 0.5) {
          filter_desc += ",atempo=0.5";
          remaining_tempo /= 0.5;
        }
        filter_desc += std::format(",atempo={}", remaining_tempo);
      }
    } else {
      // Simple pitch shift without tempo preservation
      const double pitch_ratio = std::pow(2.0, params_.semitones / 12.0);
      const int new_sample_rate =
          static_cast<int>(input_codec_ctx_->sample_rate * pitch_ratio);

      filter_desc = std::format("asetrate={},aresample={}", new_sample_rate,
                                input_codec_ctx_->sample_rate);
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

  std::string input_file_;
  fs::path output_file_;
  PitchShiftParams params_;

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

    AudioPitchShift pitch_shifter(input, output, *params);
    pitch_shifter.process();

    return 0;
  } catch (const std::exception &e) {
    std::cerr << std::format("Error: {}\n", e.what());
    return 1;
  }
}
