/**
 * Video Information Reader
 *
 * This sample demonstrates how to read and display video file information
 * using FFmpeg libraries.
 */

#include <iostream>
#include <iomanip>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

void print_stream_info(AVStream* stream, int index) {
    AVCodecParameters* codecpar = stream->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);

    std::cout << "Stream #" << index << ":\n";
    std::cout << "  Type: " << av_get_media_type_string(codecpar->codec_type) << "\n";
    std::cout << "  Codec: " << (codec ? codec->name : "unknown") << "\n";

    if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        std::cout << "  Resolution: " << codecpar->width << "x" << codecpar->height << "\n";
        std::cout << "  Pixel Format: " << av_get_pix_fmt_name(static_cast<AVPixelFormat>(codecpar->format)) << "\n";

        if (stream->avg_frame_rate.den && stream->avg_frame_rate.num) {
            double fps = av_q2d(stream->avg_frame_rate);
            std::cout << "  Frame Rate: " << std::fixed << std::setprecision(2) << fps << " fps\n";
        }

        std::cout << "  Bit Rate: " << codecpar->bit_rate / 1000 << " kbps\n";
    } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        std::cout << "  Sample Rate: " << codecpar->sample_rate << " Hz\n";
        std::cout << "  Channels: " << codecpar->ch_layout.nb_channels << "\n";
        std::cout << "  Bit Rate: " << codecpar->bit_rate / 1000 << " kbps\n";
    }

    if (stream->duration != AV_NOPTS_VALUE) {
        double duration = stream->duration * av_q2d(stream->time_base);
        std::cout << "  Duration: " << std::fixed << std::setprecision(2) << duration << " seconds\n";
    }

    std::cout << "\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input_file>\n";
        return 1;
    }

    const char* input_filename = argv[1];
    AVFormatContext* format_ctx = nullptr;

    // Open input file
    int ret = avformat_open_input(&format_ctx, input_filename, nullptr, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        std::cerr << "Error opening input file: " << errbuf << "\n";
        return 1;
    }

    // Retrieve stream information
    ret = avformat_find_stream_info(format_ctx, nullptr);
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
        std::cerr << "Error finding stream info: " << errbuf << "\n";
        avformat_close_input(&format_ctx);
        return 1;
    }

    // Print file information
    std::cout << "File: " << input_filename << "\n";
    std::cout << "Format: " << format_ctx->iformat->long_name << "\n";

    if (format_ctx->duration != AV_NOPTS_VALUE) {
        double duration = format_ctx->duration / static_cast<double>(AV_TIME_BASE);
        int hours = static_cast<int>(duration / 3600);
        int minutes = static_cast<int>((duration - hours * 3600) / 60);
        int seconds = static_cast<int>(duration - hours * 3600 - minutes * 60);

        std::cout << "Duration: " << std::setfill('0') << std::setw(2) << hours << ":"
                  << std::setw(2) << minutes << ":" << std::setw(2) << seconds << "\n";
    }

    if (format_ctx->bit_rate > 0) {
        std::cout << "Overall Bit Rate: " << format_ctx->bit_rate / 1000 << " kbps\n";
    }

    std::cout << "Number of Streams: " << format_ctx->nb_streams << "\n\n";

    // Print information for each stream
    for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
        print_stream_info(format_ctx->streams[i], i);
    }

    // Cleanup
    avformat_close_input(&format_ctx);

    return 0;
}
