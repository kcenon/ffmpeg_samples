/**
 * Video Transcoder
 *
 * This sample demonstrates how to transcode a video file from one format
 * to another, including changing codec, resolution, and bitrate using
 * modern C++20 and FFmpeg libraries.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <format>
#include <string_view>

namespace {

class VideoTranscoder {
public:
    VideoTranscoder(std::string_view input_file, std::string_view output_file,
                   int width, int height, int bitrate, int fps)
        : output_file_(output_file)
        , output_width_(width)
        , output_height_(height)
        , bitrate_(bitrate)
        , fps_(fps)
        , input_format_ctx_(ffmpeg::open_input_format(input_file.data()))
        , input_packet_(ffmpeg::create_packet())
        , input_frame_(ffmpeg::create_frame())
        , output_frame_(ffmpeg::create_frame()) {

        initialize();
    }

    void transcode() {
        std::cout << "Transcoding in progress...\n";

        int64_t pts_counter = 0;
        int frame_count = 0;

        while (av_read_frame(input_format_ctx_.get(), input_packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(input_packet_.get());

            if (input_packet_->stream_index != video_stream_index_) {
                continue;
            }

            // Decode video frame
            if (const auto ret = avcodec_send_packet(input_video_codec_ctx_.get(), input_packet_.get()); ret < 0) {
                continue;
            }

            while (true) {
                const auto ret = avcodec_receive_frame(input_video_codec_ctx_.get(), input_frame_.get());

                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }

                if (ret < 0) {
                    std::cerr << "Error decoding video frame\n";
                    break;
                }

                ffmpeg::ScopedFrameUnref frame_guard(input_frame_.get());

                // Scale frame
                ffmpeg::check_error(
                    av_frame_make_writable(output_frame_.get()),
                    "make output frame writable"
                );

                sws_scale(sws_ctx_.get(),
                         input_frame_->data, input_frame_->linesize,
                         0, input_video_codec_ctx_->height,
                         output_frame_->data, output_frame_->linesize);

                output_frame_->pts = pts_counter++;

                // Encode frame
                encode_video_frame();
                ++frame_count;

                if (frame_count % 30 == 0) {
                    std::cout << std::format("Processed {} frames\r", frame_count) << std::flush;
                }
            }
        }

        std::cout << std::format("\nTotal frames transcoded: {}\n", frame_count);

        // Flush encoder
        flush_encoder();

        // Write trailer
        ffmpeg::check_error(
            av_write_trailer(output_format_ctx_.get()),
            "write trailer"
        );

        std::cout << std::format("Transcoding completed successfully!\n");
        std::cout << std::format("Output file: {}\n", output_file_);
    }

private:
    void initialize() {
        // Find video stream
        const auto stream_idx = ffmpeg::find_stream_index(input_format_ctx_.get(), AVMEDIA_TYPE_VIDEO);
        if (!stream_idx) {
            throw ffmpeg::FFmpegError("No video stream found");
        }
        video_stream_index_ = *stream_idx;

        // Open video decoder
        const auto* input_codecpar = input_format_ctx_->streams[video_stream_index_]->codecpar;
        const auto* decoder = avcodec_find_decoder(input_codecpar->codec_id);
        if (!decoder) {
            throw ffmpeg::FFmpegError("Decoder not found");
        }

        input_video_codec_ctx_ = ffmpeg::create_codec_context(decoder);
        ffmpeg::check_error(
            avcodec_parameters_to_context(input_video_codec_ctx_.get(), input_codecpar),
            "copy decoder parameters"
        );
        ffmpeg::check_error(
            avcodec_open2(input_video_codec_ctx_.get(), decoder, nullptr),
            "open decoder"
        );

        // Create output format context
        AVFormatContext* raw_ctx = nullptr;
        ffmpeg::check_error(
            avformat_alloc_output_context2(&raw_ctx, nullptr, nullptr, output_file_.data()),
            "allocate output context"
        );
        output_format_ctx_.reset(raw_ctx);

        // Create video encoder
        const auto* encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!encoder) {
            throw ffmpeg::FFmpegError("H264 encoder not found");
        }

        output_stream_ = avformat_new_stream(output_format_ctx_.get(), nullptr);
        if (!output_stream_) {
            throw ffmpeg::FFmpegError("Failed to create output stream");
        }

        output_video_codec_ctx_ = ffmpeg::create_codec_context(encoder);

        output_video_codec_ctx_->width = output_width_;
        output_video_codec_ctx_->height = output_height_;
        output_video_codec_ctx_->time_base = AVRational{1, fps_};
        output_video_codec_ctx_->framerate = AVRational{fps_, 1};
        output_video_codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
        output_video_codec_ctx_->bit_rate = bitrate_;
        output_video_codec_ctx_->gop_size = 10;
        output_video_codec_ctx_->max_b_frames = 1;

        av_opt_set(output_video_codec_ctx_->priv_data, "preset", "medium", 0);

        if (output_format_ctx_->oformat->flags & AVFMT_GLOBALHEADER) {
            output_video_codec_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        ffmpeg::check_error(
            avcodec_open2(output_video_codec_ctx_.get(), encoder, nullptr),
            "open encoder"
        );

        ffmpeg::check_error(
            avcodec_parameters_from_context(output_stream_->codecpar, output_video_codec_ctx_.get()),
            "copy encoder parameters"
        );

        output_stream_->time_base = output_video_codec_ctx_->time_base;

        // Open output file
        if (!(output_format_ctx_->oformat->flags & AVFMT_NOFILE)) {
            ffmpeg::check_error(
                avio_open(&output_format_ctx_->pb, output_file_.data(), AVIO_FLAG_WRITE),
                "open output file"
            );
        }

        // Write header
        ffmpeg::check_error(
            avformat_write_header(output_format_ctx_.get(), nullptr),
            "write header"
        );

        // Initialize scaling context
        sws_ctx_.reset(sws_getContext(
            input_video_codec_ctx_->width, input_video_codec_ctx_->height,
            input_video_codec_ctx_->pix_fmt,
            output_width_, output_height_, AV_PIX_FMT_YUV420P,
            SWS_BICUBIC, nullptr, nullptr, nullptr
        ));

        if (!sws_ctx_) {
            throw ffmpeg::FFmpegError("Failed to initialize scaling context");
        }

        // Allocate output frame buffer
        output_frame_->format = AV_PIX_FMT_YUV420P;
        output_frame_->width = output_width_;
        output_frame_->height = output_height_;

        ffmpeg::check_error(
            av_frame_get_buffer(output_frame_.get(), 0),
            "allocate output frame buffer"
        );
    }

    void encode_video_frame() {
        auto packet = ffmpeg::create_packet();

        const auto ret = avcodec_send_frame(output_video_codec_ctx_.get(), output_frame_.get());
        if (ret < 0) {
            return;
        }

        while (true) {
            const auto recv_ret = avcodec_receive_packet(output_video_codec_ctx_.get(), packet.get());

            if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF) {
                break;
            }

            if (recv_ret < 0) {
                break;
            }

            ffmpeg::ScopedPacketUnref packet_guard(packet.get());

            av_packet_rescale_ts(packet.get(), output_video_codec_ctx_->time_base,
                               output_stream_->time_base);
            packet->stream_index = 0;

            ffmpeg::check_error(
                av_interleaved_write_frame(output_format_ctx_.get(), packet.get()),
                "write encoded packet"
            );
        }
    }

    void flush_encoder() {
        auto packet = ffmpeg::create_packet();
        avcodec_send_frame(output_video_codec_ctx_.get(), nullptr);

        while (true) {
            const auto ret = avcodec_receive_packet(output_video_codec_ctx_.get(), packet.get());

            if (ret < 0) {
                break;
            }

            ffmpeg::ScopedPacketUnref packet_guard(packet.get());

            av_packet_rescale_ts(packet.get(), output_video_codec_ctx_->time_base,
                               output_stream_->time_base);
            packet->stream_index = 0;

            av_interleaved_write_frame(output_format_ctx_.get(), packet.get());
        }
    }

    std::string output_file_;
    int output_width_;
    int output_height_;
    int bitrate_;
    int fps_;
    int video_stream_index_ = -1;

    ffmpeg::FormatContextPtr input_format_ctx_;
    ffmpeg::FormatContextPtr output_format_ctx_;
    ffmpeg::CodecContextPtr input_video_codec_ctx_;
    ffmpeg::CodecContextPtr output_video_codec_ctx_;
    ffmpeg::PacketPtr input_packet_;
    ffmpeg::FramePtr input_frame_;
    ffmpeg::FramePtr output_frame_;
    ffmpeg::SwsContextPtr sws_ctx_;
    AVStream* output_stream_ = nullptr;
};

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << std::format("Usage: {} <input_file> <output_file> [width] [height] [bitrate] [fps]\n", argv[0]);
        std::cerr << std::format("Example: {} input.mp4 output.mp4 1280 720 2000000 30\n", argv[0]);
        return 1;
    }

    try {
        const std::string_view input_filename{argv[1]};
        const std::string_view output_filename{argv[2]};
        const int width = argc > 3 ? std::atoi(argv[3]) : 1280;
        const int height = argc > 4 ? std::atoi(argv[4]) : 720;
        const int bitrate = argc > 5 ? std::atoi(argv[5]) : 2000000;
        const int fps = argc > 6 ? std::atoi(argv[6]) : 30;

        std::cout << "FFmpeg Video Transcoder\n";
        std::cout << "=======================\n";
        std::cout << std::format("Input: {}\n", input_filename);
        std::cout << std::format("Output: {}\n", output_filename);
        std::cout << std::format("Resolution: {}x{}\n", width, height);
        std::cout << std::format("Bitrate: {} kbps\n", bitrate / 1000);
        std::cout << std::format("Frame rate: {} fps\n\n", fps);

        VideoTranscoder transcoder(input_filename, output_filename, width, height, bitrate, fps);
        transcoder.transcode();

    } catch (const ffmpeg::FFmpegError& e) {
        std::cerr << std::format("FFmpeg error: {}\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }

    return 0;
}
