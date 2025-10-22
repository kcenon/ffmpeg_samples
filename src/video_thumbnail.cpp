/**
 * Video Thumbnail Generator
 *
 * This sample demonstrates how to extract frames from a video file
 * and save them as thumbnail images (JPEG/PNG) using modern C++20 and FFmpeg.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <format>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;

namespace {

enum class ImageFormat {
    JPEG,
    PNG
};

ImageFormat parse_format(std::string_view filename) {
    if (filename.ends_with(".png")) {
        return ImageFormat::PNG;
    }
    return ImageFormat::JPEG;
}

AVCodecID get_codec_id(ImageFormat format) {
    switch (format) {
        case ImageFormat::PNG:
            return AV_CODEC_ID_PNG;
        case ImageFormat::JPEG:
        default:
            return AV_CODEC_ID_MJPEG;
    }
}

class VideoThumbnailGenerator {
public:
    VideoThumbnailGenerator(std::string_view input_file)
        : format_ctx_(ffmpeg::open_input_format(input_file.data()))
        , packet_(ffmpeg::create_packet())
        , frame_(ffmpeg::create_frame())
        , rgb_frame_(ffmpeg::create_frame()) {

        initialize();
    }

    void generate_at_time(double timestamp_seconds, const fs::path& output_file, int quality = 85) {
        const auto format = parse_format(output_file.string());

        std::cout << std::format("Generating thumbnail at {:.2f} seconds\n", timestamp_seconds);
        std::cout << std::format("Output: {}\n", output_file.string());
        std::cout << std::format("Quality: {}\n", quality);
        std::cout << std::format("Resolution: {}x{}\n\n", codec_ctx_->width, codec_ctx_->height);

        // Seek to the desired timestamp
        const auto timestamp = static_cast<int64_t>(timestamp_seconds * AV_TIME_BASE);

        ffmpeg::check_error(
            av_seek_frame(format_ctx_.get(), -1, timestamp, AVSEEK_FLAG_BACKWARD),
            "seek to timestamp"
        );

        // Flush codec buffers after seek
        avcodec_flush_buffers(codec_ctx_.get());

        // Find and decode the frame
        if (auto decoded_frame = find_frame_at_timestamp(timestamp_seconds)) {
            save_frame_as_image(*decoded_frame, output_file, format, quality);
            std::cout << std::format("✓ Thumbnail saved successfully\n");
        } else {
            throw std::runtime_error("Failed to find frame at specified timestamp");
        }
    }

    void generate_grid(int count, const fs::path& output_dir, int quality = 85) {
        std::cout << std::format("Generating {} thumbnails\n", count);
        std::cout << std::format("Output directory: {}\n", output_dir.string());
        std::cout << std::format("Video duration: {:.2f} seconds\n\n", get_duration());

        fs::create_directories(output_dir);

        const auto duration = get_duration();
        const auto interval = duration / (count + 1);

        for (int i = 1; i <= count; ++i) {
            const auto timestamp = interval * i;
            const auto output_file = output_dir / std::format("thumbnail_{:03d}.jpg", i);

            try {
                generate_at_time(timestamp, output_file, quality);
            } catch (const std::exception& e) {
                std::cerr << std::format("Failed to generate thumbnail {}: {}\n", i, e.what());
            }
        }

        std::cout << std::format("\n✓ Generated {} thumbnails in {}\n", count, output_dir.string());
    }

    void generate_best_frame(const fs::path& output_file, int quality = 85) {
        std::cout << "Analyzing video to find best frame...\n";

        const auto duration = get_duration();
        constexpr int sample_count = 10;
        const auto interval = duration / (sample_count + 1);

        struct FrameScore {
            double timestamp;
            double score;
            ffmpeg::FramePtr frame;
        };

        std::vector<FrameScore> candidates;

        // Sample frames throughout the video
        for (int i = 1; i <= sample_count; ++i) {
            const auto timestamp = interval * i;

            // Seek to timestamp
            const auto ts = static_cast<int64_t>(timestamp * AV_TIME_BASE);
            av_seek_frame(format_ctx_.get(), -1, ts, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(codec_ctx_.get());

            if (auto decoded_frame = find_frame_at_timestamp(timestamp)) {
                const auto score = calculate_frame_quality(*decoded_frame);

                auto frame_copy = ffmpeg::create_frame();
                av_frame_ref(frame_copy.get(), decoded_frame.get());

                candidates.push_back({timestamp, score, std::move(frame_copy)});

                std::cout << std::format("  Frame at {:.2f}s - Score: {:.2f}\r", timestamp, score) << std::flush;
            }
        }

        if (candidates.empty()) {
            throw std::runtime_error("No valid frames found");
        }

        // Find frame with highest score
        auto best = std::ranges::max_element(candidates, {}, &FrameScore::score);

        std::cout << std::format("\n\n✓ Best frame found at {:.2f}s (score: {:.2f})\n",
                                best->timestamp, best->score);

        const auto format = parse_format(output_file.string());
        save_frame_as_image(*best->frame, output_file, format, quality);

        std::cout << std::format("✓ Thumbnail saved to {}\n", output_file.string());
    }

private:
    void initialize() {
        // Find video stream
        const auto stream_idx = ffmpeg::find_stream_index(format_ctx_.get(), AVMEDIA_TYPE_VIDEO);
        if (!stream_idx) {
            throw ffmpeg::FFmpegError("No video stream found");
        }
        video_stream_index_ = *stream_idx;

        // Get codec and open decoder
        const auto* codecpar = format_ctx_->streams[video_stream_index_]->codecpar;
        const auto* decoder = avcodec_find_decoder(codecpar->codec_id);
        if (!decoder) {
            throw ffmpeg::FFmpegError("Decoder not found");
        }

        codec_ctx_ = ffmpeg::create_codec_context(decoder);
        ffmpeg::check_error(
            avcodec_parameters_to_context(codec_ctx_.get(), codecpar),
            "copy codec parameters"
        );
        ffmpeg::check_error(
            avcodec_open2(codec_ctx_.get(), decoder, nullptr),
            "open decoder"
        );

        // Initialize scaler for RGB conversion
        sws_ctx_.reset(sws_getContext(
            codec_ctx_->width, codec_ctx_->height, codec_ctx_->pix_fmt,
            codec_ctx_->width, codec_ctx_->height, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        ));

        if (!sws_ctx_) {
            throw ffmpeg::FFmpegError("Failed to initialize scaler");
        }

        // Allocate RGB frame
        rgb_frame_->format = AV_PIX_FMT_RGB24;
        rgb_frame_->width = codec_ctx_->width;
        rgb_frame_->height = codec_ctx_->height;

        ffmpeg::check_error(
            av_frame_get_buffer(rgb_frame_.get(), 0),
            "allocate RGB frame buffer"
        );
    }

    ffmpeg::FramePtr find_frame_at_timestamp(double timestamp_seconds) {
        const auto target_pts = static_cast<int64_t>(
            timestamp_seconds / av_q2d(format_ctx_->streams[video_stream_index_]->time_base)
        );

        while (av_read_frame(format_ctx_.get(), packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(packet_.get());

            if (packet_->stream_index != video_stream_index_) {
                continue;
            }

            const auto ret = avcodec_send_packet(codec_ctx_.get(), packet_.get());
            if (ret < 0) {
                continue;
            }

            while (true) {
                auto decoded_frame = ffmpeg::create_frame();
                const auto recv_ret = avcodec_receive_frame(codec_ctx_.get(), decoded_frame.get());

                if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF) {
                    break;
                }

                if (recv_ret < 0) {
                    break;
                }

                // Check if we've reached or passed the target timestamp
                if (decoded_frame->pts >= target_pts) {
                    return decoded_frame;
                }
            }
        }

        return nullptr;
    }

    void save_frame_as_image(AVFrame& frame, const fs::path& output_file,
                            ImageFormat format, int quality) {
        // Convert to RGB
        sws_scale(sws_ctx_.get(),
                 frame.data, frame.linesize, 0, codec_ctx_->height,
                 rgb_frame_->data, rgb_frame_->linesize);

        // Find encoder
        const auto codec_id = get_codec_id(format);
        const auto* encoder = avcodec_find_encoder(codec_id);
        if (!encoder) {
            throw ffmpeg::FFmpegError("Image encoder not found");
        }

        // Create encoder context
        auto encoder_ctx = ffmpeg::create_codec_context(encoder);
        encoder_ctx->width = codec_ctx_->width;
        encoder_ctx->height = codec_ctx_->height;
        encoder_ctx->pix_fmt = AV_PIX_FMT_YUVJ420P;
        encoder_ctx->time_base = AVRational{1, 1};

        // Set quality (for JPEG)
        if (format == ImageFormat::JPEG) {
            encoder_ctx->qmin = encoder_ctx->qmax = quality;
        }

        ffmpeg::check_error(
            avcodec_open2(encoder_ctx.get(), encoder, nullptr),
            "open image encoder"
        );

        // Convert RGB to encoder format
        auto yuv_frame = ffmpeg::create_frame();
        yuv_frame->format = encoder_ctx->pix_fmt;
        yuv_frame->width = encoder_ctx->width;
        yuv_frame->height = encoder_ctx->height;

        ffmpeg::check_error(
            av_frame_get_buffer(yuv_frame.get(), 0),
            "allocate YUV frame buffer"
        );

        auto rgb_to_yuv_ctx = ffmpeg::SwsContextPtr(sws_getContext(
            codec_ctx_->width, codec_ctx_->height, AV_PIX_FMT_RGB24,
            codec_ctx_->width, codec_ctx_->height, AV_PIX_FMT_YUVJ420P,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        ));

        sws_scale(rgb_to_yuv_ctx.get(),
                 rgb_frame_->data, rgb_frame_->linesize, 0, codec_ctx_->height,
                 yuv_frame->data, yuv_frame->linesize);

        yuv_frame->pts = 0;

        // Encode frame
        ffmpeg::check_error(
            avcodec_send_frame(encoder_ctx.get(), yuv_frame.get()),
            "send frame to encoder"
        );

        auto encoded_packet = ffmpeg::create_packet();
        ffmpeg::check_error(
            avcodec_receive_packet(encoder_ctx.get(), encoded_packet.get()),
            "receive encoded packet"
        );

        // Write to file
        std::ofstream output(output_file, std::ios::binary);
        if (!output) {
            throw std::runtime_error(std::format("Failed to open output file: {}", output_file.string()));
        }

        output.write(reinterpret_cast<char*>(encoded_packet->data), encoded_packet->size);
    }

    double calculate_frame_quality(const AVFrame& frame) const {
        // Simple quality metric based on brightness variance and edge detection
        // Higher variance = more detail = better thumbnail

        double sum = 0.0;
        double sum_sq = 0.0;
        int count = 0;

        // Sample every 4th pixel to speed up calculation
        for (int y = 0; y < frame.height; y += 4) {
            const auto* row = frame.data[0] + y * frame.linesize[0];
            for (int x = 0; x < frame.width; x += 4) {
                const auto pixel = row[x];
                sum += pixel;
                sum_sq += pixel * pixel;
                ++count;
            }
        }

        const auto mean = sum / count;
        const auto variance = (sum_sq / count) - (mean * mean);

        // Penalize too dark or too bright frames
        const auto brightness_penalty = std::abs(mean - 128.0) / 128.0;

        return variance * (1.0 - brightness_penalty * 0.5);
    }

    double get_duration() const {
        const auto duration_ts = format_ctx_->duration;
        return duration_ts / static_cast<double>(AV_TIME_BASE);
    }

    ffmpeg::FormatContextPtr format_ctx_;
    ffmpeg::CodecContextPtr codec_ctx_;
    ffmpeg::PacketPtr packet_;
    ffmpeg::FramePtr frame_;
    ffmpeg::FramePtr rgb_frame_;
    ffmpeg::SwsContextPtr sws_ctx_;
    int video_stream_index_ = -1;
};

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <input_video> <mode> [options]\n\n", prog_name);
    std::cout << "Modes:\n";
    std::cout << "  time <seconds> <output_file> [quality]    - Extract frame at specific time\n";
    std::cout << "  grid <count> <output_dir> [quality]       - Generate multiple thumbnails\n";
    std::cout << "  best <output_file> [quality]              - Find and save best frame\n\n";
    std::cout << "Examples:\n";
    std::cout << std::format("  {} video.mp4 time 30.5 thumb.jpg 90\n", prog_name);
    std::cout << std::format("  {} video.mp4 grid 10 thumbnails 85\n", prog_name);
    std::cout << std::format("  {} video.mp4 best thumbnail.jpg 95\n", prog_name);
    std::cout << "\nQuality: 1-100 (default: 85, higher = better)\n";
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        const std::string_view input_file{argv[1]};
        const std::string_view mode{argv[2]};

        VideoThumbnailGenerator generator(input_file);

        if (mode == "time") {
            if (argc < 5) {
                std::cerr << "Error: time mode requires <seconds> <output_file>\n";
                return 1;
            }
            const auto timestamp = std::atof(argv[3]);
            const fs::path output_file{argv[4]};
            const int quality = argc > 5 ? std::atoi(argv[5]) : 85;

            generator.generate_at_time(timestamp, output_file, quality);

        } else if (mode == "grid") {
            if (argc < 5) {
                std::cerr << "Error: grid mode requires <count> <output_dir>\n";
                return 1;
            }
            const int count = std::atoi(argv[3]);
            const fs::path output_dir{argv[4]};
            const int quality = argc > 5 ? std::atoi(argv[5]) : 85;

            generator.generate_grid(count, output_dir, quality);

        } else if (mode == "best") {
            if (argc < 4) {
                std::cerr << "Error: best mode requires <output_file>\n";
                return 1;
            }
            const fs::path output_file{argv[3]};
            const int quality = argc > 4 ? std::atoi(argv[4]) : 85;

            generator.generate_best_frame(output_file, quality);

        } else {
            std::cerr << std::format("Error: Unknown mode '{}'\n", mode);
            print_usage(argv[0]);
            return 1;
        }

    } catch (const ffmpeg::FFmpegError& e) {
        std::cerr << std::format("FFmpeg error: {}\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }

    return 0;
}
