/**
 * Audio Mixer
 *
 * This sample demonstrates how to mix two audio files together
 * using FFmpeg libraries.
 */

#include <iostream>
#include <fstream>
#include <algorithm>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
}

struct AudioDecoder {
    AVFormatContext* format_ctx;
    AVCodecContext* codec_ctx;
    SwrContext* swr_ctx;
    int stream_index;
    AVPacket* packet;
    AVFrame* frame;
    bool eof;

    AudioDecoder() : format_ctx(nullptr), codec_ctx(nullptr), swr_ctx(nullptr),
                     stream_index(-1), packet(nullptr), frame(nullptr), eof(false) {}

    ~AudioDecoder() {
        if (frame) av_frame_free(&frame);
        if (packet) av_packet_free(&packet);
        if (swr_ctx) swr_free(&swr_ctx);
        if (codec_ctx) avcodec_free_context(&codec_ctx);
        if (format_ctx) avformat_close_input(&format_ctx);
    }

    bool open(const char* filename, int target_sample_rate, int target_channels) {
        // Open file
        int ret = avformat_open_input(&format_ctx, filename, nullptr, nullptr);
        if (ret < 0) {
            return false;
        }

        // Find stream info
        if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
            return false;
        }

        // Find audio stream
        for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
            if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                stream_index = i;
                break;
            }
        }

        if (stream_index == -1) {
            return false;
        }

        // Setup decoder
        AVCodecParameters* codecpar = format_ctx->streams[stream_index]->codecpar;
        const AVCodec* decoder = avcodec_find_decoder(codecpar->codec_id);
        if (!decoder) {
            return false;
        }

        codec_ctx = avcodec_alloc_context3(decoder);
        avcodec_parameters_to_context(codec_ctx, codecpar);

        if (avcodec_open2(codec_ctx, decoder, nullptr) < 0) {
            return false;
        }

        // Setup resampler
        AVChannelLayout out_ch_layout = target_channels == 1 ?
                                         AV_CHANNEL_LAYOUT_MONO : AV_CHANNEL_LAYOUT_STEREO;

        ret = swr_alloc_set_opts2(&swr_ctx,
                                  &out_ch_layout,
                                  AV_SAMPLE_FMT_S16,
                                  target_sample_rate,
                                  &codec_ctx->ch_layout,
                                  codec_ctx->sample_fmt,
                                  codec_ctx->sample_rate,
                                  0, nullptr);

        if (ret < 0 || swr_init(swr_ctx) < 0) {
            return false;
        }

        packet = av_packet_alloc();
        frame = av_frame_alloc();

        return true;
    }

    int read_samples(int16_t* buffer, int num_samples) {
        int samples_read = 0;

        while (samples_read < num_samples && !eof) {
            int ret = avcodec_receive_frame(codec_ctx, frame);

            if (ret == AVERROR(EAGAIN)) {
                // Need more packets
                ret = av_read_frame(format_ctx, packet);
                if (ret < 0) {
                    eof = true;
                    break;
                }

                if (packet->stream_index == stream_index) {
                    avcodec_send_packet(codec_ctx, packet);
                }
                av_packet_unref(packet);
                continue;
            } else if (ret == AVERROR_EOF) {
                eof = true;
                break;
            } else if (ret < 0) {
                eof = true;
                break;
            }

            // Resample
            uint8_t* out_buf = reinterpret_cast<uint8_t*>(buffer + samples_read);
            int dst_nb_samples = num_samples - samples_read;

            ret = swr_convert(swr_ctx, &out_buf, dst_nb_samples,
                             const_cast<const uint8_t**>(frame->data), frame->nb_samples);

            if (ret > 0) {
                samples_read += ret;
            }
        }

        return samples_read;
    }
};

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
    header.num_channels = channels;
    header.sample_rate = sample_rate;
    header.bits_per_sample = 16;
    header.byte_rate = sample_rate * channels * 2;
    header.block_align = channels * 2;
    header.data_bytes = data_size;
    header.wav_size = 36 + data_size;

    file.write(reinterpret_cast<const char*>(&header), sizeof(WAVHeader));
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <input1> <input2> <output> [volume1] [volume2]\n";
        std::cerr << "Example: " << argv[0] << " audio1.mp3 audio2.mp3 mixed.wav 0.5 0.5\n";
        std::cerr << "\nMixes two audio files together.\n";
        std::cerr << "Volume range: 0.0 to 1.0 (default: 0.5 for both)\n";
        std::cerr << "Output: WAV file, 44.1kHz, Stereo, 16-bit\n";
        return 1;
    }

    const char* input1_filename = argv[1];
    const char* input2_filename = argv[2];
    const char* output_filename = argv[3];
    float volume1 = argc > 4 ? std::atof(argv[4]) : 0.5f;
    float volume2 = argc > 5 ? std::atof(argv[5]) : 0.5f;

    // Clamp volumes
    volume1 = std::max(0.0f, std::min(1.0f, volume1));
    volume2 = std::max(0.0f, std::min(1.0f, volume2));

    const int target_sample_rate = 44100;
    const int target_channels = 2;
    const int buffer_size = 4096;

    std::cout << "Audio Mixer\n";
    std::cout << "===========\n\n";
    std::cout << "Input 1: " << input1_filename << " (volume: " << volume1 << ")\n";
    std::cout << "Input 2: " << input2_filename << " (volume: " << volume2 << ")\n";
    std::cout << "Output: " << output_filename << "\n";
    std::cout << "Output format: 44.1kHz, Stereo, 16-bit PCM\n\n";

    // Open decoders
    AudioDecoder decoder1, decoder2;

    if (!decoder1.open(input1_filename, target_sample_rate, target_channels)) {
        std::cerr << "Failed to open input file 1\n";
        return 1;
    }

    if (!decoder2.open(input2_filename, target_sample_rate, target_channels)) {
        std::cerr << "Failed to open input file 2\n";
        return 1;
    }

    // Open output file
    std::ofstream output_file(output_filename, std::ios::binary);
    if (!output_file.is_open()) {
        std::cerr << "Failed to open output file\n";
        return 1;
    }

    // Write placeholder WAV header
    write_wav_header(output_file, target_sample_rate, target_channels, 0);

    std::vector<int16_t> buffer1(buffer_size * target_channels);
    std::vector<int16_t> buffer2(buffer_size * target_channels);
    std::vector<int16_t> output_buffer(buffer_size * target_channels);

    uint32_t total_samples_written = 0;
    int iteration = 0;

    std::cout << "Mixing in progress...\n";

    // Mix audio
    while (!decoder1.eof || !decoder2.eof) {
        int samples1 = decoder1.read_samples(buffer1.data(), buffer_size);
        int samples2 = decoder2.read_samples(buffer2.data(), buffer_size);

        int max_samples = std::max(samples1, samples2);

        if (max_samples == 0) {
            break;
        }

        // Mix samples
        for (int i = 0; i < max_samples * target_channels; i++) {
            int16_t sample1 = (i < samples1 * target_channels) ? buffer1[i] : 0;
            int16_t sample2 = (i < samples2 * target_channels) ? buffer2[i] : 0;

            // Mix with volume control and clipping prevention
            int32_t mixed = static_cast<int32_t>(sample1 * volume1) +
                           static_cast<int32_t>(sample2 * volume2);

            // Clamp to prevent overflow
            if (mixed > 32767) mixed = 32767;
            if (mixed < -32768) mixed = -32768;

            output_buffer[i] = static_cast<int16_t>(mixed);
        }

        // Write to file
        int bytes_to_write = max_samples * target_channels * sizeof(int16_t);
        output_file.write(reinterpret_cast<char*>(output_buffer.data()), bytes_to_write);
        total_samples_written += max_samples;

        iteration++;
        if (iteration % 100 == 0) {
            double seconds = total_samples_written / static_cast<double>(target_sample_rate);
            std::cout << "Mixed " << std::fixed << std::setprecision(2)
                      << seconds << " seconds\r" << std::flush;
        }
    }

    uint32_t total_bytes = total_samples_written * target_channels * sizeof(int16_t);

    std::cout << "\nTotal samples mixed: " << total_samples_written << "\n";
    std::cout << "Duration: " << std::fixed << std::setprecision(2)
              << (total_samples_written / static_cast<double>(target_sample_rate)) << " seconds\n";
    std::cout << "Output size: " << total_bytes << " bytes\n";

    // Update WAV header
    output_file.seekp(0, std::ios::beg);
    write_wav_header(output_file, target_sample_rate, target_channels, total_bytes);
    output_file.close();

    std::cout << "\nMixing completed successfully!\n";
    std::cout << "Output file: " << output_filename << "\n";

    return 0;
}
