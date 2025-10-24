/**
 * Audio Ducking (Sidechain Compression)
 *
 * This sample demonstrates automatic ducking, where background music
 * automatically reduces in volume when voice/narration is present.
 * This is also known as sidechain compression.
 *
 * Features:
 * - Automatic volume reduction based on trigger signal
 * - Adjustable threshold and ratio
 * - Configurable attack and release times
 * - Multiple presets for common use cases
 * - Perfect for podcasts, videos, and voiceovers
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
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;

namespace {

// WAV header structure
struct WAVHeader {
    char riff_header[4] = {'R', 'I', 'F', 'F'};
    uint32_t wav_size;
    char wave_header[4] = {'W', 'A', 'V', 'E'};
    char fmt_header[4] = {'f', 'm', 't', ' '};
    uint32_t fmt_chunk_size = 16;
    uint16_t audio_format = 1;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data_header[4] = {'d', 'a', 't', 'a'};
    uint32_t data_bytes;
};

void write_wav_header(std::ofstream& file, int sample_rate, int channels, uint32_t data_size) {
    WAVHeader header;
    header.num_channels = static_cast<uint16_t>(channels);
    header.sample_rate = static_cast<uint32_t>(sample_rate);
    header.bits_per_sample = 16;
    header.byte_rate = static_cast<uint32_t>(sample_rate * channels * 2);
    header.block_align = static_cast<uint16_t>(channels * 2);
    header.data_bytes = data_size;
    header.wav_size = 36 + data_size;

    file.write(reinterpret_cast<const char*>(&header), sizeof(WAVHeader));
}

struct DuckingParams {
    double threshold = -30.0;       // Threshold in dB (when ducking starts)
    double ratio = 4.0;             // Ducking ratio (how much to reduce)
    double attack = 10.0;           // Attack time in ms
    double release = 200.0;         // Release time in ms
    double knee = 2.8;              // Knee width in dB
    double target_level = 0.25;    // Target reduction (0.0-1.0, lower = more ducking)
    std::string preset;
};

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <background> <trigger> <output> [options]\n\n", prog_name);
    std::cout << "Arguments:\n";
    std::cout << "  background    Audio file to be ducked (e.g., music)\n";
    std::cout << "  trigger       Audio file that triggers ducking (e.g., voice)\n";
    std::cout << "  output        Output WAV file\n\n";

    std::cout << "Options:\n";
    std::cout << "  -t, --threshold <dB>     Threshold level in dB (default: -30)\n";
    std::cout << "  -r, --ratio <ratio>      Ducking ratio (default: 4.0)\n";
    std::cout << "  -a, --attack <ms>        Attack time in milliseconds (default: 10)\n";
    std::cout << "  -R, --release <ms>       Release time in milliseconds (default: 200)\n";
    std::cout << "  -k, --knee <dB>          Knee width in dB (default: 2.8)\n";
    std::cout << "  -l, --level <0.0-1.0>    Target reduction level (default: 0.25)\n";
    std::cout << "  -p, --preset <name>      Use preset configuration\n\n";

    std::cout << "Presets:\n";
    std::cout << "  podcast     - Podcast narration over music (gentle ducking)\n";
    std::cout << "  voiceover   - Voiceover for video (moderate ducking)\n";
    std::cout << "  radio       - Radio-style ducking (aggressive)\n";
    std::cout << "  subtle      - Very subtle background reduction\n";
    std::cout << "  aggressive  - Heavy ducking for clear speech\n\n";

    std::cout << "Examples:\n";
    std::cout << std::format("  {} music.wav voice.wav output.wav\n", prog_name);
    std::cout << "    Basic ducking with default settings\n\n";

    std::cout << std::format("  {} bgm.mp3 narration.wav output.wav -p podcast\n", prog_name);
    std::cout << "    Use podcast preset for gentle ducking\n\n";

    std::cout << std::format("  {} music.wav voice.wav output.wav -t -25 -r 6 -a 5 -R 300\n", prog_name);
    std::cout << "    Custom settings: faster attack, slower release\n\n";

    std::cout << std::format("  {} background.wav speech.wav output.wav -p voiceover\n", prog_name);
    std::cout << "    Voiceover preset for video production\n\n";

    std::cout << std::format("  {} music.flac podcast.wav output.wav -p radio\n", prog_name);
    std::cout << "    Radio-style aggressive ducking\n\n";

    std::cout << "Parameter Guide:\n";
    std::cout << "  Threshold:     Level at which ducking begins (-60dB to 0dB)\n";
    std::cout << "                 Lower = triggers more easily\n";
    std::cout << "  Ratio:         Amount of volume reduction (1 to 20)\n";
    std::cout << "                 Higher = more reduction\n";
    std::cout << "  Attack:        How quickly music ducks (1ms to 1000ms)\n";
    std::cout << "                 Faster = more responsive, may sound abrupt\n";
    std::cout << "  Release:       How quickly music returns (10ms to 5000ms)\n";
    std::cout << "                 Slower = smoother, more natural\n";
    std::cout << "  Level:         Target volume during ducking (0.0 to 1.0)\n";
    std::cout << "                 Lower = quieter background\n\n";

    std::cout << "Use Cases:\n";
    std::cout << "  - Podcast production (music under voice)\n";
    std::cout << "  - Video voiceover narration\n";
    std::cout << "  - Radio broadcasting\n";
    std::cout << "  - Tutorial videos\n";
    std::cout << "  - DJ transitions\n";
    std::cout << "  - Conference presentations\n\n";

    std::cout << "Tips:\n";
    std::cout << "  - Set threshold just below normal speech level\n";
    std::cout << "  - Use faster attack for snappy ducking\n";
    std::cout << "  - Use slower release for smooth transitions\n";
    std::cout << "  - Test different ratios to find the right balance\n";
    std::cout << "  - Podcast: gentle (ratio 2-4), smooth release (200-500ms)\n";
    std::cout << "  - Radio: aggressive (ratio 6-10), fast times (5-50ms)\n";
}

std::optional<DuckingParams> parse_preset(std::string_view preset) {
    DuckingParams params;

    if (preset == "podcast") {
        params.threshold = -30.0;
        params.ratio = 3.0;
        params.attack = 15.0;
        params.release = 300.0;
        params.knee = 3.0;
        params.target_level = 0.3;
    }
    else if (preset == "voiceover") {
        params.threshold = -28.0;
        params.ratio = 4.0;
        params.attack = 10.0;
        params.release = 250.0;
        params.knee = 2.5;
        params.target_level = 0.25;
    }
    else if (preset == "radio") {
        params.threshold = -25.0;
        params.ratio = 8.0;
        params.attack = 5.0;
        params.release = 100.0;
        params.knee = 1.5;
        params.target_level = 0.15;
    }
    else if (preset == "subtle") {
        params.threshold = -35.0;
        params.ratio = 2.0;
        params.attack = 20.0;
        params.release = 500.0;
        params.knee = 4.0;
        params.target_level = 0.5;
    }
    else if (preset == "aggressive") {
        params.threshold = -22.0;
        params.ratio = 10.0;
        params.attack = 3.0;
        params.release = 80.0;
        params.knee = 1.0;
        params.target_level = 0.1;
    }
    else {
        return std::nullopt;
    }

    params.preset = std::string(preset);
    return params;
}

// Simple envelope follower for trigger signal
class EnvelopeFollower {
public:
    EnvelopeFollower(double attack_ms, double release_ms, int sample_rate)
        : sample_rate_(sample_rate) {
        attack_coeff_ = std::exp(-1.0 / (attack_ms * 0.001 * sample_rate));
        release_coeff_ = std::exp(-1.0 / (release_ms * 0.001 * sample_rate));
    }

    double process(double input) {
        const double input_abs = std::abs(input);

        if (input_abs > envelope_) {
            // Attack
            envelope_ = attack_coeff_ * envelope_ + (1.0 - attack_coeff_) * input_abs;
        } else {
            // Release
            envelope_ = release_coeff_ * envelope_ + (1.0 - release_coeff_) * input_abs;
        }

        return envelope_;
    }

    void reset() {
        envelope_ = 0.0;
    }

private:
    int sample_rate_;
    double attack_coeff_ = 0.0;
    double release_coeff_ = 0.0;
    double envelope_ = 0.0;
};

class AudioDucker {
public:
    AudioDucker(const fs::path& background_file,
                const fs::path& trigger_file,
                const fs::path& output_file,
                const DuckingParams& params)
        : background_file_(background_file)
        , trigger_file_(trigger_file)
        , output_file_(output_file)
        , params_(params) {
    }

    void process() {
        print_processing_info();

        // Open both input files
        auto bg_format = ffmpeg::open_input_format(background_file_.string().c_str());
        auto trigger_format = ffmpeg::open_input_format(trigger_file_.string().c_str());

        // Setup decoders
        auto [bg_decoder, bg_stream_idx, bg_sample_rate, bg_channels] = setup_decoder(bg_format.get(), "background");
        auto [trigger_decoder, trigger_stream_idx, trigger_sample_rate, trigger_channels] = setup_decoder(trigger_format.get(), "trigger");

        // Use background audio's properties for output
        const int output_sample_rate = bg_sample_rate;
        const int output_channels = bg_channels;

        // Setup resamplers
        auto bg_resampler = setup_resampler(bg_decoder.get(), output_sample_rate, output_channels);
        auto trigger_resampler = setup_resampler(trigger_decoder.get(), output_sample_rate, output_channels);

        // Create envelope follower
        EnvelopeFollower envelope(params_.attack, params_.release, output_sample_rate);

        // Read all audio
        std::cout << "\nReading audio files...\n";
        auto bg_samples = read_all_samples(bg_format.get(), bg_decoder.get(), bg_resampler.get(),
                                          bg_stream_idx, output_channels, "background");
        auto trigger_samples = read_all_samples(trigger_format.get(), trigger_decoder.get(),
                                               trigger_resampler.get(), trigger_stream_idx,
                                               output_channels, "trigger");

        // Make both the same length (pad shorter one with silence)
        const auto max_samples = std::max(bg_samples.size(), trigger_samples.size());
        bg_samples.resize(max_samples, 0);
        trigger_samples.resize(max_samples, 0);

        // Apply ducking
        std::cout << "\nApplying ducking...\n";
        const double threshold_linear = std::pow(10.0, params_.threshold / 20.0);

        for (size_t i = 0; i < max_samples; i += output_channels) {
            // Get trigger level (average of all channels)
            double trigger_level = 0.0;
            for (int ch = 0; ch < output_channels && i + ch < max_samples; ++ch) {
                trigger_level += std::abs(trigger_samples[i + ch] / 32768.0);
            }
            trigger_level /= output_channels;

            // Process through envelope follower
            const double envelope_level = envelope.process(trigger_level);

            // Calculate gain reduction
            double gain = 1.0;
            if (envelope_level > threshold_linear) {
                // Calculate how much over threshold
                const double over_threshold = envelope_level / threshold_linear;
                const double gain_reduction = std::pow(over_threshold, 1.0 / params_.ratio - 1.0);
                gain = std::lerp(gain_reduction, 1.0, 1.0 - params_.target_level);
                gain = std::max(params_.target_level, std::min(1.0, gain));
            }

            // Apply gain to background
            for (int ch = 0; ch < output_channels && i + ch < max_samples; ++ch) {
                const auto mixed = static_cast<int32_t>(bg_samples[i + ch] * gain) +
                                  trigger_samples[i + ch];
                bg_samples[i + ch] = static_cast<int16_t>(
                    std::clamp(mixed, static_cast<int32_t>(-32768), static_cast<int32_t>(32767))
                );
            }

            // Progress
            if (i % (output_sample_rate * output_channels) == 0) {
                const auto seconds = i / (output_sample_rate * output_channels);
                std::cout << std::format("Processing: {}s\r", seconds) << std::flush;
            }
        }

        // Write output
        std::cout << "\nWriting output...\n";
        std::ofstream output_stream(output_file_, std::ios::binary);
        if (!output_stream.is_open()) {
            throw std::runtime_error(std::format("Failed to open output file: {}", output_file_.string()));
        }

        const uint32_t data_size = static_cast<uint32_t>(bg_samples.size() * sizeof(int16_t));
        write_wav_header(output_stream, output_sample_rate, output_channels, data_size);
        output_stream.write(reinterpret_cast<const char*>(bg_samples.data()), data_size);

        const auto total_seconds = bg_samples.size() / (output_sample_rate * output_channels);
        std::cout << std::format("\nDucking completed!\n");
        std::cout << std::format("Duration: {} seconds\n", total_seconds);
        std::cout << std::format("Output: {}\n", output_file_.string());
    }

private:
    struct DecoderInfo {
        ffmpeg::CodecContextPtr decoder;
        int stream_index;
        int sample_rate;
        int channels;
    };

    DecoderInfo setup_decoder(AVFormatContext* format_ctx, const char* label) {
        const auto stream_idx = ffmpeg::find_stream_index(format_ctx, AVMEDIA_TYPE_AUDIO);
        if (!stream_idx) {
            throw ffmpeg::FFmpegError(std::format("No audio stream found in {}", label));
        }

        const auto* codecpar = format_ctx->streams[*stream_idx]->codecpar;
        const auto* decoder = avcodec_find_decoder(codecpar->codec_id);
        if (!decoder) {
            throw ffmpeg::FFmpegError(std::format("Decoder not found for {}", label));
        }

        auto decoder_ctx = ffmpeg::create_codec_context(decoder);
        ffmpeg::check_error(
            avcodec_parameters_to_context(decoder_ctx.get(), codecpar),
            "copy codec parameters"
        );
        ffmpeg::check_error(
            avcodec_open2(decoder_ctx.get(), decoder, nullptr),
            "open decoder"
        );

        return {
            std::move(decoder_ctx),
            *stream_idx,
            codecpar->sample_rate,
            codecpar->ch_layout.nb_channels
        };
    }

    ffmpeg::SwrContextPtr setup_resampler(AVCodecContext* decoder_ctx, int target_rate, int target_channels) {
        AVChannelLayout out_ch_layout;
        if (target_channels == 1) {
            out_ch_layout = AV_CHANNEL_LAYOUT_MONO;
        } else {
            out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
        }

        SwrContext* swr_ctx_raw = nullptr;
        ffmpeg::check_error(
            swr_alloc_set_opts2(&swr_ctx_raw,
                              &out_ch_layout, AV_SAMPLE_FMT_S16, target_rate,
                              &decoder_ctx->ch_layout, decoder_ctx->sample_fmt, decoder_ctx->sample_rate,
                              0, nullptr),
            "allocate resampler"
        );

        ffmpeg::SwrContextPtr swr_ctx(swr_ctx_raw);
        ffmpeg::check_error(swr_init(swr_ctx.get()), "initialize resampler");

        return swr_ctx;
    }

    std::vector<int16_t> read_all_samples(AVFormatContext* format_ctx,
                                          AVCodecContext* decoder_ctx,
                                          SwrContext* resampler,
                                          int stream_index,
                                          int output_channels,
                                          const char* label) {
        std::vector<int16_t> samples;
        auto packet = ffmpeg::create_packet();
        auto frame = ffmpeg::create_frame();
        int64_t total_samples = 0;

        while (av_read_frame(format_ctx, packet.get()) >= 0) {
            if (packet->stream_index == stream_index) {
                avcodec_send_packet(decoder_ctx, packet.get());

                while (avcodec_receive_frame(decoder_ctx, frame.get()) >= 0) {
                    const int max_samples = frame->nb_samples;
                    std::vector<int16_t> buffer(max_samples * output_channels);

                    auto* out_buf = reinterpret_cast<uint8_t*>(buffer.data());
                    const int converted = swr_convert(resampler, &out_buf, max_samples,
                                                     const_cast<const uint8_t**>(frame->data),
                                                     frame->nb_samples);

                    if (converted > 0) {
                        samples.insert(samples.end(), buffer.begin(),
                                     buffer.begin() + converted * output_channels);
                        total_samples += converted;
                    }
                }
            }
            av_packet_unref(packet.get());
        }

        std::cout << std::format("Read {} ({} samples)\n", label, total_samples);
        return samples;
    }

    void print_processing_info() const {
        std::cout << "Audio Ducking (Sidechain Compression)\n";
        std::cout << "=====================================\n\n";
        std::cout << std::format("Background: {}\n", background_file_.string());
        std::cout << std::format("Trigger:    {}\n", trigger_file_.string());
        std::cout << std::format("Output:     {}\n", output_file_.string());

        if (!params_.preset.empty()) {
            std::cout << std::format("\nPreset: {}\n", params_.preset);
        }

        std::cout << "\nDucking Settings:\n";
        std::cout << std::format("  Threshold:     {:.1f} dB\n", params_.threshold);
        std::cout << std::format("  Ratio:         {:.1f}:1\n", params_.ratio);
        std::cout << std::format("  Attack:        {:.1f} ms\n", params_.attack);
        std::cout << std::format("  Release:       {:.1f} ms\n", params_.release);
        std::cout << std::format("  Target Level:  {:.0f}%\n", params_.target_level * 100);
    }

    fs::path background_file_;
    fs::path trigger_file_;
    fs::path output_file_;
    DuckingParams params_;
};

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 4) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        fs::path background_file{argv[1]};
        fs::path trigger_file{argv[2]};
        fs::path output_file{argv[3]};
        DuckingParams params;

        // Parse arguments
        for (int i = 4; i < argc; ++i) {
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
            else if ((arg == "-l" || arg == "--level") && i + 1 < argc) {
                params.target_level = std::clamp(std::stod(argv[++i]), 0.0, 1.0);
            }
        }

        // Validate
        if (!fs::exists(background_file)) {
            std::cerr << std::format("Error: Background file does not exist: {}\n",
                                    background_file.string());
            return 1;
        }
        if (!fs::exists(trigger_file)) {
            std::cerr << std::format("Error: Trigger file does not exist: {}\n",
                                    trigger_file.string());
            return 1;
        }

        // Process
        AudioDucker ducker(background_file, trigger_file, output_file, params);
        ducker.process();

    } catch (const ffmpeg::FFmpegError& e) {
        std::cerr << std::format("FFmpeg error: {}\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }

    return 0;
}
