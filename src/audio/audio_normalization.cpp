/**
 * Audio Normalization
 *
 * This sample demonstrates how to normalize audio levels using peak
 * normalization and loudness normalization (EBU R128) with modern C++20.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <fstream>
#include <format>
#include <string>
#include <string_view>
#include <filesystem>
#include <optional>
#include <cmath>
#include <algorithm>

extern "C" {
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

namespace fs = std::filesystem;

namespace {

enum class NormalizationMode {
    PEAK,           // Peak normalization
    LOUDNESS,       // EBU R128 loudness normalization
    RMS,            // RMS-based normalization
    TRUEPEAK        // True peak limiting
};

struct NormalizationParams {
    NormalizationMode mode = NormalizationMode::PEAK;
    double target_level = -1.0;     // Target level in dB (peak) or LUFS (loudness)
    double true_peak = -1.0;        // True peak limit in dBTP
    bool dual_pass = false;         // Two-pass analysis + normalization
    bool print_stats = false;       // Print detailed statistics
};

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <input> <output> [options]\n\n", prog_name);
    std::cout << "Options:\n";
    std::cout << "  -m, --mode <mode>         Normalization mode (default: peak)\n";
    std::cout << "                              peak     - Peak normalization\n";
    std::cout << "                              loudness - EBU R128 loudness (LUFS)\n";
    std::cout << "                              rms      - RMS-based normalization\n";
    std::cout << "                              truepeak - True peak limiting\n";
    std::cout << "  -l, --level <dB/LUFS>     Target level (default: -1.0 dB or -23 LUFS)\n";
    std::cout << "  -t, --truepeak <dBTP>     True peak limit (default: -1.0 dBTP)\n";
    std::cout << "  -d, --dual-pass           Enable two-pass processing\n";
    std::cout << "  -s, --stats               Print detailed statistics\n\n";

    std::cout << "Examples:\n";
    std::cout << std::format("  {} input.wav output.wav\n", prog_name);
    std::cout << "    Normalize to -1dB peak level\n\n";

    std::cout << std::format("  {} audio.mp3 normalized.mp3 -m loudness -l -16\n", prog_name);
    std::cout << "    Normalize to -16 LUFS (podcast standard)\n\n";

    std::cout << std::format("  {} podcast.wav output.wav -m loudness -l -19 -t -1.5\n", prog_name);
    std::cout << "    Normalize to -19 LUFS with -1.5 dBTP true peak limit\n\n";

    std::cout << std::format("  {} music.flac output.flac -m peak -l -0.1 -d\n", prog_name);
    std::cout << "    Two-pass peak normalization to -0.1 dB\n\n";

    std::cout << std::format("  {} audio.wav out.wav -m rms -l -20 -s\n", prog_name);
    std::cout << "    RMS normalization to -20 dB with statistics\n\n";

    std::cout << "Standard Levels:\n";
    std::cout << "  Podcast/Voice:    -16 to -19 LUFS\n";
    std::cout << "  Music Streaming:  -14 to -16 LUFS\n";
    std::cout << "  Broadcast:        -23 LUFS (EBU R128)\n";
    std::cout << "  CD Mastering:     -9 to -13 LUFS\n";
    std::cout << "  YouTube:          -13 to -15 LUFS\n\n";

    std::cout << "Notes:\n";
    std::cout << "  - Peak normalization: Simple, but ignores perceived loudness\n";
    std::cout << "  - Loudness normalization: Perceptually accurate (EBU R128)\n";
    std::cout << "  - RMS: Average-based, good for consistent material\n";
    std::cout << "  - True peak: Prevents inter-sample peaks and clipping\n";
    std::cout << "  - Two-pass mode: More accurate but slower\n";
}

NormalizationMode parse_mode(std::string_view mode_str) {
    if (mode_str == "peak") return NormalizationMode::PEAK;
    if (mode_str == "loudness") return NormalizationMode::LOUDNESS;
    if (mode_str == "rms") return NormalizationMode::RMS;
    if (mode_str == "truepeak") return NormalizationMode::TRUEPEAK;

    throw std::invalid_argument(std::format("Invalid mode: {}", mode_str));
}

std::optional<NormalizationParams> parse_arguments(int argc, char* argv[]) {
    if (argc < 3) {
        return std::nullopt;
    }

    NormalizationParams params;

    for (int i = 3; i < argc; ++i) {
        const std::string_view arg = argv[i];

        if ((arg == "-m" || arg == "--mode") && i + 1 < argc) {
            params.mode = parse_mode(argv[++i]);
        }
        else if ((arg == "-l" || arg == "--level") && i + 1 < argc) {
            params.target_level = std::stod(argv[++i]);
        }
        else if ((arg == "-t" || arg == "--truepeak") && i + 1 < argc) {
            params.true_peak = std::stod(argv[++i]);
        }
        else if (arg == "-d" || arg == "--dual-pass") {
            params.dual_pass = true;
        }
        else if (arg == "-s" || arg == "--stats") {
            params.print_stats = true;
        }
        else {
            std::cerr << std::format("Error: Unknown option '{}'\n", arg);
            return std::nullopt;
        }
    }

    // Set default target levels based on mode
    if (params.target_level == -1.0) {
        switch (params.mode) {
            case NormalizationMode::PEAK:
            case NormalizationMode::TRUEPEAK:
                params.target_level = -1.0;  // -1 dB
                break;
            case NormalizationMode::LOUDNESS:
                params.target_level = -23.0;  // -23 LUFS (EBU R128)
                break;
            case NormalizationMode::RMS:
                params.target_level = -20.0;  // -20 dB RMS
                break;
        }
    }

    return params;
}

class AudioNormalizer {
public:
    AudioNormalizer(std::string_view input_file, const fs::path& output_file,
                   const NormalizationParams& params)
        : input_file_(input_file)
        , output_file_(output_file)
        , params_(params)
        , input_format_ctx_(ffmpeg::open_input_format(input_file.data()))
        , input_packet_(ffmpeg::create_packet())
        , input_frame_(ffmpeg::create_frame())
        , filtered_frame_(ffmpeg::create_frame()) {

        initialize_decoder();
    }

    void normalize() {
        std::cout << "Audio Normalization\n";
        std::cout << "===================\n\n";
        std::cout << std::format("Input: {}\n", input_file_);
        std::cout << std::format("Output: {}\n", output_file_.string());
        std::cout << std::format("Mode: {}\n", get_mode_name());
        std::cout << std::format("Target Level: {:.1f} {}\n",
                                params_.target_level,
                                params_.mode == NormalizationMode::LOUDNESS ? "LUFS" : "dB");

        if (params_.true_peak > -100.0) {
            std::cout << std::format("True Peak Limit: {:.1f} dBTP\n", params_.true_peak);
        }

        std::cout << std::format("Processing Mode: {}\n\n",
                                params_.dual_pass ? "Two-pass" : "Single-pass");

        if (params_.dual_pass) {
            normalize_two_pass();
        } else {
            normalize_single_pass();
        }

        std::cout << "\nNormalization completed successfully!\n";
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

        const auto* input_stream = input_format_ctx_->streams[audio_stream_index_];

        // Setup decoder
        const auto* decoder = avcodec_find_decoder(input_stream->codecpar->codec_id);
        if (!decoder) {
            throw std::runtime_error("Failed to find decoder");
        }

        input_codec_ctx_ = ffmpeg::create_codec_context(decoder);
        avcodec_parameters_to_context(input_codec_ctx_.get(), input_stream->codecpar);

        if (avcodec_open2(input_codec_ctx_.get(), decoder, nullptr) < 0) {
            throw std::runtime_error("Failed to open decoder");
        }
    }

    std::string get_mode_name() const {
        switch (params_.mode) {
            case NormalizationMode::PEAK: return "Peak Normalization";
            case NormalizationMode::LOUDNESS: return "Loudness Normalization (EBU R128)";
            case NormalizationMode::RMS: return "RMS Normalization";
            case NormalizationMode::TRUEPEAK: return "True Peak Limiting";
            default: return "Unknown";
        }
    }

    void normalize_single_pass() {
        setup_filter_graph(0.0);  // Will be calculated on-the-fly for peak mode

        // Setup encoder
        initialize_encoder();

        int frame_count = 0;
        double max_peak = 0.0;

        std::cout << "Processing audio...\n";

        while (av_read_frame(input_format_ctx_.get(), input_packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(input_packet_.get());

            if (input_packet_->stream_index != audio_stream_index_) {
                continue;
            }

            if (avcodec_send_packet(input_codec_ctx_.get(), input_packet_.get()) < 0) {
                continue;
            }

            while (avcodec_receive_frame(input_codec_ctx_.get(), input_frame_.get()) >= 0) {
                ffmpeg::ScopedFrameUnref frame_guard(input_frame_.get());

                // Push frame to filter
                if (av_buffersrc_add_frame_flags(buffersrc_ctx_, input_frame_.get(),
                                                AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                    std::cerr << "Error feeding frame to filter\n";
                    continue;
                }

                // Pull filtered frames
                while (av_buffersink_get_frame(buffersink_ctx_, filtered_frame_.get()) >= 0) {
                    ffmpeg::ScopedFrameUnref filtered_guard(filtered_frame_.get());

                    encode_frame(filtered_frame_.get());
                    frame_count++;

                    if (frame_count % 100 == 0) {
                        std::cout << std::format("Processed {} frames\r", frame_count) << std::flush;
                    }
                }
            }
        }

        // Flush decoder and filter
        flush_pipeline();

        std::cout << std::format("\nProcessed {} frames\n", frame_count);

        if (params_.print_stats) {
            print_statistics();
        }
    }

    void normalize_two_pass() {
        std::cout << "Pass 1: Analyzing audio...\n";
        double gain = analyze_audio();

        std::cout << std::format("Analysis complete. Calculated gain: {:.2f} dB\n\n", gain);

        // Reset for second pass
        av_seek_frame(input_format_ctx_.get(), audio_stream_index_, 0, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(input_codec_ctx_.get());

        std::cout << "Pass 2: Applying normalization...\n";

        setup_filter_graph(gain);
        initialize_encoder();

        int frame_count = 0;

        while (av_read_frame(input_format_ctx_.get(), input_packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(input_packet_.get());

            if (input_packet_->stream_index != audio_stream_index_) {
                continue;
            }

            if (avcodec_send_packet(input_codec_ctx_.get(), input_packet_.get()) < 0) {
                continue;
            }

            while (avcodec_receive_frame(input_codec_ctx_.get(), input_frame_.get()) >= 0) {
                ffmpeg::ScopedFrameUnref frame_guard(input_frame_.get());

                if (av_buffersrc_add_frame_flags(buffersrc_ctx_, input_frame_.get(),
                                                AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                    continue;
                }

                while (av_buffersink_get_frame(buffersink_ctx_, filtered_frame_.get()) >= 0) {
                    ffmpeg::ScopedFrameUnref filtered_guard(filtered_frame_.get());
                    encode_frame(filtered_frame_.get());
                    frame_count++;

                    if (frame_count % 100 == 0) {
                        std::cout << std::format("Processed {} frames\r", frame_count) << std::flush;
                    }
                }
            }
        }

        flush_pipeline();
        std::cout << std::format("\nProcessed {} frames\n", frame_count);
    }

    double analyze_audio() {
        double max_peak = 0.0;
        double sum_squares = 0.0;
        int64_t total_samples = 0;

        while (av_read_frame(input_format_ctx_.get(), input_packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(input_packet_.get());

            if (input_packet_->stream_index != audio_stream_index_) {
                continue;
            }

            if (avcodec_send_packet(input_codec_ctx_.get(), input_packet_.get()) < 0) {
                continue;
            }

            while (avcodec_receive_frame(input_codec_ctx_.get(), input_frame_.get()) >= 0) {
                ffmpeg::ScopedFrameUnref frame_guard(input_frame_.get());

                // Analyze samples (assuming float or s16 format)
                const int num_samples = input_frame_->nb_samples;
                const int channels = input_frame_->ch_layout.nb_channels;

                if (input_frame_->format == AV_SAMPLE_FMT_FLT ||
                    input_frame_->format == AV_SAMPLE_FMT_FLTP) {

                    for (int ch = 0; ch < channels; ++ch) {
                        const float* samples = reinterpret_cast<float*>(
                            input_frame_->data[input_frame_->format == AV_SAMPLE_FMT_FLTP ? ch : 0]);

                        for (int i = 0; i < num_samples; ++i) {
                            const int idx = input_frame_->format == AV_SAMPLE_FMT_FLTP ? i : i * channels + ch;
                            const float sample = std::abs(samples[idx]);
                            max_peak = std::max(max_peak, static_cast<double>(sample));
                            sum_squares += sample * sample;
                        }
                    }
                    total_samples += num_samples * channels;
                }
            }
        }

        // Calculate gain based on mode
        double gain = 0.0;

        switch (params_.mode) {
            case NormalizationMode::PEAK:
            case NormalizationMode::TRUEPEAK: {
                if (max_peak > 0.0) {
                    const double current_db = 20.0 * std::log10(max_peak);
                    gain = params_.target_level - current_db;
                }
                break;
            }
            case NormalizationMode::RMS: {
                if (total_samples > 0) {
                    const double rms = std::sqrt(sum_squares / total_samples);
                    if (rms > 0.0) {
                        const double current_db = 20.0 * std::log10(rms);
                        gain = params_.target_level - current_db;
                    }
                }
                break;
            }
            case NormalizationMode::LOUDNESS: {
                // For loudness normalization, we'd typically use the loudnorm filter's two-pass mode
                // This is a simplified version
                if (total_samples > 0) {
                    const double rms = std::sqrt(sum_squares / total_samples);
                    if (rms > 0.0) {
                        const double estimated_lufs = 20.0 * std::log10(rms) - 3.0; // Rough estimate
                        gain = params_.target_level - estimated_lufs;
                    }
                }
                break;
            }
        }

        if (params_.print_stats) {
            std::cout << std::format("  Peak: {:.2f} dB\n", 20.0 * std::log10(max_peak));
            if (total_samples > 0) {
                const double rms = std::sqrt(sum_squares / total_samples);
                std::cout << std::format("  RMS: {:.2f} dB\n", 20.0 * std::log10(rms));
            }
        }

        return gain;
    }

    void setup_filter_graph(double gain) {
        filter_graph_.reset(avfilter_graph_alloc());
        if (!filter_graph_) {
            throw std::runtime_error("Failed to allocate filter graph");
        }

        // Create buffer source
        const auto* buffersrc = avfilter_get_by_name("abuffer");
        if (!buffersrc) {
            throw std::runtime_error("Failed to find abuffer filter");
        }

        char ch_layout_str[64];
        av_channel_layout_describe(&input_codec_ctx_->ch_layout, ch_layout_str, sizeof(ch_layout_str));

        const std::string args = std::format(
            "time_base={}/{}:sample_rate={}:sample_fmt={}:channel_layout={}",
            input_codec_ctx_->time_base.num, input_codec_ctx_->time_base.den,
            input_codec_ctx_->sample_rate,
            av_get_sample_fmt_name(input_codec_ctx_->sample_fmt),
            ch_layout_str);

        if (avfilter_graph_create_filter(&buffersrc_ctx_, buffersrc, "in",
                                         args.c_str(), nullptr, filter_graph_.get()) < 0) {
            throw std::runtime_error("Failed to create buffer source");
        }

        // Create buffer sink
        const auto* buffersink = avfilter_get_by_name("abuffersink");
        if (!buffersink) {
            throw std::runtime_error("Failed to find abuffersink filter");
        }

        if (avfilter_graph_create_filter(&buffersink_ctx_, buffersink, "out",
                                         nullptr, nullptr, filter_graph_.get()) < 0) {
            throw std::runtime_error("Failed to create buffer sink");
        }

        // Build filter description
        std::string filter_desc;

        switch (params_.mode) {
            case NormalizationMode::PEAK:
            case NormalizationMode::RMS:
            case NormalizationMode::TRUEPEAK:
                if (params_.dual_pass) {
                    filter_desc = std::format("volume={}dB", gain);
                } else {
                    // Use dynaudnorm for single-pass normalization
                    filter_desc = "dynaudnorm=f=500:g=31:p=0.95:m=100";
                }
                break;
            case NormalizationMode::LOUDNESS:
                filter_desc = std::format("loudnorm=I={}:TP={}:LRA=11",
                    params_.target_level,
                    params_.true_peak > -100.0 ? params_.true_peak : -1.0);
                break;
        }

        // Parse filter description
        AVFilterInOut* outputs = avfilter_inout_alloc();
        AVFilterInOut* inputs = avfilter_inout_alloc();

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
                                           nullptr, output_file_.string().c_str()) < 0) {
            throw std::runtime_error("Failed to allocate output context");
        }
        output_format_ctx_.reset(output_format_ctx_raw_);

        // Find encoder
        const auto* encoder = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
        if (!encoder) {
            throw std::runtime_error("Failed to find encoder");
        }

        // Create output stream
        auto* stream = avformat_new_stream(output_format_ctx_.get(), nullptr);
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
            if (avio_open(&output_format_ctx_->pb, output_file_.string().c_str(), AVIO_FLAG_WRITE) < 0) {
                throw std::runtime_error("Failed to open output file");
            }
        }

        if (avformat_write_header(output_format_ctx_.get(), nullptr) < 0) {
            throw std::runtime_error("Failed to write header");
        }
    }

    void encode_frame(AVFrame* frame) {
        if (avcodec_send_frame(output_codec_ctx_.get(), frame) < 0) {
            return;
        }

        auto output_packet = ffmpeg::create_packet();
        while (avcodec_receive_packet(output_codec_ctx_.get(), output_packet.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(output_packet.get());
            output_packet->stream_index = 0;
            av_interleaved_write_frame(output_format_ctx_.get(), output_packet.get());
        }
    }

    void flush_pipeline() {
        // Flush decoder
        avcodec_send_packet(input_codec_ctx_.get(), nullptr);
        while (avcodec_receive_frame(input_codec_ctx_.get(), input_frame_.get()) >= 0) {
            ffmpeg::ScopedFrameUnref frame_guard(input_frame_.get());

            if (av_buffersrc_add_frame_flags(buffersrc_ctx_, input_frame_.get(),
                                            AV_BUFFERSRC_FLAG_KEEP_REF) >= 0) {
                while (av_buffersink_get_frame(buffersink_ctx_, filtered_frame_.get()) >= 0) {
                    ffmpeg::ScopedFrameUnref filtered_guard(filtered_frame_.get());
                    encode_frame(filtered_frame_.get());
                }
            }
        }

        // Flush filter
        if (av_buffersrc_add_frame_flags(buffersrc_ctx_, nullptr, 0) >= 0) {
            while (av_buffersink_get_frame(buffersink_ctx_, filtered_frame_.get()) >= 0) {
                ffmpeg::ScopedFrameUnref filtered_guard(filtered_frame_.get());
                encode_frame(filtered_frame_.get());
            }
        }

        // Flush encoder
        avcodec_send_frame(output_codec_ctx_.get(), nullptr);
        auto output_packet = ffmpeg::create_packet();
        while (avcodec_receive_packet(output_codec_ctx_.get(), output_packet.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(output_packet.get());
            output_packet->stream_index = 0;
            av_interleaved_write_frame(output_format_ctx_.get(), output_packet.get());
        }

        av_write_trailer(output_format_ctx_.get());
    }

    void print_statistics() {
        std::cout << "\nStatistics:\n";
        std::cout << "===========\n";
        std::cout << std::format("Input file: {}\n", input_file_);
        std::cout << std::format("Output file: {}\n", output_file_.string());
        std::cout << std::format("Mode: {}\n", get_mode_name());
        std::cout << std::format("Target level: {:.1f} {}\n",
                                params_.target_level,
                                params_.mode == NormalizationMode::LOUDNESS ? "LUFS" : "dB");
    }

    std::string input_file_;
    fs::path output_file_;
    NormalizationParams params_;

    ffmpeg::FormatContextPtr input_format_ctx_;
    ffmpeg::CodecContextPtr input_codec_ctx_;
    ffmpeg::CodecContextPtr output_codec_ctx_;
    ffmpeg::FormatContextPtr output_format_ctx_;
    AVFormatContext* output_format_ctx_raw_ = nullptr;
    ffmpeg::PacketPtr input_packet_;
    ffmpeg::FramePtr input_frame_;
    ffmpeg::FramePtr filtered_frame_;

    ffmpeg::FilterGraphPtr filter_graph_;
    AVFilterContext* buffersrc_ctx_ = nullptr;
    AVFilterContext* buffersink_ctx_ = nullptr;

    int audio_stream_index_ = -1;
};

} // anonymous namespace

int main(int argc, char* argv[]) {
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

        AudioNormalizer normalizer(input, output, *params);
        normalizer.normalize();

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }
}
