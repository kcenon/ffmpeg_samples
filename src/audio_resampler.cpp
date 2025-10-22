/**
 * Audio Resampler
 *
 * This sample demonstrates how to resample audio (change sample rate,
 * channel layout, or sample format) using FFmpeg's libswresample.
 */

#include <iostream>
#include <fstream>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
}

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
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_file> <output_file> [sample_rate] [channels]\n";
        std::cerr << "Example: " << argv[0] << " input.mp3 output.wav 48000 1\n";
        std::cerr << "\nDefault output: 44100 Hz, Stereo\n";
        std::cerr << "Channels: 1 (mono), 2 (stereo)\n";
        return 1;
    }

    const char* input_filename = argv[1];
    const char* output_filename = argv[2];
    int target_sample_rate = argc > 3 ? std::atoi(argv[3]) : 44100;
    int target_channels = argc > 4 ? std::atoi(argv[4]) : 2;

    if (target_channels < 1 || target_channels > 2) {
        std::cerr << "Error: channels must be 1 (mono) or 2 (stereo)\n";
        return 1;
    }

    AVFormatContext* format_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    SwrContext* swr_ctx = nullptr;
    int audio_stream_index = -1;

    // Open input file
    int ret = avformat_open_input(&format_ctx, input_filename, nullptr, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        std::cerr << "Error opening input file: " << errbuf << "\n";
        return 1;
    }

    // Find stream information
    if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
        std::cerr << "Error finding stream information\n";
        avformat_close_input(&format_ctx);
        return 1;
    }

    // Find audio stream
    for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            break;
        }
    }

    if (audio_stream_index == -1) {
        std::cerr << "No audio stream found\n";
        avformat_close_input(&format_ctx);
        return 1;
    }

    // Get decoder
    AVCodecParameters* codecpar = format_ctx->streams[audio_stream_index]->codecpar;
    const AVCodec* decoder = avcodec_find_decoder(codecpar->codec_id);

    if (!decoder) {
        std::cerr << "Decoder not found\n";
        avformat_close_input(&format_ctx);
        return 1;
    }

    codec_ctx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(codec_ctx, codecpar);

    if (avcodec_open2(codec_ctx, decoder, nullptr) < 0) {
        std::cerr << "Failed to open decoder\n";
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        return 1;
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
        std::cerr << "Failed to initialize resampler\n";
        swr_free(&swr_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        return 1;
    }

    // Print resampling information
    std::cout << "Audio Resampler\n";
    std::cout << "===============\n\n";
    std::cout << "Input file: " << input_filename << "\n";
    std::cout << "Output file: " << output_filename << "\n\n";

    std::cout << "Input format:\n";
    std::cout << "  Sample rate: " << codec_ctx->sample_rate << " Hz\n";
    std::cout << "  Channels: " << codec_ctx->ch_layout.nb_channels << "\n";
    std::cout << "  Sample format: " << av_get_sample_fmt_name(codec_ctx->sample_fmt) << "\n\n";

    std::cout << "Output format:\n";
    std::cout << "  Sample rate: " << target_sample_rate << " Hz\n";
    std::cout << "  Channels: " << target_channels << "\n";
    std::cout << "  Sample format: S16 (16-bit signed integer)\n\n";

    // Open output file
    std::ofstream output_file(output_filename, std::ios::binary);
    if (!output_file.is_open()) {
        std::cerr << "Failed to open output file\n";
        swr_free(&swr_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        return 1;
    }

    // Write placeholder WAV header
    write_wav_header(output_file, target_sample_rate, target_channels, 0);

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    // Allocate output buffer
    int max_dst_nb_samples = av_rescale_rnd(4096, target_sample_rate,
                                            codec_ctx->sample_rate, AV_ROUND_UP);
    uint8_t** dst_data = nullptr;
    int dst_linesize;

    av_samples_alloc_array_and_samples(&dst_data, &dst_linesize, target_channels,
                                       max_dst_nb_samples, AV_SAMPLE_FMT_S16, 0);

    uint32_t total_data_size = 0;
    int frame_count = 0;

    std::cout << "Resampling in progress...\n";

    // Read and resample frames
    while (av_read_frame(format_ctx, packet) >= 0) {
        if (packet->stream_index == audio_stream_index) {
            ret = avcodec_send_packet(codec_ctx, packet);
            if (ret < 0) {
                av_packet_unref(packet);
                continue;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(codec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    break;
                }

                // Resample
                int dst_nb_samples = av_rescale_rnd(
                    swr_get_delay(swr_ctx, codec_ctx->sample_rate) + frame->nb_samples,
                    target_sample_rate, codec_ctx->sample_rate, AV_ROUND_UP);

                if (dst_nb_samples > max_dst_nb_samples) {
                    av_freep(&dst_data[0]);
                    av_samples_alloc(dst_data, &dst_linesize, target_channels,
                                    dst_nb_samples, AV_SAMPLE_FMT_S16, 1);
                    max_dst_nb_samples = dst_nb_samples;
                }

                ret = swr_convert(swr_ctx, dst_data, dst_nb_samples,
                                 const_cast<const uint8_t**>(frame->data), frame->nb_samples);

                if (ret > 0) {
                    int dst_bufsize = av_samples_get_buffer_size(&dst_linesize, target_channels,
                                                                 ret, AV_SAMPLE_FMT_S16, 1);
                    output_file.write(reinterpret_cast<char*>(dst_data[0]), dst_bufsize);
                    total_data_size += dst_bufsize;
                }

                frame_count++;
                if (frame_count % 100 == 0) {
                    std::cout << "Processed " << frame_count << " frames\r" << std::flush;
                }
            }
        }
        av_packet_unref(packet);
    }

    // Flush resampler
    while (true) {
        ret = swr_convert(swr_ctx, dst_data, max_dst_nb_samples, nullptr, 0);
        if (ret <= 0) break;

        int dst_bufsize = av_samples_get_buffer_size(&dst_linesize, target_channels,
                                                     ret, AV_SAMPLE_FMT_S16, 1);
        output_file.write(reinterpret_cast<char*>(dst_data[0]), dst_bufsize);
        total_data_size += dst_bufsize;
    }

    std::cout << "\nTotal frames processed: " << frame_count << "\n";
    std::cout << "Output data size: " << total_data_size << " bytes\n";

    // Update WAV header
    output_file.seekp(0, std::ios::beg);
    write_wav_header(output_file, target_sample_rate, target_channels, total_data_size);

    // Cleanup
    output_file.close();

    if (dst_data) {
        av_freep(&dst_data[0]);
        av_freep(&dst_data);
    }

    av_packet_free(&packet);
    av_frame_free(&frame);
    swr_free(&swr_ctx);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&format_ctx);

    std::cout << "\nResampling completed successfully!\n";
    std::cout << "Output file: " << output_filename << "\n";

    return 0;
}
