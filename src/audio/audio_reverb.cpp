#include <format>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

namespace {

struct ReverbParams {
  double room_size = 0.5;
  double reverberance = 0.5;
  double hf_damping = 0.5;
  double wet_gain = 0.0;
  double dry_gain = 0.0;
  double pre_delay = 0.0;
  double stereo_depth = 100.0;
};

const ReverbParams PRESET_ROOM = {.room_size = 0.3,
                                  .reverberance = 0.5,
                                  .hf_damping = 0.5,
                                  .wet_gain = -5.0,
                                  .dry_gain = 0.0,
                                  .pre_delay = 20.0,
                                  .stereo_depth = 100.0};

const ReverbParams PRESET_HALL = {.room_size = 0.8,
                                  .reverberance = 0.7,
                                  .hf_damping = 0.5,
                                  .wet_gain = -3.0,
                                  .dry_gain = 0.0,
                                  .pre_delay = 30.0,
                                  .stereo_depth = 100.0};

const ReverbParams PRESET_PLATE = {.room_size = 0.5,
                                   .reverberance = 0.5,
                                   .hf_damping = 0.7,
                                   .wet_gain = -4.0,
                                   .dry_gain = 0.0,
                                   .pre_delay = 5.0,
                                   .stereo_depth = 100.0};

const ReverbParams PRESET_SPRING = {.room_size = 0.2,
                                    .reverberance = 0.6,
                                    .hf_damping = 0.3,
                                    .wet_gain = -6.0,
                                    .dry_gain = 0.0,
                                    .pre_delay = 0.0,
                                    .stereo_depth = 50.0};

const ReverbParams PRESET_CATHEDRAL = {.room_size = 1.0,
                                       .reverberance = 0.84,
                                       .hf_damping = 0.7,
                                       .wet_gain = -1.0,
                                       .dry_gain = 0.0,
                                       .pre_delay = 40.0,
                                       .stereo_depth = 100.0};

const ReverbParams PRESET_CHAMBER = {.room_size = 0.4,
                                     .reverberance = 0.6,
                                     .hf_damping = 0.4,
                                     .wet_gain = -4.0,
                                     .dry_gain = 0.0,
                                     .pre_delay = 15.0,
                                     .stereo_depth = 80.0};

template <typename T> struct AVDeleter {
  void operator()(T *ptr) const {
    if constexpr (std::is_same_v<T, AVFormatContext>) {
      avformat_close_input(&ptr);
    } else if constexpr (std::is_same_v<T, AVCodecContext>) {
      avcodec_free_context(&ptr);
    } else if constexpr (std::is_same_v<T, AVFrame>) {
      av_frame_free(&ptr);
    } else if constexpr (std::is_same_v<T, AVPacket>) {
      av_packet_free(&ptr);
    } else if constexpr (std::is_same_v<T, AVFilterGraph>) {
      avfilter_graph_free(&ptr);
    }
  }
};

template <typename T> using AVPtr = std::unique_ptr<T, AVDeleter<T>>;

void check_error(const int error_code, const std::string &operation) {
  if (error_code < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(error_code, errbuf, sizeof(errbuf));
    throw std::runtime_error(std::format("{} failed: {}", operation, errbuf));
  }
}

class ReverbProcessor {
public:
  ReverbProcessor(const std::string &input_file, const std::string &output_file,
                  const ReverbParams &params)
      : input_file_(input_file), output_file_(output_file), params_(params) {}

  void process() {
    open_input();
    find_audio_stream();
    open_decoder();
    setup_filter_graph();
    open_output();
    process_audio();
    finalize_output();

    std::cout << std::format("✓ Reverb effect applied successfully!\n");
    std::cout << std::format("  Room size: {:.0f}%\n", params_.room_size * 100);
    std::cout << std::format("  Reverberance: {:.0f}%\n",
                             params_.reverberance * 100);
    std::cout << std::format("  Pre-delay: {:.1f} ms\n", params_.pre_delay);
    std::cout << std::format("  Output: {}\n", output_file_);
  }

private:
  void open_input() {
    AVFormatContext *raw_fmt_ctx = nullptr;
    check_error(avformat_open_input(&raw_fmt_ctx, input_file_.c_str(), nullptr,
                                    nullptr),
                "Opening input file");
    fmt_ctx_.reset(raw_fmt_ctx);

    check_error(avformat_find_stream_info(fmt_ctx_.get(), nullptr),
                "Finding stream info");
  }

  void find_audio_stream() {
    for (unsigned i = 0; i < fmt_ctx_->nb_streams; ++i) {
      if (fmt_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        audio_stream_idx_ = i;
        return;
      }
    }
    throw std::runtime_error("No audio stream found");
  }

  void open_decoder() {
    const auto *codec_params = fmt_ctx_->streams[audio_stream_idx_]->codecpar;
    const auto *decoder = avcodec_find_decoder(codec_params->codec_id);
    if (!decoder) {
      throw std::runtime_error("Decoder not found");
    }

    AVCodecContext *raw_dec_ctx = avcodec_alloc_context3(decoder);
    if (!raw_dec_ctx) {
      throw std::runtime_error("Failed to allocate decoder context");
    }
    dec_ctx_.reset(raw_dec_ctx);

    check_error(avcodec_parameters_to_context(dec_ctx_.get(), codec_params),
                "Copying codec parameters");
    check_error(avcodec_open2(dec_ctx_.get(), decoder, nullptr),
                "Opening decoder");

    // Store input parameters for output
    sample_rate_ = dec_ctx_->sample_rate;
    if (dec_ctx_->ch_layout.nb_channels == 1) {
      ch_layout_ = AV_CHANNEL_LAYOUT_MONO;
    } else {
      ch_layout_ = AV_CHANNEL_LAYOUT_STEREO;
    }
    sample_fmt_ = dec_ctx_->sample_fmt;
  }

  void setup_filter_graph() {
    AVFilterGraph *raw_graph = avfilter_graph_alloc();
    if (!raw_graph) {
      throw std::runtime_error("Failed to allocate filter graph");
    }
    filter_graph_.reset(raw_graph);

    const auto *abuffer = avfilter_get_by_name("abuffer");
    const auto *abuffersink = avfilter_get_by_name("abuffersink");

    // Check if freeverb is available (it might not be in all builds)
    const auto *freeverb = avfilter_get_by_name("freeverb");
    if (!freeverb) {
      throw std::runtime_error("freeverb filter not found. Please use FFmpeg "
                               "built with freeverb support.");
    }

    if (!abuffer || !abuffersink) {
      throw std::runtime_error("Required filters not found");
    }

    // Create buffer source
    char ch_layout_str[64];
    av_channel_layout_describe(&dec_ctx_->ch_layout, ch_layout_str,
                               sizeof(ch_layout_str));

    const auto args = std::format(
        "time_base=1/{}:sample_rate={}:sample_fmt={}:channel_layout={}",
        sample_rate_, sample_rate_, av_get_sample_fmt_name(sample_fmt_),
        ch_layout_str);

    AVFilterContext *buffersrc_ctx = nullptr;
    check_error(avfilter_graph_create_filter(&buffersrc_ctx, abuffer, "in",
                                             args.c_str(), nullptr,
                                             filter_graph_.get()),
                "Creating buffer source");

    // Create buffer sink
    AVFilterContext *buffersink_ctx = nullptr;
    check_error(avfilter_graph_create_filter(&buffersink_ctx, abuffersink,
                                             "out", nullptr, nullptr,
                                             filter_graph_.get()),
                "Creating buffer sink");

    // Create freeverb filter
    const auto reverb_args =
        std::format("room_size={}:reverberance={}:hf_damping={}:wet_gain={}:"
                    "dry_gain={}:stereo_depth={}",
                    params_.room_size, params_.reverberance, params_.hf_damping,
                    params_.wet_gain, params_.dry_gain, params_.stereo_depth);

    AVFilterContext *reverb_ctx = nullptr;
    check_error(avfilter_graph_create_filter(&reverb_ctx, freeverb, "reverb",
                                             reverb_args.c_str(), nullptr,
                                             filter_graph_.get()),
                "Creating reverb filter");

    // If pre-delay is needed, add a delay filter
    if (params_.pre_delay > 0.0) {
      const auto *adelay = avfilter_get_by_name("adelay");
      if (!adelay) {
        throw std::runtime_error("adelay filter not found");
      }

      const auto delay_args =
          std::format("delays={}:all=1", static_cast<int>(params_.pre_delay));

      AVFilterContext *delay_ctx = nullptr;
      check_error(avfilter_graph_create_filter(&delay_ctx, adelay, "predelay",
                                               delay_args.c_str(), nullptr,
                                               filter_graph_.get()),
                  "Creating delay filter");

      // Connect: buffer -> delay -> reverb -> buffersink
      check_error(avfilter_link(buffersrc_ctx, 0, delay_ctx, 0),
                  "Linking buffer to delay");
      check_error(avfilter_link(delay_ctx, 0, reverb_ctx, 0),
                  "Linking delay to reverb");
      check_error(avfilter_link(reverb_ctx, 0, buffersink_ctx, 0),
                  "Linking reverb to sink");
    } else {
      // Connect: buffer -> reverb -> buffersink
      check_error(avfilter_link(buffersrc_ctx, 0, reverb_ctx, 0),
                  "Linking buffer to reverb");
      check_error(avfilter_link(reverb_ctx, 0, buffersink_ctx, 0),
                  "Linking reverb to sink");
    }

    check_error(avfilter_graph_config(filter_graph_.get(), nullptr),
                "Configuring filter graph");

    buffersrc_ctx_ = buffersrc_ctx;
    buffersink_ctx_ = buffersink_ctx;
  }

  void open_output() {
    // Open output file for writing WAV
    FILE *f = fopen(output_file_.c_str(), "wb");
    if (!f) {
      throw std::runtime_error(
          std::format("Failed to open output file: {}", output_file_));
    }
    output_file_handle_ = f;

    // Write WAV header placeholder (will update later)
    write_wav_header(0);
  }

  void write_wav_header(const uint32_t data_size) {
    fseek(output_file_handle_, 0, SEEK_SET);

    const int channels = ch_layout_.nb_channels;
    const int bits_per_sample = 16;
    const int byte_rate = sample_rate_ * channels * bits_per_sample / 8;
    const int block_align = channels * bits_per_sample / 8;

    // RIFF header
    fwrite("RIFF", 1, 4, output_file_handle_);
    const uint32_t chunk_size = 36 + data_size;
    fwrite(&chunk_size, 4, 1, output_file_handle_);
    fwrite("WAVE", 1, 4, output_file_handle_);

    // fmt subchunk
    fwrite("fmt ", 1, 4, output_file_handle_);
    const uint32_t subchunk1_size = 16;
    fwrite(&subchunk1_size, 4, 1, output_file_handle_);
    const uint16_t audio_format = 1; // PCM
    fwrite(&audio_format, 2, 1, output_file_handle_);
    const uint16_t num_channels = channels;
    fwrite(&num_channels, 2, 1, output_file_handle_);
    fwrite(&sample_rate_, 4, 1, output_file_handle_);
    fwrite(&byte_rate, 4, 1, output_file_handle_);
    const uint16_t block_align_val = block_align;
    fwrite(&block_align_val, 2, 1, output_file_handle_);
    const uint16_t bits_per_sample_val = bits_per_sample;
    fwrite(&bits_per_sample_val, 2, 1, output_file_handle_);

    // data subchunk
    fwrite("data", 1, 4, output_file_handle_);
    fwrite(&data_size, 4, 1, output_file_handle_);
  }

  void process_audio() {
    AVFrame *raw_frame = av_frame_alloc();
    AVFrame *raw_filt_frame = av_frame_alloc();
    AVPacket *raw_packet = av_packet_alloc();

    if (!raw_frame || !raw_filt_frame || !raw_packet) {
      throw std::runtime_error("Failed to allocate frame/packet");
    }

    AVPtr<AVFrame> frame(raw_frame);
    AVPtr<AVFrame> filt_frame(raw_filt_frame);
    AVPtr<AVPacket> packet(raw_packet);

    // Read and process packets
    while (av_read_frame(fmt_ctx_.get(), packet.get()) >= 0) {
      if (packet->stream_index == audio_stream_idx_) {
        check_error(avcodec_send_packet(dec_ctx_.get(), packet.get()),
                    "Sending packet to decoder");

        while (avcodec_receive_frame(dec_ctx_.get(), frame.get()) >= 0) {
          // Push frame to filter
          check_error(
              av_buffersrc_add_frame_flags(buffersrc_ctx_, frame.get(), 0),
              "Adding frame to buffer source");

          // Pull filtered frames
          while (av_buffersink_get_frame(buffersink_ctx_, filt_frame.get()) >=
                 0) {
            write_audio_frame(filt_frame.get());
            av_frame_unref(filt_frame.get());
          }

          av_frame_unref(frame.get());
        }
      }
      av_packet_unref(packet.get());
    }

    // Flush decoder
    check_error(avcodec_send_packet(dec_ctx_.get(), nullptr),
                "Flushing decoder");
    while (avcodec_receive_frame(dec_ctx_.get(), frame.get()) >= 0) {
      check_error(av_buffersrc_add_frame_flags(buffersrc_ctx_, frame.get(), 0),
                  "Adding frame to buffer source");

      while (av_buffersink_get_frame(buffersink_ctx_, filt_frame.get()) >= 0) {
        write_audio_frame(filt_frame.get());
        av_frame_unref(filt_frame.get());
      }

      av_frame_unref(frame.get());
    }

    // Flush filter
    check_error(av_buffersrc_add_frame_flags(buffersrc_ctx_, nullptr, 0),
                "Flushing filter");
    while (av_buffersink_get_frame(buffersink_ctx_, filt_frame.get()) >= 0) {
      write_audio_frame(filt_frame.get());
      av_frame_unref(filt_frame.get());
    }
  }

  void write_audio_frame(AVFrame *frame) {
    const int channels = ch_layout_.nb_channels;
    const int samples = frame->nb_samples;

    // Convert to 16-bit PCM
    for (int i = 0; i < samples; ++i) {
      for (int ch = 0; ch < channels; ++ch) {
        float sample = 0.0f;

        if (frame->format == AV_SAMPLE_FMT_FLTP) {
          sample = reinterpret_cast<float *>(frame->data[ch])[i];
        } else if (frame->format == AV_SAMPLE_FMT_FLT) {
          sample = reinterpret_cast<float *>(frame->data[0])[i * channels + ch];
        } else if (frame->format == AV_SAMPLE_FMT_S16P) {
          sample = reinterpret_cast<int16_t *>(frame->data[ch])[i] / 32768.0f;
        } else if (frame->format == AV_SAMPLE_FMT_S16) {
          sample =
              reinterpret_cast<int16_t *>(frame->data[0])[i * channels + ch] /
              32768.0f;
        }

        // Clamp and convert to 16-bit
        sample = std::clamp(sample, -1.0f, 1.0f);
        const auto sample_s16 = static_cast<int16_t>(sample * 32767.0f);
        fwrite(&sample_s16, sizeof(int16_t), 1, output_file_handle_);
        total_samples_written_++;
      }
    }
  }

  void finalize_output() {
    const uint32_t data_size = total_samples_written_ * sizeof(int16_t);
    write_wav_header(data_size);
    fclose(output_file_handle_);
    output_file_handle_ = nullptr;
  }

  std::string input_file_;
  std::string output_file_;
  ReverbParams params_;

  AVPtr<AVFormatContext> fmt_ctx_;
  AVPtr<AVCodecContext> dec_ctx_;
  AVPtr<AVFilterGraph> filter_graph_;

  AVFilterContext *buffersrc_ctx_ = nullptr;
  AVFilterContext *buffersink_ctx_ = nullptr;

  int audio_stream_idx_ = -1;
  int sample_rate_ = 44100;
  AVChannelLayout ch_layout_ = AV_CHANNEL_LAYOUT_STEREO;
  AVSampleFormat sample_fmt_ = AV_SAMPLE_FMT_FLTP;

  FILE *output_file_handle_ = nullptr;
  uint32_t total_samples_written_ = 0;
};

void print_usage(const char *program_name) {
  std::cout << std::format(R"(
Audio Reverb Effect

Usage: {} <input> <output> [options]

Options:
  --preset <name>        Use a preset configuration
                         Available: room, hall, plate, spring, cathedral, chamber
  --room-size <0-1>      Room size (default: 0.5)
  --reverberance <0-1>   Reverberance/decay time (default: 0.5)
  --hf-damping <0-1>     High frequency damping (default: 0.5)
  --wet-gain <dB>        Wet signal gain in dB (default: 0.0)
  --dry-gain <dB>        Dry signal gain in dB (default: 0.0)
  --pre-delay <ms>       Pre-delay in milliseconds (default: 0.0)
  --stereo-depth <0-100> Stereo depth percentage (default: 100.0)

Presets:
  room         Small room reverb (warm, intimate)
  hall         Concert hall reverb (spacious, natural)
  plate        Plate reverb (smooth, vintage)
  spring       Spring reverb (bright, guitar amp style)
  cathedral    Large cathedral reverb (long decay, epic)
  chamber      Echo chamber (medium size, controlled)

Examples:
  # Apply hall reverb preset
  {} input.wav output.wav --preset hall

  # Small room reverb
  {} input.wav output.wav --preset room

  # Cathedral reverb for vocals
  {} input.wav output.wav --preset cathedral

  # Custom reverb settings
  {} input.wav output.wav --room-size 0.7 --reverberance 0.6 \
                             --wet-gain -3 --pre-delay 25

  # Plate reverb with custom wet/dry mix
  {} input.wav output.wav --preset plate --wet-gain -2 --dry-gain -1

)",
                           program_name, program_name, program_name,
                           program_name, program_name, program_name);
}

} // namespace

int main(int argc, char *argv[]) {
  try {
    if (argc < 3) {
      print_usage(argv[0]);
      return 1;
    }

    const std::string input_file = argv[1];
    const std::string output_file = argv[2];

    ReverbParams params;

    // Parse arguments
    for (int i = 3; i < argc; ++i) {
      const std::string arg = argv[i];

      if (arg == "--preset" && i + 1 < argc) {
        const std::string preset = argv[++i];
        if (preset == "room") {
          params = PRESET_ROOM;
        } else if (preset == "hall") {
          params = PRESET_HALL;
        } else if (preset == "plate") {
          params = PRESET_PLATE;
        } else if (preset == "spring") {
          params = PRESET_SPRING;
        } else if (preset == "cathedral") {
          params = PRESET_CATHEDRAL;
        } else if (preset == "chamber") {
          params = PRESET_CHAMBER;
        } else {
          throw std::runtime_error(std::format("Unknown preset: {}", preset));
        }
      } else if (arg == "--room-size" && i + 1 < argc) {
        params.room_size = std::stod(argv[++i]);
        if (params.room_size < 0.0 || params.room_size > 1.0) {
          throw std::runtime_error("Room size must be between 0.0 and 1.0");
        }
      } else if (arg == "--reverberance" && i + 1 < argc) {
        params.reverberance = std::stod(argv[++i]);
        if (params.reverberance < 0.0 || params.reverberance > 1.0) {
          throw std::runtime_error("Reverberance must be between 0.0 and 1.0");
        }
      } else if (arg == "--hf-damping" && i + 1 < argc) {
        params.hf_damping = std::stod(argv[++i]);
        if (params.hf_damping < 0.0 || params.hf_damping > 1.0) {
          throw std::runtime_error("HF damping must be between 0.0 and 1.0");
        }
      } else if (arg == "--wet-gain" && i + 1 < argc) {
        params.wet_gain = std::stod(argv[++i]);
      } else if (arg == "--dry-gain" && i + 1 < argc) {
        params.dry_gain = std::stod(argv[++i]);
      } else if (arg == "--pre-delay" && i + 1 < argc) {
        params.pre_delay = std::stod(argv[++i]);
      } else if (arg == "--stereo-depth" && i + 1 < argc) {
        params.stereo_depth = std::stod(argv[++i]);
        if (params.stereo_depth < 0.0 || params.stereo_depth > 100.0) {
          throw std::runtime_error(
              "Stereo depth must be between 0.0 and 100.0");
        }
      }
    }

    ReverbProcessor processor(input_file, output_file, params);
    processor.process();

    return 0;
  } catch (const std::exception &e) {
    std::cerr << std::format("Error: {}\n", e.what());
    return 1;
  }
}
