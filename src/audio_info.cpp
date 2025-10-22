/**
 * Audio Information Reader
 *
 * This sample demonstrates how to read and display audio file information
 * using FFmpeg libraries.
 */

#include <iostream>
#include <iomanip>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

void print_audio_stream_info(AVStream* stream, int index) {
    AVCodecParameters* codecpar = stream->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);

    std::cout << "Audio Stream #" << index << ":\n";
    std::cout << "  Codec: " << (codec ? codec->long_name : "unknown") << " ("
              << (codec ? codec->name : "unknown") << ")\n";
    std::cout << "  Sample Rate: " << codecpar->sample_rate << " Hz\n";
    std::cout << "  Channels: " << codecpar->ch_layout.nb_channels << "\n";

    // Channel layout
    char ch_layout_str[64];
    av_channel_layout_describe(&codecpar->ch_layout, ch_layout_str, sizeof(ch_layout_str));
    std::cout << "  Channel Layout: " << ch_layout_str << "\n";

    // Sample format
    const char* sample_fmt_name = av_get_sample_fmt_name(static_cast<AVSampleFormat>(codecpar->format));
    std::cout << "  Sample Format: " << (sample_fmt_name ? sample_fmt_name : "unknown") << "\n";

    // Bit rate
    if (codecpar->bit_rate > 0) {
        std::cout << "  Bit Rate: " << codecpar->bit_rate / 1000 << " kbps\n";
    }

    // Frame size
    if (codecpar->frame_size > 0) {
        std::cout << "  Frame Size: " << codecpar->frame_size << " samples\n";
    }

    // Duration
    if (stream->duration != AV_NOPTS_VALUE) {
        double duration = stream->duration * av_q2d(stream->time_base);
        int minutes = static_cast<int>(duration / 60);
        int seconds = static_cast<int>(duration) % 60;
        int milliseconds = static_cast<int>((duration - static_cast<int>(duration)) * 1000);

        std::cout << "  Duration: " << std::setfill('0')
                  << std::setw(2) << minutes << ":"
                  << std::setw(2) << seconds << "."
                  << std::setw(3) << milliseconds << "\n";
    }

    std::cout << "\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input_file>\n";
        std::cerr << "Example: " << argv[0] << " audio.mp3\n";
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
    std::cout << "======================================\n";
    std::cout << "Audio File Information\n";
    std::cout << "======================================\n\n";

    std::cout << "File: " << input_filename << "\n";
    std::cout << "Format: " << format_ctx->iformat->long_name << "\n";

    // Duration
    if (format_ctx->duration != AV_NOPTS_VALUE) {
        double duration = format_ctx->duration / static_cast<double>(AV_TIME_BASE);
        int hours = static_cast<int>(duration / 3600);
        int minutes = static_cast<int>((duration - hours * 3600) / 60);
        int seconds = static_cast<int>(duration - hours * 3600 - minutes * 60);

        std::cout << "Duration: " << std::setfill('0') << std::setw(2) << hours << ":"
                  << std::setw(2) << minutes << ":" << std::setw(2) << seconds << "\n";
    }

    // Overall bitrate
    if (format_ctx->bit_rate > 0) {
        std::cout << "Overall Bit Rate: " << format_ctx->bit_rate / 1000 << " kbps\n";
    }

    std::cout << "Number of Streams: " << format_ctx->nb_streams << "\n\n";

    // Count audio streams
    int audio_stream_count = 0;
    for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_count++;
        }
    }

    std::cout << "Audio Streams: " << audio_stream_count << "\n\n";

    // Print information for each audio stream
    std::cout << "======================================\n";
    std::cout << "Stream Details\n";
    std::cout << "======================================\n\n";

    for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            print_audio_stream_info(format_ctx->streams[i], i);
        }
    }

    // Print metadata if available
    AVDictionaryEntry* tag = nullptr;
    bool has_metadata = false;

    std::cout << "======================================\n";
    std::cout << "Metadata\n";
    std::cout << "======================================\n";

    while ((tag = av_dict_get(format_ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
        std::cout << tag->key << ": " << tag->value << "\n";
        has_metadata = true;
    }

    if (!has_metadata) {
        std::cout << "No metadata available\n";
    }

    // Cleanup
    avformat_close_input(&format_ctx);

    return 0;
}
