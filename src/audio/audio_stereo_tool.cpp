/**
 * Audio Stereo Tool
 *
 * This sample demonstrates various stereo manipulation techniques
 * using modern C++20 and FFmpeg filters.
 *
 * Features:
 * - Stereo to Mono conversion
 * - Mono to Stereo conversion
 * - Stereo width adjustment (narrow/wide)
 * - Channel swapping (left <-> right)
 * - Mid/Side processing
 * - Phase correction
 * - Balance adjustment
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <fstream>
#include <format>
#include <string>
#include <string_view>
#include <filesystem>
#include <optional>

extern "C" {
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

namespace fs = std::filesystem;

namespace {

enum class StereoOperation {
    WIDTH,              // Adjust stereo width
    TO_MONO,           // Convert stereo to mono
    TO_STEREO,         // Convert mono to stereo (duplicate)
    SWAP_CHANNELS,     // Swap left and right channels
    MID_SIDE,          // Mid/Side processing
    BALANCE,           // Adjust left/right balance
    PHASE_INVERT       // Invert phase of one channel
};

struct StereoParams {
    StereoOperation operation = StereoOperation::WIDTH;

    // Width adjustment (0.0 = mono, 1.0 = normal, 2.0 = wide)
    double width = 1.0;

    // Mid/Side processing
    double mid_gain = 0.0;      // dB
    double side_gain = 0.0;     // dB

    // Balance (-1.0 = full left, 0.0 = center, 1.0 = full right)
    double balance = 0.0;

    // Phase inversion
    bool invert_left = false;
    bool invert_right = false;

    std::string preset;
};

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <input> <output> [options]\n\n", prog_name);
    std::cout << "Operations:\n";
    std::cout << "  --width <value>          Adjust stereo width (0.0-2.0, default: 1.0)\n";
    std::cout << "                             0.0 = mono, 1.0 = normal, 2.0 = wide\n";
    std::cout << "  --to-mono                Convert stereo to mono\n";
    std::cout << "  --to-stereo              Convert mono to stereo (duplicate)\n";
    std::cout << "  --swap                   Swap left and right channels\n";
    std::cout << "  --balance <value>        Adjust L/R balance (-1.0 to 1.0)\n";
    std::cout << "                             -1.0 = full left, 0.0 = center, 1.0 = full right\n";
    std::cout << "  --mid-side               Enable Mid/Side processing\n";
    std::cout << "  --mid-gain <dB>          Mid gain adjustment (default: 0)\n";
    std::cout << "  --side-gain <dB>         Side gain adjustment (default: 0)\n";
    std::cout << "  --phase-invert-left      Invert phase of left channel\n";
    std::cout << "  --phase-invert-right     Invert phase of right channel\n";
    std::cout << "  -p, --preset <name>      Use preset configuration\n\n";

    std::cout << "Presets:\n";
    std::cout << "  narrow      - Narrow stereo image (width: 0.5)\n";
    std::cout << "  wide        - Wide stereo image (width: 1.5)\n";
    std::cout << "  extra-wide  - Extra wide stereo (width: 2.0)\n";
    std::cout << "  mono        - Convert to mono\n";
    std::cout << "  vocal-wide  - Enhance vocal width with mid/side\n";
    std::cout << "  side-boost  - Boost stereo information\n\n";

    std::cout << "Examples:\n";
    std::cout << std::format("  {} stereo.wav wide.wav --width 1.5\n", prog_name);
    std::cout << "    Make stereo image wider\n\n";

    std::cout << std::format("  {} stereo.wav mono.wav --to-mono\n", prog_name);
    std::cout << "    Convert stereo to mono\n\n";

    std::cout << std::format("  {} mono.wav stereo.wav --to-stereo\n", prog_name);
    std::cout << "    Convert mono to stereo (duplicate channels)\n\n";

    std::cout << std::format("  {} input.wav swapped.wav --swap\n", prog_name);
    std::cout << "    Swap left and right channels\n\n";

    std::cout << std::format("  {} stereo.wav balanced.wav --balance -0.3\n", prog_name);
    std::cout << "    Shift balance 30% to the left\n\n";

    std::cout << std::format("  {} music.wav enhanced.wav --mid-side --mid-gain 0 --side-gain 3\n", prog_name);
    std::cout << "    Enhance stereo width using mid/side processing\n\n";

    std::cout << std::format("  {} audio.wav narrow.wav -p narrow\n", prog_name);
    std::cout << "    Use narrow preset\n\n";

    std::cout << std::format("  {} stereo.wav corrected.wav --phase-invert-right\n", prog_name);
    std::cout << "    Invert phase of right channel\n\n";

    std::cout << "Concepts:\n";
    std::cout << "  Stereo Width:   Controls the perceived width of stereo image\n";
    std::cout << "                  Narrow = more focused, Wide = more spacious\n";
    std::cout << "  Mid/Side:       Mid = center (mono), Side = stereo information\n";
    std::cout << "                  Boost side to enhance stereo, boost mid for mono compatibility\n";
    std::cout << "  Balance:        Pan entire mix left or right\n";
    std::cout << "  Phase Invert:   Fix phase issues or create special effects\n\n";

    std::cout << "Use Cases:\n";
    std::cout << "  - Make narrow recordings sound wider\n";
    std::cout << "  - Create mono mixes for compatibility\n";
    std::cout << "  - Fix swapped channels\n";
    std::cout << "  - Enhance or reduce stereo separation\n";
    std::cout << "  - Fix phase issues between channels\n";
    std::cout << "  - Adjust stereo balance\n";
}

std::optional<StereoParams> parse_preset(std::string_view preset) {
    StereoParams params;

    if (preset == "narrow") {
        params.operation = StereoOperation::WIDTH;
        params.width = 0.5;
    }
    else if (preset == "wide") {
        params.operation = StereoOperation::WIDTH;
        params.width = 1.5;
    }
    else if (preset == "extra-wide") {
        params.operation = StereoOperation::WIDTH;
        params.width = 2.0;
    }
    else if (preset == "mono") {
        params.operation = StereoOperation::TO_MONO;
    }
    else if (preset == "vocal-wide") {
        params.operation = StereoOperation::MID_SIDE;
        params.mid_gain = 0.0;
        params.side_gain = 3.0;
    }
    else if (preset == "side-boost") {
        params.operation = StereoOperation::MID_SIDE;
        params.mid_gain = -2.0;
        params.side_gain = 4.0;
    }
    else {
        return std::nullopt;
    }

    params.preset = std::string(preset);
    return params;
}

std::string build_filter_spec(const StereoParams& params) {
    std::string filter_spec;

    switch (params.operation) {
        case StereoOperation::WIDTH:
            // Use stereotools filter for width adjustment
            filter_spec = std::format("stereotools=mlev={}", params.width);
            break;

        case StereoOperation::TO_MONO:
            // Average left and right channels
            filter_spec = "pan=mono|c0=0.5*c0+0.5*c1";
            break;

        case StereoOperation::TO_STEREO:
            // Duplicate mono to both channels
            filter_spec = "pan=stereo|c0=c0|c1=c0";
            break;

        case StereoOperation::SWAP_CHANNELS:
            // Swap left and right
            filter_spec = "pan=stereo|c0=c1|c1=c0";
            break;

        case StereoOperation::MID_SIDE: {
            // Mid/Side processing
            // 1. Convert L/R to M/S
            // 2. Adjust gains
            // 3. Convert back to L/R
            const auto mid_linear = std::pow(10.0, params.mid_gain / 20.0);
            const auto side_linear = std::pow(10.0, params.side_gain / 20.0);

            filter_spec = std::format(
                "pan=stereo|c0={}*c0+{}*c1|c1={}*c0-{}*c1",
                mid_linear, side_linear,
                mid_linear, side_linear
            );
            break;
        }

        case StereoOperation::BALANCE: {
            // Adjust balance by changing relative volumes
            double left_gain = 1.0;
            double right_gain = 1.0;

            if (params.balance < 0) {
                // Shift to left - reduce right
                right_gain = 1.0 + params.balance;
            } else if (params.balance > 0) {
                // Shift to right - reduce left
                left_gain = 1.0 - params.balance;
            }

            filter_spec = std::format(
                "pan=stereo|c0={}*c0|c1={}*c1",
                left_gain, right_gain
            );
            break;
        }

        case StereoOperation::PHASE_INVERT: {
            // Invert phase of specified channels
            if (params.invert_left && params.invert_right) {
                filter_spec = "pan=stereo|c0=-c0|c1=-c1";
            } else if (params.invert_left) {
                filter_spec = "pan=stereo|c0=-c0|c1=c1";
            } else if (params.invert_right) {
                filter_spec = "pan=stereo|c0=c0|c1=-c1";
            } else {
                filter_spec = "anull";  // No change
            }
            break;
        }
    }

    return filter_spec;
}

class StereoProcessor {
public:
    StereoProcessor(const fs::path& input_file,
                    const fs::path& output_file,
                    const StereoParams& params)
        : input_file_(input_file)
        , output_file_(output_file)
        , params_(params)
        , format_ctx_(ffmpeg::open_input_format(input_file.string().c_str()))
        , packet_(ffmpeg::create_packet())
        , frame_(ffmpeg::create_frame())
        , filtered_frame_(ffmpeg::create_frame()) {

        initialize();
    }

    void process() {
        print_processing_info();

        // Determine output format
        auto output_codec_id = (params_.operation == StereoOperation::TO_MONO) ?
                               AV_CODEC_ID_PCM_S16LE : AV_CODEC_ID_PCM_S16LE;

        auto* output_codec = avcodec_find_encoder(output_codec_id);
        if (!output_codec) {
            throw ffmpeg::FFmpegError("PCM encoder not found");
        }

        // Setup encoder
        encoder_ctx_ = ffmpeg::create_codec_context(output_codec);
        encoder_ctx_->sample_rate = decoder_ctx_->sample_rate;

        // Set output channel layout based on operation
        if (params_.operation == StereoOperation::TO_MONO) {
            encoder_ctx_->ch_layout = AV_CHANNEL_LAYOUT_MONO;
        } else if (params_.operation == StereoOperation::TO_STEREO) {
            encoder_ctx_->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
        } else {
            encoder_ctx_->ch_layout = decoder_ctx_->ch_layout;
        }

        encoder_ctx_->sample_fmt = AV_SAMPLE_FMT_S16;
        encoder_ctx_->time_base = {1, decoder_ctx_->sample_rate};

        ffmpeg::check_error(
            avcodec_open2(encoder_ctx_.get(), output_codec, nullptr),
            "open encoder"
        );

        // Create output format context
        AVFormatContext* out_fmt_ctx_raw = nullptr;
        ffmpeg::check_error(
            avformat_alloc_output_context2(&out_fmt_ctx_raw, nullptr,
                                          "wav", output_file_.string().c_str()),
            "allocate output context"
        );
        output_format_ctx_.reset(out_fmt_ctx_raw);

        // Create output stream
        auto* out_stream = avformat_new_stream(output_format_ctx_.get(), nullptr);
        if (!out_stream) {
            throw ffmpeg::FFmpegError("Failed to create output stream");
        }

        ffmpeg::check_error(
            avcodec_parameters_from_context(out_stream->codecpar, encoder_ctx_.get()),
            "copy encoder parameters"
        );

        // Open output file
        ffmpeg::check_error(
            avio_open(&output_format_ctx_->pb, output_file_.string().c_str(),
                     AVIO_FLAG_WRITE),
            "open output file"
        );

        ffmpeg::check_error(
            avformat_write_header(output_format_ctx_.get(), nullptr),
            "write output header"
        );

        // Process audio
        std::cout << "\nProcessing...\n";
        int64_t samples_processed = 0;
        int iteration = 0;

        // Decode, filter, and encode
        while (av_read_frame(format_ctx_.get(), packet_.get()) >= 0) {
            if (packet_->stream_index == audio_stream_index_) {
                ffmpeg::check_error(
                    avcodec_send_packet(decoder_ctx_.get(), packet_.get()),
                    "send packet to decoder"
                );

                while (avcodec_receive_frame(decoder_ctx_.get(), frame_.get()) >= 0) {
                    // Push frame to filter
                    ffmpeg::check_error(
                        av_buffersrc_add_frame_flags(buffersrc_ctx_, frame_.get(),
                                                    AV_BUFFERSRC_FLAG_KEEP_REF),
                        "feed filter graph"
                    );

                    // Pull filtered frames
                    while (av_buffersink_get_frame(buffersink_ctx_, filtered_frame_.get()) >= 0) {
                        // Encode filtered frame
                        encode_and_write_frame(filtered_frame_.get());
                        samples_processed += filtered_frame_->nb_samples;
                        av_frame_unref(filtered_frame_.get());

                        ++iteration;
                        if (iteration % 100 == 0) {
                            const auto seconds = samples_processed /
                                               static_cast<double>(decoder_ctx_->sample_rate);
                            std::cout << std::format("Processed: {:.2f}s\r", seconds)
                                     << std::flush;
                        }
                    }
                }
            }
            av_packet_unref(packet_.get());
        }

        // Flush filter graph
        ffmpeg::check_error(
            av_buffersrc_add_frame_flags(buffersrc_ctx_, nullptr, 0),
            "flush filter graph"
        );

        while (av_buffersink_get_frame(buffersink_ctx_, filtered_frame_.get()) >= 0) {
            encode_and_write_frame(filtered_frame_.get());
            av_frame_unref(filtered_frame_.get());
        }

        // Flush encoder
        flush_encoder();

        // Write trailer
        ffmpeg::check_error(
            av_write_trailer(output_format_ctx_.get()),
            "write output trailer"
        );

        // Close output file
        avio_closep(&output_format_ctx_->pb);

        const auto total_seconds = samples_processed /
                                  static_cast<double>(decoder_ctx_->sample_rate);

        std::cout << std::format("\n\nProcessing completed!\n");
        std::cout << std::format("Duration: {:.2f} seconds\n", total_seconds);
        std::cout << std::format("Output: {}\n", output_file_.string());
    }

private:
    void initialize() {
        // Find audio stream
        const auto stream_idx = ffmpeg::find_stream_index(format_ctx_.get(),
                                                         AVMEDIA_TYPE_AUDIO);
        if (!stream_idx) {
            throw ffmpeg::FFmpegError("No audio stream found");
        }
        audio_stream_index_ = *stream_idx;

        // Setup decoder
        const auto* codecpar = format_ctx_->streams[audio_stream_index_]->codecpar;
        const auto* decoder = avcodec_find_decoder(codecpar->codec_id);
        if (!decoder) {
            throw ffmpeg::FFmpegError("Decoder not found");
        }

        decoder_ctx_ = ffmpeg::create_codec_context(decoder);
        ffmpeg::check_error(
            avcodec_parameters_to_context(decoder_ctx_.get(), codecpar),
            "copy codec parameters"
        );
        ffmpeg::check_error(
            avcodec_open2(decoder_ctx_.get(), decoder, nullptr),
            "open decoder"
        );

        // Setup filter graph
        setup_filter_graph();
    }

    void setup_filter_graph() {
        filter_graph_.reset(avfilter_graph_alloc());
        if (!filter_graph_) {
            throw ffmpeg::FFmpegError("Failed to allocate filter graph");
        }

        // Get channel layout string
        char ch_layout_str[64];
        av_channel_layout_describe(&decoder_ctx_->ch_layout, ch_layout_str,
                                   sizeof(ch_layout_str));

        // Create buffer source
        const auto* buffersrc = avfilter_get_by_name("abuffer");
        const auto args = std::format(
            "sample_rate={}:sample_fmt={}:channel_layout={}:time_base={}/{}",
            decoder_ctx_->sample_rate,
            av_get_sample_fmt_name(decoder_ctx_->sample_fmt),
            ch_layout_str,
            decoder_ctx_->time_base.num,
            decoder_ctx_->time_base.den
        );

        ffmpeg::check_error(
            avfilter_graph_create_filter(&buffersrc_ctx_, buffersrc, "in",
                                        args.c_str(), nullptr, filter_graph_.get()),
            "create buffer source"
        );

        // Create buffer sink
        const auto* buffersink = avfilter_get_by_name("abuffersink");
        ffmpeg::check_error(
            avfilter_graph_create_filter(&buffersink_ctx_, buffersink, "out",
                                        nullptr, nullptr, filter_graph_.get()),
            "create buffer sink"
        );

        // Build filter specification
        const auto filter_spec = build_filter_spec(params_);

        // Parse filter chain
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

        ffmpeg::check_error(
            avfilter_graph_parse_ptr(filter_graph_.get(), filter_spec.c_str(),
                                    &inputs, &outputs, nullptr),
            "parse filter graph"
        );

        ffmpeg::check_error(
            avfilter_graph_config(filter_graph_.get(), nullptr),
            "configure filter graph"
        );

        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
    }

    void encode_and_write_frame(AVFrame* frame) {
        ffmpeg::check_error(
            avcodec_send_frame(encoder_ctx_.get(), frame),
            "send frame to encoder"
        );

        auto out_packet = ffmpeg::create_packet();
        while (avcodec_receive_packet(encoder_ctx_.get(), out_packet.get()) >= 0) {
            out_packet->stream_index = 0;
            av_packet_rescale_ts(out_packet.get(),
                               encoder_ctx_->time_base,
                               output_format_ctx_->streams[0]->time_base);

            ffmpeg::check_error(
                av_interleaved_write_frame(output_format_ctx_.get(), out_packet.get()),
                "write frame"
            );

            av_packet_unref(out_packet.get());
        }
    }

    void flush_encoder() {
        avcodec_send_frame(encoder_ctx_.get(), nullptr);

        auto out_packet = ffmpeg::create_packet();
        while (avcodec_receive_packet(encoder_ctx_.get(), out_packet.get()) >= 0) {
            out_packet->stream_index = 0;
            av_packet_rescale_ts(out_packet.get(),
                               encoder_ctx_->time_base,
                               output_format_ctx_->streams[0]->time_base);

            av_interleaved_write_frame(output_format_ctx_.get(), out_packet.get());
            av_packet_unref(out_packet.get());
        }
    }

    void print_processing_info() const {
        std::cout << "Audio Stereo Tool\n";
        std::cout << "=================\n\n";
        std::cout << std::format("Input:  {}\n", input_file_.string());
        std::cout << std::format("Output: {}\n", output_file_.string());

        if (!params_.preset.empty()) {
            std::cout << std::format("\nPreset: {}\n", params_.preset);
        }

        std::cout << "\nOperation: ";
        switch (params_.operation) {
            case StereoOperation::WIDTH:
                std::cout << std::format("Stereo Width Adjustment ({})\n", params_.width);
                break;
            case StereoOperation::TO_MONO:
                std::cout << "Convert to Mono\n";
                break;
            case StereoOperation::TO_STEREO:
                std::cout << "Convert to Stereo\n";
                break;
            case StereoOperation::SWAP_CHANNELS:
                std::cout << "Swap Channels\n";
                break;
            case StereoOperation::MID_SIDE:
                std::cout << std::format("Mid/Side Processing (Mid: {:+.1f}dB, Side: {:+.1f}dB)\n",
                                        params_.mid_gain, params_.side_gain);
                break;
            case StereoOperation::BALANCE:
                std::cout << std::format("Balance Adjustment ({:+.2f})\n", params_.balance);
                break;
            case StereoOperation::PHASE_INVERT:
                std::cout << "Phase Inversion";
                if (params_.invert_left) std::cout << " (Left)";
                if (params_.invert_right) std::cout << " (Right)";
                std::cout << "\n";
                break;
        }
    }

    fs::path input_file_;
    fs::path output_file_;
    StereoParams params_;

    ffmpeg::FormatContextPtr format_ctx_;
    ffmpeg::FormatContextPtr output_format_ctx_;
    ffmpeg::CodecContextPtr decoder_ctx_;
    ffmpeg::CodecContextPtr encoder_ctx_;
    ffmpeg::PacketPtr packet_;
    ffmpeg::FramePtr frame_;
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
        fs::path input_file{argv[1]};
        fs::path output_file{argv[2]};
        StereoParams params;

        // Parse arguments
        for (int i = 3; i < argc; ++i) {
            std::string arg{argv[i]};

            if ((arg == "-p" || arg == "--preset") && i + 1 < argc) {
                auto preset = parse_preset(argv[++i]);
                if (preset) {
                    params = *preset;
                } else {
                    std::cerr << std::format("Unknown preset: {}\n", argv[i]);
                    return 1;
                }
            }
            else if (arg == "--width" && i + 1 < argc) {
                params.operation = StereoOperation::WIDTH;
                params.width = std::clamp(std::stod(argv[++i]), 0.0, 2.0);
            }
            else if (arg == "--to-mono") {
                params.operation = StereoOperation::TO_MONO;
            }
            else if (arg == "--to-stereo") {
                params.operation = StereoOperation::TO_STEREO;
            }
            else if (arg == "--swap") {
                params.operation = StereoOperation::SWAP_CHANNELS;
            }
            else if (arg == "--balance" && i + 1 < argc) {
                params.operation = StereoOperation::BALANCE;
                params.balance = std::clamp(std::stod(argv[++i]), -1.0, 1.0);
            }
            else if (arg == "--mid-side") {
                params.operation = StereoOperation::MID_SIDE;
            }
            else if (arg == "--mid-gain" && i + 1 < argc) {
                params.mid_gain = std::stod(argv[++i]);
            }
            else if (arg == "--side-gain" && i + 1 < argc) {
                params.side_gain = std::stod(argv[++i]);
            }
            else if (arg == "--phase-invert-left") {
                params.operation = StereoOperation::PHASE_INVERT;
                params.invert_left = true;
            }
            else if (arg == "--phase-invert-right") {
                params.operation = StereoOperation::PHASE_INVERT;
                params.invert_right = true;
            }
        }

        // Validate
        if (!fs::exists(input_file)) {
            std::cerr << std::format("Error: Input file does not exist: {}\n",
                                    input_file.string());
            return 1;
        }

        // Process
        StereoProcessor processor(input_file, output_file, params);
        processor.process();

    } catch (const ffmpeg::FFmpegError& e) {
        std::cerr << std::format("FFmpeg error: {}\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }

    return 0;
}
