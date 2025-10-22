/**
 * Audio Information Reader
 *
 * This sample demonstrates how to read and display audio file information
 * using modern C++20 and FFmpeg libraries.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <iomanip>
#include <format>
#include <span>
#include <array>

namespace {

void print_audio_stream_info(const AVStream& stream, int index) {
    const auto* codecpar = stream.codecpar;
    const auto* codec = avcodec_find_decoder(codecpar->codec_id);

    std::cout << std::format("Audio Stream #{}:\n", index);
    std::cout << std::format("  Codec: {} ({})\n",
                            codec ? codec->long_name : "unknown",
                            codec ? codec->name : "unknown");
    std::cout << std::format("  Sample Rate: {} Hz\n", codecpar->sample_rate);
    std::cout << std::format("  Channels: {}\n", codecpar->ch_layout.nb_channels);

    // Channel layout
    std::array<char, 64> ch_layout_str{};
    av_channel_layout_describe(&codecpar->ch_layout, ch_layout_str.data(), ch_layout_str.size());
    std::cout << std::format("  Channel Layout: {}\n", ch_layout_str.data());

    // Sample format
    if (const auto* sample_fmt_name = av_get_sample_fmt_name(
            static_cast<AVSampleFormat>(codecpar->format))) {
        std::cout << std::format("  Sample Format: {}\n", sample_fmt_name);
    }

    // Bit rate
    if (codecpar->bit_rate > 0) {
        std::cout << std::format("  Bit Rate: {} kbps\n", codecpar->bit_rate / 1000);
    }

    // Frame size
    if (codecpar->frame_size > 0) {
        std::cout << std::format("  Frame Size: {} samples\n", codecpar->frame_size);
    }

    // Duration
    if (stream.duration != AV_NOPTS_VALUE) {
        const auto duration = stream.duration * av_q2d(stream.time_base);
        const auto minutes = static_cast<int>(duration / 60);
        const auto seconds = static_cast<int>(duration) % 60;
        const auto milliseconds = static_cast<int>((duration - static_cast<int>(duration)) * 1000);

        std::cout << std::format("  Duration: {:02}:{:02}.{:03}\n",
                                minutes, seconds, milliseconds);
    }

    std::cout << "\n";
}

void print_metadata(const AVDictionary* metadata) {
    std::cout << "======================================\n";
    std::cout << "Metadata\n";
    std::cout << "======================================\n";

    AVDictionaryEntry* tag = nullptr;
    bool has_metadata = false;

    while ((tag = av_dict_get(metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        std::cout << std::format("{}: {}\n", tag->key, tag->value);
        has_metadata = true;
    }

    if (!has_metadata) {
        std::cout << "No metadata available\n";
    }
}

void print_format_info(const AVFormatContext& ctx, std::string_view filename) {
    std::cout << "======================================\n";
    std::cout << "Audio File Information\n";
    std::cout << "======================================\n\n";

    std::cout << std::format("File: {}\n", filename);
    std::cout << std::format("Format: {}\n", ctx.iformat->long_name);

    if (ctx.duration != AV_NOPTS_VALUE) {
        const auto duration = ctx.duration / static_cast<double>(AV_TIME_BASE);
        const auto hours = static_cast<int>(duration / 3600);
        const auto minutes = static_cast<int>((duration - hours * 3600) / 60);
        const auto seconds = static_cast<int>(duration - hours * 3600 - minutes * 60);

        std::cout << std::format("Duration: {:02}:{:02}:{:02}\n", hours, minutes, seconds);
    }

    if (ctx.bit_rate > 0) {
        std::cout << std::format("Overall Bit Rate: {} kbps\n", ctx.bit_rate / 1000);
    }

    std::cout << std::format("Number of Streams: {}\n\n", ctx.nb_streams);

    // Count audio streams
    const std::span streams{ctx.streams, ctx.nb_streams};
    const auto audio_stream_count = std::ranges::count_if(streams,
        [](const auto* stream) {
            return stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO;
        });

    std::cout << std::format("Audio Streams: {}\n\n", audio_stream_count);

    std::cout << "======================================\n";
    std::cout << "Stream Details\n";
    std::cout << "======================================\n\n";
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << std::format("Usage: {} <input_file>\n", argv[0]);
        std::cerr << std::format("Example: {} audio.mp3\n", argv[0]);
        return 1;
    }

    try {
        const std::string_view input_filename{argv[1]};

        // Open input file using RAII wrapper
        auto format_ctx = ffmpeg::open_input_format(input_filename.data());

        // Print file information
        print_format_info(*format_ctx, input_filename);

        // Print information for each audio stream
        const std::span streams{format_ctx->streams, format_ctx->nb_streams};
        for (int i = 0; const auto* stream : streams) {
            if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                print_audio_stream_info(*stream, i);
            }
            ++i;
        }

        // Print metadata
        print_metadata(format_ctx->metadata);

    } catch (const ffmpeg::FFmpegError& e) {
        std::cerr << std::format("FFmpeg error: {}\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }

    return 0;
}
