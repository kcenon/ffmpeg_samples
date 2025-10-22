/**
 * Video Filter
 *
 * This sample demonstrates how to apply various video filters
 * using FFmpeg's filter graph API with modern C++20.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <format>
#include <string_view>
#include <unordered_map>
#include <cstring>

namespace {

std::string_view get_filter_description(std::string_view filter_type) {
    static const std::unordered_map<std::string_view, std::string_view> filter_map = {
        {"grayscale", "hue=s=0"},
        {"blur", "gblur=sigma=5"},
        {"sharpen", "unsharp=5:5:1.0:5:5:0.0"},
        {"rotate", "transpose=1"},
        {"flip_h", "hflip"},
        {"flip_v", "vflip"},
        {"brightness", "eq=brightness=0.2"},
        {"contrast", "eq=contrast=1.5"},
        {"edge", "edgedetect=low=0.1:high=0.4"},
        {"negative", "negate"},
        {"custom", "eq=brightness=0.1:contrast=1.2,hue=s=1.2"}
    };

    const auto it = filter_map.find(filter_type);
    return it != filter_map.end() ? it->second : "";
}

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <input_file> <output_file> <filter_type>\n\n", prog_name);
    std::cout << "Available filter types:\n";
    std::cout << "  grayscale    - Convert to grayscale\n";
    std::cout << "  blur         - Apply Gaussian blur\n";
    std::cout << "  sharpen      - Apply sharpening\n";
    std::cout << "  rotate       - Rotate 90 degrees clockwise\n";
    std::cout << "  flip_h       - Flip horizontally\n";
    std::cout << "  flip_v       - Flip vertically\n";
    std::cout << "  brightness   - Increase brightness\n";
    std::cout << "  contrast     - Increase contrast\n";
    std::cout << "  edge         - Edge detection\n";
    std::cout << "  negative     - Negative image\n";
    std::cout << "  custom       - Custom filter (you can modify the code)\n";
    std::cout << std::format("\nExample: {} input.mp4 output.mp4 grayscale\n", prog_name);
}

class VideoFilter {
public:
    VideoFilter(std::string_view input_file, std::string_view output_file,
               std::string_view filter_description)
        : output_file_(output_file)
        , filter_description_(filter_description)
        , input_format_ctx_(ffmpeg::open_input_format(input_file.data()))
        , input_packet_(ffmpeg::create_packet())
        , input_frame_(ffmpeg::create_frame())
        , filtered_frame_(ffmpeg::create_frame()) {

        initialize();
    }

    void process() {
        std::cout << "Processing video with filters...\n";

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
                ffmpeg::check_error(
                    av_buffersrc_add_frame_flags(buffersrc_ctx_, input_frame_.get(),
                                               AV_BUFFERSRC_FLAG_KEEP_REF),
                    "feed frame to filter graph"
                );

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

                    filtered_frame_->pts = pts_counter++;
                    encode_frame();
                    ++frame_count;

                    if (frame_count % 30 == 0) {
                        std::cout << std::format("Processed {} frames\r", frame_count) << std::flush;
                    }
                }
            }
        }

        std::cout << std::format("\nTotal frames processed: {}\n", frame_count);

        // Flush encoder
        flush_encoder();

        // Write trailer
        ffmpeg::check_error(
            av_write_trailer(output_format_ctx_.get()),
            "write trailer"
        );

        std::cout << std::format("Filtering completed successfully!\n");
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

        // Open decoder
        const auto* codecpar = input_format_ctx_->streams[video_stream_index_]->codecpar;
        const auto* decoder = avcodec_find_decoder(codecpar->codec_id);
        if (!decoder) {
            throw ffmpeg::FFmpegError("Decoder not found");
        }

        input_codec_ctx_ = ffmpeg::create_codec_context(decoder);
        ffmpeg::check_error(
            avcodec_parameters_to_context(input_codec_ctx_.get(), codecpar),
            "copy decoder parameters"
        );
        ffmpeg::check_error(
            avcodec_open2(input_codec_ctx_.get(), decoder, nullptr),
            "open decoder"
        );

        // Initialize filter graph
        initialize_filter();

        // Create output
        create_output();
    }

    void initialize_filter() {
        const auto* buffersrc = avfilter_get_by_name("buffer");
        const auto* buffersink = avfilter_get_by_name("buffersink");

        filter_graph_.reset(avfilter_graph_alloc());
        if (!filter_graph_) {
            throw ffmpeg::FFmpegError("Failed to allocate filter graph");
        }

        // Buffer video source
        const auto time_base = input_format_ctx_->streams[video_stream_index_]->time_base;
        const auto args = std::format(
            "video_size={}x{}:pix_fmt={}:time_base={}/{}:pixel_aspect={}/{}",
            input_codec_ctx_->width, input_codec_ctx_->height,
            static_cast<int>(input_codec_ctx_->pix_fmt),
            time_base.num, time_base.den,
            input_codec_ctx_->sample_aspect_ratio.num,
            input_codec_ctx_->sample_aspect_ratio.den
        );

        ffmpeg::check_error(
            avfilter_graph_create_filter(&buffersrc_ctx_, buffersrc, "in",
                                        args.c_str(), nullptr, filter_graph_.get()),
            "create buffer source"
        );

        // Buffer video sink
        ffmpeg::check_error(
            avfilter_graph_create_filter(&buffersink_ctx_, buffersink, "out",
                                        nullptr, nullptr, filter_graph_.get()),
            "create buffer sink"
        );

        // Set pixel format for sink
        enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };
        ffmpeg::check_error(
            av_opt_set_int_list(buffersink_ctx_, "pix_fmts", pix_fmts,
                              AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN),
            "set output pixel format"
        );

        // Set up filter graph endpoints
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

        // Parse filter description
        const auto ret = avfilter_graph_parse_ptr(filter_graph_.get(), filter_description_.c_str(),
                                                 &inputs, &outputs, nullptr);
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);

        ffmpeg::check_error(ret, "parse filter graph");

        // Configure filter graph
        ffmpeg::check_error(
            avfilter_graph_config(filter_graph_.get(), nullptr),
            "configure filter graph"
        );
    }

    void create_output() {
        // Allocate output context
        AVFormatContext* raw_ctx = nullptr;
        ffmpeg::check_error(
            avformat_alloc_output_context2(&raw_ctx, nullptr, nullptr, output_file_.c_str()),
            "allocate output context"
        );
        output_format_ctx_.reset(raw_ctx);

        // Find encoder
        const auto* encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!encoder) {
            throw ffmpeg::FFmpegError("H264 encoder not found");
        }

        // Create output stream
        output_stream_ = avformat_new_stream(output_format_ctx_.get(), nullptr);
        if (!output_stream_) {
            throw ffmpeg::FFmpegError("Failed to create output stream");
        }

        // Create and configure encoder context
        output_codec_ctx_ = ffmpeg::create_codec_context(encoder);

        output_codec_ctx_->width = input_codec_ctx_->width;
        output_codec_ctx_->height = input_codec_ctx_->height;
        output_codec_ctx_->time_base = AVRational{1, 30};
        output_codec_ctx_->framerate = AVRational{30, 1};
        output_codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
        output_codec_ctx_->bit_rate = 2000000;
        output_codec_ctx_->gop_size = 10;
        output_codec_ctx_->max_b_frames = 1;

        av_opt_set(output_codec_ctx_->priv_data, "preset", "medium", 0);

        if (output_format_ctx_->oformat->flags & AVFMT_GLOBALHEADER) {
            output_codec_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        ffmpeg::check_error(
            avcodec_open2(output_codec_ctx_.get(), encoder, nullptr),
            "open encoder"
        );

        ffmpeg::check_error(
            avcodec_parameters_from_context(output_stream_->codecpar, output_codec_ctx_.get()),
            "copy encoder parameters"
        );

        output_stream_->time_base = output_codec_ctx_->time_base;

        // Open output file
        if (!(output_format_ctx_->oformat->flags & AVFMT_NOFILE)) {
            ffmpeg::check_error(
                avio_open(&output_format_ctx_->pb, output_file_.c_str(), AVIO_FLAG_WRITE),
                "open output file"
            );
        }

        // Write header
        ffmpeg::check_error(
            avformat_write_header(output_format_ctx_.get(), nullptr),
            "write header"
        );
    }

    void encode_frame() {
        auto packet = ffmpeg::create_packet();

        const auto ret = avcodec_send_frame(output_codec_ctx_.get(), filtered_frame_.get());
        if (ret < 0) {
            return;
        }

        while (true) {
            const auto recv_ret = avcodec_receive_packet(output_codec_ctx_.get(), packet.get());

            if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF) {
                break;
            }

            if (recv_ret < 0) {
                break;
            }

            ffmpeg::ScopedPacketUnref packet_guard(packet.get());

            av_packet_rescale_ts(packet.get(), output_codec_ctx_->time_base,
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
        avcodec_send_frame(output_codec_ctx_.get(), nullptr);

        while (true) {
            const auto ret = avcodec_receive_packet(output_codec_ctx_.get(), packet.get());

            if (ret < 0) {
                break;
            }

            ffmpeg::ScopedPacketUnref packet_guard(packet.get());

            av_packet_rescale_ts(packet.get(), output_codec_ctx_->time_base,
                               output_stream_->time_base);
            packet->stream_index = 0;

            av_interleaved_write_frame(output_format_ctx_.get(), packet.get());
        }
    }

    std::string output_file_;
    std::string filter_description_;
    int video_stream_index_ = -1;

    ffmpeg::FormatContextPtr input_format_ctx_;
    ffmpeg::FormatContextPtr output_format_ctx_;
    ffmpeg::CodecContextPtr input_codec_ctx_;
    ffmpeg::CodecContextPtr output_codec_ctx_;
    ffmpeg::FilterGraphPtr filter_graph_;
    ffmpeg::PacketPtr input_packet_;
    ffmpeg::FramePtr input_frame_;
    ffmpeg::FramePtr filtered_frame_;

    AVFilterContext* buffersrc_ctx_ = nullptr;
    AVFilterContext* buffersink_ctx_ = nullptr;
    AVStream* output_stream_ = nullptr;
};

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 4) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        const std::string_view input_filename{argv[1]};
        const std::string_view output_filename{argv[2]};
        const std::string_view filter_type{argv[3]};

        const auto filter_description = get_filter_description(filter_type);
        if (filter_description.empty()) {
            std::cerr << std::format("Unknown filter type: {}\n\n", filter_type);
            print_usage(argv[0]);
            return 1;
        }

        std::cout << "FFmpeg Video Filter\n";
        std::cout << "===================\n";
        std::cout << std::format("Input: {}\n", input_filename);
        std::cout << std::format("Output: {}\n", output_filename);
        std::cout << std::format("Filter: {}\n", filter_type);
        std::cout << std::format("Filter description: {}\n\n", filter_description);

        VideoFilter video_filter(input_filename, output_filename, filter_description);
        video_filter.process();

    } catch (const ffmpeg::FFmpegError& e) {
        std::cerr << std::format("FFmpeg error: {}\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }

    return 0;
}
