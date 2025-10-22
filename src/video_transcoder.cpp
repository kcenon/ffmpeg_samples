/**
 * Video Transcoder
 *
 * This sample demonstrates how to transcode a video file from one format
 * to another, including changing codec, resolution, and bitrate.
 */

#include <iostream>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

class VideoTranscoder {
private:
    AVFormatContext* input_format_ctx;
    AVFormatContext* output_format_ctx;
    AVCodecContext* input_video_codec_ctx;
    AVCodecContext* output_video_codec_ctx;
    AVCodecContext* input_audio_codec_ctx;
    AVCodecContext* output_audio_codec_ctx;
    SwsContext* sws_ctx;
    SwrContext* swr_ctx;
    int video_stream_index;
    int audio_stream_index;
    int output_width;
    int output_height;

public:
    VideoTranscoder()
        : input_format_ctx(nullptr), output_format_ctx(nullptr),
          input_video_codec_ctx(nullptr), output_video_codec_ctx(nullptr),
          input_audio_codec_ctx(nullptr), output_audio_codec_ctx(nullptr),
          sws_ctx(nullptr), swr_ctx(nullptr),
          video_stream_index(-1), audio_stream_index(-1),
          output_width(0), output_height(0) {}

    ~VideoTranscoder() {
        cleanup();
    }

    bool open_input(const char* filename) {
        int ret = avformat_open_input(&input_format_ctx, filename, nullptr, nullptr);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            std::cerr << "Error opening input file: " << errbuf << "\n";
            return false;
        }

        if (avformat_find_stream_info(input_format_ctx, nullptr) < 0) {
            std::cerr << "Error finding stream information\n";
            return false;
        }

        // Find video and audio streams
        for (unsigned int i = 0; i < input_format_ctx->nb_streams; i++) {
            AVCodecParameters* codecpar = input_format_ctx->streams[i]->codecpar;
            if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_stream_index < 0) {
                video_stream_index = i;
            } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_stream_index < 0) {
                audio_stream_index = i;
            }
        }

        if (video_stream_index == -1) {
            std::cerr << "No video stream found\n";
            return false;
        }

        // Open video decoder
        if (!open_decoder(video_stream_index, &input_video_codec_ctx)) {
            return false;
        }

        // Open audio decoder if available
        if (audio_stream_index >= 0) {
            open_decoder(audio_stream_index, &input_audio_codec_ctx);
        }

        return true;
    }

    bool open_decoder(int stream_index, AVCodecContext** codec_ctx) {
        AVCodecParameters* codecpar = input_format_ctx->streams[stream_index]->codecpar;
        const AVCodec* decoder = avcodec_find_decoder(codecpar->codec_id);

        if (!decoder) {
            std::cerr << "Decoder not found\n";
            return false;
        }

        *codec_ctx = avcodec_alloc_context3(decoder);
        if (!*codec_ctx) {
            std::cerr << "Failed to allocate decoder context\n";
            return false;
        }

        if (avcodec_parameters_to_context(*codec_ctx, codecpar) < 0) {
            std::cerr << "Failed to copy codec parameters\n";
            return false;
        }

        if (avcodec_open2(*codec_ctx, decoder, nullptr) < 0) {
            std::cerr << "Failed to open decoder\n";
            return false;
        }

        return true;
    }

    bool create_output(const char* filename, int width, int height, int bitrate, int fps) {
        output_width = width;
        output_height = height;

        avformat_alloc_output_context2(&output_format_ctx, nullptr, nullptr, filename);
        if (!output_format_ctx) {
            std::cerr << "Could not create output context\n";
            return false;
        }

        // Create video output stream
        if (!create_video_encoder(width, height, bitrate, fps)) {
            return false;
        }

        // Create audio output stream if input has audio
        if (audio_stream_index >= 0 && input_audio_codec_ctx) {
            create_audio_encoder();
        }

        // Open output file
        if (!(output_format_ctx->oformat->flags & AVFMT_NOFILE)) {
            int ret = avio_open(&output_format_ctx->pb, filename, AVIO_FLAG_WRITE);
            if (ret < 0) {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
                std::cerr << "Failed to open output file: " << errbuf << "\n";
                return false;
            }
        }

        // Write header
        int ret = avformat_write_header(output_format_ctx, nullptr);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            std::cerr << "Error writing header: " << errbuf << "\n";
            return false;
        }

        // Initialize scaling context
        sws_ctx = sws_getContext(
            input_video_codec_ctx->width, input_video_codec_ctx->height,
            input_video_codec_ctx->pix_fmt,
            output_width, output_height, AV_PIX_FMT_YUV420P,
            SWS_BICUBIC, nullptr, nullptr, nullptr
        );

        if (!sws_ctx) {
            std::cerr << "Failed to initialize scaling context\n";
            return false;
        }

        return true;
    }

    bool create_video_encoder(int width, int height, int bitrate, int fps) {
        const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!encoder) {
            std::cerr << "H264 encoder not found\n";
            return false;
        }

        AVStream* out_stream = avformat_new_stream(output_format_ctx, nullptr);
        if (!out_stream) {
            std::cerr << "Failed to create output stream\n";
            return false;
        }

        output_video_codec_ctx = avcodec_alloc_context3(encoder);
        if (!output_video_codec_ctx) {
            std::cerr << "Failed to allocate encoder context\n";
            return false;
        }

        output_video_codec_ctx->width = width;
        output_video_codec_ctx->height = height;
        output_video_codec_ctx->time_base = AVRational{1, fps};
        output_video_codec_ctx->framerate = AVRational{fps, 1};
        output_video_codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        output_video_codec_ctx->bit_rate = bitrate;
        output_video_codec_ctx->gop_size = 10;
        output_video_codec_ctx->max_b_frames = 1;

        av_opt_set(output_video_codec_ctx->priv_data, "preset", "medium", 0);

        if (output_format_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
            output_video_codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        if (avcodec_open2(output_video_codec_ctx, encoder, nullptr) < 0) {
            std::cerr << "Failed to open encoder\n";
            return false;
        }

        avcodec_parameters_from_context(out_stream->codecpar, output_video_codec_ctx);
        out_stream->time_base = output_video_codec_ctx->time_base;

        return true;
    }

    bool create_audio_encoder() {
        const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!encoder) {
            std::cerr << "AAC encoder not found\n";
            return false;
        }

        AVStream* out_stream = avformat_new_stream(output_format_ctx, nullptr);
        if (!out_stream) {
            std::cerr << "Failed to create audio output stream\n";
            return false;
        }

        output_audio_codec_ctx = avcodec_alloc_context3(encoder);
        if (!output_audio_codec_ctx) {
            std::cerr << "Failed to allocate audio encoder context\n";
            return false;
        }

        output_audio_codec_ctx->sample_rate = input_audio_codec_ctx->sample_rate;
        output_audio_codec_ctx->ch_layout = input_audio_codec_ctx->ch_layout;
        output_audio_codec_ctx->sample_fmt = encoder->sample_fmts[0];
        output_audio_codec_ctx->time_base = AVRational{1, input_audio_codec_ctx->sample_rate};
        output_audio_codec_ctx->bit_rate = 128000;

        if (output_format_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
            output_audio_codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        if (avcodec_open2(output_audio_codec_ctx, encoder, nullptr) < 0) {
            std::cerr << "Failed to open audio encoder\n";
            return false;
        }

        avcodec_parameters_from_context(out_stream->codecpar, output_audio_codec_ctx);
        out_stream->time_base = output_audio_codec_ctx->time_base;

        return true;
    }

    bool transcode() {
        AVPacket* packet = av_packet_alloc();
        AVFrame* input_frame = av_frame_alloc();
        AVFrame* output_frame = av_frame_alloc();

        if (!packet || !input_frame || !output_frame) {
            std::cerr << "Failed to allocate packet or frames\n";
            av_packet_free(&packet);
            av_frame_free(&input_frame);
            av_frame_free(&output_frame);
            return false;
        }

        // Allocate output frame buffer
        output_frame->format = AV_PIX_FMT_YUV420P;
        output_frame->width = output_width;
        output_frame->height = output_height;
        av_frame_get_buffer(output_frame, 0);

        int64_t pts_counter = 0;
        int frame_count = 0;

        std::cout << "Transcoding in progress...\n";

        while (av_read_frame(input_format_ctx, packet) >= 0) {
            if (packet->stream_index == video_stream_index) {
                // Decode video frame
                int ret = avcodec_send_packet(input_video_codec_ctx, packet);
                if (ret < 0) {
                    av_packet_unref(packet);
                    continue;
                }

                while (ret >= 0) {
                    ret = avcodec_receive_frame(input_video_codec_ctx, input_frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    } else if (ret < 0) {
                        std::cerr << "Error decoding video frame\n";
                        break;
                    }

                    // Scale frame
                    av_frame_make_writable(output_frame);
                    sws_scale(sws_ctx, input_frame->data, input_frame->linesize,
                             0, input_video_codec_ctx->height,
                             output_frame->data, output_frame->linesize);

                    output_frame->pts = pts_counter++;

                    // Encode frame
                    encode_video_frame(output_frame);
                    frame_count++;

                    if (frame_count % 30 == 0) {
                        std::cout << "Processed " << frame_count << " frames\r" << std::flush;
                    }
                }
            }
            av_packet_unref(packet);
        }

        std::cout << "\nTotal frames transcoded: " << frame_count << "\n";

        // Flush encoders
        flush_encoder(output_video_codec_ctx, 0);

        // Write trailer
        av_write_trailer(output_format_ctx);

        av_packet_free(&packet);
        av_frame_free(&input_frame);
        av_frame_free(&output_frame);

        return true;
    }

    void encode_video_frame(AVFrame* frame) {
        AVPacket* packet = av_packet_alloc();

        int ret = avcodec_send_frame(output_video_codec_ctx, frame);
        if (ret < 0) {
            av_packet_free(&packet);
            return;
        }

        while (ret >= 0) {
            ret = avcodec_receive_packet(output_video_codec_ctx, packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                break;
            }

            av_packet_rescale_ts(packet, output_video_codec_ctx->time_base,
                                output_format_ctx->streams[0]->time_base);
            packet->stream_index = 0;
            av_interleaved_write_frame(output_format_ctx, packet);
            av_packet_unref(packet);
        }

        av_packet_free(&packet);
    }

    void flush_encoder(AVCodecContext* codec_ctx, int stream_index) {
        AVPacket* packet = av_packet_alloc();
        avcodec_send_frame(codec_ctx, nullptr);

        int ret;
        while ((ret = avcodec_receive_packet(codec_ctx, packet)) >= 0) {
            av_packet_rescale_ts(packet, codec_ctx->time_base,
                                output_format_ctx->streams[stream_index]->time_base);
            packet->stream_index = stream_index;
            av_interleaved_write_frame(output_format_ctx, packet);
            av_packet_unref(packet);
        }

        av_packet_free(&packet);
    }

    void cleanup() {
        if (sws_ctx) sws_freeContext(sws_ctx);
        if (swr_ctx) swr_free(&swr_ctx);
        if (input_video_codec_ctx) avcodec_free_context(&input_video_codec_ctx);
        if (input_audio_codec_ctx) avcodec_free_context(&input_audio_codec_ctx);
        if (output_video_codec_ctx) avcodec_free_context(&output_video_codec_ctx);
        if (output_audio_codec_ctx) avcodec_free_context(&output_audio_codec_ctx);
        if (input_format_ctx) avformat_close_input(&input_format_ctx);
        if (output_format_ctx) {
            if (!(output_format_ctx->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&output_format_ctx->pb);
            }
            avformat_free_context(output_format_ctx);
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_file> <output_file> [width] [height] [bitrate] [fps]\n";
        std::cerr << "Example: " << argv[0] << " input.mp4 output.mp4 1280 720 2000000 30\n";
        return 1;
    }

    const char* input_filename = argv[1];
    const char* output_filename = argv[2];
    int width = argc > 3 ? std::atoi(argv[3]) : 1280;
    int height = argc > 4 ? std::atoi(argv[4]) : 720;
    int bitrate = argc > 5 ? std::atoi(argv[5]) : 2000000;
    int fps = argc > 6 ? std::atoi(argv[6]) : 30;

    std::cout << "FFmpeg Video Transcoder\n";
    std::cout << "=======================\n";
    std::cout << "Input: " << input_filename << "\n";
    std::cout << "Output: " << output_filename << "\n";
    std::cout << "Resolution: " << width << "x" << height << "\n";
    std::cout << "Bitrate: " << bitrate / 1000 << " kbps\n";
    std::cout << "Frame rate: " << fps << " fps\n\n";

    VideoTranscoder transcoder;

    if (!transcoder.open_input(input_filename)) {
        std::cerr << "Failed to open input file\n";
        return 1;
    }

    if (!transcoder.create_output(output_filename, width, height, bitrate, fps)) {
        std::cerr << "Failed to create output file\n";
        return 1;
    }

    if (!transcoder.transcode()) {
        std::cerr << "Transcoding failed\n";
        return 1;
    }

    std::cout << "Transcoding completed successfully!\n";
    std::cout << "Output file: " << output_filename << "\n";

    return 0;
}
