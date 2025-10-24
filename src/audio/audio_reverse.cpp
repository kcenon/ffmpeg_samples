/**
 * Audio Reverse
 *
 * This sample demonstrates how to reverse audio playback using
 * modern C++20 and FFmpeg libraries.
 *
 * Features:
 * - Reverse entire audio file
 * - Reverse specific time range
 * - Maintain audio quality
 * - Support for all audio formats
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <fstream>
#include <format>
#include <string>
#include <string_view>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <cmath>

extern "C" {
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

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

struct ReverseParams {
    double start_time = 0.0;        // Start time in seconds (0 = beginning)
    double end_time = -1.0;         // End time in seconds (-1 = end of file)
    bool reverse_all = true;        // Reverse entire file
};

class AudioReverser {
public:
    AudioReverser(const fs::path& input_file,
                  const fs::path& output_file,
                  const ReverseParams& params)
        : input_file_(input_file)
        , output_file_(output_file)
        , params_(params)
        , format_ctx_(ffmpeg::open_input_format(input_file.string().c_str()))
        , packet_(ffmpeg::create_packet())
        , frame_(ffmpeg::create_frame()) {

        initialize();
    }

    void process() {
        print_info();

        // First pass: read all audio samples
        std::cout << "Reading audio...\n";
        read_all_samples();

        // Calculate reverse range
        int64_t start_sample = static_cast<int64_t>(params_.start_time * sample_rate_);
        int64_t end_sample = params_.end_time < 0 ?
                            total_samples_ :
                            static_cast<int64_t>(params_.end_time * sample_rate_);

        // Clamp to valid range
        start_sample = std::clamp(start_sample, int64_t{0}, total_samples_);
        end_sample = std::clamp(end_sample, start_sample, total_samples_);

        std::cout << std::format("Reversing samples {} to {} ({:.2f}s to {:.2f}s)\n",
                                start_sample, end_sample,
                                start_sample / static_cast<double>(sample_rate_),
                                end_sample / static_cast<double>(sample_rate_));

        // Reverse the specified range
        reverse_samples(start_sample, end_sample);

        // Write output
        std::cout << "Writing output...\n";
        write_output();

        std::cout << std::format("\nReverse completed!\n");
        std::cout << std::format("Duration: {:.2f} seconds\n",
                                total_samples_ / static_cast<double>(sample_rate_));
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

        sample_rate_ = decoder_ctx_->sample_rate;
        channels_ = decoder_ctx_->ch_layout.nb_channels;

        // Setup resampler to convert to S16
        setup_resampler();

        // Calculate duration
        if (format_ctx_->duration != AV_NOPTS_VALUE) {
            duration_ = format_ctx_->duration / static_cast<double>(AV_TIME_BASE);
        }
    }

    void setup_resampler() {
        AVChannelLayout out_ch_layout;
        if (channels_ == 1) {
            out_ch_layout = AV_CHANNEL_LAYOUT_MONO;
        } else {
            out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
        }

        SwrContext* swr_ctx_raw = nullptr;
        ffmpeg::check_error(
            swr_alloc_set_opts2(&swr_ctx_raw,
                              &out_ch_layout,
                              AV_SAMPLE_FMT_S16,
                              sample_rate_,
                              &decoder_ctx_->ch_layout,
                              decoder_ctx_->sample_fmt,
                              decoder_ctx_->sample_rate,
                              0, nullptr),
            "allocate resampler"
        );

        swr_ctx_.reset(swr_ctx_raw);

        ffmpeg::check_error(
            swr_init(swr_ctx_.get()),
            "initialize resampler"
        );
    }

    void read_all_samples() {
        int iteration = 0;

        while (av_read_frame(format_ctx_.get(), packet_.get()) >= 0) {
            if (packet_->stream_index == audio_stream_index_) {
                ffmpeg::check_error(
                    avcodec_send_packet(decoder_ctx_.get(), packet_.get()),
                    "send packet to decoder"
                );

                while (avcodec_receive_frame(decoder_ctx_.get(), frame_.get()) >= 0) {
                    // Resample to S16
                    const int max_samples = frame_->nb_samples;
                    std::vector<int16_t> buffer(max_samples * channels_);

                    auto* out_buf = reinterpret_cast<uint8_t*>(buffer.data());
                    const int samples_converted = swr_convert(
                        swr_ctx_.get(),
                        &out_buf,
                        max_samples,
                        const_cast<const uint8_t**>(frame_->data),
                        frame_->nb_samples
                    );

                    if (samples_converted > 0) {
                        // Store samples
                        for (int i = 0; i < samples_converted * channels_; ++i) {
                            all_samples_.push_back(buffer[i]);
                        }
                        total_samples_ += samples_converted;
                    }

                    ++iteration;
                    if (iteration % 100 == 0) {
                        const auto seconds = total_samples_ / static_cast<double>(sample_rate_);
                        std::cout << std::format("Read: {:.2f}s\r", seconds) << std::flush;
                    }
                }
            }
            av_packet_unref(packet_.get());
        }

        std::cout << std::format("Read: {:.2f}s (total samples: {})\n",
                                total_samples_ / static_cast<double>(sample_rate_),
                                total_samples_);
    }

    void reverse_samples(int64_t start_sample, int64_t end_sample) {
        if (params_.reverse_all) {
            // Reverse entire audio
            std::cout << "Reversing entire audio...\n";
            reverse_sample_range(0, all_samples_.size());
        } else {
            // Reverse only specified range
            const auto start_idx = static_cast<size_t>(start_sample * channels_);
            const auto end_idx = static_cast<size_t>(end_sample * channels_);

            if (start_idx < all_samples_.size() && end_idx <= all_samples_.size()) {
                std::cout << std::format("Reversing samples from index {} to {}\n",
                                        start_idx, end_idx);
                reverse_sample_range(start_idx, end_idx);
            } else {
                std::cerr << "Warning: Invalid range, reversing entire audio\n";
                reverse_sample_range(0, all_samples_.size());
            }
        }
    }

    void reverse_sample_range(size_t start_idx, size_t end_idx) {
        // Reverse in frame units (preserving channel order within each frame)
        const size_t num_frames = (end_idx - start_idx) / channels_;

        for (size_t i = 0; i < num_frames / 2; ++i) {
            const size_t front_frame_idx = start_idx + i * channels_;
            const size_t back_frame_idx = start_idx + (num_frames - 1 - i) * channels_;

            // Swap entire frames
            for (int ch = 0; ch < channels_; ++ch) {
                std::swap(all_samples_[front_frame_idx + ch],
                         all_samples_[back_frame_idx + ch]);
            }

            // Progress indicator
            if (i % 100000 == 0) {
                const auto progress = (i * 2 * 100) / num_frames;
                std::cout << std::format("Progress: {}%\r", progress) << std::flush;
            }
        }
        std::cout << "Progress: 100%\n";
    }

    void write_output() {
        std::ofstream output_stream(output_file_, std::ios::binary);
        if (!output_stream.is_open()) {
            throw std::runtime_error(
                std::format("Failed to open output file: {}", output_file_.string())
            );
        }

        // Calculate data size
        const uint32_t data_size = static_cast<uint32_t>(all_samples_.size() * sizeof(int16_t));

        // Write WAV header
        write_wav_header(output_stream, sample_rate_, channels_, data_size);

        // Write audio data
        output_stream.write(reinterpret_cast<const char*>(all_samples_.data()), data_size);

        std::cout << std::format("Written {} bytes\n", data_size);
    }

    void print_info() const {
        std::cout << "Audio Reverse\n";
        std::cout << "=============\n\n";
        std::cout << std::format("Input:  {}\n", input_file_.string());
        std::cout << std::format("Output: {}\n", output_file_.string());
        std::cout << std::format("Sample rate: {} Hz\n", sample_rate_);
        std::cout << std::format("Channels: {}\n", channels_);
        if (duration_ > 0) {
            std::cout << std::format("Duration: {:.2f} seconds\n", duration_);
        }

        if (!params_.reverse_all) {
            std::cout << std::format("\nReverse range: {:.2f}s to {:.2f}s\n",
                                    params_.start_time,
                                    params_.end_time < 0 ? duration_ : params_.end_time);
        } else {
            std::cout << "\nReversing entire audio file\n";
        }
        std::cout << "\n";
    }

    fs::path input_file_;
    fs::path output_file_;
    ReverseParams params_;

    ffmpeg::FormatContextPtr format_ctx_;
    ffmpeg::CodecContextPtr decoder_ctx_;
    ffmpeg::SwrContextPtr swr_ctx_;
    ffmpeg::PacketPtr packet_;
    ffmpeg::FramePtr frame_;

    int audio_stream_index_ = -1;
    int sample_rate_ = 0;
    int channels_ = 0;
    double duration_ = 0.0;

    std::vector<int16_t> all_samples_;
    int64_t total_samples_ = 0;
};

void print_usage(const char* prog_name) {
    std::cout << std::format("Usage: {} <input> <output> [options]\n\n", prog_name);
    std::cout << "Options:\n";
    std::cout << "  -s, --start <seconds>     Start time for reversal (default: 0.0)\n";
    std::cout << "  -e, --end <seconds>       End time for reversal (default: end of file)\n";
    std::cout << "  -r, --range               Reverse only specified range\n";
    std::cout << "  -a, --all                 Reverse entire file (default)\n\n";

    std::cout << "Examples:\n";
    std::cout << std::format("  {} input.wav reversed.wav\n", prog_name);
    std::cout << "    Reverse entire audio file\n\n";

    std::cout << std::format("  {} audio.mp3 output.wav -r -s 5.0 -e 10.0\n", prog_name);
    std::cout << "    Reverse audio from 5 seconds to 10 seconds\n\n";

    std::cout << std::format("  {} speech.wav backward.wav -r -s 0 -e 3.5\n", prog_name);
    std::cout << "    Reverse first 3.5 seconds\n\n";

    std::cout << std::format("  {} music.flac reversed.wav\n", prog_name);
    std::cout << "    Reverse entire FLAC file to WAV\n\n";

    std::cout << "Use Cases:\n";
    std::cout << "  - Creative audio effects\n";
    std::cout << "  - Finding hidden messages in audio\n";
    std::cout << "  - Creating atmospheric sounds\n";
    std::cout << "  - Reverse cymbal effects\n";
    std::cout << "  - Audio restoration analysis\n\n";

    std::cout << "Notes:\n";
    std::cout << "  - Output is always in WAV format (16-bit PCM)\n";
    std::cout << "  - Entire audio is loaded into memory for reversal\n";
    std::cout << "  - Large files may require significant RAM\n";
    std::cout << "  - Channel order is preserved within each frame\n";
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        fs::path input_file{argv[1]};
        fs::path output_file{argv[2]};
        ReverseParams params;

        // Parse arguments
        for (int i = 3; i < argc; ++i) {
            std::string arg{argv[i]};

            if ((arg == "-s" || arg == "--start") && i + 1 < argc) {
                params.start_time = std::max(0.0, std::stod(argv[++i]));
                params.reverse_all = false;
            }
            else if ((arg == "-e" || arg == "--end") && i + 1 < argc) {
                params.end_time = std::stod(argv[++i]);
                params.reverse_all = false;
            }
            else if (arg == "-r" || arg == "--range") {
                params.reverse_all = false;
            }
            else if (arg == "-a" || arg == "--all") {
                params.reverse_all = true;
            }
        }

        // Validate
        if (!fs::exists(input_file)) {
            std::cerr << std::format("Error: Input file does not exist: {}\n",
                                    input_file.string());
            return 1;
        }

        if (!params.reverse_all && params.end_time >= 0 &&
            params.end_time <= params.start_time) {
            std::cerr << "Error: End time must be greater than start time\n";
            return 1;
        }

        // Process
        AudioReverser reverser(input_file, output_file, params);
        reverser.process();

    } catch (const ffmpeg::FFmpegError& e) {
        std::cerr << std::format("FFmpeg error: {}\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }

    return 0;
}
