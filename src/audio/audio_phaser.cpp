/**
 * Audio Phaser Effect
 *
 * This sample demonstrates how to apply phaser effect to audio using
 * modern C++20 and FFmpeg's aphaser filter.
 *
 * A phaser creates a sweeping, whooshing sound by mixing the original
 * signal with a phase-shifted copy. The phase shift is modulated by
 * an LFO (Low Frequency Oscillator) to create the characteristic
 * sweeping effect.
 *
 * Features:
 * - Adjustable LFO speed (rate)
 * - Depth control
 * - Feedback amount
 * - Dry/wet mix
 * - Multiple presets for classic sounds
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

struct PhaserParams {
    double in_gain = 0.4;           // Input gain (0.0-1.0)
    double out_gain = 0.74;         // Output gain (0.0-1.0)
    double delay = 3.0;             // Delay in milliseconds (0-5)
    double decay = 0.4;             // Decay/feedback (0.0-0.99)
    double speed = 0.5;             // LFO speed in Hz (0.1-2.0)
    double type_sine = true;        // LFO type: true=sine, false=triangle
    std::string preset;
};

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <input> <output> [options]\n\n", prog_name);
    std::cout << "Options:\n";
    std::cout << "  -s, --speed <Hz>         LFO speed in Hz (0.1-2.0, default: 0.5)\n";
    std::cout << "  -d, --delay <ms>         Delay time in ms (0-5, default: 3.0)\n";
    std::cout << "  -f, --feedback <value>   Feedback amount (0.0-0.99, default: 0.4)\n";
    std::cout << "  -i, --in-gain <value>    Input gain (0.0-1.0, default: 0.4)\n";
    std::cout << "  -o, --out-gain <value>   Output gain (0.0-1.0, default: 0.74)\n";
    std::cout << "  --triangle               Use triangle LFO (default: sine)\n";
    std::cout << "  -p, --preset <name>      Use preset configuration\n\n";

    std::cout << "Presets:\n";
    std::cout << "  classic     - Classic 70s phaser (slow, deep)\n";
    std::cout << "  fast        - Fast sweeping phaser\n";
    std::cout << "  subtle      - Gentle, subtle phasing\n";
    std::cout << "  intense     - Intense, dramatic effect\n";
    std::cout << "  jet         - Jet plane flanging sound\n";
    std::cout << "  psychedelic - Psychedelic rock sound\n\n";

    std::cout << "Examples:\n";
    std::cout << std::format("  {} guitar.wav phased.wav\n", prog_name);
    std::cout << "    Apply classic phaser with default settings\n\n";

    std::cout << std::format("  {} input.wav output.wav -p classic\n", prog_name);
    std::cout << "    Use classic 70s phaser preset\n\n";

    std::cout << std::format("  {} guitar.wav output.wav -s 0.8 -f 0.6\n", prog_name);
    std::cout << "    Fast sweep with more feedback\n\n";

    std::cout << std::format("  {} synth.wav phased.wav -p psychedelic\n", prog_name);
    std::cout << "    Psychedelic rock phaser sound\n\n";

    std::cout << std::format("  {} audio.wav output.wav -s 1.5 --triangle\n", prog_name);
    std::cout << "    Fast phaser with triangle LFO\n\n";

    std::cout << std::format("  {} music.flac phased.flac -p jet\n", prog_name);
    std::cout << "    Jet plane flanging effect\n\n";

    std::cout << "Parameter Guide:\n";
    std::cout << "  Speed:      Rate of the sweeping effect (Hz)\n";
    std::cout << "              Slower = smoother, Faster = more dramatic\n";
    std::cout << "  Delay:      Base delay time affects tone character\n";
    std::cout << "              Higher = deeper, more resonant\n";
    std::cout << "  Feedback:   Amount of processed signal fed back\n";
    std::cout << "              Higher = more intense, metallic sound\n";
    std::cout << "  In Gain:    Input signal level\n";
    std::cout << "              Affects intensity of effect\n";
    std::cout << "  Out Gain:   Output signal level\n";
    std::cout << "              Adjust for desired output volume\n";
    std::cout << "  LFO Type:   Shape of modulation waveform\n";
    std::cout << "              Sine = smooth, Triangle = linear\n\n";

    std::cout << "Use Cases:\n";
    std::cout << "  - Electric guitar processing\n";
    std::cout << "  - Synthesizer enhancement\n";
    std::cout << "  - Psychedelic rock production\n";
    std::cout << "  - Electronic music effects\n";
    std::cout << "  - Vintage sound design\n";
    std::cout << "  - Creative vocal processing\n\n";

    std::cout << "History:\n";
    std::cout << "  The phaser effect became popular in the 1970s, used extensively\n";
    std::cout << "  in psychedelic and progressive rock. Famous examples include:\n";
    std::cout << "  - Pink Floyd's guitar tones\n";
    std::cout << "  - Funkadelic's synthesizer sounds\n";
    std::cout << "  - Jean-Michel Jarre's electronic compositions\n\n";

    std::cout << "Tips:\n";
    std::cout << "  - Start with presets and adjust to taste\n";
    std::cout << "  - Slower speeds (0.3-0.7 Hz) for smooth sweeps\n";
    std::cout << "  - Faster speeds (1.0-2.0 Hz) for dramatic effects\n";
    std::cout << "  - Higher feedback for more intense, metallic sound\n";
    std::cout << "  - Combine with overdrive for classic rock tones\n";
    std::cout << "  - Use triangle wave for sharper, more defined sweeps\n";
}

std::optional<PhaserParams> parse_preset(std::string_view preset) {
    PhaserParams params;

    if (preset == "classic") {
        // Classic 70s phaser - slow, smooth sweep
        params.speed = 0.5;
        params.delay = 3.0;
        params.decay = 0.4;
        params.in_gain = 0.4;
        params.out_gain = 0.74;
        params.type_sine = true;
    }
    else if (preset == "fast") {
        // Fast sweeping phaser
        params.speed = 1.2;
        params.delay = 2.5;
        params.decay = 0.5;
        params.in_gain = 0.5;
        params.out_gain = 0.7;
        params.type_sine = true;
    }
    else if (preset == "subtle") {
        // Gentle, subtle phasing
        params.speed = 0.3;
        params.delay = 2.0;
        params.decay = 0.2;
        params.in_gain = 0.3;
        params.out_gain = 0.8;
        params.type_sine = true;
    }
    else if (preset == "intense") {
        // Intense, dramatic effect
        params.speed = 0.8;
        params.delay = 4.0;
        params.decay = 0.7;
        params.in_gain = 0.6;
        params.out_gain = 0.7;
        params.type_sine = true;
    }
    else if (preset == "jet") {
        // Jet plane flanging sound
        params.speed = 0.4;
        params.delay = 3.5;
        params.decay = 0.9;
        params.in_gain = 0.5;
        params.out_gain = 0.7;
        params.type_sine = false;  // Triangle wave
    }
    else if (preset == "psychedelic") {
        // Psychedelic rock sound
        params.speed = 0.6;
        params.delay = 3.5;
        params.decay = 0.6;
        params.in_gain = 0.5;
        params.out_gain = 0.72;
        params.type_sine = true;
    }
    else {
        return std::nullopt;
    }

    params.preset = std::string(preset);
    return params;
}

class AudioPhaser {
public:
    AudioPhaser(const fs::path& input_file,
                const fs::path& output_file,
                const PhaserParams& params)
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

        std::cout << std::format("\n\nPhaser effect applied!\n");
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

        // Build filter specification for aphaser
        const auto filter_spec = std::format(
            "aphaser=in_gain={}:out_gain={}:delay={}:decay={}:speed={}:type={}",
            params_.in_gain,
            params_.out_gain,
            params_.delay,
            params_.decay,
            params_.speed,
            params_.type_sine ? "t" : "s"  // t=triangular, s=sinusoidal
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
        std::cout << "Audio Phaser Effect\n";
        std::cout << "===================\n\n";
        std::cout << std::format("Input:  {}\n", input_file_.string());
        std::cout << std::format("Output: {}\n", output_file_.string());

        if (!params_.preset.empty()) {
            std::cout << std::format("\nPreset: {}\n", params_.preset);
        }

        std::cout << "\nPhaser Settings:\n";
        std::cout << std::format("  LFO Speed:     {:.2f} Hz\n", params_.speed);
        std::cout << std::format("  Delay:         {:.1f} ms\n", params_.delay);
        std::cout << std::format("  Feedback:      {:.2f}\n", params_.decay);
        std::cout << std::format("  Input Gain:    {:.2f}\n", params_.in_gain);
        std::cout << std::format("  Output Gain:   {:.2f}\n", params_.out_gain);
        std::cout << std::format("  LFO Type:      {}\n",
                                params_.type_sine ? "Sine" : "Triangle");
    }

    fs::path input_file_;
    fs::path output_file_;
    PhaserParams params_;

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
        PhaserParams params;

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
            else if ((arg == "-s" || arg == "--speed") && i + 1 < argc) {
                params.speed = std::clamp(std::stod(argv[++i]), 0.1, 2.0);
            }
            else if ((arg == "-d" || arg == "--delay") && i + 1 < argc) {
                params.delay = std::clamp(std::stod(argv[++i]), 0.0, 5.0);
            }
            else if ((arg == "-f" || arg == "--feedback") && i + 1 < argc) {
                params.decay = std::clamp(std::stod(argv[++i]), 0.0, 0.99);
            }
            else if ((arg == "-i" || arg == "--in-gain") && i + 1 < argc) {
                params.in_gain = std::clamp(std::stod(argv[++i]), 0.0, 1.0);
            }
            else if ((arg == "-o" || arg == "--out-gain") && i + 1 < argc) {
                params.out_gain = std::clamp(std::stod(argv[++i]), 0.0, 1.0);
            }
            else if (arg == "--triangle") {
                params.type_sine = false;
            }
        }

        // Validate
        if (!fs::exists(input_file)) {
            std::cerr << std::format("Error: Input file does not exist: {}\n",
                                    input_file.string());
            return 1;
        }

        // Process
        AudioPhaser phaser(input_file, output_file, params);
        phaser.process();

    } catch (const ffmpeg::FFmpegError& e) {
        std::cerr << std::format("FFmpeg error: {}\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }

    return 0;
}
