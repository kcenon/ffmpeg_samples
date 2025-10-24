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

struct ChorusParams {
    double in_gain = 0.4;
    double out_gain = 0.4;
    std::string delays = "40|60|80";     // Multiple delay times in ms
    std::string decays = "0.5|0.5|0.5";  // Decay for each voice
    std::string speeds = "0.25|0.4|0.48"; // LFO speed for each voice
    std::string depths = "2|2.3|1.8";    // LFO depth for each voice
};

const ChorusParams PRESET_SUBTLE = {
    .in_gain = 0.5,
    .out_gain = 0.5,
    .delays = "40|50",
    .decays = "0.4|0.4",
    .speeds = "0.25|0.3",
    .depths = "1|1.2"
};

const ChorusParams PRESET_CLASSIC = {
    .in_gain = 0.4,
    .out_gain = 0.4,
    .delays = "40|60|80",
    .decays = "0.5|0.5|0.5",
    .speeds = "0.25|0.4|0.48",
    .depths = "2|2.3|1.8"
};

const ChorusParams PRESET_RICH = {
    .in_gain = 0.3,
    .out_gain = 0.5,
    .delays = "30|50|70|90",
    .decays = "0.4|0.45|0.5|0.45",
    .speeds = "0.2|0.35|0.45|0.6",
    .depths = "1.5|2|2.5|2"
};

const ChorusParams PRESET_WIDE = {
    .in_gain = 0.35,
    .out_gain = 0.45,
    .delays = "35|55|75",
    .decays = "0.5|0.5|0.5",
    .speeds = "0.3|0.5|0.7",
    .depths = "2.5|3|3.5"
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

class ChorusProcessor {
public:
    ChorusProcessor(const std::string& input_file, const std::string& output_file,
                    const ChorusParams& params)
        : input_file_(input_file), output_file_(output_file), params_(params) {}

    void process() {
        open_input();
        find_audio_stream();
        open_decoder();
        setup_filter_graph();
        open_output();
        process_audio();
        finalize_output();

        std::cout << std::format("✓ Chorus effect applied successfully!\n");
        std::cout << std::format("  In gain: {:.2f}\n", params_.in_gain);
        std::cout << std::format("  Out gain: {:.2f}\n", params_.out_gain);
        std::cout << std::format("  Delays: {} ms\n", params_.delays);
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
        const auto* chorus = avfilter_get_by_name("chorus");

        if (!abuffer || !abuffersink || !chorus) {
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

        // Create chorus filter
        const auto chorus_args = std::format(
            "in_gain={}:out_gain={}:delays={}:decays={}:speeds={}:depths={}",
            params_.in_gain, params_.out_gain, params_.delays,
            params_.decays, params_.speeds, params_.depths);

        AVFilterContext* chorus_ctx = nullptr;
        check_error(avfilter_graph_create_filter(&chorus_ctx, chorus, "chorus",
                                                  chorus_args.c_str(), nullptr,
                                                  filter_graph_.get()),
                    "Creating chorus filter");

        // Connect filters: buffer -> chorus -> buffersink
        check_error(avfilter_link(buffersrc_ctx, 0, chorus_ctx, 0), "Linking buffer to chorus");
        check_error(avfilter_link(chorus_ctx, 0, buffersink_ctx, 0),
                    "Linking chorus to sink");

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
    ChorusParams params_;

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
Audio Chorus Effect

Usage: {} <input> <output> [options]

Options:
  --preset <name>        Use a preset configuration
                         Available: subtle, classic, rich, wide
  --in-gain <0-1>        Input gain (default: 0.4)
  --out-gain <0-1>       Output gain (default: 0.4)
  --delays <ms|ms|...>   Delay times in ms, separated by '|'
                         (default: "40|60|80")
  --decays <val|val>     Decay values for each voice (0-1)
                         (default: "0.5|0.5|0.5")
  --speeds <hz|hz>       LFO speeds for each voice in Hz
                         (default: "0.25|0.4|0.48")
  --depths <ms|ms>       LFO depth for each voice in ms
                         (default: "2|2.3|1.8")

Presets:
  subtle       Gentle 2-voice chorus for subtle enhancement
  classic      Standard 3-voice chorus (default sound)
  rich         Lush 4-voice chorus for thick sounds
  wide         Wide stereo 3-voice chorus with deep modulation

Examples:
  # Apply classic chorus preset
  {} input.wav output.wav --preset classic

  # Subtle vocal chorus
  {} input.wav output.wav --preset subtle

  # Rich guitar chorus
  {} input.wav output.wav --preset rich

  # Wide stereo chorus
  {} input.wav output.wav --preset wide

  # Custom 2-voice chorus
  {} input.wav output.wav --delays "50|70" --decays "0.4|0.5" \
                                     --speeds "0.3|0.45" --depths "2|2.5"

Note: The number of values in delays, decays, speeds, and depths must match.

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

        ChorusParams params;

        // Parse arguments
        for (int i = 3; i < argc; ++i) {
            const std::string arg = argv[i];

            if (arg == "--preset" && i + 1 < argc) {
                const std::string preset = argv[++i];
                if (preset == "subtle") {
                    params = PRESET_SUBTLE;
                } else if (preset == "classic") {
                    params = PRESET_CLASSIC;
                } else if (preset == "rich") {
                    params = PRESET_RICH;
                } else if (preset == "wide") {
                    params = PRESET_WIDE;
                } else {
                    throw std::runtime_error(std::format("Unknown preset: {}", preset));
                }
            } else if (arg == "--in-gain" && i + 1 < argc) {
                params.in_gain = std::stod(argv[++i]);
                if (params.in_gain < 0.0 || params.in_gain > 1.0) {
                    throw std::runtime_error("In gain must be between 0.0 and 1.0");
                }
            } else if (arg == "--out-gain" && i + 1 < argc) {
                params.out_gain = std::stod(argv[++i]);
                if (params.out_gain < 0.0 || params.out_gain > 1.0) {
                    throw std::runtime_error("Out gain must be between 0.0 and 1.0");
                }
            } else if (arg == "--delays" && i + 1 < argc) {
                params.delays = argv[++i];
            } else if (arg == "--decays" && i + 1 < argc) {
                params.decays = argv[++i];
            } else if (arg == "--speeds" && i + 1 < argc) {
                params.speeds = argv[++i];
            } else if (arg == "--depths" && i + 1 < argc) {
                params.depths = argv[++i];
            }
        }

        ChorusProcessor processor(input_file, output_file, params);
        processor.process();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }
}
