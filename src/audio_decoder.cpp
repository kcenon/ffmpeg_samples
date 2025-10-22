/**
 * Audio Decoder
 *
 * This sample demonstrates how to decode audio frames from a file
 * and save them as raw PCM data or WAV file.
 */

#include <iostream>
#include <fstream>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
}

// WAV file header structure
struct WAVHeader {
    // RIFF chunk
    char riff_header[4] = {'R', 'I', 'F', 'F'};
    uint32_t wav_size;
    char wave_header[4] = {'W', 'A', 'V', 'E'};

    // fmt chunk
    char fmt_header[4] = {'f', 'm', 't', ' '};
    uint32_t fmt_chunk_size = 16;
    uint16_t audio_format = 1;  // PCM
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;

    // data chunk
    char data_header[4] = {'d', 'a', 't', 'a'};
    uint32_t data_bytes;
};

void write_wav_header(std::ofstream& file, int sample_rate, int channels, int bits_per_sample, uint32_t data_size) {
    WAVHeader header;
    header.num_channels = channels;
    header.sample_rate = sample_rate;
    header.bits_per_sample = bits_per_sample;
    header.byte_rate = sample_rate * channels * bits_per_sample / 8;
    header.block_align = channels * bits_per_sample / 8;
    header.data_bytes = data_size;
    header.wav_size = 36 + data_size;

    file.write(reinterpret_cast<const char*>(&header), sizeof(WAVHeader));
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_file> <output_file>\n";
        std::cerr << "Example: " << argv[0] << " audio.mp3 output.wav\n";
        std::cerr << "\nNote: Output will be in WAV format (16-bit PCM, stereo, 44.1kHz)\n";
        return 1;
    }

    const char* input_filename = argv[1];
    const char* output_filename = argv[2];

    AVFormatContext* format_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    const AVCodec* codec = nullptr;
    SwrContext* swr_ctx = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    int audio_stream_index = -1;

    // Open input file
    int ret = avformat_open_input(&format_ctx, input_filename, nullptr, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        std::cerr << "Error opening input file: " << errbuf << "\n";
        return 1;
    }

    // Retrieve stream information
    if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
        std::cerr << "Error finding stream information\n";
        avformat_close_input(&format_ctx);
        return 1;
    }

    // Find the first audio stream
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

    // Get codec parameters and find decoder
    AVCodecParameters* codecpar = format_ctx->streams[audio_stream_index]->codecpar;
    codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        std::cerr << "Codec not found\n";
        avformat_close_input(&format_ctx);
        return 1;
    }

    // Allocate codec context
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        std::cerr << "Failed to allocate codec context\n";
        avformat_close_input(&format_ctx);
        return 1;
    }

    // Copy codec parameters to context
    if (avcodec_parameters_to_context(codec_ctx, codecpar) < 0) {
        std::cerr << "Failed to copy codec parameters\n";
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        return 1;
    }

    // Open codec
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        std::cerr << "Failed to open codec\n";
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        return 1;
    }

    // Setup resampler for output format (16-bit stereo PCM at 44.1kHz)
    AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    int out_sample_rate = 44100;
    AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;

    ret = swr_alloc_set_opts2(&swr_ctx,
                              &out_ch_layout,
                              out_sample_fmt,
                              out_sample_rate,
                              &codec_ctx->ch_layout,
                              codec_ctx->sample_fmt,
                              codec_ctx->sample_rate,
                              0, nullptr);

    if (ret < 0) {
        std::cerr << "Failed to allocate resampler\n";
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        return 1;
    }

    if (swr_init(swr_ctx) < 0) {
        std::cerr << "Failed to initialize resampler\n";
        swr_free(&swr_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        return 1;
    }

    // Allocate frames
    frame = av_frame_alloc();
    packet = av_packet_alloc();

    if (!frame || !packet) {
        std::cerr << "Failed to allocate frame or packet\n";
        av_frame_free(&frame);
        av_packet_free(&packet);
        swr_free(&swr_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        return 1;
    }

    // Open output file
    std::ofstream output_file(output_filename, std::ios::binary);
    if (!output_file.is_open()) {
        std::cerr << "Failed to open output file\n";
        av_frame_free(&frame);
        av_packet_free(&packet);
        swr_free(&swr_ctx);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        return 1;
    }

    // Write placeholder WAV header (will be updated later)
    write_wav_header(output_file, out_sample_rate, 2, 16, 0);

    std::cout << "Decoding audio from " << input_filename << "\n";
    std::cout << "Input format: " << codec->long_name << "\n";
    std::cout << "Input sample rate: " << codec_ctx->sample_rate << " Hz\n";
    std::cout << "Input channels: " << codec_ctx->ch_layout.nb_channels << "\n";
    std::cout << "Output format: WAV (16-bit PCM, Stereo, 44.1kHz)\n\n";

    uint32_t total_data_size = 0;
    int frame_count = 0;

    // Allocate buffer for resampled data
    int max_dst_nb_samples = av_rescale_rnd(4096, out_sample_rate,
                                            codec_ctx->sample_rate, AV_ROUND_UP);
    uint8_t** dst_data = nullptr;
    int dst_linesize;

    av_samples_alloc_array_and_samples(&dst_data, &dst_linesize, 2,
                                       max_dst_nb_samples, out_sample_fmt, 0);

    // Read and decode frames
    while (av_read_frame(format_ctx, packet) >= 0) {
        if (packet->stream_index == audio_stream_index) {
            // Send packet to decoder
            ret = avcodec_send_packet(codec_ctx, packet);
            if (ret < 0) {
                std::cerr << "Error sending packet to decoder\n";
                break;
            }

            // Receive decoded frames
            while (ret >= 0) {
                ret = avcodec_receive_frame(codec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    std::cerr << "Error during decoding\n";
                    break;
                }

                // Resample audio
                int dst_nb_samples = av_rescale_rnd(
                    swr_get_delay(swr_ctx, codec_ctx->sample_rate) + frame->nb_samples,
                    out_sample_rate, codec_ctx->sample_rate, AV_ROUND_UP);

                if (dst_nb_samples > max_dst_nb_samples) {
                    av_freep(&dst_data[0]);
                    av_samples_alloc(dst_data, &dst_linesize, 2,
                                    dst_nb_samples, out_sample_fmt, 1);
                    max_dst_nb_samples = dst_nb_samples;
                }

                ret = swr_convert(swr_ctx, dst_data, dst_nb_samples,
                                 const_cast<const uint8_t**>(frame->data), frame->nb_samples);

                if (ret < 0) {
                    std::cerr << "Error during resampling\n";
                    break;
                }

                // Write resampled data to file
                int dst_bufsize = av_samples_get_buffer_size(&dst_linesize, 2,
                                                             ret, out_sample_fmt, 1);
                output_file.write(reinterpret_cast<char*>(dst_data[0]), dst_bufsize);
                total_data_size += dst_bufsize;

                frame_count++;
                if (frame_count % 100 == 0) {
                    std::cout << "Decoded " << frame_count << " frames\r" << std::flush;
                }
            }
        }
        av_packet_unref(packet);
    }

    // Flush resampler
    while (true) {
        ret = swr_convert(swr_ctx, dst_data, max_dst_nb_samples, nullptr, 0);
        if (ret <= 0) break;

        int dst_bufsize = av_samples_get_buffer_size(&dst_linesize, 2,
                                                     ret, out_sample_fmt, 1);
        output_file.write(reinterpret_cast<char*>(dst_data[0]), dst_bufsize);
        total_data_size += dst_bufsize;
    }

    std::cout << "\nTotal frames decoded: " << frame_count << "\n";
    std::cout << "Total data size: " << total_data_size << " bytes\n";

    // Update WAV header with actual data size
    output_file.seekp(0, std::ios::beg);
    write_wav_header(output_file, out_sample_rate, 2, 16, total_data_size);

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

    std::cout << "Decoding completed successfully!\n";
    std::cout << "Output file: " << output_filename << "\n";

    return 0;
}
