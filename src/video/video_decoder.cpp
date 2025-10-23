/**
 * Video Decoder
 *
 * This sample demonstrates how to decode video frames from a file
 * and save them as PPM images using modern C++20 and FFmpeg libraries.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <format>
#include <ranges>

namespace fs = std::filesystem;

namespace {

void save_frame_as_ppm(const AVFrame& frame, int width, int height,
                       int frame_number, const fs::path& output_dir) {
    const auto filename = output_dir / std::format("frame_{}.ppm", frame_number);

    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        throw std::runtime_error(std::format("Failed to open output file: {}", filename.string()));
    }

    // Write PPM header
    file << std::format("P6\n{} {}\n255\n", width, height);

    // Write pixel data
    for (int y = 0; y < height; ++y) {
        file.write(reinterpret_cast<const char*>(frame.data[0] + y * frame.linesize[0]),
                   width * 3);
    }

    std::cout << std::format("Saved frame {} to {}\n", frame_number, filename.string());
}

class VideoDecoder {
public:
    VideoDecoder(std::string_view input_file, const fs::path& output_dir, int max_frames)
        : output_dir_(output_dir)
        , max_frames_(max_frames)
        , format_ctx_(ffmpeg::open_input_format(input_file.data()))
        , packet_(ffmpeg::create_packet())
        , frame_(ffmpeg::create_frame())
        , frame_rgb_(ffmpeg::create_frame()) {

        initialize();
    }

    void decode_and_save() {
        std::cout << std::format("Decoding video from {}\n", format_ctx_->url);
        std::cout << std::format("Resolution: {}x{}\n", codec_ctx_->width, codec_ctx_->height);
        std::cout << std::format("Maximum frames to decode: {}\n\n", max_frames_);

        int frame_count = 0;

        while (av_read_frame(format_ctx_.get(), packet_.get()) >= 0 && frame_count < max_frames_) {
            ffmpeg::ScopedPacketUnref packet_guard(packet_.get());

            if (packet_->stream_index != video_stream_index_) {
                continue;
            }

            if (const auto ret = avcodec_send_packet(codec_ctx_.get(), packet_.get()); ret < 0) {
                std::cerr << "Error sending packet to decoder\n";
                break;
            }

            while (true) {
                const auto ret = avcodec_receive_frame(codec_ctx_.get(), frame_.get());

                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }

                if (ret < 0) {
                    std::cerr << "Error during decoding\n";
                    return;
                }

                ffmpeg::ScopedFrameUnref frame_guard(frame_.get());

                // Convert frame to RGB
                sws_scale(sws_ctx_.get(),
                         frame_->data, frame_->linesize, 0, codec_ctx_->height,
                         frame_rgb_->data, frame_rgb_->linesize);

                // Save frame
                save_frame_as_ppm(*frame_rgb_, codec_ctx_->width, codec_ctx_->height,
                                 frame_count, output_dir_);

                if (++frame_count >= max_frames_) {
                    break;
                }
            }
        }

        std::cout << std::format("\nTotal frames decoded: {}\n", frame_count);
    }

private:
    void initialize() {
        // Find video stream
        const auto stream_idx = ffmpeg::find_stream_index(format_ctx_.get(), AVMEDIA_TYPE_VIDEO);
        if (!stream_idx) {
            throw ffmpeg::FFmpegError("No video stream found");
        }
        video_stream_index_ = *stream_idx;

        // Get codec
        const auto* codecpar = format_ctx_->streams[video_stream_index_]->codecpar;
        const auto* codec = avcodec_find_decoder(codecpar->codec_id);
        if (!codec) {
            throw ffmpeg::FFmpegError("Codec not found");
        }

        // Create and configure codec context
        codec_ctx_ = ffmpeg::create_codec_context(codec);
        ffmpeg::check_error(
            avcodec_parameters_to_context(codec_ctx_.get(), codecpar),
            "copy codec parameters"
        );
        ffmpeg::check_error(
            avcodec_open2(codec_ctx_.get(), codec, nullptr),
            "open codec"
        );

        // Allocate RGB frame buffer
        frame_rgb_->format = AV_PIX_FMT_RGB24;
        frame_rgb_->width = codec_ctx_->width;
        frame_rgb_->height = codec_ctx_->height;

        ffmpeg::check_error(
            av_frame_get_buffer(frame_rgb_.get(), 0),
            "allocate RGB frame buffer"
        );

        // Initialize scaler
        sws_ctx_.reset(sws_getContext(
            codec_ctx_->width, codec_ctx_->height, codec_ctx_->pix_fmt,
            codec_ctx_->width, codec_ctx_->height, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        ));

        if (!sws_ctx_) {
            throw ffmpeg::FFmpegError("Failed to initialize SWS context");
        }
    }

    fs::path output_dir_;
    int max_frames_;
    int video_stream_index_ = -1;

    ffmpeg::FormatContextPtr format_ctx_;
    ffmpeg::CodecContextPtr codec_ctx_;
    ffmpeg::PacketPtr packet_;
    ffmpeg::FramePtr frame_;
    ffmpeg::FramePtr frame_rgb_;
    ffmpeg::SwsContextPtr sws_ctx_;
};

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << std::format("Usage: {} <input_file> <output_dir> [max_frames]\n", argv[0]);
        return 1;
    }

    try {
        const std::string_view input_filename{argv[1]};
        const fs::path output_dir{argv[2]};
        const int max_frames = argc > 3 ? std::atoi(argv[3]) : 10;

        // Create output directory if it doesn't exist
        fs::create_directories(output_dir);

        VideoDecoder decoder(input_filename, output_dir, max_frames);
        decoder.decode_and_save();

    } catch (const ffmpeg::FFmpegError& e) {
        std::cerr << std::format("FFmpeg error: {}\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }

    return 0;
}
