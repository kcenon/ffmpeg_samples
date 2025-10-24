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

enum class DistortionType {
    OVERDRIVE,
    FUZZ,
    TUBE,
    HARD_CLIP,
    SOFT_CLIP,
    BITCRUSHER
};

struct DistortionParams {
    DistortionType type = DistortionType::OVERDRIVE;
    double drive = 5.0;      // Input gain/drive (0-20 dB)
    double tone = 0.5;       // Tone control (0-1)
    double output_gain = 0.0; // Output level compensation
    double mix = 1.0;        // Wet/dry mix (0-1)
    int bits = 8;            // For bitcrusher (1-16)
};

const DistortionParams PRESET_OVERDRIVE = {
    .type = DistortionType::OVERDRIVE,
    .drive = 6.0,
    .tone = 0.5,
    .output_gain = -3.0,
    .mix = 1.0,
    .bits = 16
};

const DistortionParams PRESET_FUZZ = {
    .type = DistortionType::FUZZ,
    .drive = 15.0,
    .tone = 0.6,
    .output_gain = -6.0,
    .mix = 1.0,
    .bits = 16
};

const DistortionParams PRESET_TUBE = {
    .type = DistortionType::TUBE,
    .drive = 8.0,
    .tone = 0.4,
    .output_gain = -4.0,
    .mix = 1.0,
    .bits = 16
};

const DistortionParams PRESET_HARD_CLIP = {
    .type = DistortionType::HARD_CLIP,
    .drive = 10.0,
    .tone = 0.5,
    .output_gain = -5.0,
    .mix = 1.0,
    .bits = 16
};

const DistortionParams PRESET_BITCRUSHER = {
    .type = DistortionType::BITCRUSHER,
    .drive = 0.0,
    .tone = 0.5,
    .output_gain = 0.0,
    .mix = 1.0,
    .bits = 8
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

std::string distortion_type_to_string(DistortionType type) {
    switch (type) {
        case DistortionType::OVERDRIVE: return "overdrive";
        case DistortionType::FUZZ: return "fuzz";
        case DistortionType::TUBE: return "tube";
        case DistortionType::HARD_CLIP: return "hard_clip";
        case DistortionType::SOFT_CLIP: return "soft_clip";
        case DistortionType::BITCRUSHER: return "bitcrusher";
        default: return "unknown";
    }
}

class DistortionProcessor {
public:
    DistortionProcessor(const std::string& input_file, const std::string& output_file,
                        const DistortionParams& params)
        : input_file_(input_file), output_file_(output_file), params_(params) {}

    void process() {
        open_input();
        find_audio_stream();
        open_decoder();
        setup_filter_graph();
        open_output();
        process_audio();
        finalize_output();

        std::cout << std::format("✓ Distortion effect applied successfully!\n");
        std::cout << std::format("  Type: {}\n", distortion_type_to_string(params_.type));
        std::cout << std::format("  Drive: {:.1f} dB\n", params_.drive);
        std::cout << std::format("  Output gain: {:.1f} dB\n", params_.output_gain);
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

        if (!abuffer || !abuffersink) {
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

        // Build filter chain based on distortion type
        AVFilterContext* last_filter = buffersrc_ctx;

        // 1. Apply input gain (drive)
        if (params_.drive > 0.0) {
            const auto* volume = avfilter_get_by_name("volume");
            if (!volume) {
                throw std::runtime_error("volume filter not found");
            }

            const auto volume_args = std::format("volume={}dB", params_.drive);
            AVFilterContext* volume_ctx = nullptr;
            check_error(avfilter_graph_create_filter(&volume_ctx, volume, "drive",
                                                      volume_args.c_str(), nullptr,
                                                      filter_graph_.get()),
                        "Creating drive filter");

            check_error(avfilter_link(last_filter, 0, volume_ctx, 0),
                       "Linking to drive filter");
            last_filter = volume_ctx;
        }

        // 2. Apply distortion based on type
        if (params_.type == DistortionType::BITCRUSHER) {
            // Bitcrusher: reduce bit depth
            const auto* aformat = avfilter_get_by_name("aformat");
            if (!aformat) {
                throw std::runtime_error("aformat filter not found");
            }

            // Simulate bitcrushing by converting to lower bit depth and back
            std::string sample_fmt_str;
            if (params_.bits <= 8) {
                sample_fmt_str = "u8";
            } else {
                sample_fmt_str = "s16";
            }

            const auto format_args = std::format("sample_fmts={}", sample_fmt_str);
            AVFilterContext* format_ctx = nullptr;
            check_error(avfilter_graph_create_filter(&format_ctx, aformat, "bitcrush",
                                                      format_args.c_str(), nullptr,
                                                      filter_graph_.get()),
                        "Creating bitcrusher filter");

            check_error(avfilter_link(last_filter, 0, format_ctx, 0),
                       "Linking to bitcrusher");
            last_filter = format_ctx;

            // Convert back to original format
            const auto format_back_args = std::format("sample_fmts={}",
                                                      av_get_sample_fmt_name(sample_fmt_));
            AVFilterContext* format_back_ctx = nullptr;
            check_error(avfilter_graph_create_filter(&format_back_ctx, aformat, "format_back",
                                                      format_back_args.c_str(), nullptr,
                                                      filter_graph_.get()),
                        "Creating format restoration filter");

            check_error(avfilter_link(last_filter, 0, format_back_ctx, 0),
                       "Linking format restoration");
            last_filter = format_back_ctx;
        } else {
            // Use audio limiter with different settings for different distortion types
            const auto* alimiter = avfilter_get_by_name("alimiter");
            if (!alimiter) {
                throw std::runtime_error("alimiter filter not found");
            }

            double limit = 1.0;
            double attack = 5.0;
            double release = 50.0;

            switch (params_.type) {
                case DistortionType::OVERDRIVE:
                    limit = 0.7;
                    attack = 5.0;
                    release = 50.0;
                    break;
                case DistortionType::FUZZ:
                    limit = 0.3;
                    attack = 0.1;
                    release = 10.0;
                    break;
                case DistortionType::TUBE:
                    limit = 0.8;
                    attack = 10.0;
                    release = 100.0;
                    break;
                case DistortionType::HARD_CLIP:
                    limit = 0.5;
                    attack = 0.1;
                    release = 5.0;
                    break;
                case DistortionType::SOFT_CLIP:
                    limit = 0.9;
                    attack = 20.0;
                    release = 200.0;
                    break;
                default:
                    break;
            }

            const auto limiter_args = std::format("limit={}:attack={}:release={}",
                                                  limit, attack, release);
            AVFilterContext* limiter_ctx = nullptr;
            check_error(avfilter_graph_create_filter(&limiter_ctx, alimiter, "distortion",
                                                      limiter_args.c_str(), nullptr,
                                                      filter_graph_.get()),
                        "Creating distortion filter");

            check_error(avfilter_link(last_filter, 0, limiter_ctx, 0),
                       "Linking to distortion");
            last_filter = limiter_ctx;
        }

        // 3. Apply tone control (simple high-shelf EQ)
        if (params_.tone != 0.5) {
            const auto* highshelf = avfilter_get_by_name("highshelf");
            if (highshelf) {
                // Map tone (0-1) to gain (-12 to +12 dB)
                const double tone_gain = (params_.tone - 0.5) * 24.0;
                const auto eq_args = std::format("frequency=2000:gain={}", tone_gain);

                AVFilterContext* eq_ctx = nullptr;
                check_error(avfilter_graph_create_filter(&eq_ctx, highshelf, "tone",
                                                          eq_args.c_str(), nullptr,
                                                          filter_graph_.get()),
                            "Creating tone filter");

                check_error(avfilter_link(last_filter, 0, eq_ctx, 0),
                           "Linking to tone filter");
                last_filter = eq_ctx;
            }
        }

        // 4. Apply output gain
        if (params_.output_gain != 0.0) {
            const auto* volume = avfilter_get_by_name("volume");
            if (!volume) {
                throw std::runtime_error("volume filter not found");
            }

            const auto volume_args = std::format("volume={}dB", params_.output_gain);
            AVFilterContext* volume_ctx = nullptr;
            check_error(avfilter_graph_create_filter(&volume_ctx, volume, "output_gain",
                                                      volume_args.c_str(), nullptr,
                                                      filter_graph_.get()),
                        "Creating output gain filter");

            check_error(avfilter_link(last_filter, 0, volume_ctx, 0),
                       "Linking to output gain");
            last_filter = volume_ctx;
        }

        // Connect to sink
        check_error(avfilter_link(last_filter, 0, buffersink_ctx, 0),
                   "Linking to sink");

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
                } else if (frame->format == AV_SAMPLE_FMT_U8) {
                    sample = (reinterpret_cast<uint8_t*>(frame->data[0])[i * channels + ch] - 128) / 128.0f;
                } else if (frame->format == AV_SAMPLE_FMT_U8P) {
                    sample = (reinterpret_cast<uint8_t*>(frame->data[ch])[i] - 128) / 128.0f;
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
    DistortionParams params_;

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
Audio Distortion Effect

Usage: {} <input> <output> [options]

Options:
  --preset <name>        Use a preset configuration
                         Available: overdrive, fuzz, tube, hard_clip, bitcrusher
  --type <name>          Distortion type (same as preset names)
  --drive <dB>           Input gain/drive (0-20 dB, default: 5.0)
  --tone <0-1>           Tone control (0=dark, 0.5=neutral, 1=bright)
  --output-gain <dB>     Output level compensation (default: 0.0)
  --bits <1-16>          Bit depth for bitcrusher (default: 8)

Presets:
  overdrive    Warm overdrive (moderate gain, smooth clipping)
  fuzz         Heavy fuzz distortion (extreme gain, aggressive)
  tube         Tube amp simulation (warm, vintage character)
  hard_clip    Hard clipping distortion (digital, aggressive)
  bitcrusher   Lo-fi bit reduction (8-bit, retro sound)

Examples:
  # Apply overdrive preset
  {} input.wav output.wav --preset overdrive

  # Heavy fuzz distortion
  {} input.wav output.wav --preset fuzz

  # Tube amp sound with bright tone
  {} input.wav output.wav --preset tube --tone 0.7

  # Custom distortion
  {} input.wav output.wav --type overdrive --drive 8 --tone 0.6 --output-gain -4

  # 4-bit crusher for extreme lo-fi
  {} input.wav output.wav --preset bitcrusher --bits 4

)",
                                 program_name, program_name, program_name, program_name,
                                 program_name, program_name);
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

        DistortionParams params;

        // Parse arguments
        for (int i = 3; i < argc; ++i) {
            const std::string arg = argv[i];

            if ((arg == "--preset" || arg == "--type") && i + 1 < argc) {
                const std::string preset = argv[++i];
                if (preset == "overdrive") {
                    params = PRESET_OVERDRIVE;
                } else if (preset == "fuzz") {
                    params = PRESET_FUZZ;
                } else if (preset == "tube") {
                    params = PRESET_TUBE;
                } else if (preset == "hard_clip") {
                    params = PRESET_HARD_CLIP;
                } else if (preset == "bitcrusher") {
                    params = PRESET_BITCRUSHER;
                } else {
                    throw std::runtime_error(std::format("Unknown preset: {}", preset));
                }
            } else if (arg == "--drive" && i + 1 < argc) {
                params.drive = std::stod(argv[++i]);
                if (params.drive < 0.0 || params.drive > 20.0) {
                    throw std::runtime_error("Drive must be between 0 and 20 dB");
                }
            } else if (arg == "--tone" && i + 1 < argc) {
                params.tone = std::stod(argv[++i]);
                if (params.tone < 0.0 || params.tone > 1.0) {
                    throw std::runtime_error("Tone must be between 0.0 and 1.0");
                }
            } else if (arg == "--output-gain" && i + 1 < argc) {
                params.output_gain = std::stod(argv[++i]);
            } else if (arg == "--bits" && i + 1 < argc) {
                params.bits = std::stoi(argv[++i]);
                if (params.bits < 1 || params.bits > 16) {
                    throw std::runtime_error("Bits must be between 1 and 16");
                }
            }
        }

        DistortionProcessor processor(input_file, output_file, params);
        processor.process();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }
}
