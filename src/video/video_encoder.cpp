/**
 * Video Encoder
 *
 * This sample demonstrates how to encode a series of generated frames
 * into a video file using modern C++20 and FFmpeg libraries.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <format>
#include <filesystem>
#include <cmath>

namespace fs = std::filesystem;

namespace {

void generate_test_frame(AVFrame& frame, int frame_number) {
    // Generate a simple animated pattern
    for (int y = 0; y < frame.height; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            // Y plane (brightness)
            const auto offset = (frame_number * 5 + x + y) % 256;
            frame.data[0][y * frame.linesize[0] + x] = static_cast<uint8_t>(offset);
        }
    }

    // U and V planes (color)
    for (int y = 0; y < frame.height / 2; ++y) {
        for (int x = 0; x < frame.width / 2; ++x) {
            frame.data[1][y * frame.linesize[1] + x] = static_cast<uint8_t>(128 + y + frame_number);
            frame.data[2][y * frame.linesize[2] + x] = static_cast<uint8_t>(64 + x + frame_number);
        }
    }
}

class VideoEncoder {
public:
    VideoEncoder(std::string_view output_file, int num_frames, int width, int height, int fps)
        : output_file_(output_file)
        , num_frames_(num_frames)
        , width_(width)
        , height_(height)
        , fps_(fps)
        , packet_(ffmpeg::create_packet())
        , frame_(ffmpeg::create_frame()) {

        initialize();
    }

    void encode() {
        std::cout << std::format("Encoding video to {}\n", output_file_);
        std::cout << std::format("Resolution: {}x{}\n", width_, height_);
        std::cout << std::format("Frame rate: {} fps\n", fps_);
        std::cout << std::format("Number of frames: {}\n\n", num_frames_);

        // Configure frame
        frame_->format = codec_ctx_->pix_fmt;
        frame_->width = codec_ctx_->width;
        frame_->height = codec_ctx_->height;

        ffmpeg::check_error(
            av_frame_get_buffer(frame_.get(), 0),
            "allocate frame buffer"
        );

        // Encode all frames
        for (int i = 0; i < num_frames_; ++i) {
            encode_frame(i);

            if ((i + 1) % 10 == 0) {
                std::cout << std::format("Encoded frame {}/{}\n", i + 1, num_frames_);
            }
        }

        // Flush encoder
        flush_encoder();

        // Write trailer
        ffmpeg::check_error(
            av_write_trailer(format_ctx_.get()),
            "write trailer"
        );

        std::cout << std::format("\nEncoding completed successfully!\n");
        std::cout << std::format("Output file: {}\n", output_file_);
    }

private:
    void initialize() {
        // Allocate output format context
        AVFormatContext* raw_ctx = nullptr;
        ffmpeg::check_error(
            avformat_alloc_output_context2(&raw_ctx, nullptr, nullptr, output_file_.data()),
            "allocate output context"
        );
        format_ctx_.reset(raw_ctx);

        // Find H264 encoder
        const auto* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!codec) {
            throw ffmpeg::FFmpegError("H264 codec not found");
        }

        // Create stream
        stream_ = avformat_new_stream(format_ctx_.get(), nullptr);
        if (!stream_) {
            throw ffmpeg::FFmpegError("Failed to create stream");
        }

        // Create and configure codec context
        codec_ctx_ = ffmpeg::create_codec_context(codec);

        codec_ctx_->codec_id = AV_CODEC_ID_H264;
        codec_ctx_->codec_type = AVMEDIA_TYPE_VIDEO;
        codec_ctx_->width = width_;
        codec_ctx_->height = height_;
        codec_ctx_->time_base = AVRational{1, fps_};
        codec_ctx_->framerate = AVRational{fps_, 1};
        codec_ctx_->gop_size = 10;
        codec_ctx_->max_b_frames = 1;
        codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
        codec_ctx_->bit_rate = 2000000;

        // Set H264 preset for better encoding
        av_opt_set(codec_ctx_->priv_data, "preset", "medium", 0);

        // Some formats require global headers
        if (format_ctx_->oformat->flags & AVFMT_GLOBALHEADER) {
            codec_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        // Open codec
        ffmpeg::check_error(
            avcodec_open2(codec_ctx_.get(), codec, nullptr),
            "open codec"
        );

        // Copy codec parameters to stream
        ffmpeg::check_error(
            avcodec_parameters_from_context(stream_->codecpar, codec_ctx_.get()),
            "copy codec parameters"
        );

        stream_->time_base = codec_ctx_->time_base;

        // Open output file
        if (!(format_ctx_->oformat->flags & AVFMT_NOFILE)) {
            ffmpeg::check_error(
                avio_open(&format_ctx_->pb, output_file_.data(), AVIO_FLAG_WRITE),
                "open output file"
            );
        }

        // Write file header
        ffmpeg::check_error(
            avformat_write_header(format_ctx_.get(), nullptr),
            "write header"
        );
    }

    void encode_frame(int frame_number) {
        ffmpeg::check_error(
            av_frame_make_writable(frame_.get()),
            "make frame writable"
        );

        // Generate test frame
        generate_test_frame(*frame_, frame_number);
        frame_->pts = frame_number;

        // Send frame to encoder
        ffmpeg::check_error(
            avcodec_send_frame(codec_ctx_.get(), frame_.get()),
            "send frame"
        );

        // Receive and write encoded packets
        receive_packets();
    }

    void flush_encoder() {
        avcodec_send_frame(codec_ctx_.get(), nullptr);
        receive_packets();
    }

    void receive_packets() {
        while (true) {
            const auto ret = avcodec_receive_packet(codec_ctx_.get(), packet_.get());

            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }

            if (ret < 0) {
                throw ffmpeg::FFmpegError(ret);
            }

            ffmpeg::ScopedPacketUnref packet_guard(packet_.get());

            // Rescale packet timestamps
            av_packet_rescale_ts(packet_.get(), codec_ctx_->time_base, stream_->time_base);
            packet_->stream_index = stream_->index;

            // Write packet to output file
            ffmpeg::check_error(
                av_interleaved_write_frame(format_ctx_.get(), packet_.get()),
                "write frame"
            );
        }
    }

    std::string output_file_;
    int num_frames_;
    int width_;
    int height_;
    int fps_;

    ffmpeg::FormatContextPtr format_ctx_;
    ffmpeg::CodecContextPtr codec_ctx_;
    ffmpeg::PacketPtr packet_;
    ffmpeg::FramePtr frame_;
    AVStream* stream_ = nullptr;
};

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << std::format("Usage: {} <output_file> [num_frames] [width] [height] [fps]\n", argv[0]);
        std::cerr << std::format("Example: {} output.mp4 100 1280 720 30\n", argv[0]);
        return 1;
    }

    try {
        const std::string_view output_filename{argv[1]};
        const int num_frames = argc > 2 ? std::atoi(argv[2]) : 100;
        const int width = argc > 3 ? std::atoi(argv[3]) : 1280;
        const int height = argc > 4 ? std::atoi(argv[4]) : 720;
        const int fps = argc > 5 ? std::atoi(argv[5]) : 30;

        VideoEncoder encoder(output_filename, num_frames, width, height, fps);
        encoder.encode();

    } catch (const ffmpeg::FFmpegError& e) {
        std::cerr << std::format("FFmpeg error: {}\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }

    return 0;
}
