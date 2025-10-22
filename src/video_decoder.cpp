/**
 * Video Decoder
 *
 * This sample demonstrates how to decode video frames from a file
 * and save them as PPM images using FFmpeg libraries.
 */

#include <iostream>
#include <fstream>
#include <iomanip>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

void save_frame_as_ppm(AVFrame* frame, int width, int height, int frame_number, const char* output_dir) {
    std::string filename = std::string(output_dir) + "/frame_"
                          + std::to_string(frame_number) + ".ppm";

    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error opening output file: " << filename << "\n";
        return;
    }

    // Write PPM header
    file << "P6\n" << width << " " << height << "\n255\n";

    // Write pixel data
    for (int y = 0; y < height; y++) {
        file.write(reinterpret_cast<char*>(frame->data[0] + y * frame->linesize[0]), width * 3);
    }

    file.close();
    std::cout << "Saved frame " << frame_number << " to " << filename << "\n";
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_file> <output_dir> [max_frames]\n";
        return 1;
    }

    const char* input_filename = argv[1];
    const char* output_dir = argv[2];
    int max_frames = argc > 3 ? std::atoi(argv[3]) : 10;

    AVFormatContext* format_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    const AVCodec* codec = nullptr;
    SwsContext* sws_ctx = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* frame_rgb = nullptr;
    int video_stream_index = -1;

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

    // Find the first video stream
    for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }

    if (video_stream_index == -1) {
        std::cerr << "No video stream found\n";
        avformat_close_input(&format_ctx);
        return 1;
    }

    // Get codec parameters and find decoder
    AVCodecParameters* codecpar = format_ctx->streams[video_stream_index]->codecpar;
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

    // Allocate frames
    frame = av_frame_alloc();
    frame_rgb = av_frame_alloc();
    if (!frame || !frame_rgb) {
        std::cerr << "Failed to allocate frames\n";
        av_frame_free(&frame);
        av_frame_free(&frame_rgb);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        return 1;
    }

    // Allocate buffer for RGB frame
    int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, codec_ctx->width,
                                              codec_ctx->height, 1);
    uint8_t* buffer = static_cast<uint8_t*>(av_malloc(num_bytes * sizeof(uint8_t)));

    av_image_fill_arrays(frame_rgb->data, frame_rgb->linesize, buffer, AV_PIX_FMT_RGB24,
                        codec_ctx->width, codec_ctx->height, 1);

    // Initialize SWS context for color conversion
    sws_ctx = sws_getContext(codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
                            codec_ctx->width, codec_ctx->height, AV_PIX_FMT_RGB24,
                            SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!sws_ctx) {
        std::cerr << "Failed to initialize SWS context\n";
        av_free(buffer);
        av_frame_free(&frame);
        av_frame_free(&frame_rgb);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);
        return 1;
    }

    packet = av_packet_alloc();
    int frame_count = 0;

    std::cout << "Decoding video from " << input_filename << "\n";
    std::cout << "Resolution: " << codec_ctx->width << "x" << codec_ctx->height << "\n";
    std::cout << "Maximum frames to decode: " << max_frames << "\n\n";

    // Read frames
    while (av_read_frame(format_ctx, packet) >= 0 && frame_count < max_frames) {
        if (packet->stream_index == video_stream_index) {
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

                // Convert frame to RGB
                sws_scale(sws_ctx, frame->data, frame->linesize, 0, codec_ctx->height,
                         frame_rgb->data, frame_rgb->linesize);

                // Save frame
                save_frame_as_ppm(frame_rgb, codec_ctx->width, codec_ctx->height,
                                 frame_count, output_dir);

                frame_count++;
                if (frame_count >= max_frames) {
                    break;
                }
            }
        }
        av_packet_unref(packet);
    }

    std::cout << "\nTotal frames decoded: " << frame_count << "\n";

    // Cleanup
    av_packet_free(&packet);
    av_free(buffer);
    av_frame_free(&frame);
    av_frame_free(&frame_rgb);
    sws_freeContext(sws_ctx);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&format_ctx);

    return 0;
}
