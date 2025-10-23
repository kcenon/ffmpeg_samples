/**
 * Video GIF Creator
 *
 * This sample demonstrates how to create optimized GIF animations
 * from video files using FFmpeg's palettegen and paletteuse filters
 * with modern C++20.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <format>
#include <string>
#include <string_view>
#include <optional>
#include <cmath>

extern "C" {
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

namespace {

struct GifParams {
    int width = -1;              // Output width (-1 = keep original)
    int height = -1;             // Output height (-1 = keep original)
    int fps = 10;                // Frame rate (default: 10 fps)
    double start_time = 0.0;     // Start time in seconds
    double duration = 0.0;       // Duration in seconds (0 = entire video)
    int max_colors = 256;        // Maximum colors in palette (1-256)
    bool dither = true;          // Enable dithering
    std::string dither_mode = "sierra2_4a";  // Dithering algorithm
};

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <input> <output.gif> [options]\n\n", prog_name);
    std::cout << "Options:\n";
    std::cout << "  -s, --size <WxH>         Output size (e.g., 640x480, 320x-1 for auto height)\n";
    std::cout << "  -r, --fps <fps>          Frame rate (default: 10)\n";
    std::cout << "  -ss <time>               Start time in seconds (default: 0)\n";
    std::cout << "  -t <duration>            Duration in seconds (default: entire video)\n";
    std::cout << "  --colors <n>             Maximum colors 1-256 (default: 256)\n";
    std::cout << "  --no-dither              Disable dithering\n";
    std::cout << "  --dither <mode>          Dithering mode: bayer, heckbert, floyd_steinberg,\n";
    std::cout << "                           sierra2, sierra2_4a (default: sierra2_4a)\n\n";

    std::cout << "Examples:\n";
    std::cout << std::format("  {} video.mp4 output.gif\n", prog_name);
    std::cout << "    Convert entire video to GIF with default settings\n\n";

    std::cout << std::format("  {} video.mp4 output.gif -s 480x270 -r 15\n", prog_name);
    std::cout << "    Create 480x270 GIF at 15 fps\n\n";

    std::cout << std::format("  {} video.mp4 output.gif -ss 10 -t 3 --fps 12\n", prog_name);
    std::cout << "    Create 3-second GIF starting at 10 seconds, 12 fps\n\n";

    std::cout << std::format("  {} video.mp4 output.gif --colors 128 --no-dither\n", prog_name);
    std::cout << "    Create GIF with 128 colors, no dithering\n\n";

    std::cout << "Notes:\n";
    std::cout << "  - Uses two-pass processing for optimal palette generation\n";
    std::cout << "  - Lower FPS = smaller file size\n";
    std::cout << "  - Fewer colors = smaller file but lower quality\n";
    std::cout << "  - Dithering improves quality but may increase size\n";
}

std::optional<GifParams> parse_arguments(int argc, char* argv[]) {
    GifParams params;

    for (int i = 3; i < argc; ++i) {
        const std::string_view arg = argv[i];

        if ((arg == "-s" || arg == "--size") && i + 1 < argc) {
            const std::string size_str = argv[++i];
            const auto x_pos = size_str.find('x');
            if (x_pos != std::string::npos) {
                params.width = std::stoi(size_str.substr(0, x_pos));
                params.height = std::stoi(size_str.substr(x_pos + 1));
            } else {
                std::cerr << "Error: Invalid size format. Use WxH (e.g., 640x480)\n";
                return std::nullopt;
            }
        }
        else if ((arg == "-r" || arg == "--fps") && i + 1 < argc) {
            params.fps = std::stoi(argv[++i]);
            if (params.fps < 1 || params.fps > 50) {
                std::cerr << "Error: FPS must be between 1 and 50\n";
                return std::nullopt;
            }
        }
        else if (arg == "-ss" && i + 1 < argc) {
            params.start_time = std::stod(argv[++i]);
        }
        else if (arg == "-t" && i + 1 < argc) {
            params.duration = std::stod(argv[++i]);
        }
        else if (arg == "--colors" && i + 1 < argc) {
            params.max_colors = std::stoi(argv[++i]);
            if (params.max_colors < 1 || params.max_colors > 256) {
                std::cerr << "Error: Colors must be between 1 and 256\n";
                return std::nullopt;
            }
        }
        else if (arg == "--no-dither") {
            params.dither = false;
        }
        else if (arg == "--dither" && i + 1 < argc) {
            params.dither_mode = argv[++i];
        }
        else {
            std::cerr << std::format("Error: Unknown option '{}'\n", arg);
            return std::nullopt;
        }
    }

    return params;
}

class VideoGifCreator {
public:
    VideoGifCreator(std::string_view input_file, std::string_view output_file,
                   const GifParams& params)
        : input_file_(input_file)
        , output_file_(output_file)
        , params_(params)
        , input_format_ctx_(ffmpeg::open_input_format(input_file.data())) {
    }

    void create() {
        std::cout << "Video to GIF Converter\n";
        std::cout << "======================\n\n";
        std::cout << std::format("Input: {}\n", input_file_);
        std::cout << std::format("Output: {}\n", output_file_);
        std::cout << std::format("FPS: {}\n", params_.fps);
        std::cout << std::format("Colors: {}\n", params_.max_colors);
        std::cout << std::format("Dithering: {}\n",
                                params_.dither ? params_.dither_mode : "disabled");

        // Find video stream
        video_stream_index_ = av_find_best_stream(
            input_format_ctx_.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);

        if (video_stream_index_ < 0) {
            throw std::runtime_error("No video stream found");
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

        // Calculate output dimensions
        if (params_.width == -1 && params_.height == -1) {
            output_width_ = input_codec_ctx_->width;
            output_height_ = input_codec_ctx_->height;
        } else if (params_.width == -1) {
            output_height_ = params_.height;
            output_width_ = (input_codec_ctx_->width * params_.height) / input_codec_ctx_->height;
        } else if (params_.height == -1) {
            output_width_ = params_.width;
            output_height_ = (input_codec_ctx_->height * params_.width) / input_codec_ctx_->width;
        } else {
            output_width_ = params_.width;
            output_height_ = params_.height;
        }

        std::cout << std::format("Size: {}x{}\n", output_width_, output_height_);

        if (params_.duration > 0.0) {
            std::cout << std::format("Time: {:.1f}s - {:.1f}s\n\n",
                                    params_.start_time, params_.start_time + params_.duration);
        } else {
            std::cout << std::format("Start: {:.1f}s\n\n", params_.start_time);
        }

        // Two-pass process
        std::cout << "Pass 1: Generating palette...\n";
        generate_palette();

        std::cout << "\nPass 2: Creating GIF...\n";
        create_gif();

        std::cout << std::format("\nGIF created successfully: {}\n", output_file_);
    }

private:
    void generate_palette() {
        // Setup palette generation filter
        setup_palette_filter();

        input_packet_ = ffmpeg::create_packet();
        input_frame_ = ffmpeg::create_frame();
        filtered_frame_ = ffmpeg::create_frame();

        // Seek to start time if needed
        if (params_.start_time > 0.0) {
            const int64_t seek_target = static_cast<int64_t>(
                params_.start_time * AV_TIME_BASE);
            av_seek_frame(input_format_ctx_.get(), -1, seek_target, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(input_codec_ctx_.get());
        }

        const double end_time = params_.duration > 0.0
            ? params_.start_time + params_.duration
            : std::numeric_limits<double>::max();

        int frame_count = 0;
        auto* stream = input_format_ctx_->streams[video_stream_index_];

        while (av_read_frame(input_format_ctx_.get(), input_packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(input_packet_.get());

            if (input_packet_->stream_index != video_stream_index_) {
                continue;
            }

            // Check current time
            const double current_time = input_packet_->pts * av_q2d(stream->time_base);
            if (current_time < params_.start_time) {
                continue;
            }
            if (current_time >= end_time) {
                break;
            }

            if (avcodec_send_packet(input_codec_ctx_.get(), input_packet_.get()) < 0) {
                continue;
            }

            while (avcodec_receive_frame(input_codec_ctx_.get(), input_frame_.get()) >= 0) {
                ffmpeg::ScopedFrameUnref frame_guard(input_frame_.get());

                // Push frame to palette filter
                if (av_buffersrc_add_frame_flags(palette_buffersrc_ctx_, input_frame_.get(),
                                                AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                    continue;
                }

                // Pull filtered frames
                while (av_buffersink_get_frame(palette_buffersink_ctx_, filtered_frame_.get()) >= 0) {
                    ffmpeg::ScopedFrameUnref filtered_guard(filtered_frame_.get());

                    // Save palette frame
                    if (!palette_frame_) {
                        palette_frame_ = ffmpeg::create_frame();
                        palette_frame_->format = filtered_frame_->format;
                        palette_frame_->width = filtered_frame_->width;
                        palette_frame_->height = filtered_frame_->height;
                        av_frame_get_buffer(palette_frame_.get(), 0);
                    }
                    av_frame_copy(palette_frame_.get(), filtered_frame_.get());
                    frame_count++;
                }
            }
        }

        // Flush
        avcodec_send_packet(input_codec_ctx_.get(), nullptr);
        while (avcodec_receive_frame(input_codec_ctx_.get(), input_frame_.get()) >= 0) {
            ffmpeg::ScopedFrameUnref frame_guard(input_frame_.get());

            if (av_buffersrc_add_frame_flags(palette_buffersrc_ctx_, input_frame_.get(),
                                            AV_BUFFERSRC_FLAG_KEEP_REF) >= 0) {
                while (av_buffersink_get_frame(palette_buffersink_ctx_, filtered_frame_.get()) >= 0) {
                    ffmpeg::ScopedFrameUnref filtered_guard(filtered_frame_.get());

                    if (!palette_frame_) {
                        palette_frame_ = ffmpeg::create_frame();
                        palette_frame_->format = filtered_frame_->format;
                        palette_frame_->width = filtered_frame_->width;
                        palette_frame_->height = filtered_frame_->height;
                        av_frame_get_buffer(palette_frame_.get(), 0);
                    }
                    av_frame_copy(palette_frame_.get(), filtered_frame_.get());
                }
            }
        }

        if (av_buffersrc_add_frame_flags(palette_buffersrc_ctx_, nullptr, 0) >= 0) {
            while (av_buffersink_get_frame(palette_buffersink_ctx_, filtered_frame_.get()) >= 0) {
                ffmpeg::ScopedFrameUnref filtered_guard(filtered_frame_.get());

                if (!palette_frame_) {
                    palette_frame_ = ffmpeg::create_frame();
                    palette_frame_->format = filtered_frame_->format;
                    palette_frame_->width = filtered_frame_->width;
                    palette_frame_->height = filtered_frame_->height;
                    av_frame_get_buffer(palette_frame_.get(), 0);
                }
                av_frame_copy(palette_frame_.get(), filtered_frame_.get());
            }
        }

        if (!palette_frame_) {
            throw std::runtime_error("Failed to generate palette");
        }

        std::cout << std::format("Palette generated ({} frames processed)", frame_count);
    }

    void create_gif() {
        // Reopen input file
        input_format_ctx_.reset();
        input_format_ctx_ = ffmpeg::open_input_format(input_file_.data());

        video_stream_index_ = av_find_best_stream(
            input_format_ctx_.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);

        auto* input_stream = input_format_ctx_->streams[video_stream_index_];

        const auto* decoder = avcodec_find_decoder(input_stream->codecpar->codec_id);
        input_codec_ctx_ = ffmpeg::create_codec_context(decoder);
        avcodec_parameters_to_context(input_codec_ctx_.get(), input_stream->codecpar);
        avcodec_open2(input_codec_ctx_.get(), decoder, nullptr);

        // Setup GIF output
        setup_gif_output();

        // Setup paletteuse filter
        setup_paletteuse_filter();

        input_packet_ = ffmpeg::create_packet();
        input_frame_ = ffmpeg::create_frame();
        filtered_frame_ = ffmpeg::create_frame();

        // Seek to start time
        if (params_.start_time > 0.0) {
            const int64_t seek_target = static_cast<int64_t>(
                params_.start_time * AV_TIME_BASE);
            av_seek_frame(input_format_ctx_.get(), -1, seek_target, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(input_codec_ctx_.get());
        }

        const double end_time = params_.duration > 0.0
            ? params_.start_time + params_.duration
            : std::numeric_limits<double>::max();

        int frame_count = 0;
        int64_t pts_counter = 0;

        while (av_read_frame(input_format_ctx_.get(), input_packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(input_packet_.get());

            if (input_packet_->stream_index != video_stream_index_) {
                continue;
            }

            const double current_time = input_packet_->pts * av_q2d(input_stream->time_base);
            if (current_time < params_.start_time) {
                continue;
            }
            if (current_time >= end_time) {
                break;
            }

            if (avcodec_send_packet(input_codec_ctx_.get(), input_packet_.get()) < 0) {
                continue;
            }

            while (avcodec_receive_frame(input_codec_ctx_.get(), input_frame_.get()) >= 0) {
                ffmpeg::ScopedFrameUnref frame_guard(input_frame_.get());

                // Push frame to paletteuse filter
                if (av_buffersrc_add_frame_flags(gif_buffersrc_ctx_, input_frame_.get(),
                                                AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                    continue;
                }

                // Pull filtered frames
                while (av_buffersink_get_frame(gif_buffersink_ctx_, filtered_frame_.get()) >= 0) {
                    ffmpeg::ScopedFrameUnref filtered_guard(filtered_frame_.get());

                    filtered_frame_->pts = pts_counter++;
                    encode_frame();
                    frame_count++;

                    if (frame_count % 10 == 0) {
                        std::cout << std::format("\rFrames: {}", frame_count) << std::flush;
                    }
                }
            }
        }

        // Flush
        flush_encoder();
        av_write_trailer(output_format_ctx_.get());

        std::cout << std::format("\rTotal frames: {}", frame_count);
    }

    void setup_palette_filter() {
        palette_filter_graph_.reset(avfilter_graph_alloc());
        if (!palette_filter_graph_) {
            throw std::runtime_error("Failed to allocate palette filter graph");
        }

        const auto* buffersrc = avfilter_get_by_name("buffer");
        const auto* buffersink = avfilter_get_by_name("buffersink");

        const std::string args = std::format(
            "video_size={}x{}:pix_fmt={}:time_base={}/{}:pixel_aspect={}/{}",
            input_codec_ctx_->width, input_codec_ctx_->height,
            static_cast<int>(input_codec_ctx_->pix_fmt),
            input_codec_ctx_->time_base.num, input_codec_ctx_->time_base.den,
            input_codec_ctx_->sample_aspect_ratio.num,
            input_codec_ctx_->sample_aspect_ratio.den);

        avfilter_graph_create_filter(&palette_buffersrc_ctx_, buffersrc, "in",
                                     args.c_str(), nullptr, palette_filter_graph_.get());
        avfilter_graph_create_filter(&palette_buffersink_ctx_, buffersink, "out",
                                     nullptr, nullptr, palette_filter_graph_.get());

        // Build filter: fps, scale, palettegen
        std::string filter_desc = std::format("fps={}", params_.fps);

        if (output_width_ != input_codec_ctx_->width ||
            output_height_ != input_codec_ctx_->height) {
            filter_desc += std::format(",scale={}:{}", output_width_, output_height_);
        }

        filter_desc += std::format(",palettegen=max_colors={}", params_.max_colors);

        AVFilterInOut* outputs = avfilter_inout_alloc();
        AVFilterInOut* inputs = avfilter_inout_alloc();

        outputs->name = av_strdup("in");
        outputs->filter_ctx = palette_buffersrc_ctx_;
        outputs->pad_idx = 0;
        outputs->next = nullptr;

        inputs->name = av_strdup("out");
        inputs->filter_ctx = palette_buffersink_ctx_;
        inputs->pad_idx = 0;
        inputs->next = nullptr;

        if (avfilter_graph_parse_ptr(palette_filter_graph_.get(), filter_desc.c_str(),
                                     &inputs, &outputs, nullptr) < 0) {
            avfilter_inout_free(&inputs);
            avfilter_inout_free(&outputs);
            throw std::runtime_error("Failed to parse palette filter graph");
        }

        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);

        if (avfilter_graph_config(palette_filter_graph_.get(), nullptr) < 0) {
            throw std::runtime_error("Failed to configure palette filter graph");
        }
    }

    void setup_paletteuse_filter() {
        gif_filter_graph_.reset(avfilter_graph_alloc());
        if (!gif_filter_graph_) {
            throw std::runtime_error("Failed to allocate GIF filter graph");
        }

        const auto* buffersrc = avfilter_get_by_name("buffer");
        const auto* buffersink = avfilter_get_by_name("buffersink");

        const std::string args = std::format(
            "video_size={}x{}:pix_fmt={}:time_base={}/{}:pixel_aspect={}/{}",
            input_codec_ctx_->width, input_codec_ctx_->height,
            static_cast<int>(input_codec_ctx_->pix_fmt),
            input_codec_ctx_->time_base.num, input_codec_ctx_->time_base.den,
            input_codec_ctx_->sample_aspect_ratio.num,
            input_codec_ctx_->sample_aspect_ratio.den);

        avfilter_graph_create_filter(&gif_buffersrc_ctx_, buffersrc, "in",
                                     args.c_str(), nullptr, gif_filter_graph_.get());
        avfilter_graph_create_filter(&gif_buffersink_ctx_, buffersink, "out",
                                     nullptr, nullptr, gif_filter_graph_.get());

        // Build filter: fps, scale, paletteuse
        std::string filter_desc = std::format("fps={}", params_.fps);

        if (output_width_ != input_codec_ctx_->width ||
            output_height_ != input_codec_ctx_->height) {
            filter_desc += std::format(",scale={}:{}", output_width_, output_height_);
        }

        filter_desc += ",paletteuse";
        if (params_.dither) {
            filter_desc += std::format("=dither={}", params_.dither_mode);
        } else {
            filter_desc += "=dither=none";
        }

        AVFilterInOut* outputs = avfilter_inout_alloc();
        AVFilterInOut* inputs = avfilter_inout_alloc();

        outputs->name = av_strdup("in");
        outputs->filter_ctx = gif_buffersrc_ctx_;
        outputs->pad_idx = 0;
        outputs->next = nullptr;

        inputs->name = av_strdup("out");
        inputs->filter_ctx = gif_buffersink_ctx_;
        inputs->pad_idx = 0;
        inputs->next = nullptr;

        if (avfilter_graph_parse_ptr(gif_filter_graph_.get(), filter_desc.c_str(),
                                     &inputs, &outputs, nullptr) < 0) {
            avfilter_inout_free(&inputs);
            avfilter_inout_free(&outputs);
            throw std::runtime_error("Failed to parse GIF filter graph");
        }

        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);

        if (avfilter_graph_config(gif_filter_graph_.get(), nullptr) < 0) {
            throw std::runtime_error("Failed to configure GIF filter graph");
        }
    }

    void setup_gif_output() {
        avformat_alloc_output_context2(&output_format_ctx_raw_, nullptr,
                                       "gif", output_file_.c_str());
        if (!output_format_ctx_raw_) {
            throw std::runtime_error("Failed to create output format context");
        }
        output_format_ctx_.reset(output_format_ctx_raw_);

        const auto* encoder = avcodec_find_encoder(AV_CODEC_ID_GIF);
        if (!encoder) {
            throw std::runtime_error("GIF encoder not found");
        }

        output_codec_ctx_ = ffmpeg::create_codec_context(encoder);
        output_codec_ctx_->width = output_width_;
        output_codec_ctx_->height = output_height_;
        output_codec_ctx_->pix_fmt = AV_PIX_FMT_PAL8;
        output_codec_ctx_->time_base = {1, params_.fps};

        if (avcodec_open2(output_codec_ctx_.get(), encoder, nullptr) < 0) {
            throw std::runtime_error("Failed to open GIF encoder");
        }

        auto* out_stream = avformat_new_stream(output_format_ctx_.get(), nullptr);
        if (!out_stream) {
            throw std::runtime_error("Failed to create output stream");
        }

        avcodec_parameters_from_context(out_stream->codecpar, output_codec_ctx_.get());
        out_stream->time_base = output_codec_ctx_->time_base;

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

    void encode_frame() {
        if (avcodec_send_frame(output_codec_ctx_.get(), filtered_frame_.get()) < 0) {
            return;
        }

        while (avcodec_receive_packet(output_codec_ctx_.get(), output_packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(output_packet_.get());

            output_packet_->stream_index = 0;
            av_packet_rescale_ts(output_packet_.get(),
                               output_codec_ctx_->time_base,
                               output_format_ctx_->streams[0]->time_base);

            av_interleaved_write_frame(output_format_ctx_.get(), output_packet_.get());
        }
    }

    void flush_encoder() {
        avcodec_send_frame(output_codec_ctx_.get(), nullptr);

        while (avcodec_receive_packet(output_codec_ctx_.get(), output_packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(output_packet_.get());

            output_packet_->stream_index = 0;
            av_packet_rescale_ts(output_packet_.get(),
                               output_codec_ctx_->time_base,
                               output_format_ctx_->streams[0]->time_base);

            av_interleaved_write_frame(output_format_ctx_.get(), output_packet_.get());
        }
    }

    std::string input_file_;
    std::string output_file_;
    GifParams params_;

    int output_width_ = 0;
    int output_height_ = 0;

    ffmpeg::FormatContextPtr input_format_ctx_;
    ffmpeg::CodecContextPtr input_codec_ctx_;
    ffmpeg::PacketPtr input_packet_;
    ffmpeg::FramePtr input_frame_;
    ffmpeg::FramePtr filtered_frame_;
    ffmpeg::FramePtr palette_frame_;

    // Palette generation
    ffmpeg::FilterGraphPtr palette_filter_graph_;
    AVFilterContext* palette_buffersrc_ctx_ = nullptr;
    AVFilterContext* palette_buffersink_ctx_ = nullptr;

    // GIF creation
    ffmpeg::FilterGraphPtr gif_filter_graph_;
    AVFilterContext* gif_buffersrc_ctx_ = nullptr;
    AVFilterContext* gif_buffersink_ctx_ = nullptr;

    AVFormatContext* output_format_ctx_raw_ = nullptr;
    ffmpeg::FormatContextPtr output_format_ctx_;
    ffmpeg::CodecContextPtr output_codec_ctx_;
    ffmpeg::PacketPtr output_packet_;

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

        // Check output extension
        if (!output.ends_with(".gif")) {
            std::cerr << "Warning: Output file should have .gif extension\n";
        }

        const auto params = parse_arguments(argc, argv);
        if (!params) {
            print_usage(argv[0]);
            return 1;
        }

        VideoGifCreator creator(input, output, *params);
        creator.create();

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }
}
