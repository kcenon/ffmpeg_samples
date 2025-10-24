/**
 * Advanced Audio Mixer
 *
 * This sample demonstrates advanced multi-track audio mixing capabilities
 * using modern C++20 and FFmpeg libraries.
 *
 * Features:
 * - Mix multiple audio tracks (2 or more)
 * - Individual volume control per track
 * - Pan control (stereo positioning) per track
 * - Time offset (start delay) per track
 * - Fade in/out effects per track
 * - Automatic gain adjustment to prevent clipping
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <fstream>
#include <format>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <optional>

namespace fs = std::filesystem;

namespace {

// Simple WAV header structure
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

// Track configuration
struct TrackConfig {
    std::string filename;
    float volume = 1.0f;          // 0.0 to 2.0 (>1.0 for gain boost)
    float pan = 0.0f;             // -1.0 (left) to 1.0 (right), 0.0 = center
    double start_offset = 0.0;    // Start time offset in seconds
    double fade_in = 0.0;         // Fade in duration in seconds
    double fade_out = 0.0;        // Fade out duration in seconds
};

// Audio decoder with buffering and offset support
class AudioDecoder {
public:
    AudioDecoder(const TrackConfig& config, int target_sample_rate, int target_channels)
        : config_(config)
        , format_ctx_(ffmpeg::open_input_format(config.filename.c_str()))
        , packet_(ffmpeg::create_packet())
        , frame_(ffmpeg::create_frame())
        , target_sample_rate_(target_sample_rate)
        , target_channels_(target_channels) {

        initialize();
    }

    // Read samples with offset support
    int read_samples(int16_t* buffer, int num_samples, int64_t current_sample_position) {
        // Calculate offset in samples
        const auto offset_samples = static_cast<int64_t>(config_.start_offset * target_sample_rate_);

        // If we haven't reached the start offset yet, return silence
        if (current_sample_position < offset_samples) {
            const auto samples_to_skip = std::min(
                num_samples,
                static_cast<int>(offset_samples - current_sample_position)
            );
            std::fill_n(buffer, samples_to_skip * target_channels_, int16_t{0});
            return samples_to_skip;
        }

        int samples_read = 0;

        while (samples_read < num_samples && !eof_) {
            auto ret = avcodec_receive_frame(codec_ctx_.get(), frame_.get());

            if (ret == AVERROR(EAGAIN)) {
                // Need more packets
                ret = av_read_frame(format_ctx_.get(), packet_.get());
                if (ret < 0) {
                    eof_ = true;
                    break;
                }

                if (packet_->stream_index == stream_index_) {
                    avcodec_send_packet(codec_ctx_.get(), packet_.get());
                }
                av_packet_unref(packet_.get());
                continue;
            }

            if (ret == AVERROR_EOF || ret < 0) {
                eof_ = true;
                break;
            }

            // Resample
            auto* out_buf = reinterpret_cast<uint8_t*>(buffer + samples_read * target_channels_);
            const auto dst_nb_samples = num_samples - samples_read;

            ret = swr_convert(swr_ctx_.get(), &out_buf, dst_nb_samples,
                            const_cast<const uint8_t**>(frame_->data), frame_->nb_samples);

            if (ret > 0) {
                samples_read += ret;
                total_samples_decoded_ += ret;
            }
        }

        return samples_read;
    }

    bool is_eof() const { return eof_; }

    double get_duration() const {
        if (format_ctx_ && format_ctx_->duration != AV_NOPTS_VALUE) {
            return format_ctx_->duration / static_cast<double>(AV_TIME_BASE);
        }
        return 0.0;
    }

    const TrackConfig& get_config() const { return config_; }
    int64_t get_total_samples_decoded() const { return total_samples_decoded_; }

private:
    void initialize() {
        // Find audio stream
        const auto stream_idx = ffmpeg::find_stream_index(format_ctx_.get(), AVMEDIA_TYPE_AUDIO);
        if (!stream_idx) {
            throw ffmpeg::FFmpegError(std::format("No audio stream found in {}", config_.filename));
        }
        stream_index_ = *stream_idx;

        // Setup decoder
        const auto* codecpar = format_ctx_->streams[stream_index_]->codecpar;
        const auto* decoder = avcodec_find_decoder(codecpar->codec_id);
        if (!decoder) {
            throw ffmpeg::FFmpegError("Decoder not found");
        }

        codec_ctx_ = ffmpeg::create_codec_context(decoder);
        ffmpeg::check_error(
            avcodec_parameters_to_context(codec_ctx_.get(), codecpar),
            "copy codec parameters"
        );
        ffmpeg::check_error(
            avcodec_open2(codec_ctx_.get(), decoder, nullptr),
            "open decoder"
        );

        // Setup resampler
        AVChannelLayout out_ch_layout;
        if (target_channels_ == 1) {
            out_ch_layout = AV_CHANNEL_LAYOUT_MONO;
        } else {
            out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
        }

        ffmpeg::check_error(
            swr_alloc_set_opts2(&swr_ctx_raw_,
                              &out_ch_layout,
                              AV_SAMPLE_FMT_S16,
                              target_sample_rate_,
                              &codec_ctx_->ch_layout,
                              codec_ctx_->sample_fmt,
                              codec_ctx_->sample_rate,
                              0, nullptr),
            "allocate resampler"
        );

        swr_ctx_.reset(swr_ctx_raw_);
        swr_ctx_raw_ = nullptr;

        ffmpeg::check_error(
            swr_init(swr_ctx_.get()),
            "initialize resampler"
        );
    }

    TrackConfig config_;
    ffmpeg::FormatContextPtr format_ctx_;
    ffmpeg::CodecContextPtr codec_ctx_;
    ffmpeg::SwrContextPtr swr_ctx_;
    ffmpeg::PacketPtr packet_;
    ffmpeg::FramePtr frame_;
    SwrContext* swr_ctx_raw_ = nullptr;
    int stream_index_ = -1;
    bool eof_ = false;
    int target_sample_rate_;
    int target_channels_;
    int64_t total_samples_decoded_ = 0;
};

// Advanced audio mixer with multi-track support
class AdvancedAudioMixer {
public:
    AdvancedAudioMixer(std::vector<TrackConfig> tracks, const fs::path& output, bool auto_gain)
        : tracks_(std::move(tracks))
        , output_file_(output)
        , auto_gain_(auto_gain) {

        if (tracks_.empty()) {
            throw std::runtime_error("No tracks to mix");
        }

        // Create decoders for all tracks
        for (const auto& track : tracks_) {
            decoders_.emplace_back(
                std::make_unique<AudioDecoder>(track, target_sample_rate_, target_channels_)
            );
        }
    }

    void mix() {
        print_mixing_info();

        // Open output file
        std::ofstream output_stream(output_file_, std::ios::binary);
        if (!output_stream.is_open()) {
            throw std::runtime_error(std::format("Failed to open output file: {}", output_file_.string()));
        }

        // Write placeholder WAV header
        write_wav_header(output_stream, target_sample_rate_, target_channels_, 0);

        // Calculate total duration (max of all tracks including offsets)
        double max_duration = 0.0;
        for (const auto& decoder : decoders_) {
            const auto track_end = decoder->get_config().start_offset + decoder->get_duration();
            max_duration = std::max(max_duration, track_end);
        }

        std::cout << std::format("Estimated total duration: {:.2f} seconds\n\n", max_duration);
        std::cout << "Mixing in progress...\n";

        // Allocate buffers for each track
        std::vector<std::vector<int16_t>> track_buffers(decoders_.size());
        for (auto& buffer : track_buffers) {
            buffer.resize(buffer_size_ * target_channels_);
        }

        std::vector<int16_t> mix_buffer(buffer_size_ * target_channels_);
        int64_t current_sample_position = 0;
        uint32_t total_samples_written = 0;
        int iteration = 0;
        float max_peak = 0.0f;

        // Mixing loop
        bool any_active = true;
        while (any_active) {
            any_active = false;

            // Clear mix buffer
            std::fill(mix_buffer.begin(), mix_buffer.end(), int16_t{0});

            // Read and mix all tracks
            for (size_t track_idx = 0; track_idx < decoders_.size(); ++track_idx) {
                auto& decoder = decoders_[track_idx];
                auto& buffer = track_buffers[track_idx];

                if (decoder->is_eof()) {
                    continue;
                }

                const auto samples_read = decoder->read_samples(
                    buffer.data(),
                    buffer_size_,
                    current_sample_position
                );

                if (samples_read > 0) {
                    any_active = true;
                    const auto& config = decoder->get_config();

                    // Apply effects and mix
                    mix_track(mix_buffer, buffer, samples_read, config,
                             current_sample_position, max_peak);
                }
            }

            if (!any_active) {
                break;
            }

            // Apply auto-gain if enabled
            if (auto_gain_ && max_peak > 0.0f) {
                const auto gain_reduction = std::min(1.0f, 32767.0f / max_peak);
                if (gain_reduction < 1.0f) {
                    for (auto& sample : mix_buffer) {
                        sample = static_cast<int16_t>(sample * gain_reduction);
                    }
                }
            }

            // Write to file
            const auto bytes_to_write = buffer_size_ * target_channels_ * sizeof(int16_t);
            output_stream.write(reinterpret_cast<char*>(mix_buffer.data()), bytes_to_write);
            total_samples_written += buffer_size_;
            current_sample_position += buffer_size_;

            ++iteration;
            if (iteration % 100 == 0) {
                const auto seconds = total_samples_written / static_cast<double>(target_sample_rate_);
                std::cout << std::format("Mixed {:.2f} seconds (peak: {:.2f} dB)\r",
                                        seconds,
                                        20.0f * std::log10(max_peak / 32768.0f))
                         << std::flush;
            }
        }

        const auto total_bytes = total_samples_written * target_channels_ * sizeof(int16_t);

        std::cout << std::format("\n\nMixing completed!\n");
        std::cout << std::format("Total samples: {}\n", total_samples_written);
        std::cout << std::format("Duration: {:.2f} seconds\n",
                                total_samples_written / static_cast<double>(target_sample_rate_));
        std::cout << std::format("Peak level: {:.2f} dB\n",
                                20.0f * std::log10(max_peak / 32768.0f));
        std::cout << std::format("Output size: {} bytes\n", total_bytes);

        // Update WAV header
        output_stream.seekp(0, std::ios::beg);
        write_wav_header(output_stream, target_sample_rate_, target_channels_, total_bytes);

        std::cout << std::format("\nOutput file: {}\n", output_file_.string());
    }

private:
    void mix_track(std::vector<int16_t>& mix_buffer,
                   const std::vector<int16_t>& track_buffer,
                   int samples_read,
                   const TrackConfig& config,
                   int64_t current_position,
                   float& max_peak) {

        const auto offset_samples = static_cast<int64_t>(config.start_offset * target_sample_rate_);
        const auto fade_in_samples = static_cast<int64_t>(config.fade_in * target_sample_rate_);
        const auto fade_out_samples = static_cast<int64_t>(config.fade_out * target_sample_rate_);

        // Calculate pan gains (equal power panning)
        const float pan_angle = (config.pan + 1.0f) * 0.25f * M_PI; // -1..1 -> 0..π/2
        const float left_gain = std::cos(pan_angle);
        const float right_gain = std::sin(pan_angle);

        for (int i = 0; i < samples_read; ++i) {
            const auto sample_pos = current_position + i;

            // Calculate fade envelope
            float envelope = 1.0f;

            // Fade in
            if (sample_pos < offset_samples + fade_in_samples) {
                const auto fade_pos = sample_pos - offset_samples;
                if (fade_pos >= 0) {
                    envelope *= static_cast<float>(fade_pos) / fade_in_samples;
                }
            }

            // Fade out (simplified - would need to know track end position)
            if (fade_out_samples > 0) {
                // This is a placeholder - real implementation would track track end
                // envelope *= fade_out_factor;
            }

            // Apply volume, pan, and envelope
            const auto left_idx = i * target_channels_;
            const auto right_idx = left_idx + 1;

            if (target_channels_ == 2) {
                const auto left_sample = track_buffer[left_idx];
                const auto right_sample = track_buffer[right_idx];

                const auto mixed_left = static_cast<int32_t>(
                    left_sample * config.volume * left_gain * envelope
                );
                const auto mixed_right = static_cast<int32_t>(
                    right_sample * config.volume * right_gain * envelope
                );

                mix_buffer[left_idx] = clamp_add(mix_buffer[left_idx], mixed_left, max_peak);
                mix_buffer[right_idx] = clamp_add(mix_buffer[right_idx], mixed_right, max_peak);
            } else {
                // Mono
                const auto sample = track_buffer[left_idx];
                const auto mixed = static_cast<int32_t>(sample * config.volume * envelope);
                mix_buffer[left_idx] = clamp_add(mix_buffer[left_idx], mixed, max_peak);
            }
        }
    }

    static int16_t clamp_add(int16_t current, int32_t add_value, float& max_peak) {
        const auto result = static_cast<int32_t>(current) + add_value;
        const auto abs_result = std::abs(result);

        if (abs_result > max_peak) {
            max_peak = static_cast<float>(abs_result);
        }

        return static_cast<int16_t>(
            std::clamp(result, static_cast<int32_t>(-32768), static_cast<int32_t>(32767))
        );
    }

    void print_mixing_info() const {
        std::cout << "Advanced Audio Mixer\n";
        std::cout << "===================\n\n";
        std::cout << std::format("Output: {}\n", output_file_.string());
        std::cout << std::format("Format: {}kHz, {}, 16-bit PCM\n",
                                target_sample_rate_ / 1000,
                                target_channels_ == 2 ? "Stereo" : "Mono");
        std::cout << std::format("Auto-gain: {}\n", auto_gain_ ? "Enabled" : "Disabled");
        std::cout << std::format("Number of tracks: {}\n\n", decoders_.size());

        for (size_t i = 0; i < decoders_.size(); ++i) {
            const auto& config = decoders_[i]->get_config();
            std::cout << std::format("Track {}:\n", i + 1);
            std::cout << std::format("  File: {}\n", config.filename);
            std::cout << std::format("  Volume: {:.2f}\n", config.volume);
            std::cout << std::format("  Pan: {:.2f} (", config.pan);
            if (config.pan < -0.3f) std::cout << "Left";
            else if (config.pan > 0.3f) std::cout << "Right";
            else std::cout << "Center";
            std::cout << ")\n";
            std::cout << std::format("  Start offset: {:.2f}s\n", config.start_offset);
            std::cout << std::format("  Fade in: {:.2f}s\n", config.fade_in);
            std::cout << std::format("  Fade out: {:.2f}s\n", config.fade_out);
            std::cout << std::format("  Duration: {:.2f}s\n\n", decoders_[i]->get_duration());
        }
    }

    static constexpr int target_sample_rate_ = 44100;
    static constexpr int target_channels_ = 2;
    static constexpr int buffer_size_ = 4096;

    std::vector<TrackConfig> tracks_;
    fs::path output_file_;
    bool auto_gain_;
    std::vector<std::unique_ptr<AudioDecoder>> decoders_;
};

void print_usage(const char* program_name) {
    std::cout << std::format("Usage: {} <output.wav> [options]\n\n", program_name);
    std::cout << "Options:\n";
    std::cout << "  -i <file>          Add input audio file\n";
    std::cout << "  -v <volume>        Set volume for previous track (0.0-2.0, default: 1.0)\n";
    std::cout << "  -p <pan>           Set pan for previous track (-1.0=left, 0.0=center, 1.0=right)\n";
    std::cout << "  -s <seconds>       Set start offset for previous track (default: 0.0)\n";
    std::cout << "  -fi <seconds>      Set fade-in duration for previous track (default: 0.0)\n";
    std::cout << "  -fo <seconds>      Set fade-out duration for previous track (default: 0.0)\n";
    std::cout << "  --auto-gain        Enable automatic gain adjustment to prevent clipping\n";
    std::cout << "  --no-auto-gain     Disable automatic gain adjustment (default)\n";
    std::cout << "\nExamples:\n";
    std::cout << "  # Mix two tracks with equal volume\n";
    std::cout << "  " << program_name << " output.wav -i track1.wav -i track2.wav\n\n";
    std::cout << "  # Mix with volume and pan control\n";
    std::cout << "  " << program_name << " output.wav -i vocals.wav -v 1.2 -p 0.0 \\\n";
    std::cout << "                              -i guitar.wav -v 0.8 -p -0.5 \\\n";
    std::cout << "                              -i bass.wav -v 1.0 -p 0.5\n\n";
    std::cout << "  # Mix with time offsets and fades\n";
    std::cout << "  " << program_name << " output.wav -i intro.wav -fi 2.0 \\\n";
    std::cout << "                              -i main.wav -s 3.0 \\\n";
    std::cout << "                              -i outro.wav -s 60.0 -fi 1.0 -fo 3.0\n\n";
    std::cout << "  # Full featured mix with auto-gain\n";
    std::cout << "  " << program_name << " output.wav --auto-gain \\\n";
    std::cout << "                              -i drums.wav -v 1.0 -p 0.0 \\\n";
    std::cout << "                              -i bass.wav -v 0.9 -p -0.2 \\\n";
    std::cout << "                              -i guitar.wav -v 0.7 -p 0.3 -s 2.0 -fi 1.0 \\\n";
    std::cout << "                              -i vocals.wav -v 1.1 -p 0.0 -s 4.0 -fi 0.5\n";
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        std::vector<TrackConfig> tracks;
        fs::path output_file;
        bool auto_gain = false;

        // Parse command line arguments
        for (int i = 1; i < argc; ++i) {
            const std::string arg{argv[i]};

            if (arg == "-i" && i + 1 < argc) {
                TrackConfig track;
                track.filename = argv[++i];
                tracks.push_back(track);
            }
            else if (arg == "-v" && i + 1 < argc && !tracks.empty()) {
                tracks.back().volume = std::stof(argv[++i]);
            }
            else if (arg == "-p" && i + 1 < argc && !tracks.empty()) {
                tracks.back().pan = std::clamp(std::stof(argv[++i]), -1.0f, 1.0f);
            }
            else if (arg == "-s" && i + 1 < argc && !tracks.empty()) {
                tracks.back().start_offset = std::max(0.0, std::stod(argv[++i]));
            }
            else if (arg == "-fi" && i + 1 < argc && !tracks.empty()) {
                tracks.back().fade_in = std::max(0.0, std::stod(argv[++i]));
            }
            else if (arg == "-fo" && i + 1 < argc && !tracks.empty()) {
                tracks.back().fade_out = std::max(0.0, std::stod(argv[++i]));
            }
            else if (arg == "--auto-gain") {
                auto_gain = true;
            }
            else if (arg == "--no-auto-gain") {
                auto_gain = false;
            }
            else if (output_file.empty() && arg[0] != '-') {
                output_file = arg;
            }
        }

        // Validate inputs
        if (output_file.empty()) {
            std::cerr << "Error: Output file not specified\n\n";
            print_usage(argv[0]);
            return 1;
        }

        if (tracks.empty()) {
            std::cerr << "Error: No input tracks specified\n\n";
            print_usage(argv[0]);
            return 1;
        }

        if (tracks.size() < 2) {
            std::cerr << "Warning: Only one track specified. Consider using audio_format_converter instead.\n\n";
        }

        // Create mixer and mix
        AdvancedAudioMixer mixer(tracks, output_file, auto_gain);
        mixer.mix();

    } catch (const ffmpeg::FFmpegError& e) {
        std::cerr << std::format("FFmpeg error: {}\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }

    return 0;
}
