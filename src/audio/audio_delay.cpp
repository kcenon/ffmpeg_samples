/**
 * Audio Delay/Echo Effect
 *
 * This sample demonstrates how to apply delay and echo effects to audio
 * using modern C++20 and FFmpeg libraries.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <fstream>
#include <format>
#include <string>
#include <string_view>
#include <filesystem>
#include <optional>
#include <vector>

extern "C" {
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

namespace fs = std::filesystem;

namespace {

enum class DelayMode {
    SIMPLE,         // Simple single delay
    MULTITAP,       // Multiple delay taps
    PINGPONG,       // Ping-pong stereo delay
    SLAPBACK,       // Slapback echo (short delay)
    TAPE,           // Tape echo simulation
    REVERSE         // Reverse delay
};

struct DelayParams {
    DelayMode mode = DelayMode::SIMPLE;
    double delay_time = 500.0;      // Delay time in ms (default: 500ms)
    double feedback = 0.5;          // Feedback amount 0.0-1.0 (default: 0.5)
    double mix = 0.5;               // Dry/wet mix 0.0-1.0 (default: 0.5)
    double decay = 0.5;             // Decay rate for tape echo
    int num_taps = 3;               // Number of taps for multitap
    bool tempo_sync = false;        // Sync to tempo (BPM)
    double bpm = 120.0;             // Tempo in BPM
    std::string preset;             // Preset name
};

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <input> <output> [options]\n\n", prog_name);
    std::cout << "Options:\n";
    std::cout << "  -m, --mode <mode>         Delay mode (default: simple)\n";
    std::cout << "                              simple   - Single delay\n";
    std::cout << "                              multitap - Multiple delay taps\n";
    std::cout << "                              pingpong - Stereo ping-pong delay\n";
    std::cout << "                              slapback - Short slapback echo\n";
    std::cout << "                              tape     - Tape echo simulation\n";
    std::cout << "  -d, --delay <ms>          Delay time in milliseconds (default: 500)\n";
    std::cout << "  -f, --feedback <0-1>      Feedback amount (default: 0.5)\n";
    std::cout << "  -x, --mix <0-1>           Dry/wet mix (default: 0.5)\n";
    std::cout << "  -c, --decay <0-1>         Decay rate for tape echo (default: 0.5)\n";
    std::cout << "  -n, --taps <number>       Number of taps for multitap (default: 3)\n";
    std::cout << "  -t, --tempo <bpm>         Sync to tempo in BPM\n";
    std::cout << "  -p, --preset <name>       Use preset configuration\n\n";

    std::cout << "Presets:\n";
    std::cout << "  vocal     - Vocal doubling (short delay, low feedback)\n";
    std::cout << "  slap      - Slapback echo (80-120ms, medium feedback)\n";
    std::cout << "  ambient   - Ambient space (long delay, high feedback)\n";
    std::cout << "  dub       - Dub/reggae delay (medium delay, high feedback)\n";
    std::cout << "  pingpong  - Ping-pong stereo delay\n";
    std::cout << "  tape      - Vintage tape echo\n\n";

    std::cout << "Examples:\n";
    std::cout << std::format("  {} input.wav output.wav\n", prog_name);
    std::cout << "    Apply default simple delay\n\n";

    std::cout << std::format("  {} audio.mp3 delayed.mp3 -p slap\n", prog_name);
    std::cout << "    Apply slapback echo preset\n\n";

    std::cout << std::format("  {} input.wav output.wav -d 250 -f 0.6 -x 0.3\n", prog_name);
    std::cout << "    Custom delay: 250ms, 60% feedback, 30% wet\n\n";

    std::cout << std::format("  {} vocal.wav doubled.wav -p vocal\n", prog_name);
    std::cout << "    Vocal doubling effect\n\n";

    std::cout << std::format("  {} guitar.wav echo.wav -m pingpong -d 375 -f 0.4\n", prog_name);
    std::cout << "    Ping-pong delay for guitar\n\n";

    std::cout << std::format("  {} music.flac output.flac -t 120 -f 0.5\n", prog_name);
    std::cout << "    Tempo-synced delay at 120 BPM (quarter note)\n\n";

    std::cout << "Notes:\n";
    std::cout << "  - Delay time: Duration between original and delayed sound\n";
    std::cout << "  - Feedback: Amount of delayed signal fed back (creates repeats)\n";
    std::cout << "  - Mix: Balance between dry (original) and wet (delayed) signals\n";
    std::cout << "  - Tempo sync: Automatically calculates delay time from BPM\n";
    std::cout << "    Quarter note = 60000/BPM ms, Eighth note = 30000/BPM ms\n\n";

    std::cout << "Common Delay Times:\n";
    std::cout << "  - Vocal doubling: 15-40ms\n";
    std::cout << "  - Slapback echo: 80-120ms\n";
    std::cout << "  - Short delay: 200-400ms\n";
    std::cout << "  - Medium delay: 400-600ms\n";
    std::cout << "  - Long delay: 600-1000ms+\n";
}

DelayMode parse_mode(std::string_view mode_str) {
    if (mode_str == "simple") return DelayMode::SIMPLE;
    if (mode_str == "multitap") return DelayMode::MULTITAP;
    if (mode_str == "pingpong") return DelayMode::PINGPONG;
    if (mode_str == "slapback") return DelayMode::SLAPBACK;
    if (mode_str == "tape") return DelayMode::TAPE;

    throw std::invalid_argument(std::format("Invalid mode: {}", mode_str));
}

std::optional<DelayParams> parse_preset(std::string_view preset) {
    DelayParams params;

    if (preset == "vocal") {
        params.mode = DelayMode::SIMPLE;
        params.delay_time = 30.0;
        params.feedback = 0.2;
        params.mix = 0.3;
    }
    else if (preset == "slap") {
        params.mode = DelayMode::SLAPBACK;
        params.delay_time = 100.0;
        params.feedback = 0.4;
        params.mix = 0.4;
    }
    else if (preset == "ambient") {
        params.mode = DelayMode::SIMPLE;
        params.delay_time = 800.0;
        params.feedback = 0.7;
        params.mix = 0.5;
    }
    else if (preset == "dub") {
        params.mode = DelayMode::SIMPLE;
        params.delay_time = 500.0;
        params.feedback = 0.65;
        params.mix = 0.6;
    }
    else if (preset == "pingpong") {
        params.mode = DelayMode::PINGPONG;
        params.delay_time = 375.0;
        params.feedback = 0.5;
        params.mix = 0.5;
    }
    else if (preset == "tape") {
        params.mode = DelayMode::TAPE;
        params.delay_time = 400.0;
        params.feedback = 0.6;
        params.mix = 0.4;
        params.decay = 0.7;
    }
    else {
        return std::nullopt;
    }

    params.preset = std::string(preset);
    return params;
}

std::optional<DelayParams> parse_arguments(int argc, char* argv[]) {
    DelayParams params;

    for (int i = 3; i < argc; ++i) {
        const std::string_view arg = argv[i];

        if ((arg == "-m" || arg == "--mode") && i + 1 < argc) {
            params.mode = parse_mode(argv[++i]);
        }
        else if ((arg == "-d" || arg == "--delay") && i + 1 < argc) {
            params.delay_time = std::stod(argv[++i]);
        }
        else if ((arg == "-f" || arg == "--feedback") && i + 1 < argc) {
            params.feedback = std::stod(argv[++i]);
        }
        else if ((arg == "-x" || arg == "--mix") && i + 1 < argc) {
            params.mix = std::stod(argv[++i]);
        }
        else if ((arg == "-c" || arg == "--decay") && i + 1 < argc) {
            params.decay = std::stod(argv[++i]);
        }
        else if ((arg == "-n" || arg == "--taps") && i + 1 < argc) {
            params.num_taps = std::stoi(argv[++i]);
        }
        else if ((arg == "-t" || arg == "--tempo") && i + 1 < argc) {
            params.tempo_sync = true;
            params.bpm = std::stod(argv[++i]);
            // Quarter note delay
            params.delay_time = 60000.0 / params.bpm;
        }
        else if ((arg == "-p" || arg == "--preset") && i + 1 < argc) {
            const auto preset = parse_preset(argv[++i]);
            if (!preset) {
                std::cerr << std::format("Error: Invalid preset '{}'\n", argv[i]);
                return std::nullopt;
            }
            params = *preset;
        }
        else {
            std::cerr << std::format("Error: Unknown option '{}'\n", arg);
            return std::nullopt;
        }
    }

    // Clamp values
    params.feedback = std::clamp(params.feedback, 0.0, 0.99);
    params.mix = std::clamp(params.mix, 0.0, 1.0);
    params.decay = std::clamp(params.decay, 0.0, 1.0);
    params.num_taps = std::clamp(params.num_taps, 1, 8);

    return params;
}

class AudioDelay {
public:
    AudioDelay(std::string_view input_file, const fs::path& output_file,
              const DelayParams& params)
        : input_file_(input_file)
        , output_file_(output_file)
        , params_(params)
        , input_format_ctx_(ffmpeg::open_input_format(input_file.data()))
        , input_packet_(ffmpeg::create_packet())
        , input_frame_(ffmpeg::create_frame())
        , filtered_frame_(ffmpeg::create_frame()) {

        initialize_decoder();
    }

    void process() {
        std::cout << "Audio Delay/Echo\n";
        std::cout << "================\n\n";
        std::cout << std::format("Input: {}\n", input_file_);
        std::cout << std::format("Output: {}\n", output_file_.string());

        if (!params_.preset.empty()) {
            std::cout << std::format("Preset: {}\n", params_.preset);
        }

        std::cout << std::format("Mode: {}\n", get_mode_name());
        std::cout << std::format("Delay Time: {:.1f} ms\n", params_.delay_time);

        if (params_.tempo_sync) {
            std::cout << std::format("Tempo Sync: {} BPM (quarter note)\n", params_.bpm);
        }

        std::cout << std::format("Feedback: {:.0f}%\n", params_.feedback * 100);
        std::cout << std::format("Mix: {:.0f}%\n", params_.mix * 100);

        if (params_.mode == DelayMode::TAPE) {
            std::cout << std::format("Decay: {:.0f}%\n", params_.decay * 100);
        }

        if (params_.mode == DelayMode::MULTITAP) {
            std::cout << std::format("Number of Taps: {}\n", params_.num_taps);
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

        // Flush pipeline
        flush_pipeline();

        std::cout << std::format("\nProcessed {} frames\n", frame_count);
        std::cout << "\nDelay effect applied successfully!\n";
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
            case DelayMode::SIMPLE: return "Simple Delay";
            case DelayMode::MULTITAP: return "Multi-tap Delay";
            case DelayMode::PINGPONG: return "Ping-pong Delay";
            case DelayMode::SLAPBACK: return "Slapback Echo";
            case DelayMode::TAPE: return "Tape Echo";
            case DelayMode::REVERSE: return "Reverse Delay";
            default: return "Unknown";
        }
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

        // Build filter description using aecho
        // aecho parameters:
        // in_gain: input gain (0-1, default: 0.6)
        // out_gain: output gain (0-1, default: 0.3)
        // delays: delay times in ms separated by |
        // decays: decay factors (0-1) separated by |

        std::string filter_desc;

        switch (params_.mode) {
            case DelayMode::SIMPLE:
            case DelayMode::SLAPBACK: {
                // Simple single delay
                filter_desc = std::format(
                    "aecho={}:{}:{}:{}",
                    1.0 - params_.mix,           // in_gain (dry)
                    params_.mix,                 // out_gain (wet)
                    params_.delay_time,          // delays
                    params_.feedback             // decays
                );
                break;
            }
            case DelayMode::MULTITAP: {
                // Multiple delay taps
                std::string delays;
                std::string decays;
                for (int i = 1; i <= params_.num_taps; ++i) {
                    if (i > 1) {
                        delays += "|";
                        decays += "|";
                    }
                    delays += std::format("{:.0f}", params_.delay_time * i);
                    decays += std::format("{:.2f}", params_.feedback / i);
                }
                filter_desc = std::format(
                    "aecho={}:{}:{}:{}",
                    1.0 - params_.mix,
                    params_.mix,
                    delays,
                    decays
                );
                break;
            }
            case DelayMode::PINGPONG: {
                // Ping-pong delay (alternating left-right)
                filter_desc = std::format(
                    "aecho={}:{}:{}|{}:{}|{}",
                    1.0 - params_.mix,
                    params_.mix,
                    params_.delay_time,
                    params_.delay_time * 2,
                    params_.feedback,
                    params_.feedback * 0.7
                );
                break;
            }
            case DelayMode::TAPE: {
                // Tape echo with decay
                const double decay_factor = params_.decay * params_.feedback;
                filter_desc = std::format(
                    "aecho={}:{}:{}|{}|{}:{}|{}|{}",
                    1.0 - params_.mix,
                    params_.mix,
                    params_.delay_time,
                    params_.delay_time * 2,
                    params_.delay_time * 3,
                    params_.feedback,
                    decay_factor,
                    decay_factor * 0.7
                );
                break;
            }
            default: {
                filter_desc = std::format(
                    "aecho={}:{}:{}:{}",
                    1.0 - params_.mix,
                    params_.mix,
                    params_.delay_time,
                    params_.feedback
                );
                break;
            }
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

    std::string input_file_;
    fs::path output_file_;
    DelayParams params_;

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

        AudioDelay delay(input, output, *params);
        delay.process();

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }
}
