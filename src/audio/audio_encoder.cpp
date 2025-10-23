/**
 * Audio Encoder
 *
 * This sample demonstrates how to encode audio data (sine wave) into
 * various audio formats using modern C++20 and FFmpeg libraries.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <format>
#include <string_view>
#include <cmath>
#include <numbers>
#include <iomanip>

namespace {

constexpr auto PI = std::numbers::pi;

void generate_sine_wave(AVFrame& frame, int frame_num, double frequency, int sample_rate) {
    auto* samples = reinterpret_cast<int16_t*>(frame.data[0]);
    const auto t = frame_num * frame.nb_samples / static_cast<double>(sample_rate);

    for (int i = 0; i < frame.nb_samples; ++i) {
        const auto sample_time = t + i / static_cast<double>(sample_rate);
        const auto sample = static_cast<int16_t>(std::sin(2.0 * PI * frequency * sample_time) * 10000.0);

        // Stereo: same sample for both channels
        samples[2 * i] = sample;      // Left channel
        samples[2 * i + 1] = sample;  // Right channel
    }
}

std::string_view select_codec(std::string_view filename) {
    if (filename.find(".mp3") != std::string_view::npos) {
        return "libmp3lame";
    }
    if (filename.find(".aac") != std::string_view::npos ||
        filename.find(".m4a") != std::string_view::npos) {
        return "aac";
    }
    if (filename.find(".ogg") != std::string_view::npos ||
        filename.find(".oga") != std::string_view::npos) {
        return "libvorbis";
    }
    if (filename.find(".flac") != std::string_view::npos) {
        return "flac";
    }
    return "aac";  // Default
}

class AudioEncoder {
public:
    AudioEncoder(std::string_view output_file, double duration, double frequency)
        : output_file_(output_file)
        , duration_(duration)
        , frequency_(frequency)
        , packet_(ffmpeg::create_packet())
        , frame_(ffmpeg::create_frame()) {

        initialize();
    }

    void encode() {
        std::cout << std::format("Encoding audio to {}\n", output_file_);
        std::cout << std::format("Codec: {}\n", codec_->long_name);
        std::cout << std::format("Sample Rate: {} Hz\n", codec_ctx_->sample_rate);
        std::cout << "Channels: 2 (Stereo)\n";
        std::cout << std::format("Bit Rate: {} kbps\n", codec_ctx_->bit_rate / 1000);
        std::cout << std::format("Duration: {} seconds\n", duration_);
        std::cout << std::format("Frequency: {} Hz\n\n", frequency_);

        // Configure frame
        frame_->format = AV_SAMPLE_FMT_S16;
        frame_->ch_layout = codec_ctx_->ch_layout;
        frame_->sample_rate = codec_ctx_->sample_rate;
        frame_->nb_samples = codec_ctx_->frame_size > 0 ? codec_ctx_->frame_size : 1024;

        ffmpeg::check_error(
            av_frame_get_buffer(frame_.get(), 0),
            "allocate frame buffer"
        );

        const auto total_samples = static_cast<int>(duration_ * codec_ctx_->sample_rate);
        int frame_count = 0;
        int64_t pts = 0;

        // Encode frames
        while (pts < total_samples) {
            ffmpeg::check_error(
                av_frame_make_writable(frame_.get()),
                "make frame writable"
            );

            // Generate sine wave
            generate_sine_wave(*frame_, frame_count, frequency_, codec_ctx_->sample_rate);
            frame_->pts = pts;
            pts += frame_->nb_samples;

            // Encode frame
            encode_frame();
            ++frame_count;

            if (frame_count % 10 == 0) {
                const auto progress = (pts * 100.0) / total_samples;
                std::cout << std::format("Encoding progress: {:.1f}%\r", progress) << std::flush;
            }
        }

        std::cout << "\n";

        // Flush encoder
        flush_encoder();

        // Write trailer
        ffmpeg::check_error(
            av_write_trailer(format_ctx_.get()),
            "write trailer"
        );

        std::cout << std::format("Encoding completed successfully!\n");
        std::cout << std::format("Total frames encoded: {}\n", frame_count);
        std::cout << std::format("Output file: {}\n", output_file_);
    }

private:
    void initialize() {
        // Allocate output format context
        AVFormatContext* raw_ctx = nullptr;
        ffmpeg::check_error(
            avformat_alloc_output_context2(&raw_ctx, nullptr, nullptr, output_file_.data()),
            "allocate output context"
        );
        format_ctx_.reset(raw_ctx);

        // Find encoder
        const auto codec_name = select_codec(output_file_);
        codec_ = avcodec_find_encoder_by_name(codec_name.data());

        if (!codec_) {
            std::cerr << std::format("Codec '{}' not found, trying default\n", codec_name);
            codec_ = avcodec_find_encoder(format_ctx_->oformat->audio_codec);
        }

        if (!codec_) {
            throw ffmpeg::FFmpegError("Audio codec not found");
        }

        // Create stream
        stream_ = avformat_new_stream(format_ctx_.get(), nullptr);
        if (!stream_) {
            throw ffmpeg::FFmpegError("Failed to create stream");
        }

        // Create and configure codec context
        codec_ctx_ = ffmpeg::create_codec_context(codec_);

        codec_ctx_->codec_id = codec_->id;
        codec_ctx_->codec_type = AVMEDIA_TYPE_AUDIO;
        codec_ctx_->sample_rate = 44100;
        codec_ctx_->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
        codec_ctx_->bit_rate = 128000;

        // Select sample format
        codec_ctx_->sample_fmt = codec_->sample_fmts ?
                                 codec_->sample_fmts[0] : AV_SAMPLE_FMT_S16;

        // Set time base
        codec_ctx_->time_base = AVRational{1, codec_ctx_->sample_rate};

        // Some formats require global headers
        if (format_ctx_->oformat->flags & AVFMT_GLOBALHEADER) {
            codec_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        // Open codec
        ffmpeg::check_error(
            avcodec_open2(codec_ctx_.get(), codec_, nullptr),
            "open codec"
        );

        // Copy codec parameters to stream
        ffmpeg::check_error(
            avcodec_parameters_from_context(stream_->codecpar, codec_ctx_.get()),
            "copy codec parameters"
        );

        stream_->time_base = codec_ctx_->time_base;

        // Open output file
        if (!(format_ctx_->oformat->flags & AVFMT_NOFILE)) {
            ffmpeg::check_error(
                avio_open(&format_ctx_->pb, output_file_.data(), AVIO_FLAG_WRITE),
                "open output file"
            );
        }

        // Write header
        ffmpeg::check_error(
            avformat_write_header(format_ctx_.get(), nullptr),
            "write header"
        );
    }

    void encode_frame() {
        ffmpeg::check_error(
            avcodec_send_frame(codec_ctx_.get(), frame_.get()),
            "send frame"
        );

        receive_packets();
    }

    void flush_encoder() {
        avcodec_send_frame(codec_ctx_.get(), nullptr);
        receive_packets();
    }

    void receive_packets() {
        while (true) {
            const auto ret = avcodec_receive_packet(codec_ctx_.get(), packet_.get());

            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }

            if (ret < 0) {
                throw ffmpeg::FFmpegError(ret);
            }

            ffmpeg::ScopedPacketUnref packet_guard(packet_.get());

            // Rescale packet timestamps
            av_packet_rescale_ts(packet_.get(), codec_ctx_->time_base, stream_->time_base);
            packet_->stream_index = stream_->index;

            // Write packet to output file
            ffmpeg::check_error(
                av_interleaved_write_frame(format_ctx_.get(), packet_.get()),
                "write frame"
            );
        }
    }

    std::string output_file_;
    double duration_;
    double frequency_;

    ffmpeg::FormatContextPtr format_ctx_;
    ffmpeg::CodecContextPtr codec_ctx_;
    ffmpeg::PacketPtr packet_;
    ffmpeg::FramePtr frame_;
    const AVCodec* codec_ = nullptr;
    AVStream* stream_ = nullptr;
};

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << std::format("Usage: {} <output_file> [duration_seconds] [frequency_hz]\n", argv[0]);
        std::cerr << std::format("Example: {} output.mp3 10 440\n", argv[0]);
        std::cerr << "\nGenerates a sine wave tone.\n";
        std::cerr << "Default: 5 seconds, 440 Hz (A4 note)\n";
        return 1;
    }

    try {
        const std::string_view output_filename{argv[1]};
        const double duration = argc > 2 ? std::atof(argv[2]) : 5.0;
        const double frequency = argc > 3 ? std::atof(argv[3]) : 440.0;

        AudioEncoder encoder(output_filename, duration, frequency);
        encoder.encode();

    } catch (const ffmpeg::FFmpegError& e) {
        std::cerr << std::format("FFmpeg error: {}\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }

    return 0;
}
