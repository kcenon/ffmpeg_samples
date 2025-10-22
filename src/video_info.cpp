/**
 * Video Information Reader
 *
 * This sample demonstrates how to read and display video file information
 * using FFmpeg libraries with modern C++17 features.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <iomanip>
#include <string_view>
#include <span>

namespace {

void print_stream_info(const AVStream& stream, int index) {
    const auto* codecpar = stream.codecpar;
    const auto* codec = avcodec_find_decoder(codecpar->codec_id);

    std::cout << "Stream #" << index << ":\n";
    std::cout << "  Type: " << av_get_media_type_string(codecpar->codec_type) << "\n";
    std::cout << "  Codec: " << (codec ? codec->name : "unknown") << "\n";

    if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        std::cout << "  Resolution: " << codecpar->width << "x" << codecpar->height << "\n";

        if (const auto* pix_fmt_name = av_get_pix_fmt_name(static_cast<AVPixelFormat>(codecpar->format))) {
            std::cout << "  Pixel Format: " << pix_fmt_name << "\n";
        }

        if (stream.avg_frame_rate.den && stream.avg_frame_rate.num) {
            const auto fps = av_q2d(stream.avg_frame_rate);
            std::cout << "  Frame Rate: " << std::fixed << std::setprecision(2) << fps << " fps\n";
        }

        std::cout << "  Bit Rate: " << codecpar->bit_rate / 1000 << " kbps\n";
    } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        std::cout << "  Sample Rate: " << codecpar->sample_rate << " Hz\n";
        std::cout << "  Channels: " << codecpar->ch_layout.nb_channels << "\n";
        std::cout << "  Bit Rate: " << codecpar->bit_rate / 1000 << " kbps\n";
    }

    if (stream.duration != AV_NOPTS_VALUE) {
        const auto duration = stream.duration * av_q2d(stream.time_base);
        std::cout << "  Duration: " << std::fixed << std::setprecision(2) << duration << " seconds\n";
    }

    std::cout << "\n";
}

void print_duration(int64_t duration_us) {
    const auto duration = duration_us / static_cast<double>(AV_TIME_BASE);
    const auto hours = static_cast<int>(duration / 3600);
    const auto minutes = static_cast<int>((duration - hours * 3600) / 60);
    const auto seconds = static_cast<int>(duration - hours * 3600 - minutes * 60);

    std::cout << "Duration: " << std::setfill('0') << std::setw(2) << hours << ":"
              << std::setw(2) << minutes << ":" << std::setw(2) << seconds << "\n";
}

void print_format_info(const AVFormatContext& format_ctx, std::string_view filename) {
    std::cout << "File: " << filename << "\n";
    std::cout << "Format: " << format_ctx.iformat->long_name << "\n";

    if (format_ctx.duration != AV_NOPTS_VALUE) {
        print_duration(format_ctx.duration);
    }

    if (format_ctx.bit_rate > 0) {
        std::cout << "Overall Bit Rate: " << format_ctx.bit_rate / 1000 << " kbps\n";
    }

    std::cout << "Number of Streams: " << format_ctx.nb_streams << "\n\n";
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input_file>\n";
        return 1;
    }

    try {
        const std::string_view input_filename{argv[1]};

        // Open input file using RAII wrapper
        auto format_ctx = ffmpeg::open_input_format(input_filename.data());

        // Print file information
        print_format_info(*format_ctx, input_filename);

        // Print information for each stream
        const std::span streams{format_ctx->streams, format_ctx->nb_streams};
        for (int i = 0; const auto* stream : streams) {
            print_stream_info(*stream, i++);
        }

    } catch (const ffmpeg::FFmpegError& e) {
        std::cerr << "FFmpeg error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
