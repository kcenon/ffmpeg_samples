/**
 * Audio Gate (Noise Gate)
 *
 * This sample demonstrates how to apply a noise gate to audio using
 * modern C++20 and FFmpeg's agate filter.
 *
 * A noise gate attenuates audio signals below a threshold level,
 * effectively removing background noise and improving audio clarity.
 *
 * Features:
 * - Adjustable threshold level
 * - Configurable attack and release times
 * - Ratio control for gate depth
 * - Knee width for smooth transitions
 * - Various presets for common use cases
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

struct GateParams {
    double threshold = -40.0;       // Threshold level in dB (default: -40dB)
    double ratio = 10.0;            // Gate ratio (default: 10:1)
    double attack = 10.0;           // Attack time in ms (default: 10ms)
    double release = 100.0;         // Release time in ms (default: 100ms)
    double knee = 2.8;              // Knee width in dB (default: 2.8dB)
    double range = -90.0;           // Maximum attenuation in dB (default: -90dB)
    std::string preset;             // Preset name
};

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <input> <output> [options]\n\n", prog_name);
    std::cout << "Options:\n";
    std::cout << "  -t, --threshold <dB>     Threshold level in dB (default: -40)\n";
    std::cout << "  -r, --ratio <ratio>      Gate ratio (default: 10)\n";
    std::cout << "  -a, --attack <ms>        Attack time in milliseconds (default: 10)\n";
    std::cout << "  -R, --release <ms>       Release time in milliseconds (default: 100)\n";
    std::cout << "  -k, --knee <dB>          Knee width in dB (default: 2.8)\n";
    std::cout << "  --range <dB>             Maximum attenuation in dB (default: -90)\n";
    std::cout << "  -p, --preset <name>      Use preset configuration\n\n";

    std::cout << "Presets:\n";
    std::cout << "  vocal       - Vocal recording (threshold: -35dB, fast attack)\n";
    std::cout << "  podcast     - Podcast/speech (threshold: -40dB, moderate release)\n";
    std::cout << "  drum        - Drum recording (threshold: -30dB, very fast attack)\n";
    std::cout << "  guitar      - Guitar/bass (threshold: -45dB, medium attack)\n";
    std::cout << "  gentle      - Gentle gating (threshold: -50dB, slow release)\n";
    std::cout << "  aggressive  - Aggressive gating (threshold: -25dB, fast times)\n\n";

    std::cout << "Examples:\n";
    std::cout << std::format("  {} input.wav output.wav\n", prog_name);
    std::cout << "    Apply default noise gate settings\n\n";

    std::cout << std::format("  {} noisy_audio.mp3 clean.wav -p vocal\n", prog_name);
    std::cout << "    Use vocal preset\n\n";

    std::cout << std::format("  {} recording.wav output.wav -t -35 -r 15 -a 5 -R 150\n", prog_name);
    std::cout << "    Custom settings: -35dB threshold, 15:1 ratio\n\n";

    std::cout << std::format("  {} podcast.wav clean.wav -p podcast\n", prog_name);
    std::cout << "    Optimize for podcast/speech\n\n";

    std::cout << std::format("  {} drums.wav gated.wav -p drum\n", prog_name);
    std::cout << "    Fast gating for drum recordings\n\n";

    std::cout << "Parameter Guide:\n";
    std::cout << "  Threshold:  Level below which gate closes (-60dB to 0dB)\n";
    std::cout << "              Lower = more aggressive, Higher = more gentle\n";
    std::cout << "  Ratio:      Amount of attenuation (1 to 20)\n";
    std::cout << "              Higher = more complete silence when closed\n";
    std::cout << "  Attack:     How quickly gate opens (0.1ms to 1000ms)\n";
    std::cout << "              Faster = more responsive, may click\n";
    std::cout << "  Release:    How quickly gate closes (1ms to 9000ms)\n";
    std::cout << "              Slower = more natural, may leave noise tail\n";
    std::cout << "  Knee:       Transition smoothness around threshold\n";
    std::cout << "              Larger = smoother, more gradual transition\n";
    std::cout << "  Range:      Maximum attenuation when gate is fully closed\n\n";

    std::cout << "Use Cases:\n";
    std::cout << "  - Remove background noise from recordings\n";
    std::cout << "  - Clean up vocal tracks\n";
    std::cout << "  - Reduce room noise in podcasts\n";
    std::cout << "  - Tighten drum recordings\n";
    std::cout << "  - Remove amp hum from guitar recordings\n";
    std::cout << "  - Improve speech intelligibility\n\n";

    std::cout << "Tips:\n";
    std::cout << "  - Set threshold just above noise floor\n";
    std::cout << "  - Use faster attack for transient-rich material (drums)\n";
    std::cout << "  - Use slower release to avoid cutting off natural decay\n";
    std::cout << "  - Increase knee for smoother, less noticeable gating\n";
    std::cout << "  - Test with different presets to find the best sound\n";
}

std::optional<GateParams> parse_preset(std::string_view preset) {
    GateParams params;

    if (preset == "vocal") {
        params.threshold = -35.0;
        params.ratio = 10.0;
        params.attack = 5.0;
        params.release = 100.0;
        params.knee = 2.0;
        params.range = -80.0;
    }
    else if (preset == "podcast") {
        params.threshold = -40.0;
        params.ratio = 8.0;
        params.attack = 10.0;
        params.release = 150.0;
        params.knee = 3.0;
        params.range = -70.0;
    }
    else if (preset == "drum") {
        params.threshold = -30.0;
        params.ratio = 15.0;
        params.attack = 0.5;
        params.release = 50.0;
        params.knee = 1.0;
        params.range = -90.0;
    }
    else if (preset == "guitar") {
        params.threshold = -45.0;
        params.ratio = 10.0;
        params.attack = 10.0;
        params.release = 200.0;
        params.knee = 2.5;
        params.range = -80.0;
    }
    else if (preset == "gentle") {
        params.threshold = -50.0;
        params.ratio = 5.0;
        params.attack = 20.0;
        params.release = 300.0;
        params.knee = 4.0;
        params.range = -60.0;
    }
    else if (preset == "aggressive") {
        params.threshold = -25.0;
        params.ratio = 20.0;
        params.attack = 2.0;
        params.release = 50.0;
        params.knee = 1.0;
        params.range = -96.0;
    }
    else {
        return std::nullopt;
    }

    params.preset = std::string(preset);
    return params;
}

class AudioGate {
public:
    AudioGate(const fs::path& input_file,
              const fs::path& output_file,
              const GateParams& params)
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

        // Open output file
        auto* output_codec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
        if (!output_codec) {
            throw ffmpeg::FFmpegError("PCM encoder not found");
        }

        // Setup encoder
        encoder_ctx_ = ffmpeg::create_codec_context(output_codec);
        encoder_ctx_->sample_rate = decoder_ctx_->sample_rate;
        encoder_ctx_->ch_layout = decoder_ctx_->ch_layout;
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

        std::cout << std::format("\n\nGating completed!\n");
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
        const auto filter_spec = std::format(
            "agate=threshold={}dB:ratio={}:attack={}:release={}:knee={}:range={}dB",
            params_.threshold,
            params_.ratio,
            params_.attack,
            params_.release,
            params_.knee,
            params_.range
        );

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
        std::cout << "Audio Noise Gate\n";
        std::cout << "================\n\n";
        std::cout << std::format("Input:  {}\n", input_file_.string());
        std::cout << std::format("Output: {}\n", output_file_.string());

        if (!params_.preset.empty()) {
            std::cout << std::format("\nPreset: {}\n", params_.preset);
        }

        std::cout << "\nGate Settings:\n";
        std::cout << std::format("  Threshold:  {:.1f} dB\n", params_.threshold);
        std::cout << std::format("  Ratio:      {:.1f}:1\n", params_.ratio);
        std::cout << std::format("  Attack:     {:.1f} ms\n", params_.attack);
        std::cout << std::format("  Release:    {:.1f} ms\n", params_.release);
        std::cout << std::format("  Knee:       {:.1f} dB\n", params_.knee);
        std::cout << std::format("  Range:      {:.1f} dB\n", params_.range);
    }

    fs::path input_file_;
    fs::path output_file_;
    GateParams params_;

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
        GateParams params;

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
            else if ((arg == "-t" || arg == "--threshold") && i + 1 < argc) {
                params.threshold = std::stod(argv[++i]);
            }
            else if ((arg == "-r" || arg == "--ratio") && i + 1 < argc) {
                params.ratio = std::stod(argv[++i]);
            }
            else if ((arg == "-a" || arg == "--attack") && i + 1 < argc) {
                params.attack = std::stod(argv[++i]);
            }
            else if ((arg == "-R" || arg == "--release") && i + 1 < argc) {
                params.release = std::stod(argv[++i]);
            }
            else if ((arg == "-k" || arg == "--knee") && i + 1 < argc) {
                params.knee = std::stod(argv[++i]);
            }
            else if (arg == "--range" && i + 1 < argc) {
                params.range = std::stod(argv[++i]);
            }
        }

        // Validate
        if (!fs::exists(input_file)) {
            std::cerr << std::format("Error: Input file does not exist: {}\n",
                                    input_file.string());
            return 1;
        }

        // Process
        AudioGate gate(input_file, output_file, params);
        gate.process();

    } catch (const ffmpeg::FFmpegError& e) {
        std::cerr << std::format("FFmpeg error: {}\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }

    return 0;
}
