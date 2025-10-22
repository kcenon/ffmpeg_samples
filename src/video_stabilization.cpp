/**
 * Video Stabilization
 *
 * This sample demonstrates how to stabilize shaky video footage using
 * FFmpeg's vidstab filter with modern C++20.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <format>
#include <filesystem>
#include <string_view>

namespace fs = std::filesystem;

namespace {

class VideoStabilizer {
public:
    VideoStabilizer(std::string_view input_file, const fs::path& output_file,
                   int smoothing, int shakiness, bool show_stats)
        : input_file_(input_file)
        , output_file_(output_file)
        , smoothing_(smoothing)
        , shakiness_(shakiness)
        , show_stats_(show_stats)
        , format_ctx_(ffmpeg::open_input_format(input_file.data()))
        , packet_(ffmpeg::create_packet())
        , frame_(ffmpeg::create_frame())
        , filtered_frame_(ffmpeg::create_frame()) {

        initialize();
    }

    void stabilize() {
        std::cout << "Video Stabilization\n";
        std::cout << "===================\n\n";
        std::cout << std::format("Input: {}\n", input_file_);
        std::cout << std::format("Output: {}\n", output_file_.string());
        std::cout << std::format("Resolution: {}x{}\n", codec_ctx_->width, codec_ctx_->height);
        std::cout << std::format("Smoothing: {}\n", smoothing_);
        std::cout << std::format("Shakiness: {}\n\n", shakiness_);

        // Step 1: Detect motion
        std::cout << "Step 1/2: Detecting motion...\n";
        const auto transforms_file = detect_motion();

        // Step 2: Apply stabilization
        std::cout << "\nStep 2/2: Applying stabilization...\n";
        apply_stabilization(transforms_file);

        // Cleanup
        fs::remove(transforms_file);

        std::cout << std::format("\n✓ Stabilization completed successfully\n");
        std::cout << std::format("Output file: {}\n", output_file_.string());
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
    }

    fs::path detect_motion() {
        const auto transforms_file = fs::temp_directory_path() / "transforms.trf";

        // Create vidstabdetect filter
        const auto filter_desc = std::format(
            "vidstabdetect=shakiness={}:result={}",
            shakiness_,
            transforms_file.string()
        );

        initialize_filter(filter_desc);

        // Process all frames
        int frame_count = 0;

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

                // Push frame through detection filter
                ffmpeg::check_error(
                    av_buffersrc_add_frame_flags(buffersrc_ctx_, frame_.get(),
                                               AV_BUFFERSRC_FLAG_KEEP_REF),
                    "feed frame to filter"
                );

                // Pull filtered frame (detection only, doesn't modify frame)
                while (true) {
                    const auto filter_ret = av_buffersink_get_frame(buffersink_ctx_,
                                                                   filtered_frame_.get());

                    if (filter_ret == AVERROR(EAGAIN) || filter_ret == AVERROR_EOF) {
                        break;
                    }

                    if (filter_ret < 0) {
                        break;
                    }

                    av_frame_unref(filtered_frame_.get());
                }

                ++frame_count;
                if (frame_count % 30 == 0) {
                    std::cout << std::format("Analyzed {} frames\r", frame_count) << std::flush;
                }
            }
        }

        std::cout << std::format("Analyzed {} frames\n", frame_count);

        return transforms_file;
    }

    void apply_stabilization(const fs::path& transforms_file) {
        // Reopen input file
        auto stab_format_ctx = ffmpeg::open_input_format(input_file_.c_str());

        // Find stream again
        const auto stream_idx = ffmpeg::find_stream_index(stab_format_ctx.get(), AVMEDIA_TYPE_VIDEO);
        if (!stream_idx) {
            throw ffmpeg::FFmpegError("No video stream found");
        }

        const auto stab_video_index = *stream_idx;

        // Setup decoder again
        const auto* codecpar = stab_format_ctx->streams[stab_video_index]->codecpar;
        const auto* decoder = avcodec_find_decoder(codecpar->codec_id);
        if (!decoder) {
            throw ffmpeg::FFmpegError("Decoder not found");
        }

        auto stab_codec_ctx = ffmpeg::create_codec_context(decoder);
        ffmpeg::check_error(
            avcodec_parameters_to_context(stab_codec_ctx.get(), codecpar),
            "copy decoder parameters"
        );
        ffmpeg::check_error(
            avcodec_open2(stab_codec_ctx.get(), decoder, nullptr),
            "open decoder"
        );

        // Create vidstabtransform filter
        const auto filter_desc = std::format(
            "vidstabtransform=input={}:smoothing={}:zoom=0:optzoom=1",
            transforms_file.string(),
            smoothing_
        );

        // Reinitialize filter for transformation
        filter_graph_.reset();
        initialize_filter(filter_desc);

        // Create output context
        AVFormatContext* output_ctx_raw = nullptr;
        ffmpeg::check_error(
            avformat_alloc_output_context2(&output_ctx_raw, nullptr, nullptr,
                                          output_file_.string().c_str()),
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
        encoder_ctx_->width = stab_codec_ctx->width;
        encoder_ctx_->height = stab_codec_ctx->height;
        encoder_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
        encoder_ctx_->time_base = stab_codec_ctx->time_base;
        encoder_ctx_->framerate = av_guess_frame_rate(stab_format_ctx.get(),
                                                      stab_format_ctx->streams[stab_video_index],
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
                avio_open(&output_ctx->pb, output_file_.string().c_str(), AVIO_FLAG_WRITE),
                "open output file"
            );
        }

        // Write header
        ffmpeg::check_error(
            avformat_write_header(output_ctx.get(), nullptr),
            "write header"
        );

        // Process and stabilize frames
        auto stab_packet = ffmpeg::create_packet();
        auto stab_frame = ffmpeg::create_frame();
        int frame_count = 0;

        while (av_read_frame(stab_format_ctx.get(), stab_packet.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(stab_packet.get());

            if (stab_packet->stream_index != stab_video_index) {
                continue;
            }

            const auto ret = avcodec_send_packet(stab_codec_ctx.get(), stab_packet.get());
            if (ret < 0) {
                continue;
            }

            while (true) {
                const auto recv_ret = avcodec_receive_frame(stab_codec_ctx.get(), stab_frame.get());

                if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF) {
                    break;
                }

                if (recv_ret < 0) {
                    break;
                }

                ffmpeg::ScopedFrameUnref frame_guard(stab_frame.get());

                // Push frame through stabilization filter
                ffmpeg::check_error(
                    av_buffersrc_add_frame_flags(buffersrc_ctx_, stab_frame.get(),
                                               AV_BUFFERSRC_FLAG_KEEP_REF),
                    "feed frame to filter"
                );

                // Get stabilized frames
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

                    // Encode stabilized frame
                    filtered_frame_->pict_type = AV_PICTURE_TYPE_NONE;
                    encode_write_frame(output_ctx.get(), out_stream);

                    ++frame_count;
                    if (frame_count % 30 == 0) {
                        std::cout << std::format("Stabilized {} frames\r", frame_count) << std::flush;
                    }
                }
            }
        }

        // Flush encoder
        flush_encoder(output_ctx.get(), out_stream);

        // Write trailer
        av_write_trailer(output_ctx.get());

        std::cout << std::format("Stabilized {} frames\n", frame_count);
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

            av_interleaved_write_frame(output_ctx, encoded_packet.get());
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

    std::string input_file_;
    fs::path output_file_;
    int smoothing_;
    int shakiness_;
    bool show_stats_;
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
    std::cout << std::format("Usage: {} <input_video> <output_video> [options]\n\n", prog_name);
    std::cout << "Options:\n";
    std::cout << "  --smoothing <value>    Smoothing strength (1-100, default: 10)\n";
    std::cout << "                         Higher values = smoother but less responsive\n";
    std::cout << "  --shakiness <value>    Shakiness detection (1-10, default: 5)\n";
    std::cout << "                         Higher values = detect more motion\n";
    std::cout << "  --stats                Show stabilization statistics\n\n";
    std::cout << "Examples:\n";
    std::cout << std::format("  {} shaky.mp4 stable.mp4\n", prog_name);
    std::cout << std::format("  {} input.mp4 output.mp4 --smoothing 20 --shakiness 8\n", prog_name);
    std::cout << std::format("  {} video.mp4 stabilized.mp4 --smoothing 15 --stats\n", prog_name);
    std::cout << "\nNote: This requires FFmpeg to be compiled with vidstab support.\n";
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        const std::string_view input_file{argv[1]};
        const fs::path output_file{argv[2]};

        // Parse options
        int smoothing = 10;
        int shakiness = 5;
        bool show_stats = false;

        for (int i = 3; i < argc; ++i) {
            const std::string_view arg{argv[i]};

            if (arg == "--smoothing" && i + 1 < argc) {
                smoothing = std::atoi(argv[++i]);
                smoothing = std::clamp(smoothing, 1, 100);
            } else if (arg == "--shakiness" && i + 1 < argc) {
                shakiness = std::atoi(argv[++i]);
                shakiness = std::clamp(shakiness, 1, 10);
            } else if (arg == "--stats") {
                show_stats = true;
            }
        }

        VideoStabilizer stabilizer(input_file, output_file, smoothing, shakiness, show_stats);
        stabilizer.stabilize();

    } catch (const ffmpeg::FFmpegError& e) {
        std::cerr << std::format("FFmpeg error: {}\n", e.what());
        std::cerr << "\nNote: Video stabilization requires FFmpeg with vidstab filter support.\n";
        std::cerr << "Install with: brew install ffmpeg (macOS) or build from source with --enable-libvidstab\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }

    return 0;
}
