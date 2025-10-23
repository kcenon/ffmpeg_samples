/**
 * Basic Streaming Server
 *
 * This sample demonstrates how to create a basic HTTP streaming server
 * for video files using modern C++20 and FFmpeg libraries.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <format>
#include <string_view>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>

namespace {

class StreamingServer {
public:
    StreamingServer(std::string_view input_file, std::string_view output_url,
                   std::string_view format)
        : input_file_(input_file)
        , output_url_(output_url)
        , format_(format)
        , format_ctx_(ffmpeg::open_input_format(input_file.data()))
        , packet_(ffmpeg::create_packet()) {

        initialize();
    }

    void start() {
        std::cout << "Streaming Server\n";
        std::cout << "================\n\n";
        std::cout << std::format("Input: {}\n", input_file_);
        std::cout << std::format("Output URL: {}\n", output_url_);
        std::cout << std::format("Format: {}\n", format_);
        std::cout << std::format("Video: {}x{}, {:.2f} fps\n",
                                codec_ctx_->width, codec_ctx_->height,
                                av_q2d(format_ctx_->streams[video_stream_index_]->avg_frame_rate));

        if (audio_stream_index_ >= 0) {
            const auto* audio_codecpar = format_ctx_->streams[audio_stream_index_]->codecpar;
            std::cout << std::format("Audio: {} Hz, {} channels\n",
                                    audio_codecpar->sample_rate,
                                    audio_codecpar->ch_layout.nb_channels);
        }

        std::cout << "\n✓ Server started. Press Ctrl+C to stop.\n\n";

        // Create output context
        AVFormatContext* output_ctx_raw = nullptr;
        ffmpeg::check_error(
            avformat_alloc_output_context2(&output_ctx_raw, nullptr,
                                          format_.c_str(), output_url_.c_str()),
            "allocate output context"
        );
        auto output_ctx = ffmpeg::FormatContextPtr(output_ctx_raw);

        // Copy streams
        std::vector<int> stream_mapping(format_ctx_->nb_streams, -1);
        int stream_index = 0;

        for (unsigned int i = 0; i < format_ctx_->nb_streams; ++i) {
            const auto* in_stream = format_ctx_->streams[i];
            const auto codec_type = in_stream->codecpar->codec_type;

            // Only copy video and audio streams
            if (codec_type != AVMEDIA_TYPE_VIDEO && codec_type != AVMEDIA_TYPE_AUDIO) {
                continue;
            }

            auto* out_stream = avformat_new_stream(output_ctx.get(), nullptr);
            if (!out_stream) {
                throw ffmpeg::FFmpegError("Failed to create output stream");
            }

            ffmpeg::check_error(
                avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar),
                "copy codec parameters"
            );

            out_stream->codecpar->codec_tag = 0;
            out_stream->time_base = in_stream->time_base;

            stream_mapping[i] = stream_index++;
        }

        // Open output
        if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
            ffmpeg::check_error(
                avio_open(&output_ctx->pb, output_url_.c_str(), AVIO_FLAG_WRITE),
                "open output URL"
            );
        }

        // Write header
        AVDictionary* opts = nullptr;
        if (format_ == "flv" || format_ == "rtmp") {
            av_dict_set(&opts, "flvflags", "no_duration_filesize", 0);
        }

        ffmpeg::check_error(
            avformat_write_header(output_ctx.get(), &opts),
            "write header"
        );

        av_dict_free(&opts);

        // Stream packets
        stream_packets(output_ctx.get(), stream_mapping);

        // Write trailer
        av_write_trailer(output_ctx.get());

        std::cout << "\n✓ Streaming stopped\n";
    }

    void loop() {
        loop_ = true;
        start();
    }

private:
    void initialize() {
        // Find video stream
        const auto stream_idx = ffmpeg::find_stream_index(format_ctx_.get(), AVMEDIA_TYPE_VIDEO);
        if (!stream_idx) {
            throw ffmpeg::FFmpegError("No video stream found");
        }
        video_stream_index_ = *stream_idx;

        // Find audio stream (optional)
        const auto audio_idx = ffmpeg::find_stream_index(format_ctx_.get(), AVMEDIA_TYPE_AUDIO);
        if (audio_idx) {
            audio_stream_index_ = *audio_idx;
        }

        // Setup decoder for video
        const auto* codecpar = format_ctx_->streams[video_stream_index_]->codecpar;
        const auto* decoder = avcodec_find_decoder(codecpar->codec_id);
        if (!decoder) {
            throw ffmpeg::FFmpegError("Video decoder not found");
        }

        codec_ctx_ = ffmpeg::create_codec_context(decoder);
        ffmpeg::check_error(
            avcodec_parameters_to_context(codec_ctx_.get(), codecpar),
            "copy decoder parameters"
        );
        ffmpeg::check_error(
            avcodec_open2(codec_ctx_.get(), decoder, nullptr),
            "open decoder"
        );
    }

    void stream_packets(AVFormatContext* output_ctx, const std::vector<int>& stream_mapping) {
        int64_t start_time = av_gettime_relative();
        int64_t first_pts = AV_NOPTS_VALUE;
        int packet_count = 0;

        do {
            // Reset for loop
            if (loop_ && av_read_frame(format_ctx_.get(), packet_.get()) < 0) {
                av_seek_frame(format_ctx_.get(), -1, 0, AVSEEK_FLAG_BACKWARD);
                start_time = av_gettime_relative();
                first_pts = AV_NOPTS_VALUE;
                std::cout << "\n[Loop] Restarting stream...\n";
                continue;
            }

            while (av_read_frame(format_ctx_.get(), packet_.get()) >= 0) {
                ffmpeg::ScopedPacketUnref packet_guard(packet_.get());

                const auto stream_idx = packet_->stream_index;
                if (stream_mapping[stream_idx] < 0) {
                    continue;
                }

                const auto* in_stream = format_ctx_->streams[stream_idx];
                auto* out_stream = output_ctx->streams[stream_mapping[stream_idx]];

                // Record first PTS for timing
                if (first_pts == AV_NOPTS_VALUE && packet_->pts != AV_NOPTS_VALUE) {
                    first_pts = packet_->pts;
                }

                // Calculate timing for real-time streaming
                if (packet_->pts != AV_NOPTS_VALUE) {
                    const auto pts_time = av_rescale_q(packet_->pts - first_pts,
                                                      in_stream->time_base,
                                                      AVRational{1, AV_TIME_BASE});

                    const auto now = av_gettime_relative() - start_time;
                    const auto delay = pts_time - now;

                    if (delay > 0) {
                        // Sleep to maintain real-time playback speed
                        av_usleep(static_cast<unsigned int>(delay));
                    }
                }

                // Rescale timestamps
                av_packet_rescale_ts(packet_.get(), in_stream->time_base, out_stream->time_base);
                packet_->stream_index = stream_mapping[stream_idx];
                packet_->pos = -1;

                // Write packet
                const auto ret = av_interleaved_write_frame(output_ctx, packet_.get());
                if (ret < 0) {
                    std::cerr << std::format("Error writing packet ({})\n",
                                           ffmpeg::get_error_string(ret));
                    break;
                }

                ++packet_count;
                if (packet_count % 100 == 0) {
                    const auto duration = (av_gettime_relative() - start_time) / 1000000.0;
                    std::cout << std::format("Streaming... {:.1f}s, {} packets\r",
                                           duration, packet_count) << std::flush;
                }
            }

        } while (loop_);

        std::cout << std::format("\nTotal packets streamed: {}\n", packet_count);
    }

    std::string input_file_;
    std::string output_url_;
    std::string format_;
    bool loop_ = false;
    int video_stream_index_ = -1;
    int audio_stream_index_ = -1;

    ffmpeg::FormatContextPtr format_ctx_;
    ffmpeg::CodecContextPtr codec_ctx_;
    ffmpeg::PacketPtr packet_;
};

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <input_file> <output_url> [options]\n\n", prog_name);
    std::cout << "Options:\n";
    std::cout << "  --format <fmt>    Output format (default: auto-detect)\n";
    std::cout << "  --loop            Loop the video continuously\n\n";
    std::cout << "Supported Formats:\n";
    std::cout << "  flv               Flash Video (for RTMP)\n";
    std::cout << "  mpegts            MPEG Transport Stream (for UDP/HTTP)\n";
    std::cout << "  hls               HTTP Live Streaming\n";
    std::cout << "  dash              MPEG-DASH\n\n";
    std::cout << "Examples:\n\n";
    std::cout << "  HTTP Streaming:\n";
    std::cout << std::format("    {} video.mp4 http://localhost:8080/stream.flv --format flv\n", prog_name);
    std::cout << "\n  RTMP Streaming (requires RTMP server):\n";
    std::cout << std::format("    {} video.mp4 rtmp://localhost/live/stream --format flv\n", prog_name);
    std::cout << "\n  UDP Streaming:\n";
    std::cout << std::format("    {} video.mp4 udp://239.1.1.1:1234 --format mpegts\n", prog_name);
    std::cout << "\n  File Output (HLS):\n";
    std::cout << std::format("    {} video.mp4 stream.m3u8 --format hls\n", prog_name);
    std::cout << "\n  Loop Streaming:\n";
    std::cout << std::format("    {} video.mp4 http://localhost:8080/stream --loop\n", prog_name);
    std::cout << "\nNote: Some formats require a running server (RTMP, HTTP) or client.\n";
    std::cout << "For testing HTTP streaming, use a media player like VLC or ffplay:\n";
    std::cout << "  ffplay http://localhost:8080/stream.flv\n";
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        const std::string_view input_file{argv[1]};
        const std::string_view output_url{argv[2]};

        // Parse options
        std::string format;
        bool loop = false;

        for (int i = 3; i < argc; ++i) {
            const std::string_view arg{argv[i]};

            if (arg == "--format" && i + 1 < argc) {
                format = argv[++i];
            } else if (arg == "--loop") {
                loop = true;
            }
        }

        // Auto-detect format if not specified
        if (format.empty()) {
            std::string url_str{output_url};
            if (url_str.starts_with("rtmp://")) {
                format = "flv";
            } else if (url_str.starts_with("udp://") || url_str.starts_with("rtp://")) {
                format = "mpegts";
            } else if (url_str.ends_with(".m3u8")) {
                format = "hls";
            } else if (url_str.ends_with(".mpd")) {
                format = "dash";
            } else if (url_str.ends_with(".flv") || url_str.starts_with("http://")) {
                format = "flv";
            }
        }

        StreamingServer server(input_file, output_url, format);

        if (loop) {
            server.loop();
        } else {
            server.start();
        }

    } catch (const ffmpeg::FFmpegError& e) {
        std::cerr << std::format("FFmpeg error: {}\n", e.what());
        std::cerr << "\nTroubleshooting:\n";
        std::cerr << "- For RTMP: Ensure RTMP server is running (e.g., nginx-rtmp)\n";
        std::cerr << "- For HTTP: Ensure HTTP server accepts PUT/POST\n";
        std::cerr << "- For UDP: Check firewall and network settings\n";
        std::cerr << "- For file output: Ensure write permissions\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }

    return 0;
}
