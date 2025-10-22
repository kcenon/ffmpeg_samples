/**
 * Video Encoder
 *
 * This sample demonstrates how to encode a series of generated frames
 * into a video file using FFmpeg libraries.
 */

#include <iostream>
#include <cmath>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

void generate_test_frame(AVFrame* frame, int frame_number) {
    // Generate a simple animated pattern
    for (int y = 0; y < frame->height; y++) {
        for (int x = 0; x < frame->width; x++) {
            // Y plane (brightness)
            int offset = (frame_number * 5 + x + y) % 256;
            frame->data[0][y * frame->linesize[0] + x] = offset;
        }
    }

    // U and V planes (color)
    for (int y = 0; y < frame->height / 2; y++) {
        for (int x = 0; x < frame->width / 2; x++) {
            frame->data[1][y * frame->linesize[1] + x] = 128 + y + frame_number;
            frame->data[2][y * frame->linesize[2] + x] = 64 + x + frame_number;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <output_file> [num_frames] [width] [height] [fps]\n";
        std::cerr << "Example: " << argv[0] << " output.mp4 100 1280 720 30\n";
        return 1;
    }

    const char* output_filename = argv[1];
    int num_frames = argc > 2 ? std::atoi(argv[2]) : 100;
    int width = argc > 3 ? std::atoi(argv[3]) : 1280;
    int height = argc > 4 ? std::atoi(argv[4]) : 720;
    int fps = argc > 5 ? std::atoi(argv[5]) : 30;

    AVFormatContext* format_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    const AVCodec* codec = nullptr;
    AVStream* stream = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    int ret;

    // Allocate output format context
    avformat_alloc_output_context2(&format_ctx, nullptr, nullptr, output_filename);
    if (!format_ctx) {
        std::cerr << "Could not deduce output format from file extension\n";
        return 1;
    }

    // Find encoder
    codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        std::cerr << "H264 codec not found\n";
        avformat_free_context(format_ctx);
        return 1;
    }

    // Create new stream
    stream = avformat_new_stream(format_ctx, nullptr);
    if (!stream) {
        std::cerr << "Failed to create stream\n";
        avformat_free_context(format_ctx);
        return 1;
    }

    // Allocate codec context
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        std::cerr << "Failed to allocate codec context\n";
        avformat_free_context(format_ctx);
        return 1;
    }

    // Set codec parameters
    codec_ctx->codec_id = AV_CODEC_ID_H264;
    codec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
    codec_ctx->width = width;
    codec_ctx->height = height;
    codec_ctx->time_base = AVRational{1, fps};
    codec_ctx->framerate = AVRational{fps, 1};
    codec_ctx->gop_size = 10;
    codec_ctx->max_b_frames = 1;
    codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    codec_ctx->bit_rate = 2000000;

    // Set H264 preset for better encoding
    av_opt_set(codec_ctx->priv_data, "preset", "medium", 0);

    // Some formats require global headers
    if (format_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    // Open codec
    ret = avcodec_open2(codec_ctx, codec, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        std::cerr << "Failed to open codec: " << errbuf << "\n";
        avcodec_free_context(&codec_ctx);
        avformat_free_context(format_ctx);
        return 1;
    }

    // Copy codec parameters to stream
    ret = avcodec_parameters_from_context(stream->codecpar, codec_ctx);
    if (ret < 0) {
        std::cerr << "Failed to copy codec parameters\n";
        avcodec_free_context(&codec_ctx);
        avformat_free_context(format_ctx);
        return 1;
    }

    stream->time_base = codec_ctx->time_base;

    // Open output file
    if (!(format_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&format_ctx->pb, output_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            std::cerr << "Failed to open output file: " << errbuf << "\n";
            avcodec_free_context(&codec_ctx);
            avformat_free_context(format_ctx);
            return 1;
        }
    }

    // Write file header
    ret = avformat_write_header(format_ctx, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        std::cerr << "Error writing header: " << errbuf << "\n";
        avcodec_free_context(&codec_ctx);
        if (!(format_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&format_ctx->pb);
        }
        avformat_free_context(format_ctx);
        return 1;
    }

    // Allocate frame
    frame = av_frame_alloc();
    if (!frame) {
        std::cerr << "Failed to allocate frame\n";
        avcodec_free_context(&codec_ctx);
        if (!(format_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&format_ctx->pb);
        }
        avformat_free_context(format_ctx);
        return 1;
    }

    frame->format = codec_ctx->pix_fmt;
    frame->width = codec_ctx->width;
    frame->height = codec_ctx->height;

    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        std::cerr << "Failed to allocate frame buffer\n";
        av_frame_free(&frame);
        avcodec_free_context(&codec_ctx);
        if (!(format_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&format_ctx->pb);
        }
        avformat_free_context(format_ctx);
        return 1;
    }

    packet = av_packet_alloc();
    if (!packet) {
        std::cerr << "Failed to allocate packet\n";
        av_frame_free(&frame);
        avcodec_free_context(&codec_ctx);
        if (!(format_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&format_ctx->pb);
        }
        avformat_free_context(format_ctx);
        return 1;
    }

    std::cout << "Encoding video to " << output_filename << "\n";
    std::cout << "Resolution: " << width << "x" << height << "\n";
    std::cout << "Frame rate: " << fps << " fps\n";
    std::cout << "Number of frames: " << num_frames << "\n\n";

    // Encode frames
    for (int i = 0; i < num_frames; i++) {
        ret = av_frame_make_writable(frame);
        if (ret < 0) {
            std::cerr << "Frame not writable\n";
            break;
        }

        // Generate test frame
        generate_test_frame(frame, i);
        frame->pts = i;

        // Send frame to encoder
        ret = avcodec_send_frame(codec_ctx, frame);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            std::cerr << "Error sending frame: " << errbuf << "\n";
            break;
        }

        // Receive encoded packets
        while (ret >= 0) {
            ret = avcodec_receive_packet(codec_ctx, packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                std::cerr << "Error during encoding\n";
                break;
            }

            // Rescale packet timestamps
            av_packet_rescale_ts(packet, codec_ctx->time_base, stream->time_base);
            packet->stream_index = stream->index;

            // Write packet to output file
            ret = av_interleaved_write_frame(format_ctx, packet);
            av_packet_unref(packet);

            if (ret < 0) {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
                std::cerr << "Error writing frame: " << errbuf << "\n";
                break;
            }
        }

        if ((i + 1) % 10 == 0) {
            std::cout << "Encoded frame " << (i + 1) << "/" << num_frames << "\n";
        }
    }

    // Flush encoder
    avcodec_send_frame(codec_ctx, nullptr);
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_ctx, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            break;
        }

        av_packet_rescale_ts(packet, codec_ctx->time_base, stream->time_base);
        packet->stream_index = stream->index;
        av_interleaved_write_frame(format_ctx, packet);
        av_packet_unref(packet);
    }

    // Write file trailer
    av_write_trailer(format_ctx);

    std::cout << "\nEncoding completed successfully!\n";
    std::cout << "Output file: " << output_filename << "\n";

    // Cleanup
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&codec_ctx);
    if (!(format_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&format_ctx->pb);
    }
    avformat_free_context(format_ctx);

    return 0;
}
