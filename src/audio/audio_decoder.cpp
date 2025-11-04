/**
 * Audio Decoder
 *
 * This sample demonstrates how to decode audio frames from a file
 * and save them as WAV file using modern C++20 and FFmpeg libraries.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <fstream>
#include <format>
#include <filesystem>
#include <vector>
#include <span>

namespace fs = std::filesystem;

namespace {

// WAV file header structure
struct WAVHeader {
    char riff_header[4] = {'R', 'I', 'F', 'F'};
    uint32_t wav_size;
    char wave_header[4] = {'W', 'A', 'V', 'E'};
    char fmt_header[4] = {'f', 'm', 't', ' '};
    uint32_t fmt_chunk_size = 16;
    uint16_t audio_format = 1;  // PCM
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data_header[4] = {'d', 'a', 't', 'a'};
    uint32_t data_bytes;
};

void write_wav_header(std::ofstream& file, int sample_rate, int channels,
                     int bits_per_sample, uint32_t data_size) {
    WAVHeader header;
    header.num_channels = static_cast<uint16_t>(channels);
    header.sample_rate = static_cast<uint32_t>(sample_rate);
    header.bits_per_sample = static_cast<uint16_t>(bits_per_sample);
    header.byte_rate = static_cast<uint32_t>(sample_rate * channels * bits_per_sample / 8);
    header.block_align = static_cast<uint16_t>(channels * bits_per_sample / 8);
    header.data_bytes = data_size;
    header.wav_size = 36 + data_size;

    file.write(reinterpret_cast<const char*>(&header), sizeof(WAVHeader));
}

class AudioDecoder {
public:
    AudioDecoder(std::string_view input_file, const fs::path& output_file)
        : output_file_(output_file)
        , format_ctx_(ffmpeg::open_input_format(input_file.data()))
        , packet_(ffmpeg::create_packet())
        , frame_(ffmpeg::create_frame()) {

        initialize();
    }

    void decode_and_save() {
        // Open output file
        std::ofstream output_stream(output_file_, std::ios::binary);
        if (!output_stream.is_open()) {
            throw std::runtime_error(std::format("Failed to open output file: {}", output_file_.string()));
        }

        // Write placeholder WAV header (will be updated later)
        write_wav_header(output_stream, out_sample_rate_, 2, 16, 0);

        std::cout << std::format("Decoding audio from {}\n", format_ctx_->url);
        std::cout << std::format("Input format: {}\n", codec_->long_name);
        std::cout << std::format("Input sample rate: {} Hz\n", codec_ctx_->sample_rate);
        std::cout << std::format("Input channels: {}\n", codec_ctx_->ch_layout.nb_channels);
        std::cout << std::format("Output format: WAV (16-bit PCM, Stereo, {}kHz)\n\n", out_sample_rate_ / 1000);

        uint32_t total_data_size = 0;
        int frame_count = 0;

        // Process all frames
        while (av_read_frame(format_ctx_.get(), packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(packet_.get());

            if (packet_->stream_index != audio_stream_index_) {
                continue;
            }

            const auto ret = avcodec_send_packet(codec_ctx_.get(), packet_.get());
            if (ret < 0) {
                continue;
            }

            while (true) {
                const auto recv_ret = avcodec_receive_frame(codec_ctx_.get(), frame_.get());

                if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF) {
                    break;
                }

                if (recv_ret < 0) {
                    std::cerr << "Error during decoding\n";
                    break;
                }

                ffmpeg::ScopedFrameUnref frame_guard(frame_.get());

                // Resample and write
                total_data_size += resample_and_write(output_stream);
                ++frame_count;

                if (frame_count % 100 == 0) {
                    std::cout << std::format("Decoded {} frames\r", frame_count) << std::flush;
                }
            }
        }

        // Flush resampler
        total_data_size += flush_resampler(output_stream);

        std::cout << std::format("\nTotal frames decoded: {}\n", frame_count);
        std::cout << std::format("Total data size: {} bytes\n", total_data_size);

        // Update WAV header with actual data size
        output_stream.seekp(0, std::ios::beg);
        write_wav_header(output_stream, out_sample_rate_, 2, 16, total_data_size);

        std::cout << std::format("Decoding completed successfully!\n");
        std::cout << std::format("Output file: {}\n", output_file_.string());
    }

private:
    void initialize() {
        // Find audio stream
        const auto stream_idx = ffmpeg::find_stream_index(format_ctx_.get(), AVMEDIA_TYPE_AUDIO);
        if (!stream_idx) {
            throw ffmpeg::FFmpegError("No audio stream found");
        }
        audio_stream_index_ = *stream_idx;

        // Get codec and open decoder
        const auto* codecpar = format_ctx_->streams[audio_stream_index_]->codecpar;
        codec_ = avcodec_find_decoder(codecpar->codec_id);
        if (!codec_) {
            throw ffmpeg::FFmpegError("Codec not found");
        }

        codec_ctx_ = ffmpeg::create_codec_context(codec_);
        ffmpeg::check_error(
            avcodec_parameters_to_context(codec_ctx_.get(), codecpar),
            "copy codec parameters"
        );
        ffmpeg::check_error(
            avcodec_open2(codec_ctx_.get(), codec_, nullptr),
            "open codec"
        );

        // Setup resampler for output format (16-bit stereo PCM at 44.1kHz)
        AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;

        ffmpeg::check_error(
            swr_alloc_set_opts2(&swr_ctx_raw_,
                              &out_ch_layout,
                              AV_SAMPLE_FMT_S16,
                              out_sample_rate_,
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

        // Allocate buffer for resampled data
        max_dst_nb_samples_ = av_rescale_rnd(4096, out_sample_rate_,
                                            codec_ctx_->sample_rate, AV_ROUND_UP);

        ffmpeg::check_error(
            av_samples_alloc_array_and_samples(&dst_data_, &dst_linesize_, 2,
                                              max_dst_nb_samples_, AV_SAMPLE_FMT_S16, 0),
            "allocate sample buffer"
        );
    }

    uint32_t resample_and_write(std::ofstream& output_stream) {
        const auto dst_nb_samples = av_rescale_rnd(
            swr_get_delay(swr_ctx_.get(), codec_ctx_->sample_rate) + frame_->nb_samples,
            out_sample_rate_, codec_ctx_->sample_rate, AV_ROUND_UP);

        if (dst_nb_samples > max_dst_nb_samples_) {
            av_freep(&dst_data_[0]);
            av_samples_alloc(dst_data_, &dst_linesize_, 2,
                           dst_nb_samples, AV_SAMPLE_FMT_S16, 1);
            max_dst_nb_samples_ = dst_nb_samples;
        }

        const auto ret = swr_convert(swr_ctx_.get(), dst_data_, dst_nb_samples,
                                    const_cast<const uint8_t**>(frame_->data), frame_->nb_samples);

        if (ret < 0) {
            return 0;
        }

        const auto dst_bufsize = av_samples_get_buffer_size(&dst_linesize_, 2,
                                                           ret, AV_SAMPLE_FMT_S16, 1);
        output_stream.write(reinterpret_cast<char*>(dst_data_[0]), dst_bufsize);

        return static_cast<uint32_t>(dst_bufsize);
    }

    uint32_t flush_resampler(std::ofstream& output_stream) {
        uint32_t total_flushed = 0;

        while (true) {
            const auto ret = swr_convert(swr_ctx_.get(), dst_data_, max_dst_nb_samples_, nullptr, 0);
            if (ret <= 0) {
                break;
            }

            const auto dst_bufsize = av_samples_get_buffer_size(&dst_linesize_, 2,
                                                               ret, AV_SAMPLE_FMT_S16, 1);
            output_stream.write(reinterpret_cast<char*>(dst_data_[0]), dst_bufsize);
            total_flushed += dst_bufsize;
        }

        return total_flushed;
    }

public:
    ~AudioDecoder() {
        if (dst_data_) {
            av_freep(&dst_data_[0]);
            av_freep(&dst_data_);
        }
    }

private:
    fs::path output_file_;
    int audio_stream_index_ = -1;
    int out_sample_rate_ = 44100;
    int max_dst_nb_samples_ = 0;
    int dst_linesize_ = 0;
    uint8_t** dst_data_ = nullptr;

    ffmpeg::FormatContextPtr format_ctx_;
    ffmpeg::CodecContextPtr codec_ctx_;
    ffmpeg::PacketPtr packet_;
    ffmpeg::FramePtr frame_;
    ffmpeg::SwrContextPtr swr_ctx_;
    SwrContext* swr_ctx_raw_ = nullptr;
    const AVCodec* codec_ = nullptr;
};

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << std::format("Usage: {} <input_file> <output_file>\n", argv[0]);
        std::cerr << std::format("Example: {} audio.mp3 output.wav\n", argv[0]);
        std::cerr << "\nNote: Output will be in WAV format (16-bit PCM, stereo, 44.1kHz)\n";
        return 1;
    }

    try {
        const std::string_view input_filename{argv[1]};
        const fs::path output_filename{argv[2]};

        AudioDecoder decoder(input_filename, output_filename);
        decoder.decode_and_save();

    } catch (const ffmpeg::FFmpegError& e) {
        std::cerr << std::format("FFmpeg error: {}\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }

    return 0;
}
