/**
 * Video Watermark Processor
 *
 * This sample demonstrates how to add image or text watermarks to video files
 * using modern C++20 and FFmpeg libraries.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <format>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace fs = std::filesystem;

namespace {

enum class WatermarkPosition {
    TOP_LEFT,
    TOP_RIGHT,
    BOTTOM_LEFT,
    BOTTOM_RIGHT,
    CENTER
};

WatermarkPosition parse_position(std::string_view pos_str) {
    static const std::unordered_map<std::string_view, WatermarkPosition> positions = {
        {"top_left", WatermarkPosition::TOP_LEFT},
        {"top_right", WatermarkPosition::TOP_RIGHT},
        {"bottom_left", WatermarkPosition::BOTTOM_LEFT},
        {"bottom_right", WatermarkPosition::BOTTOM_RIGHT},
        {"center", WatermarkPosition::CENTER}
    };

    const auto it = positions.find(pos_str);
    if (it == positions.end()) {
        throw std::invalid_argument(std::format("Invalid position: {}", pos_str));
    }
    return it->second;
}

std::string get_overlay_position(WatermarkPosition position, int margin = 10) {
    switch (position) {
        case WatermarkPosition::TOP_LEFT:
            return std::format("x={}:y={}", margin, margin);
        case WatermarkPosition::TOP_RIGHT:
            return std::format("x=W-w-{}:y={}", margin, margin);
        case WatermarkPosition::BOTTOM_LEFT:
            return std::format("x={}:y=H-h-{}", margin, margin);
        case WatermarkPosition::BOTTOM_RIGHT:
            return std::format("x=W-w-{}:y=H-h-{}", margin, margin);
        case WatermarkPosition::CENTER:
            return "x=(W-w)/2:y=(H-h)/2";
        default:
            return std::format("x={}:y={}", margin, margin);
    }
}

class VideoWatermarker {
public:
    VideoWatermarker(std::string_view input_video, const fs::path& output_video)
        : input_video_(input_video)
        , output_video_(output_video)
        , format_ctx_(ffmpeg::open_input_format(input_video.data()))
        , packet_(ffmpeg::create_packet())
        , frame_(ffmpeg::create_frame())
        , filtered_frame_(ffmpeg::create_frame()) {

        initialize();
    }

    void add_image_watermark(std::string_view watermark_image,
                           WatermarkPosition position,
                           float opacity = 1.0f) {
        std::cout << "Adding Image Watermark\n";
        std::cout << "======================\n\n";
        std::cout << std::format("Input video: {}\n", input_video_);
        std::cout << std::format("Watermark image: {}\n", watermark_image);
        std::cout << std::format("Output: {}\n", output_video_.string());
        std::cout << std::format("Opacity: {:.2f}\n\n", opacity);

        // Create filter for image overlay
        const auto overlay_pos = get_overlay_position(position);
        const auto filter_desc = std::format(
            "movie={}[wm];[in][wm]overlay={}:format=auto:alpha={}[out]",
            watermark_image, overlay_pos, opacity
        );

        initialize_filter(filter_desc);
        process_video();
    }

    void add_text_watermark(std::string_view text,
                          WatermarkPosition position,
                          int font_size = 24,
                          std::string_view font_color = "white",
                          float opacity = 0.7f) {
        std::cout << "Adding Text Watermark\n";
        std::cout << "=====================\n\n";
        std::cout << std::format("Input video: {}\n", input_video_);
        std::cout << std::format("Text: {}\n", text);
        std::cout << std::format("Output: {}\n", output_video_.string());
        std::cout << std::format("Font size: {}\n", font_size);
        std::cout << std::format("Color: {}\n", font_color);
        std::cout << std::format("Opacity: {:.2f}\n\n", opacity);

        // Calculate position coordinates
        std::string x_pos, y_pos;
        constexpr int margin = 10;

        switch (position) {
            case WatermarkPosition::TOP_LEFT:
                x_pos = std::format("{}", margin);
                y_pos = std::format("{}", margin);
                break;
            case WatermarkPosition::TOP_RIGHT:
                x_pos = std::format("w-text_w-{}", margin);
                y_pos = std::format("{}", margin);
                break;
            case WatermarkPosition::BOTTOM_LEFT:
                x_pos = std::format("{}", margin);
                y_pos = std::format("h-text_h-{}", margin);
                break;
            case WatermarkPosition::BOTTOM_RIGHT:
                x_pos = std::format("w-text_w-{}", margin);
                y_pos = std::format("h-text_h-{}", margin);
                break;
            case WatermarkPosition::CENTER:
                x_pos = "(w-text_w)/2";
                y_pos = "(h-text_h)/2";
                break;
        }

        // Create drawtext filter with alpha
        const auto alpha_value = static_cast<int>(opacity * 255);
        const auto filter_desc = std::format(
            "drawtext=text='{}':fontsize={}:fontcolor={}@{:02X}:x={}:y={}",
            text, font_size, font_color, alpha_value, x_pos, y_pos
        );

        initialize_filter(filter_desc);
        process_video();
    }

private:
    void initialize() {
        // Find video stream
        const auto stream_idx = ffmpeg::find_stream_index(format_ctx_.get(), AVMEDIA_TYPE_VIDEO);
        if (!stream_idx) {
            throw ffmpeg::FFmpegError("No video stream found");
        }
        video_stream_index_ = *stream_idx;

        // Setup decoder
        const auto* codecpar = format_ctx_->streams[video_stream_index_]->codecpar;
        const auto* decoder = avcodec_find_decoder(codecpar->codec_id);
        if (!decoder) {
            throw ffmpeg::FFmpegError("Decoder not found");
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

        std::cout << std::format("Video: {}x{}, {} fps\n",
                                codec_ctx_->width,
                                codec_ctx_->height,
                                av_q2d(format_ctx_->streams[video_stream_index_]->avg_frame_rate));
    }

    void initialize_filter(std::string_view filter_description) {
        const auto* buffersrc = avfilter_get_by_name("buffer");
        const auto* buffersink = avfilter_get_by_name("buffersink");

        filter_graph_.reset(avfilter_graph_alloc());
        if (!filter_graph_) {
            throw ffmpeg::FFmpegError("Failed to allocate filter graph");
        }

        // Create buffer source
        const auto args = std::format(
            "video_size={}x{}:pix_fmt={}:time_base={}/{}:pixel_aspect={}/{}",
            codec_ctx_->width, codec_ctx_->height,
            static_cast<int>(codec_ctx_->pix_fmt),
            codec_ctx_->time_base.num, codec_ctx_->time_base.den,
            codec_ctx_->sample_aspect_ratio.num,
            codec_ctx_->sample_aspect_ratio.den
        );

        ffmpeg::check_error(
            avfilter_graph_create_filter(&buffersrc_ctx_, buffersrc, "in",
                                        args.c_str(), nullptr, filter_graph_.get()),
            "create buffer source"
        );

        // Create buffer sink
        ffmpeg::check_error(
            avfilter_graph_create_filter(&buffersink_ctx_, buffersink, "out",
                                        nullptr, nullptr, filter_graph_.get()),
            "create buffer sink"
        );

        // Setup filter graph
        AVFilterInOut* outputs = avfilter_inout_alloc();
        AVFilterInOut* inputs = avfilter_inout_alloc();

        if (!outputs || !inputs) {
            avfilter_inout_free(&inputs);
            avfilter_inout_free(&outputs);
            throw ffmpeg::FFmpegError("Failed to allocate filter I/O");
        }

        outputs->name = av_strdup("in");
        outputs->filter_ctx = buffersrc_ctx_;
        outputs->pad_idx = 0;
        outputs->next = nullptr;

        inputs->name = av_strdup("out");
        inputs->filter_ctx = buffersink_ctx_;
        inputs->pad_idx = 0;
        inputs->next = nullptr;

        const auto ret = avfilter_graph_parse_ptr(filter_graph_.get(), filter_description.data(),
                                                 &inputs, &outputs, nullptr);
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);

        ffmpeg::check_error(ret, "parse filter graph");

        ffmpeg::check_error(
            avfilter_graph_config(filter_graph_.get(), nullptr),
            "configure filter graph"
        );
    }

    void process_video() {
        // Create output context
        AVFormatContext* output_ctx_raw = nullptr;
        ffmpeg::check_error(
            avformat_alloc_output_context2(&output_ctx_raw, nullptr, nullptr,
                                          output_video_.string().c_str()),
            "allocate output context"
        );
        auto output_ctx = ffmpeg::FormatContextPtr(output_ctx_raw);

        // Create video stream
        auto* out_stream = avformat_new_stream(output_ctx.get(), nullptr);
        if (!out_stream) {
            throw ffmpeg::FFmpegError("Failed to create output stream");
        }

        // Setup encoder
        const auto* encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!encoder) {
            throw ffmpeg::FFmpegError("H.264 encoder not found");
        }

        encoder_ctx_ = ffmpeg::create_codec_context(encoder);
        encoder_ctx_->width = codec_ctx_->width;
        encoder_ctx_->height = codec_ctx_->height;
        encoder_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
        encoder_ctx_->time_base = codec_ctx_->time_base;
        encoder_ctx_->framerate = av_guess_frame_rate(format_ctx_.get(),
                                                      format_ctx_->streams[video_stream_index_],
                                                      nullptr);
        encoder_ctx_->bit_rate = 2000000;

        if (output_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
            encoder_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        ffmpeg::check_error(
            avcodec_open2(encoder_ctx_.get(), encoder, nullptr),
            "open encoder"
        );

        ffmpeg::check_error(
            avcodec_parameters_from_context(out_stream->codecpar, encoder_ctx_.get()),
            "copy encoder parameters"
        );

        out_stream->time_base = encoder_ctx_->time_base;

        // Open output file
        if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
            ffmpeg::check_error(
                avio_open(&output_ctx->pb, output_video_.string().c_str(), AVIO_FLAG_WRITE),
                "open output file"
            );
        }

        // Write header
        ffmpeg::check_error(
            avformat_write_header(output_ctx.get(), nullptr),
            "write header"
        );

        // Process frames
        int frame_count = 0;
        std::cout << "Processing video...\n";

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
                const auto recv_ret = avcodec_receive_frame(codec_ctx_.get(), frame_.get());

                if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF) {
                    break;
                }

                if (recv_ret < 0) {
                    break;
                }

                ffmpeg::ScopedFrameUnref frame_guard(frame_.get());

                // Push frame through filter
                ffmpeg::check_error(
                    av_buffersrc_add_frame_flags(buffersrc_ctx_, frame_.get(),
                                               AV_BUFFERSRC_FLAG_KEEP_REF),
                    "feed frame to filter"
                );

                // Get filtered frame
                while (true) {
                    const auto filter_ret = av_buffersink_get_frame(buffersink_ctx_,
                                                                   filtered_frame_.get());

                    if (filter_ret == AVERROR(EAGAIN) || filter_ret == AVERROR_EOF) {
                        break;
                    }

                    if (filter_ret < 0) {
                        break;
                    }

                    ffmpeg::ScopedFrameUnref filtered_guard(filtered_frame_.get());

                    // Encode and write frame
                    filtered_frame_->pict_type = AV_PICTURE_TYPE_NONE;
                    encode_write_frame(output_ctx.get(), out_stream);

                    ++frame_count;
                    if (frame_count % 30 == 0) {
                        std::cout << std::format("Processed {} frames\r", frame_count) << std::flush;
                    }
                }
            }
        }

        // Flush encoder
        flush_encoder(output_ctx.get(), out_stream);

        // Write trailer
        ffmpeg::check_error(
            av_write_trailer(output_ctx.get()),
            "write trailer"
        );

        std::cout << std::format("\n\nTotal frames: {}\n", frame_count);
        std::cout << std::format("✓ Watermark added successfully\n");
        std::cout << std::format("Output file: {}\n", output_video_.string());
    }

    void encode_write_frame(AVFormatContext* output_ctx, AVStream* out_stream) {
        auto encoded_packet = ffmpeg::create_packet();

        const auto ret = avcodec_send_frame(encoder_ctx_.get(), filtered_frame_.get());
        if (ret < 0) {
            return;
        }

        while (avcodec_receive_packet(encoder_ctx_.get(), encoded_packet.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(encoded_packet.get());

            av_packet_rescale_ts(encoded_packet.get(), encoder_ctx_->time_base,
                               out_stream->time_base);
            encoded_packet->stream_index = out_stream->index;

            ffmpeg::check_error(
                av_interleaved_write_frame(output_ctx, encoded_packet.get()),
                "write frame"
            );
        }
    }

    void flush_encoder(AVFormatContext* output_ctx, AVStream* out_stream) {
        avcodec_send_frame(encoder_ctx_.get(), nullptr);

        auto encoded_packet = ffmpeg::create_packet();
        while (avcodec_receive_packet(encoder_ctx_.get(), encoded_packet.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(encoded_packet.get());

            av_packet_rescale_ts(encoded_packet.get(), encoder_ctx_->time_base,
                               out_stream->time_base);
            encoded_packet->stream_index = out_stream->index;

            av_interleaved_write_frame(output_ctx, encoded_packet.get());
        }
    }

    std::string input_video_;
    fs::path output_video_;
    int video_stream_index_ = -1;

    ffmpeg::FormatContextPtr format_ctx_;
    ffmpeg::CodecContextPtr codec_ctx_;
    ffmpeg::CodecContextPtr encoder_ctx_;
    ffmpeg::FilterGraphPtr filter_graph_;
    ffmpeg::PacketPtr packet_;
    ffmpeg::FramePtr frame_;
    ffmpeg::FramePtr filtered_frame_;

    AVFilterContext* buffersrc_ctx_ = nullptr;
    AVFilterContext* buffersink_ctx_ = nullptr;
};

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <command> <input_video> <output_video> [options]\n\n", prog_name);
    std::cout << "Commands:\n\n";
    std::cout << "  image <input_video> <output_video> <watermark_image> <position> [opacity]\n";
    std::cout << "      Add image watermark\n\n";
    std::cout << "  text <input_video> <output_video> <text> <position> [font_size] [color] [opacity]\n";
    std::cout << "      Add text watermark\n\n";
    std::cout << "Positions:\n";
    std::cout << "  - top_left\n";
    std::cout << "  - top_right\n";
    std::cout << "  - bottom_left\n";
    std::cout << "  - bottom_right\n";
    std::cout << "  - center\n\n";
    std::cout << "Examples:\n";
    std::cout << std::format("  {} image video.mp4 output.mp4 logo.png bottom_right 0.7\n", prog_name);
    std::cout << std::format("  {} text video.mp4 output.mp4 \"Copyright 2024\" bottom_left 24 white 0.8\n", prog_name);
    std::cout << std::format("  {} text video.mp4 output.mp4 \"MyChannel\" top_right 32 yellow\n", prog_name);
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 5) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        const std::string_view command{argv[1]};
        const std::string_view input_video{argv[2]};
        const fs::path output_video{argv[3]};

        VideoWatermarker watermarker(input_video, output_video);

        if (command == "image") {
            if (argc < 6) {
                std::cerr << "Error: image command requires <watermark_image> <position> [opacity]\n";
                return 1;
            }
            const std::string_view watermark_image{argv[4]};
            const auto position = parse_position(argv[5]);
            const float opacity = argc > 6 ? static_cast<float>(std::atof(argv[6])) : 1.0f;

            watermarker.add_image_watermark(watermark_image, position, opacity);

        } else if (command == "text") {
            if (argc < 6) {
                std::cerr << "Error: text command requires <text> <position> [font_size] [color] [opacity]\n";
                return 1;
            }
            const std::string_view text{argv[4]};
            const auto position = parse_position(argv[5]);
            const int font_size = argc > 6 ? std::atoi(argv[6]) : 24;
            const std::string_view font_color = argc > 7 ? std::string_view{argv[7]} : "white";
            const float opacity = argc > 8 ? static_cast<float>(std::atof(argv[8])) : 0.7f;

            watermarker.add_text_watermark(text, position, font_size, font_color, opacity);

        } else {
            std::cerr << std::format("Error: Unknown command '{}'\n", command);
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
