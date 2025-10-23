/**
 * Video Crop and Rotate
 *
 * This sample demonstrates how to crop and rotate videos using
 * FFmpeg's filter graph API with modern C++20.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <format>
#include <string>
#include <string_view>
#include <optional>
#include <cstring>

extern "C" {
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

namespace {

struct CropParams {
    int x = 0;           // Top-left x coordinate
    int y = 0;           // Top-left y coordinate
    int width = 0;       // Crop width
    int height = 0;      // Crop height
};

struct RotateParams {
    int angle = 0;       // Rotation angle in degrees (0, 90, 180, 270)
    bool transpose = false;  // Use transpose instead of rotate filter
};

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <input> <output> [options]\n\n", prog_name);
    std::cout << "Options:\n";
    std::cout << "  --crop x:y:width:height    Crop video (e.g., --crop 100:50:640:480)\n";
    std::cout << "  --rotate angle             Rotate video (0, 90, 180, 270 degrees)\n";
    std::cout << "  --both                     Apply both crop and rotate\n\n";

    std::cout << "Crop Examples:\n";
    std::cout << std::format("  {} input.mp4 output.mp4 --crop 0:0:640:480\n", prog_name);
    std::cout << "    Crop from top-left (0,0) with size 640x480\n\n";

    std::cout << "Rotate Examples:\n";
    std::cout << std::format("  {} input.mp4 output.mp4 --rotate 90\n", prog_name);
    std::cout << "    Rotate 90 degrees clockwise\n\n";

    std::cout << "Combined Examples:\n";
    std::cout << std::format("  {} input.mp4 output.mp4 --crop 100:100:800:600 --rotate 180\n", prog_name);
    std::cout << "    Crop and then rotate 180 degrees\n\n";

    std::cout << "Notes:\n";
    std::cout << "  - Crop coordinates must be within video dimensions\n";
    std::cout << "  - Rotation angles: 0, 90, 180, 270 degrees only\n";
    std::cout << "  - Operations are applied in order: crop → rotate\n";
}

std::optional<CropParams> parse_crop(std::string_view crop_str) {
    CropParams params;

    // Parse format: x:y:width:height
    const char* str = crop_str.data();
    char* end;

    params.x = std::strtol(str, &end, 10);
    if (*end != ':') return std::nullopt;

    str = end + 1;
    params.y = std::strtol(str, &end, 10);
    if (*end != ':') return std::nullopt;

    str = end + 1;
    params.width = std::strtol(str, &end, 10);
    if (*end != ':') return std::nullopt;

    str = end + 1;
    params.height = std::strtol(str, &end, 10);

    if (params.width <= 0 || params.height <= 0) {
        return std::nullopt;
    }

    return params;
}

std::optional<RotateParams> parse_rotate(std::string_view angle_str) {
    RotateParams params;

    const int angle = std::atoi(angle_str.data());

    if (angle == 0 || angle == 90 || angle == 180 || angle == 270) {
        params.angle = angle;
        params.transpose = (angle == 90 || angle == 270);
        return params;
    }

    return std::nullopt;
}

class VideoCropRotate {
public:
    VideoCropRotate(std::string_view input_file, std::string_view output_file,
                   std::optional<CropParams> crop, std::optional<RotateParams> rotate)
        : output_file_(output_file)
        , crop_params_(crop)
        , rotate_params_(rotate)
        , input_format_ctx_(ffmpeg::open_input_format(input_file.data()))
        , input_packet_(ffmpeg::create_packet())
        , input_frame_(ffmpeg::create_frame())
        , filtered_frame_(ffmpeg::create_frame()) {

        initialize();
    }

    void process() {
        std::cout << "Processing video...\n";

        if (crop_params_) {
            std::cout << std::format("Crop: x={}, y={}, width={}, height={}\n",
                                    crop_params_->x, crop_params_->y,
                                    crop_params_->width, crop_params_->height);
        }

        if (rotate_params_) {
            std::cout << std::format("Rotate: {} degrees\n", rotate_params_->angle);
        }

        int64_t pts_counter = 0;
        int frame_count = 0;

        while (av_read_frame(input_format_ctx_.get(), input_packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(input_packet_.get());

            if (input_packet_->stream_index != video_stream_index_) {
                continue;
            }

            const auto ret = avcodec_send_packet(input_codec_ctx_.get(), input_packet_.get());
            if (ret < 0) {
                continue;
            }

            while (true) {
                const auto recv_ret = avcodec_receive_frame(input_codec_ctx_.get(), input_frame_.get());

                if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF) {
                    break;
                }

                if (recv_ret < 0) {
                    std::cerr << "Error decoding frame\n";
                    break;
                }

                ffmpeg::ScopedFrameUnref frame_guard(input_frame_.get());

                // Push frame to filter graph
                if (av_buffersrc_add_frame_flags(buffersrc_ctx_, input_frame_.get(),
                                                AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                    std::cerr << "Error feeding frame to filter graph\n";
                    break;
                }

                // Pull filtered frames
                while (true) {
                    const auto filter_ret = av_buffersink_get_frame(buffersink_ctx_, filtered_frame_.get());

                    if (filter_ret == AVERROR(EAGAIN) || filter_ret == AVERROR_EOF) {
                        break;
                    }

                    if (filter_ret < 0) {
                        std::cerr << "Error getting filtered frame\n";
                        break;
                    }

                    ffmpeg::ScopedFrameUnref filtered_guard(filtered_frame_.get());

                    // Encode filtered frame
                    filtered_frame_->pts = pts_counter++;

                    if (avcodec_send_frame(output_codec_ctx_.get(), filtered_frame_.get()) < 0) {
                        std::cerr << "Error sending frame for encoding\n";
                        continue;
                    }

                    encode_frames();
                    frame_count++;
                }
            }
        }

        // Flush decoder
        avcodec_send_packet(input_codec_ctx_.get(), nullptr);
        while (avcodec_receive_frame(input_codec_ctx_.get(), input_frame_.get()) >= 0) {
            ffmpeg::ScopedFrameUnref frame_guard(input_frame_.get());

            if (av_buffersrc_add_frame_flags(buffersrc_ctx_, input_frame_.get(),
                                            AV_BUFFERSRC_FLAG_KEEP_REF) >= 0) {
                while (av_buffersink_get_frame(buffersink_ctx_, filtered_frame_.get()) >= 0) {
                    ffmpeg::ScopedFrameUnref filtered_guard(filtered_frame_.get());
                    filtered_frame_->pts = pts_counter++;

                    if (avcodec_send_frame(output_codec_ctx_.get(), filtered_frame_.get()) >= 0) {
                        encode_frames();
                        frame_count++;
                    }
                }
            }
        }

        // Flush filter graph
        if (av_buffersrc_add_frame_flags(buffersrc_ctx_, nullptr, 0) >= 0) {
            while (av_buffersink_get_frame(buffersink_ctx_, filtered_frame_.get()) >= 0) {
                ffmpeg::ScopedFrameUnref filtered_guard(filtered_frame_.get());
                filtered_frame_->pts = pts_counter++;

                if (avcodec_send_frame(output_codec_ctx_.get(), filtered_frame_.get()) >= 0) {
                    encode_frames();
                    frame_count++;
                }
            }
        }

        // Flush encoder
        avcodec_send_frame(output_codec_ctx_.get(), nullptr);
        encode_frames();

        av_write_trailer(output_format_ctx_.get());

        std::cout << std::format("\nProcessing complete!\n");
        std::cout << std::format("Processed {} frames\n", frame_count);
        std::cout << std::format("Output: {}\n", output_file_);
    }

private:
    void initialize() {
        // Find video stream
        video_stream_index_ = av_find_best_stream(
            input_format_ctx_.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);

        if (video_stream_index_ < 0) {
            throw std::runtime_error("Failed to find video stream");
        }

        auto* input_stream = input_format_ctx_->streams[video_stream_index_];

        // Setup decoder
        const auto* decoder = avcodec_find_decoder(input_stream->codecpar->codec_id);
        if (!decoder) {
            throw std::runtime_error("Failed to find decoder");
        }

        input_codec_ctx_ = ffmpeg::create_codec_context(decoder);
        avcodec_parameters_to_context(input_codec_ctx_.get(), input_stream->codecpar);

        if (avcodec_open2(input_codec_ctx_.get(), decoder, nullptr) < 0) {
            throw std::runtime_error("Failed to open decoder");
        }

        // Validate crop parameters
        if (crop_params_) {
            const int max_x = input_codec_ctx_->width - crop_params_->width;
            const int max_y = input_codec_ctx_->height - crop_params_->height;

            if (crop_params_->x < 0 || crop_params_->x > max_x ||
                crop_params_->y < 0 || crop_params_->y > max_y) {
                throw std::runtime_error(
                    std::format("Invalid crop parameters for video size {}x{}",
                              input_codec_ctx_->width, input_codec_ctx_->height));
            }
        }

        // Setup filter graph
        setup_filter_graph();

        // Setup output format
        avformat_alloc_output_context2(&output_format_ctx_raw_, nullptr,
                                       nullptr, output_file_.c_str());
        if (!output_format_ctx_raw_) {
            throw std::runtime_error("Failed to create output format context");
        }
        output_format_ctx_.reset(output_format_ctx_raw_);

        // Setup encoder
        const auto* encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!encoder) {
            throw std::runtime_error("H.264 encoder not found");
        }

        output_codec_ctx_ = ffmpeg::create_codec_context(encoder);

        // Get output dimensions from filter graph
        const auto* buffersink_params =
            reinterpret_cast<AVFilterContext*>(buffersink_ctx_)->inputs[0];

        output_codec_ctx_->width = buffersink_params->w;
        output_codec_ctx_->height = buffersink_params->h;
        output_codec_ctx_->pix_fmt = static_cast<AVPixelFormat>(buffersink_params->format);
        output_codec_ctx_->time_base = input_codec_ctx_->time_base;
        output_codec_ctx_->framerate = av_guess_frame_rate(input_format_ctx_.get(),
                                                          input_stream, nullptr);
        output_codec_ctx_->bit_rate = input_codec_ctx_->bit_rate;

        if (output_format_ctx_->oformat->flags & AVFMT_GLOBALHEADER) {
            output_codec_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        if (avcodec_open2(output_codec_ctx_.get(), encoder, nullptr) < 0) {
            throw std::runtime_error("Failed to open encoder");
        }

        // Create output stream
        auto* out_stream = avformat_new_stream(output_format_ctx_.get(), nullptr);
        if (!out_stream) {
            throw std::runtime_error("Failed to create output stream");
        }

        avcodec_parameters_from_context(out_stream->codecpar, output_codec_ctx_.get());
        out_stream->time_base = output_codec_ctx_->time_base;

        // Open output file
        if (!(output_format_ctx_->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&output_format_ctx_->pb, output_file_.c_str(),
                         AVIO_FLAG_WRITE) < 0) {
                throw std::runtime_error("Failed to open output file");
            }
        }

        if (avformat_write_header(output_format_ctx_.get(), nullptr) < 0) {
            throw std::runtime_error("Failed to write output header");
        }

        output_packet_ = ffmpeg::create_packet();
    }

    void setup_filter_graph() {
        filter_graph_.reset(avfilter_graph_alloc());
        if (!filter_graph_) {
            throw std::runtime_error("Failed to allocate filter graph");
        }

        // Create buffer source
        const auto* buffersrc = avfilter_get_by_name("buffer");
        if (!buffersrc) {
            throw std::runtime_error("Failed to find buffer filter");
        }

        const std::string args = std::format(
            "video_size={}x{}:pix_fmt={}:time_base={}/{}:pixel_aspect={}/{}",
            input_codec_ctx_->width, input_codec_ctx_->height,
            static_cast<int>(input_codec_ctx_->pix_fmt),
            input_codec_ctx_->time_base.num, input_codec_ctx_->time_base.den,
            input_codec_ctx_->sample_aspect_ratio.num,
            input_codec_ctx_->sample_aspect_ratio.den);

        if (avfilter_graph_create_filter(&buffersrc_ctx_, buffersrc, "in",
                                         args.c_str(), nullptr, filter_graph_.get()) < 0) {
            throw std::runtime_error("Failed to create buffer source");
        }

        // Create buffer sink
        const auto* buffersink = avfilter_get_by_name("buffersink");
        if (!buffersink) {
            throw std::runtime_error("Failed to find buffersink filter");
        }

        if (avfilter_graph_create_filter(&buffersink_ctx_, buffersink, "out",
                                         nullptr, nullptr, filter_graph_.get()) < 0) {
            throw std::runtime_error("Failed to create buffer sink");
        }

        // Build filter description
        std::string filter_desc;

        if (crop_params_ && rotate_params_) {
            // Both crop and rotate
            const std::string crop_filter = std::format(
                "crop={}:{}:{}:{}",
                crop_params_->width, crop_params_->height,
                crop_params_->x, crop_params_->y);

            std::string rotate_filter;
            if (rotate_params_->angle == 90) {
                rotate_filter = "transpose=1";  // 90 degrees clockwise
            } else if (rotate_params_->angle == 180) {
                rotate_filter = "transpose=1,transpose=1";  // 180 degrees
            } else if (rotate_params_->angle == 270) {
                rotate_filter = "transpose=2";  // 90 degrees counter-clockwise
            }

            if (!rotate_filter.empty()) {
                filter_desc = crop_filter + "," + rotate_filter;
            } else {
                filter_desc = crop_filter;
            }
        } else if (crop_params_) {
            // Only crop
            filter_desc = std::format(
                "crop={}:{}:{}:{}",
                crop_params_->width, crop_params_->height,
                crop_params_->x, crop_params_->y);
        } else if (rotate_params_) {
            // Only rotate
            if (rotate_params_->angle == 90) {
                filter_desc = "transpose=1";
            } else if (rotate_params_->angle == 180) {
                filter_desc = "transpose=1,transpose=1";
            } else if (rotate_params_->angle == 270) {
                filter_desc = "transpose=2";
            } else {
                filter_desc = "null";  // No rotation
            }
        } else {
            filter_desc = "null";  // No operation
        }

        // Parse filter description
        AVFilterInOut* outputs = avfilter_inout_alloc();
        AVFilterInOut* inputs = avfilter_inout_alloc();

        outputs->name = av_strdup("in");
        outputs->filter_ctx = buffersrc_ctx_;
        outputs->pad_idx = 0;
        outputs->next = nullptr;

        inputs->name = av_strdup("out");
        inputs->filter_ctx = buffersink_ctx_;
        inputs->pad_idx = 0;
        inputs->next = nullptr;

        if (avfilter_graph_parse_ptr(filter_graph_.get(), filter_desc.c_str(),
                                     &inputs, &outputs, nullptr) < 0) {
            avfilter_inout_free(&inputs);
            avfilter_inout_free(&outputs);
            throw std::runtime_error("Failed to parse filter graph");
        }

        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);

        if (avfilter_graph_config(filter_graph_.get(), nullptr) < 0) {
            throw std::runtime_error("Failed to configure filter graph");
        }
    }

    void encode_frames() {
        while (true) {
            const auto ret = avcodec_receive_packet(output_codec_ctx_.get(),
                                                   output_packet_.get());

            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }

            if (ret < 0) {
                std::cerr << "Error encoding frame\n";
                break;
            }

            ffmpeg::ScopedPacketUnref packet_guard(output_packet_.get());

            output_packet_->stream_index = 0;
            av_packet_rescale_ts(output_packet_.get(),
                               output_codec_ctx_->time_base,
                               output_format_ctx_->streams[0]->time_base);

            if (av_interleaved_write_frame(output_format_ctx_.get(),
                                          output_packet_.get()) < 0) {
                std::cerr << "Error writing packet\n";
            }
        }
    }

    std::string output_file_;
    std::optional<CropParams> crop_params_;
    std::optional<RotateParams> rotate_params_;

    ffmpeg::FormatContextPtr input_format_ctx_;
    ffmpeg::CodecContextPtr input_codec_ctx_;
    ffmpeg::PacketPtr input_packet_;
    ffmpeg::FramePtr input_frame_;
    ffmpeg::FramePtr filtered_frame_;

    AVFormatContext* output_format_ctx_raw_ = nullptr;
    ffmpeg::FormatContextPtr output_format_ctx_;
    ffmpeg::CodecContextPtr output_codec_ctx_;
    ffmpeg::PacketPtr output_packet_;

    ffmpeg::FilterGraphPtr filter_graph_;
    AVFilterContext* buffersrc_ctx_ = nullptr;
    AVFilterContext* buffersink_ctx_ = nullptr;

    int video_stream_index_ = -1;
};

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        const std::string input = argv[1];
        const std::string output = argv[2];

        std::optional<CropParams> crop;
        std::optional<RotateParams> rotate;

        // Parse command line arguments
        for (int i = 3; i < argc; ++i) {
            const std::string_view arg = argv[i];

            if (arg == "--crop" && i + 1 < argc) {
                crop = parse_crop(argv[++i]);
                if (!crop) {
                    std::cerr << "Error: Invalid crop format. Use x:y:width:height\n";
                    return 1;
                }
            }
            else if (arg == "--rotate" && i + 1 < argc) {
                rotate = parse_rotate(argv[++i]);
                if (!rotate) {
                    std::cerr << "Error: Invalid rotation angle. Use 0, 90, 180, or 270\n";
                    return 1;
                }
            }
            else {
                std::cerr << std::format("Error: Unknown option '{}'\n", arg);
                print_usage(argv[0]);
                return 1;
            }
        }

        if (!crop && !rotate) {
            std::cerr << "Error: Must specify at least --crop or --rotate\n";
            print_usage(argv[0]);
            return 1;
        }

        VideoCropRotate processor(input, output, crop, rotate);
        processor.process();

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }
}
