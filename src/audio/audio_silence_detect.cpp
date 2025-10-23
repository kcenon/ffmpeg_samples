/**
 * Audio Silence Detector
 *
 * This sample demonstrates how to detect silence in audio files
 * using FFmpeg's silencedetect filter with modern C++20.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <fstream>
#include <format>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <chrono>

extern "C" {
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

namespace {

struct SilenceSegment {
    double start_time;      // Start of silence in seconds
    double end_time;        // End of silence in seconds
    double duration;        // Duration in seconds
};

struct DetectionParams {
    double noise_threshold = -50.0;  // Noise threshold in dB (default: -50dB)
    double min_duration = 0.5;       // Minimum silence duration in seconds
    bool export_report = false;       // Export detailed report
    std::string report_file;          // Report output file
};

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <input> [options]\n\n", prog_name);
    std::cout << "Options:\n";
    std::cout << "  -t, --threshold <dB>       Silence threshold in dB (default: -50.0)\n";
    std::cout << "  -d, --duration <seconds>   Minimum silence duration (default: 0.5)\n";
    std::cout << "  -r, --report <file>        Export detailed report to file\n\n";

    std::cout << "Examples:\n";
    std::cout << std::format("  {} audio.mp3\n", prog_name);
    std::cout << "    Detect silence with default settings\n\n";

    std::cout << std::format("  {} audio.wav -t -40 -d 1.0\n", prog_name);
    std::cout << "    Detect silence above -40dB lasting at least 1 second\n\n";

    std::cout << std::format("  {} audio.m4a -t -30 -r report.txt\n", prog_name);
    std::cout << "    Detect silence and export detailed report\n\n";

    std::cout << "Notes:\n";
    std::cout << "  - Threshold: lower values (e.g., -60dB) detect quieter sounds\n";
    std::cout << "  - Duration: increase to ignore short pauses\n";
    std::cout << "  - Report includes timestamps and statistics\n";
}

std::optional<DetectionParams> parse_arguments(int argc, char* argv[]) {
    if (argc < 2) {
        return std::nullopt;
    }

    DetectionParams params;

    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];

        if ((arg == "-t" || arg == "--threshold") && i + 1 < argc) {
            params.noise_threshold = std::stod(argv[++i]);
        }
        else if ((arg == "-d" || arg == "--duration") && i + 1 < argc) {
            params.min_duration = std::stod(argv[++i]);
        }
        else if ((arg == "-r" || arg == "--report") && i + 1 < argc) {
            params.export_report = true;
            params.report_file = argv[++i];
        }
        else {
            std::cerr << std::format("Error: Unknown option '{}'\n", arg);
            return std::nullopt;
        }
    }

    return params;
}

class AudioSilenceDetector {
public:
    AudioSilenceDetector(std::string_view input_file, const DetectionParams& params)
        : input_file_(input_file)
        , params_(params)
        , input_format_ctx_(ffmpeg::open_input_format(input_file.data()))
        , input_packet_(ffmpeg::create_packet())
        , input_frame_(ffmpeg::create_frame())
        , filtered_frame_(ffmpeg::create_frame()) {

        initialize();
    }

    void detect() {
        std::cout << "Audio Silence Detection\n";
        std::cout << "=======================\n\n";
        std::cout << std::format("Input: {}\n", input_file_);
        std::cout << std::format("Threshold: {:.1f} dB\n", params_.noise_threshold);
        std::cout << std::format("Min Duration: {:.2f} seconds\n", params_.min_duration);
        std::cout << std::format("Sample Rate: {} Hz\n", input_codec_ctx_->sample_rate);
        std::cout << std::format("Channels: {}\n\n", input_codec_ctx_->ch_layout.nb_channels);

        // Get total duration
        const double total_duration = static_cast<double>(input_format_ctx_->duration) / AV_TIME_BASE;

        std::cout << std::format("Processing {:.2f} seconds of audio...\n\n", total_duration);

        // Process audio and collect silence segments
        int64_t frame_count = 0;

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
                    break;
                }

                // Pull filtered frames (note: silencedetect doesn't modify frames)
                while (av_buffersink_get_frame(buffersink_ctx_, filtered_frame_.get()) >= 0) {
                    ffmpeg::ScopedFrameUnref filtered_guard(filtered_frame_.get());
                    frame_count++;
                }
            }
        }

        // Flush decoder
        avcodec_send_packet(input_codec_ctx_.get(), nullptr);
        while (avcodec_receive_frame(input_codec_ctx_.get(), input_frame_.get()) >= 0) {
            ffmpeg::ScopedFrameUnref frame_guard(input_frame_.get());

            if (av_buffersrc_add_frame_flags(buffersrc_ctx_, input_frame_.get(),
                                            AV_BUFFERSRC_FLAG_KEEP_REF) >= 0) {
                while (av_buffersink_get_frame(buffersink_ctx_, filtered_frame_.get()) >= 0) {
                    ffmpeg::ScopedFrameUnref filtered_guard(filtered_frame_.get());
                    frame_count++;
                }
            }
        }

        // Flush filter
        if (av_buffersrc_add_frame_flags(buffersrc_ctx_, nullptr, 0) >= 0) {
            while (av_buffersink_get_frame(buffersink_ctx_, filtered_frame_.get()) >= 0) {
                ffmpeg::ScopedFrameUnref filtered_guard(filtered_frame_.get());
            }
        }

        std::cout << std::format("Processed {} frames\n\n", frame_count);
        std::cout << "Detection complete!\n";
        std::cout << "Note: Check FFmpeg output above for silence detection results.\n";
        std::cout << "      Silence segments are logged by the silencedetect filter.\n\n";

        // Export report if requested
        if (params_.export_report) {
            export_report(total_duration);
        }

        print_summary(total_duration);
    }

private:
    void initialize() {
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

        // Setup filter graph
        setup_filter_graph();
    }

    void setup_filter_graph() {
        filter_graph_.reset(avfilter_graph_alloc());
        if (!filter_graph_) {
            throw std::runtime_error("Failed to allocate filter graph");
        }

        // Create buffer source
        const auto* buffersrc = avfilter_get_by_name("abuffer");
        if (!buffersrc) {
            throw std::runtime_error("Failed to find abuffer filter");
        }

        // Build channel layout string
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

        // Build filter description with silencedetect
        const std::string filter_desc = std::format(
            "silencedetect=n={}dB:d={}",
            params_.noise_threshold,
            params_.min_duration);

        std::cout << std::format("Filter: {}\n\n", filter_desc);

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

        // Set log level to info to see silencedetect output
        av_log_set_level(AV_LOG_INFO);
    }

    void export_report(double total_duration) {
        std::ofstream report(params_.report_file);
        if (!report.is_open()) {
            std::cerr << std::format("Warning: Failed to create report file: {}\n",
                                    params_.report_file);
            return;
        }

        report << "Audio Silence Detection Report\n";
        report << "==============================\n\n";

        report << "Input Information:\n";
        report << std::format("  File: {}\n", input_file_);
        report << std::format("  Duration: {:.2f} seconds\n", total_duration);
        report << std::format("  Sample Rate: {} Hz\n", input_codec_ctx_->sample_rate);
        report << std::format("  Channels: {}\n", input_codec_ctx_->ch_layout.nb_channels);
        report << std::format("  Codec: {}\n\n", avcodec_get_name(input_codec_ctx_->codec_id));

        report << "Detection Parameters:\n";
        report << std::format("  Noise Threshold: {:.1f} dB\n", params_.noise_threshold);
        report << std::format("  Minimum Duration: {:.2f} seconds\n\n", params_.min_duration);

        report << "Results:\n";
        report << "  Silence segments are logged in FFmpeg output above.\n";
        report << "  Look for lines starting with '[silencedetect @'.\n\n";

        report << "Note:\n";
        report << "  The silencedetect filter logs results to stderr/stdout.\n";
        report << "  To capture programmatically, parse FFmpeg log output or\n";
        report << "  use custom filter callbacks.\n";

        std::cout << std::format("\nReport exported to: {}\n", params_.report_file);
    }

    void print_summary(double total_duration) {
        std::cout << "\nSummary:\n";
        std::cout << "========\n";
        std::cout << std::format("Total Duration: {:.2f} seconds\n", total_duration);
        std::cout << std::format("Detection completed with threshold {:.1f} dB\n",
                                params_.noise_threshold);
        std::cout << std::format("Minimum silence duration: {:.2f} seconds\n",
                                params_.min_duration);
        std::cout << "\nTip: Redirect stderr to a file to capture silence timestamps:\n";
        std::cout << "     ./audio_silence_detect audio.mp3 2> detection.log\n";
    }

    std::string input_file_;
    DetectionParams params_;

    ffmpeg::FormatContextPtr input_format_ctx_;
    ffmpeg::CodecContextPtr input_codec_ctx_;
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
    if (argc < 2) {
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

        AudioSilenceDetector detector(input, *params);
        detector.detect();

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }
}
