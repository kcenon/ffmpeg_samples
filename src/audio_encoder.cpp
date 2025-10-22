/**
 * Audio Encoder
 *
 * This sample demonstrates how to encode audio data (sine wave) into
 * various audio formats using FFmpeg libraries.
 */

#include <iostream>
#include <cmath>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void generate_sine_wave(AVFrame* frame, int frame_num, double frequency, int sample_rate) {
    int16_t* samples = reinterpret_cast<int16_t*>(frame->data[0]);
    double t = frame_num * frame->nb_samples / static_cast<double>(sample_rate);

    for (int i = 0; i < frame->nb_samples; i++) {
        double sample_time = t + i / static_cast<double>(sample_rate);
        int16_t sample = static_cast<int16_t>(sin(2.0 * M_PI * frequency * sample_time) * 10000.0);

        // Stereo: same sample for both channels
        samples[2 * i] = sample;      // Left channel
        samples[2 * i + 1] = sample;  // Right channel
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <output_file> [duration_seconds] [frequency_hz]\n";
        std::cerr << "Example: " << argv[0] << " output.mp3 10 440\n";
        std::cerr << "\nGenerates a sine wave tone.\n";
        std::cerr << "Default: 5 seconds, 440 Hz (A4 note)\n";
        return 1;
    }

    const char* output_filename = argv[1];
    double duration = argc > 2 ? std::atof(argv[2]) : 5.0;
    double frequency = argc > 3 ? std::atof(argv[3]) : 440.0;

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

    // Find encoder (prefer AAC for MP4/M4A, MP3 for MP3)
    const char* codec_name = nullptr;
    if (strstr(output_filename, ".mp3")) {
        codec_name = "libmp3lame";
    } else if (strstr(output_filename, ".aac") || strstr(output_filename, ".m4a")) {
        codec_name = "aac";
    } else if (strstr(output_filename, ".ogg") || strstr(output_filename, ".oga")) {
        codec_name = "libvorbis";
    } else if (strstr(output_filename, ".flac")) {
        codec_name = "flac";
    } else {
        codec_name = "aac";  // Default
    }

    codec = avcodec_find_encoder_by_name(codec_name);
    if (!codec) {
        std::cerr << "Codec '" << codec_name << "' not found, trying default\n";
        codec = avcodec_find_encoder(format_ctx->oformat->audio_codec);
    }

    if (!codec) {
        std::cerr << "Audio codec not found\n";
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
    codec_ctx->codec_id = codec->id;
    codec_ctx->codec_type = AVMEDIA_TYPE_AUDIO;
    codec_ctx->sample_rate = 44100;
    codec_ctx->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    codec_ctx->bit_rate = 128000;

    // Select sample format
    if (codec->sample_fmts) {
        codec_ctx->sample_fmt = codec->sample_fmts[0];
    } else {
        codec_ctx->sample_fmt = AV_SAMPLE_FMT_S16;
    }

    // Set time base
    codec_ctx->time_base = AVRational{1, codec_ctx->sample_rate};

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
        if (!(format_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&format_ctx->pb);
        }
        avcodec_free_context(&codec_ctx);
        avformat_free_context(format_ctx);
        return 1;
    }

    // Allocate frame
    frame = av_frame_alloc();
    if (!frame) {
        std::cerr << "Failed to allocate frame\n";
        av_write_trailer(format_ctx);
        if (!(format_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&format_ctx->pb);
        }
        avcodec_free_context(&codec_ctx);
        avformat_free_context(format_ctx);
        return 1;
    }

    frame->format = AV_SAMPLE_FMT_S16;
    frame->ch_layout = codec_ctx->ch_layout;
    frame->sample_rate = codec_ctx->sample_rate;
    frame->nb_samples = codec_ctx->frame_size > 0 ? codec_ctx->frame_size : 1024;

    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        std::cerr << "Failed to allocate frame buffer\n";
        av_frame_free(&frame);
        av_write_trailer(format_ctx);
        if (!(format_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&format_ctx->pb);
        }
        avcodec_free_context(&codec_ctx);
        avformat_free_context(format_ctx);
        return 1;
    }

    packet = av_packet_alloc();
    if (!packet) {
        std::cerr << "Failed to allocate packet\n";
        av_frame_free(&frame);
        av_write_trailer(format_ctx);
        if (!(format_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&format_ctx->pb);
        }
        avcodec_free_context(&codec_ctx);
        avformat_free_context(format_ctx);
        return 1;
    }

    std::cout << "Encoding audio to " << output_filename << "\n";
    std::cout << "Codec: " << codec->long_name << "\n";
    std::cout << "Sample Rate: " << codec_ctx->sample_rate << " Hz\n";
    std::cout << "Channels: 2 (Stereo)\n";
    std::cout << "Bit Rate: " << codec_ctx->bit_rate / 1000 << " kbps\n";
    std::cout << "Duration: " << duration << " seconds\n";
    std::cout << "Frequency: " << frequency << " Hz\n\n";

    int total_samples = static_cast<int>(duration * codec_ctx->sample_rate);
    int frame_count = 0;
    int64_t pts = 0;

    // Encode frames
    while (pts < total_samples) {
        ret = av_frame_make_writable(frame);
        if (ret < 0) {
            std::cerr << "Frame not writable\n";
            break;
        }

        // Generate sine wave
        generate_sine_wave(frame, frame_count, frequency, codec_ctx->sample_rate);
        frame->pts = pts;
        pts += frame->nb_samples;

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

        frame_count++;
        if (frame_count % 10 == 0) {
            double progress = (pts * 100.0) / total_samples;
            std::cout << "Encoding progress: " << std::fixed << std::setprecision(1)
                      << progress << "%\r" << std::flush;
        }
    }

    std::cout << "\n";

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

    std::cout << "Encoding completed successfully!\n";
    std::cout << "Total frames encoded: " << frame_count << "\n";
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
