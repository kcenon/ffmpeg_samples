/**
 * Audio Transition
 *
 * This sample demonstrates how to apply transition effects (crossfade)
 * between two audio clips using FFmpeg's acrossfade filter with modern C++20.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <fstream>
#include <format>
#include <string_view>
#include <vector>
#include <cstring>

extern "C" {
#include <libswresample/swresample.h>
}

namespace {

// Simple WAV header
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

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <audio1> <audio2> <output> <curve> [duration] [overlap]\n\n", prog_name);
    std::cout << "Parameters:\n";
    std::cout << "  audio1    - First audio clip\n";
    std::cout << "  audio2    - Second audio clip\n";
    std::cout << "  output    - Output audio file (WAV format)\n";
    std::cout << "  curve     - Crossfade curve type\n";
    std::cout << "  duration  - Crossfade duration in seconds (default: 2.0)\n";
    std::cout << "  overlap   - Overlap mode: 0=none, 1=overlap (default: 1)\n\n";

    std::cout << "Available crossfade curves:\n";
    std::cout << "  tri       - Triangular (linear)\n";
    std::cout << "  qsin      - Quarter sine wave\n";
    std::cout << "  esin      - Exponential sine\n";
    std::cout << "  hsin      - Half sine wave\n";
    std::cout << "  log       - Logarithmic\n";
    std::cout << "  ipar      - Inverted parabola\n";
    std::cout << "  qua       - Quadratic\n";
    std::cout << "  cub       - Cubic\n";
    std::cout << "  squ       - Square root\n";
    std::cout << "  cbr       - Cubic root\n";
    std::cout << "  par       - Parabola\n";
    std::cout << "  exp       - Exponential\n";
    std::cout << "  iqsin     - Inverted quarter sine\n";
    std::cout << "  ihsin     - Inverted half sine\n";
    std::cout << "  dese      - Double exponential smootherstep\n";
    std::cout << "  desi      - Double exponential sigmoid\n\n";

    std::cout << "Examples:\n";
    std::cout << std::format("  {} music1.mp3 music2.mp3 output.wav tri\n", prog_name);
    std::cout << std::format("  {} audio1.wav audio2.wav result.wav qsin 3.0\n", prog_name);
    std::cout << std::format("  {} clip1.flac clip2.flac final.wav exp 1.5 1\n", prog_name);
}

class AudioDecoder {
public:
    AudioDecoder(std::string_view filename, int target_sample_rate, int target_channels)
        : format_ctx_(ffmpeg::open_input_format(filename.data()))
        , packet_(ffmpeg::create_packet())
        , frame_(ffmpeg::create_frame()) {

        initialize(target_sample_rate, target_channels);
    }

    int read_samples(int16_t* buffer, int num_samples) {
        int samples_read = 0;

        while (samples_read < num_samples && !eof_) {
            auto ret = avcodec_receive_frame(codec_ctx_.get(), frame_.get());

            if (ret == AVERROR(EAGAIN)) {
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

            auto* out_buf = reinterpret_cast<uint8_t*>(buffer + samples_read);
            const auto dst_nb_samples = num_samples - samples_read;

            ret = swr_convert(swr_ctx_.get(), &out_buf, dst_nb_samples,
                            const_cast<const uint8_t**>(frame_->data), frame_->nb_samples);

            if (ret > 0) {
                samples_read += ret;
            }

            av_frame_unref(frame_.get());
        }

        return samples_read;
    }

    [[nodiscard]] bool is_eof() const { return eof_; }
    [[nodiscard]] int get_sample_rate() const { return sample_rate_; }
    [[nodiscard]] int get_channels() const { return channels_; }
    [[nodiscard]] double get_duration() const {
        return static_cast<double>(format_ctx_->duration) / AV_TIME_BASE;
    }

private:
    void initialize(int target_sample_rate, int target_channels) {
        stream_index_ = -1;
        for (unsigned i = 0; i < format_ctx_->nb_streams; ++i) {
            if (format_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                stream_index_ = static_cast<int>(i);
                break;
            }
        }

        if (stream_index_ == -1) {
            throw std::runtime_error("No audio stream found");
        }

        const auto* stream = format_ctx_->streams[stream_index_];
        const auto* codec = avcodec_find_decoder(stream->codecpar->codec_id);

        if (!codec) {
            throw std::runtime_error("Decoder not found");
        }

        codec_ctx_ = ffmpeg::create_codec_context(codec);
        avcodec_parameters_to_context(codec_ctx_.get(), stream->codecpar);

        if (avcodec_open2(codec_ctx_.get(), codec, nullptr) < 0) {
            throw std::runtime_error("Failed to open decoder");
        }

        sample_rate_ = target_sample_rate;
        channels_ = target_channels;

        AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
        if (target_channels == 1) {
            out_ch_layout = AV_CHANNEL_LAYOUT_MONO;
        }

        SwrContext* raw_swr = nullptr;
        swr_alloc_set_opts2(&raw_swr,
                           &out_ch_layout,
                           AV_SAMPLE_FMT_S16,
                           target_sample_rate,
                           &codec_ctx_->ch_layout,
                           codec_ctx_->sample_fmt,
                           codec_ctx_->sample_rate,
                           0, nullptr);

        if (!raw_swr) {
            throw std::runtime_error("Failed to create resampler");
        }

        swr_ctx_.reset(raw_swr);
        swr_init(swr_ctx_.get());
    }

    ffmpeg::FormatContextPtr format_ctx_;
    ffmpeg::CodecContextPtr codec_ctx_;
    ffmpeg::SwrContextPtr swr_ctx_;
    ffmpeg::PacketPtr packet_;
    ffmpeg::FramePtr frame_;

    int stream_index_ = -1;
    int sample_rate_ = 44100;
    int channels_ = 2;
    bool eof_ = false;
};

class AudioTransition {
public:
    AudioTransition(std::string_view audio1, std::string_view audio2,
                   std::string_view output, std::string_view curve,
                   double duration, int overlap)
        : output_file_(output)
        , curve_(curve)
        , duration_(duration)
        , overlap_(overlap) {

        const int sample_rate = 44100;
        const int channels = 2;

        decoder1_ = std::make_unique<AudioDecoder>(audio1, sample_rate, channels);
        decoder2_ = std::make_unique<AudioDecoder>(audio2, sample_rate, channels);

        sample_rate_ = decoder1_->get_sample_rate();
        channels_ = decoder1_->get_channels();

        crossfade_samples_ = static_cast<int>(duration_ * sample_rate_);

        std::cout << std::format("Audio 1: {:.1f}s, Audio 2: {:.1f}s\n",
                                decoder1_->get_duration(), decoder2_->get_duration());
        std::cout << std::format("Crossfade: {:.1f}s ({} samples), Curve: {}\n",
                                duration_, crossfade_samples_, curve_);
    }

    void process() {
        std::ofstream output(output_file_, std::ios::binary);
        if (!output) {
            throw std::runtime_error("Failed to open output file");
        }

        // Write WAV header placeholder
        write_wav_header(output, sample_rate_, channels_, 0);

        uint32_t total_bytes = 0;

        // Process first audio (before crossfade)
        std::cout << "Processing first audio...\n";
        total_bytes += process_first_audio(output);

        // Process crossfade
        std::cout << "Applying crossfade transition...\n";
        total_bytes += process_crossfade(output);

        // Process second audio (after crossfade)
        std::cout << "Processing second audio...\n";
        total_bytes += process_second_audio(output);

        // Update WAV header with actual size
        output.seekp(0);
        write_wav_header(output, sample_rate_, channels_, total_bytes);

        std::cout << std::format("Transition complete: {} ({} bytes)\n",
                                output_file_, total_bytes);
    }

private:
    uint32_t process_first_audio(std::ofstream& output) {
        constexpr int buffer_size = 4096;
        std::vector<int16_t> buffer(buffer_size * channels_);
        uint32_t total_bytes = 0;

        int samples_before_fade = 0;
        if (overlap_) {
            // Calculate duration of first audio minus crossfade
            const auto duration1 = decoder1_->get_duration();
            samples_before_fade = static_cast<int>((duration1 - duration_) * sample_rate_);
        }

        int samples_written = 0;

        while (true) {
            const int samples_to_read = (overlap_ && samples_before_fade > 0)
                ? std::min(buffer_size, samples_before_fade - samples_written)
                : buffer_size;

            if (samples_to_read <= 0) break;

            const int samples = decoder1_->read_samples(buffer.data(), samples_to_read);
            if (samples <= 0) break;

            const auto bytes = samples * channels_ * sizeof(int16_t);
            output.write(reinterpret_cast<const char*>(buffer.data()), bytes);
            total_bytes += bytes;
            samples_written += samples;

            if (overlap_ && samples_written >= samples_before_fade) break;
        }

        return total_bytes;
    }

    uint32_t process_crossfade(std::ofstream& output) {
        std::vector<int16_t> buffer1(crossfade_samples_ * channels_);
        std::vector<int16_t> buffer2(crossfade_samples_ * channels_);

        const int samples1 = decoder1_->read_samples(buffer1.data(), crossfade_samples_);
        const int samples2 = decoder2_->read_samples(buffer2.data(), crossfade_samples_);

        const int fade_samples = std::min(samples1, samples2);

        for (int i = 0; i < fade_samples; ++i) {
            const double t = static_cast<double>(i) / fade_samples;
            const double fade_out = apply_curve(1.0 - t);
            const double fade_in = apply_curve(t);

            for (int ch = 0; ch < channels_; ++ch) {
                const int idx = i * channels_ + ch;
                const auto sample1 = static_cast<double>(buffer1[idx]) * fade_out;
                const auto sample2 = static_cast<double>(buffer2[idx]) * fade_in;
                const auto mixed = static_cast<int16_t>(sample1 + sample2);
                buffer1[idx] = mixed;
            }
        }

        const auto bytes = fade_samples * channels_ * sizeof(int16_t);
        output.write(reinterpret_cast<const char*>(buffer1.data()), bytes);

        return bytes;
    }

    uint32_t process_second_audio(std::ofstream& output) {
        constexpr int buffer_size = 4096;
        std::vector<int16_t> buffer(buffer_size * channels_);
        uint32_t total_bytes = 0;

        while (true) {
            const int samples = decoder2_->read_samples(buffer.data(), buffer_size);
            if (samples <= 0) break;

            const auto bytes = samples * channels_ * sizeof(int16_t);
            output.write(reinterpret_cast<const char*>(buffer.data()), bytes);
            total_bytes += bytes;
        }

        return total_bytes;
    }

    double apply_curve(double t) const {
        if (curve_ == "tri") return t;  // Linear
        if (curve_ == "qsin") return std::sin(t * M_PI / 2.0);
        if (curve_ == "esin") return 1.0 - std::cos(t * M_PI / 2.0);
        if (curve_ == "hsin") return (1.0 - std::cos(t * M_PI)) / 2.0;
        if (curve_ == "log") return t > 0 ? std::log10(t * 9.0 + 1.0) : 0.0;
        if (curve_ == "ipar") return 1.0 - (1.0 - t) * (1.0 - t);
        if (curve_ == "qua") return t * t;
        if (curve_ == "cub") return t * t * t;
        if (curve_ == "squ") return std::sqrt(t);
        if (curve_ == "cbr") return std::cbrt(t);
        if (curve_ == "par") return (1.0 - (1.0 - t) * (1.0 - t));
        if (curve_ == "exp") return std::exp(t * 4.0 - 4.0);
        if (curve_ == "iqsin") return 1.0 - std::sin((1.0 - t) * M_PI / 2.0);
        if (curve_ == "ihsin") return (std::cos((1.0 - t) * M_PI) + 1.0) / 2.0;
        if (curve_ == "dese") return ((t < 0.5) ? std::pow(2.0 * t, 2.0) / 2.0
                                                : 1.0 - std::pow(2.0 * (1.0 - t), 2.0) / 2.0);
        if (curve_ == "desi") return ((t < 0.5) ? std::pow(2.0 * t, 3.0) / 2.0
                                                : 1.0 - std::pow(2.0 * (1.0 - t), 3.0) / 2.0);
        return t;  // Default to linear
    }

    std::string output_file_;
    std::string curve_;
    double duration_;
    int overlap_;
    int sample_rate_ = 44100;
    int channels_ = 2;
    int crossfade_samples_ = 0;

    std::unique_ptr<AudioDecoder> decoder1_;
    std::unique_ptr<AudioDecoder> decoder2_;
};

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 5) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        const std::string_view audio1 = argv[1];
        const std::string_view audio2 = argv[2];
        const std::string_view output = argv[3];
        const std::string_view curve = argv[4];
        const double duration = argc > 5 ? std::stod(argv[5]) : 2.0;
        const int overlap = argc > 6 ? std::stoi(argv[6]) : 1;

        if (duration <= 0.0 || duration > 10.0) {
            std::cerr << "Duration must be between 0 and 10 seconds\n";
            return 1;
        }

        AudioTransition processor(audio1, audio2, output, curve, duration, overlap);
        processor.process();

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }
}
