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

struct FlangerParams {
    double delay = 0.0;        // Base delay in ms (0-30)
    double depth = 2.0;        // Modulation depth in ms (0-10)
    double regen = 0.0;        // Regeneration/feedback (-95 to 95)
    double width = 71.0;       // Percentage of delayed signal mixed (0-100)
    double speed = 0.5;        // LFO speed in Hz (0.1-10)
    double phase = 25.0;       // Stereo phase shift (0-100)
    std::string shape = "sine"; // LFO shape: sine or triangle
    std::string interp = "linear"; // Interpolation: linear or quadratic
};

const FlangerParams PRESET_JET = {
    .delay = 0.0,
    .depth = 3.0,
    .regen = -95.0,
    .width = 71.0,
    .speed = 0.5,
    .phase = 25.0,
    .shape = "sine",
    .interp = "linear"
};

const FlangerParams PRESET_METALLIC = {
    .delay = 5.0,
    .depth = 5.0,
    .regen = 50.0,
    .width = 80.0,
    .speed = 0.3,
    .phase = 50.0,
    .shape = "triangle",
    .interp = "linear"
};

const FlangerParams PRESET_MILD = {
    .delay = 2.0,
    .depth = 1.5,
    .regen = 10.0,
    .width = 50.0,
    .speed = 0.25,
    .phase = 25.0,
    .shape = "sine",
    .interp = "linear"
};

const FlangerParams PRESET_THROUGH_ZERO = {
    .delay = 0.0,
    .depth = 4.0,
    .regen = -50.0,
    .width = 100.0,
    .speed = 0.4,
    .phase = 0.0,
    .shape = "sine",
    .interp = "quadratic"
};

const FlangerParams PRESET_CHORUS_FLANGER = {
    .delay = 7.0,
    .depth = 2.5,
    .regen = 20.0,
    .width = 60.0,
    .speed = 0.6,
    .phase = 40.0,
    .shape = "sine",
    .interp = "linear"
};

template <typename T>
struct AVDeleter {
    void operator()(T* ptr) const {
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

template <typename T>
using AVPtr = std::unique_ptr<T, AVDeleter<T>>;

void check_error(const int error_code, const std::string& operation) {
    if (error_code < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(error_code, errbuf, sizeof(errbuf));
        throw std::runtime_error(std::format("{} failed: {}", operation, errbuf));
    }
}

class FlangerProcessor {
public:
    FlangerProcessor(const std::string& input_file, const std::string& output_file,
                     const FlangerParams& params)
        : input_file_(input_file), output_file_(output_file), params_(params) {}

    void process() {
        open_input();
        find_audio_stream();
        open_decoder();
        setup_filter_graph();
        open_output();
        process_audio();
        finalize_output();

        std::cout << std::format("✓ Flanger effect applied successfully!\n");
        std::cout << std::format("  Delay: {:.1f} ms\n", params_.delay);
        std::cout << std::format("  Depth: {:.1f} ms\n", params_.depth);
        std::cout << std::format("  Speed: {:.2f} Hz\n", params_.speed);
        std::cout << std::format("  Feedback: {:.0f}%\n", params_.regen);
        std::cout << std::format("  Output: {}\n", output_file_);
    }

private:
    void open_input() {
        AVFormatContext* raw_fmt_ctx = nullptr;
        check_error(avformat_open_input(&raw_fmt_ctx, input_file_.c_str(), nullptr, nullptr),
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
        const auto* codec_params = fmt_ctx_->streams[audio_stream_idx_]->codecpar;
        const auto* decoder = avcodec_find_decoder(codec_params->codec_id);
        if (!decoder) {
            throw std::runtime_error("Decoder not found");
        }

        AVCodecContext* raw_dec_ctx = avcodec_alloc_context3(decoder);
        if (!raw_dec_ctx) {
            throw std::runtime_error("Failed to allocate decoder context");
        }
        dec_ctx_.reset(raw_dec_ctx);

        check_error(avcodec_parameters_to_context(dec_ctx_.get(), codec_params),
                    "Copying codec parameters");
        check_error(avcodec_open2(dec_ctx_.get(), decoder, nullptr), "Opening decoder");

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
        AVFilterGraph* raw_graph = avfilter_graph_alloc();
        if (!raw_graph) {
            throw std::runtime_error("Failed to allocate filter graph");
        }
        filter_graph_.reset(raw_graph);

        const auto* abuffer = avfilter_get_by_name("abuffer");
        const auto* abuffersink = avfilter_get_by_name("abuffersink");
        const auto* flanger = avfilter_get_by_name("flanger");

        if (!abuffer || !abuffersink || !flanger) {
            throw std::runtime_error("Required filters not found");
        }

        // Create buffer source
        char ch_layout_str[64];
        av_channel_layout_describe(&dec_ctx_->ch_layout, ch_layout_str, sizeof(ch_layout_str));

        const auto args = std::format(
            "time_base=1/{}:sample_rate={}:sample_fmt={}:channel_layout={}",
            sample_rate_, sample_rate_, av_get_sample_fmt_name(sample_fmt_), ch_layout_str);

        AVFilterContext* buffersrc_ctx = nullptr;
        check_error(avfilter_graph_create_filter(&buffersrc_ctx, abuffer, "in", args.c_str(),
                                                  nullptr, filter_graph_.get()),
                    "Creating buffer source");

        // Create buffer sink
        AVFilterContext* buffersink_ctx = nullptr;
        check_error(avfilter_graph_create_filter(&buffersink_ctx, abuffersink, "out", nullptr,
                                                  nullptr, filter_graph_.get()),
                    "Creating buffer sink");

        // Create flanger filter
        const auto flanger_args = std::format(
            "delay={}:depth={}:regen={}:width={}:speed={}:phase={}:shape={}:interp={}",
            params_.delay, params_.depth, params_.regen, params_.width,
            params_.speed, params_.phase, params_.shape, params_.interp);

        AVFilterContext* flanger_ctx = nullptr;
        check_error(avfilter_graph_create_filter(&flanger_ctx, flanger, "flanger",
                                                  flanger_args.c_str(), nullptr,
                                                  filter_graph_.get()),
                    "Creating flanger filter");

        // Connect filters: buffer -> flanger -> buffersink
        check_error(avfilter_link(buffersrc_ctx, 0, flanger_ctx, 0),
                   "Linking buffer to flanger");
        check_error(avfilter_link(flanger_ctx, 0, buffersink_ctx, 0),
                   "Linking flanger to sink");

        check_error(avfilter_graph_config(filter_graph_.get(), nullptr),
                    "Configuring filter graph");

        buffersrc_ctx_ = buffersrc_ctx;
        buffersink_ctx_ = buffersink_ctx;
    }

    void open_output() {
        // Open output file for writing WAV
        FILE* f = fopen(output_file_.c_str(), "wb");
        if (!f) {
            throw std::runtime_error(std::format("Failed to open output file: {}", output_file_));
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
        AVFrame* raw_frame = av_frame_alloc();
        AVFrame* raw_filt_frame = av_frame_alloc();
        AVPacket* raw_packet = av_packet_alloc();

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
                    check_error(av_buffersrc_add_frame_flags(buffersrc_ctx_, frame.get(), 0),
                                "Adding frame to buffer source");

                    // Pull filtered frames
                    while (av_buffersink_get_frame(buffersink_ctx_, filt_frame.get()) >= 0) {
                        write_audio_frame(filt_frame.get());
                        av_frame_unref(filt_frame.get());
                    }

                    av_frame_unref(frame.get());
                }
            }
            av_packet_unref(packet.get());
        }

        // Flush decoder
        check_error(avcodec_send_packet(dec_ctx_.get(), nullptr), "Flushing decoder");
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

    void write_audio_frame(AVFrame* frame) {
        const int channels = ch_layout_.nb_channels;
        const int samples = frame->nb_samples;

        // Convert to 16-bit PCM
        for (int i = 0; i < samples; ++i) {
            for (int ch = 0; ch < channels; ++ch) {
                float sample = 0.0f;

                if (frame->format == AV_SAMPLE_FMT_FLTP) {
                    sample = reinterpret_cast<float*>(frame->data[ch])[i];
                } else if (frame->format == AV_SAMPLE_FMT_FLT) {
                    sample = reinterpret_cast<float*>(frame->data[0])[i * channels + ch];
                } else if (frame->format == AV_SAMPLE_FMT_S16P) {
                    sample = reinterpret_cast<int16_t*>(frame->data[ch])[i] / 32768.0f;
                } else if (frame->format == AV_SAMPLE_FMT_S16) {
                    sample =
                        reinterpret_cast<int16_t*>(frame->data[0])[i * channels + ch] / 32768.0f;
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
    FlangerParams params_;

    AVPtr<AVFormatContext> fmt_ctx_;
    AVPtr<AVCodecContext> dec_ctx_;
    AVPtr<AVFilterGraph> filter_graph_;

    AVFilterContext* buffersrc_ctx_ = nullptr;
    AVFilterContext* buffersink_ctx_ = nullptr;

    int audio_stream_idx_ = -1;
    int sample_rate_ = 44100;
    AVChannelLayout ch_layout_ = AV_CHANNEL_LAYOUT_STEREO;
    AVSampleFormat sample_fmt_ = AV_SAMPLE_FMT_FLTP;

    FILE* output_file_handle_ = nullptr;
    uint32_t total_samples_written_ = 0;
};

void print_usage(const char* program_name) {
    std::cout << std::format(R"(
Audio Flanger Effect

Usage: {} <input> <output> [options]

Options:
  --preset <name>        Use a preset configuration
                         Available: jet, metallic, mild, through_zero, chorus_flanger
  --delay <ms>           Base delay (0-30 ms, default: 0.0)
  --depth <ms>           Modulation depth (0-10 ms, default: 2.0)
  --regen <percent>      Regeneration/feedback (-95 to 95, default: 0.0)
  --width <percent>      Mix width (0-100, default: 71.0)
  --speed <Hz>           LFO speed (0.1-10 Hz, default: 0.5)
  --phase <percent>      Stereo phase shift (0-100, default: 25.0)
  --shape <type>         LFO shape: sine or triangle (default: sine)
  --interp <type>        Interpolation: linear or quadratic (default: linear)

Presets:
  jet              Classic jet plane flanging (extreme negative feedback)
  metallic         Metallic sweep with positive feedback
  mild             Subtle flanging for gentle enhancement
  through_zero     Through-zero flanging (vintage tape effect)
  chorus_flanger   Hybrid chorus-flanger sound

Examples:
  # Apply jet flanger preset
  {} input.wav output.wav --preset jet

  # Metallic sweep
  {} input.wav output.wav --preset metallic

  # Mild flanging for vocals
  {} input.wav output.wav --preset mild

  # Through-zero flanging
  {} input.wav output.wav --preset through_zero

  # Custom flanger settings
  {} input.wav output.wav --delay 3 --depth 4 --regen -70 --speed 0.4

  # Fast metallic sweep
  {} input.wav output.wav --depth 5 --regen 60 --speed 1.5 --shape triangle

)",
                                 program_name, program_name, program_name, program_name,
                                 program_name, program_name, program_name);
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc < 3) {
            print_usage(argv[0]);
            return 1;
        }

        const std::string input_file = argv[1];
        const std::string output_file = argv[2];

        FlangerParams params;

        // Parse arguments
        for (int i = 3; i < argc; ++i) {
            const std::string arg = argv[i];

            if (arg == "--preset" && i + 1 < argc) {
                const std::string preset = argv[++i];
                if (preset == "jet") {
                    params = PRESET_JET;
                } else if (preset == "metallic") {
                    params = PRESET_METALLIC;
                } else if (preset == "mild") {
                    params = PRESET_MILD;
                } else if (preset == "through_zero") {
                    params = PRESET_THROUGH_ZERO;
                } else if (preset == "chorus_flanger") {
                    params = PRESET_CHORUS_FLANGER;
                } else {
                    throw std::runtime_error(std::format("Unknown preset: {}", preset));
                }
            } else if (arg == "--delay" && i + 1 < argc) {
                params.delay = std::stod(argv[++i]);
                if (params.delay < 0.0 || params.delay > 30.0) {
                    throw std::runtime_error("Delay must be between 0 and 30 ms");
                }
            } else if (arg == "--depth" && i + 1 < argc) {
                params.depth = std::stod(argv[++i]);
                if (params.depth < 0.0 || params.depth > 10.0) {
                    throw std::runtime_error("Depth must be between 0 and 10 ms");
                }
            } else if (arg == "--regen" && i + 1 < argc) {
                params.regen = std::stod(argv[++i]);
                if (params.regen < -95.0 || params.regen > 95.0) {
                    throw std::runtime_error("Regen must be between -95 and 95");
                }
            } else if (arg == "--width" && i + 1 < argc) {
                params.width = std::stod(argv[++i]);
                if (params.width < 0.0 || params.width > 100.0) {
                    throw std::runtime_error("Width must be between 0 and 100");
                }
            } else if (arg == "--speed" && i + 1 < argc) {
                params.speed = std::stod(argv[++i]);
                if (params.speed < 0.1 || params.speed > 10.0) {
                    throw std::runtime_error("Speed must be between 0.1 and 10 Hz");
                }
            } else if (arg == "--phase" && i + 1 < argc) {
                params.phase = std::stod(argv[++i]);
                if (params.phase < 0.0 || params.phase > 100.0) {
                    throw std::runtime_error("Phase must be between 0 and 100");
                }
            } else if (arg == "--shape" && i + 1 < argc) {
                params.shape = argv[++i];
                if (params.shape != "sine" && params.shape != "triangle") {
                    throw std::runtime_error("Shape must be 'sine' or 'triangle'");
                }
            } else if (arg == "--interp" && i + 1 < argc) {
                params.interp = argv[++i];
                if (params.interp != "linear" && params.interp != "quadratic") {
                    throw std::runtime_error("Interp must be 'linear' or 'quadratic'");
                }
            }
        }

        FlangerProcessor processor(input_file, output_file, params);
        processor.process();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }
}
