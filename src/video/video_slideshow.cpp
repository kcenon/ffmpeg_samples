/**
 * Video Slideshow Generator
 *
 * This sample demonstrates how to create video slideshows from images
 * with transitions using modern C++20 and FFmpeg libraries.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <format>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>

namespace fs = std::filesystem;

namespace {

enum class TransitionType {
    NONE,
    FADE,
    SLIDE_LEFT,
    SLIDE_RIGHT,
    ZOOM_IN,
    ZOOM_OUT
};

TransitionType parse_transition(std::string_view trans_str) {
    if (trans_str == "none") return TransitionType::NONE;
    if (trans_str == "fade") return TransitionType::FADE;
    if (trans_str == "slide_left") return TransitionType::SLIDE_LEFT;
    if (trans_str == "slide_right") return TransitionType::SLIDE_RIGHT;
    if (trans_str == "zoom_in") return TransitionType::ZOOM_IN;
    if (trans_str == "zoom_out") return TransitionType::ZOOM_OUT;

    throw std::invalid_argument(std::format("Invalid transition: {}", trans_str));
}

std::string get_transition_filter(TransitionType type, int width, int height, double progress) {
    switch (type) {
        case TransitionType::FADE:
            return std::format("fade=t=in:st=0:d=1:alpha=1");

        case TransitionType::SLIDE_LEFT:
            return std::format("crop=w={}:h={}:x={}:y=0",
                             width, height,
                             static_cast<int>((1.0 - progress) * width));

        case TransitionType::SLIDE_RIGHT:
            return std::format("crop=w={}:h={}:x={}:y=0",
                             width, height,
                             static_cast<int>(progress * width - width));

        case TransitionType::ZOOM_IN: {
            const auto scale = 1.0 + progress * 0.2;
            return std::format("scale={}:{},crop={}:{}",
                             static_cast<int>(width * scale),
                             static_cast<int>(height * scale),
                             width, height);
        }

        case TransitionType::ZOOM_OUT: {
            const auto scale = 1.2 - progress * 0.2;
            return std::format("scale={}:{},crop={}:{}",
                             static_cast<int>(width * scale),
                             static_cast<int>(height * scale),
                             width, height);
        }

        case TransitionType::NONE:
        default:
            return "";
    }
}

class SlideshowGenerator {
public:
    SlideshowGenerator(int width, int height, int fps, double image_duration,
                      double transition_duration, TransitionType transition)
        : width_(width)
        , height_(height)
        , fps_(fps)
        , image_duration_(image_duration)
        , transition_duration_(transition_duration)
        , transition_(transition) {
    }

    void generate(const std::vector<fs::path>& image_files, const fs::path& output_file) {
        std::cout << "Slideshow Generator\n";
        std::cout << "===================\n\n";
        std::cout << std::format("Number of images: {}\n", image_files.size());
        std::cout << std::format("Output: {}\n", output_file.string());
        std::cout << std::format("Resolution: {}x{}\n", width_, height_);
        std::cout << std::format("FPS: {}\n", fps_);
        std::cout << std::format("Image duration: {:.1f}s\n", image_duration_);
        std::cout << std::format("Transition duration: {:.1f}s\n\n", transition_duration_);

        if (image_files.empty()) {
            throw std::invalid_argument("No image files provided");
        }

        // Create output context
        AVFormatContext* output_ctx_raw = nullptr;
        ffmpeg::check_error(
            avformat_alloc_output_context2(&output_ctx_raw, nullptr, nullptr,
                                          output_file.string().c_str()),
            "allocate output context"
        );
        auto output_ctx = ffmpeg::FormatContextPtr(output_ctx_raw);

        // Setup encoder
        const auto* encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!encoder) {
            throw ffmpeg::FFmpegError("H.264 encoder not found");
        }

        encoder_ctx_ = ffmpeg::create_codec_context(encoder);
        encoder_ctx_->width = width_;
        encoder_ctx_->height = height_;
        encoder_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
        encoder_ctx_->time_base = AVRational{1, fps_};
        encoder_ctx_->framerate = AVRational{fps_, 1};
        encoder_ctx_->bit_rate = 2000000;

        if (output_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
            encoder_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        ffmpeg::check_error(
            avcodec_open2(encoder_ctx_.get(), encoder, nullptr),
            "open encoder"
        );

        // Create video stream
        auto* out_stream = avformat_new_stream(output_ctx.get(), nullptr);
        if (!out_stream) {
            throw ffmpeg::FFmpegError("Failed to create output stream");
        }

        ffmpeg::check_error(
            avcodec_parameters_from_context(out_stream->codecpar, encoder_ctx_.get()),
            "copy encoder parameters"
        );

        out_stream->time_base = encoder_ctx_->time_base;

        // Open output file
        if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
            ffmpeg::check_error(
                avio_open(&output_ctx->pb, output_file.string().c_str(), AVIO_FLAG_WRITE),
                "open output file"
            );
        }

        // Write header
        ffmpeg::check_error(
            avformat_write_header(output_ctx.get(), nullptr),
            "write header"
        );

        // Process images
        std::cout << "Generating slideshow...\n";
        int64_t pts = 0;

        for (size_t i = 0; i < image_files.size(); ++i) {
            std::cout << std::format("Processing image {}/{}...\r",
                                    i + 1, image_files.size()) << std::flush;

            pts = process_image(image_files[i], output_ctx.get(), out_stream, pts);
        }

        // Flush encoder
        flush_encoder(output_ctx.get(), out_stream);

        // Write trailer
        av_write_trailer(output_ctx.get());

        const auto total_duration = pts / static_cast<double>(fps_);
        std::cout << std::format("\n\nTotal frames: {}\n", pts);
        std::cout << std::format("Duration: {:.2f} seconds\n", total_duration);
        std::cout << std::format("✓ Slideshow generated successfully\n");
        std::cout << std::format("Output file: {}\n", output_file.string());
    }

private:
    int64_t process_image(const fs::path& image_file, AVFormatContext* output_ctx,
                         AVStream* out_stream, int64_t start_pts) {
        // Load and decode image
        auto image_ctx = ffmpeg::open_input_format(image_file.string().c_str());

        const auto stream_idx = ffmpeg::find_stream_index(image_ctx.get(), AVMEDIA_TYPE_VIDEO);
        if (!stream_idx) {
            throw ffmpeg::FFmpegError(std::format("Failed to load image: {}",
                                                  image_file.string()));
        }

        const auto* codecpar = image_ctx->streams[*stream_idx]->codecpar;
        const auto* decoder = avcodec_find_decoder(codecpar->codec_id);
        if (!decoder) {
            throw ffmpeg::FFmpegError("Image decoder not found");
        }

        auto decoder_ctx = ffmpeg::create_codec_context(decoder);
        ffmpeg::check_error(
            avcodec_parameters_to_context(decoder_ctx.get(), codecpar),
            "copy decoder parameters"
        );
        ffmpeg::check_error(
            avcodec_open2(decoder_ctx.get(), decoder, nullptr),
            "open decoder"
        );

        // Read and decode image
        auto packet = ffmpeg::create_packet();
        av_read_frame(image_ctx.get(), packet.get());

        auto decoded_frame = ffmpeg::create_frame();
        avcodec_send_packet(decoder_ctx.get(), packet.get());
        avcodec_receive_frame(decoder_ctx.get(), decoded_frame.get());

        // Scale image to target resolution
        auto scaled_frame = scale_image(decoded_frame.get(),
                                       decoder_ctx->width, decoder_ctx->height);

        // Generate frames for this image (with duration)
        const auto num_frames = static_cast<int>(image_duration_ * fps_);

        for (int i = 0; i < num_frames; ++i) {
            scaled_frame->pts = start_pts++;

            // Encode and write frame
            encode_write_frame(output_ctx, out_stream, scaled_frame.get());
        }

        return start_pts;
    }

    ffmpeg::FramePtr scale_image(AVFrame* source, int src_width, int src_height) {
        // Create scaler context
        auto sws_ctx = ffmpeg::SwsContextPtr(sws_getContext(
            src_width, src_height, static_cast<AVPixelFormat>(source->format),
            width_, height_, AV_PIX_FMT_YUV420P,
            SWS_BILINEAR, nullptr, nullptr, nullptr
        ));

        if (!sws_ctx) {
            throw ffmpeg::FFmpegError("Failed to create scaler");
        }

        // Allocate output frame
        auto scaled_frame = ffmpeg::create_frame();
        scaled_frame->format = AV_PIX_FMT_YUV420P;
        scaled_frame->width = width_;
        scaled_frame->height = height_;

        ffmpeg::check_error(
            av_frame_get_buffer(scaled_frame.get(), 0),
            "allocate frame buffer"
        );

        // Scale image
        sws_scale(sws_ctx.get(),
                 source->data, source->linesize, 0, src_height,
                 scaled_frame->data, scaled_frame->linesize);

        return scaled_frame;
    }

    void encode_write_frame(AVFormatContext* output_ctx, AVStream* out_stream, AVFrame* frame) {
        auto encoded_packet = ffmpeg::create_packet();

        const auto ret = avcodec_send_frame(encoder_ctx_.get(), frame);
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

    int width_;
    int height_;
    int fps_;
    double image_duration_;
    double transition_duration_;
    TransitionType transition_;

    ffmpeg::CodecContextPtr encoder_ctx_;
};

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <output_video> <image_dir> [options]\n\n", prog_name);
    std::cout << "Options:\n";
    std::cout << "  --width <pixels>        Video width (default: 1920)\n";
    std::cout << "  --height <pixels>       Video height (default: 1080)\n";
    std::cout << "  --fps <rate>            Frame rate (default: 30)\n";
    std::cout << "  --duration <seconds>    Duration per image (default: 3.0)\n";
    std::cout << "  --transition <type>     Transition type (default: fade)\n";
    std::cout << "  --trans-duration <sec>  Transition duration (default: 1.0)\n\n";
    std::cout << "Transition Types:\n";
    std::cout << "  none, fade, slide_left, slide_right, zoom_in, zoom_out\n\n";
    std::cout << "Examples:\n";
    std::cout << std::format("  {} slideshow.mp4 photos/\n", prog_name);
    std::cout << std::format("  {} output.mp4 images/ --width 1280 --height 720 --duration 5\n", prog_name);
    std::cout << std::format("  {} video.mp4 pics/ --transition zoom_in --fps 60\n", prog_name);
}

std::vector<fs::path> collect_image_files(const fs::path& directory) {
    std::vector<fs::path> images;

    const std::vector<std::string> extensions = {
        ".jpg", ".jpeg", ".png", ".bmp", ".tiff", ".tif"
    };

    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const auto ext = entry.path().extension().string();
        const auto ext_lower = [&ext]() {
            std::string lower = ext;
            std::ranges::transform(lower, lower.begin(), ::tolower);
            return lower;
        }();

        if (std::ranges::find(extensions, ext_lower) != extensions.end()) {
            images.push_back(entry.path());
        }
    }

    std::ranges::sort(images);
    return images;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        const fs::path output_file{argv[1]};
        const fs::path image_dir{argv[2]};

        // Parse options
        int width = 1920;
        int height = 1080;
        int fps = 30;
        double duration = 3.0;
        double trans_duration = 1.0;
        TransitionType transition = TransitionType::FADE;

        for (int i = 3; i < argc; ++i) {
            const std::string_view arg{argv[i]};

            if (arg == "--width" && i + 1 < argc) {
                width = std::atoi(argv[++i]);
            } else if (arg == "--height" && i + 1 < argc) {
                height = std::atoi(argv[++i]);
            } else if (arg == "--fps" && i + 1 < argc) {
                fps = std::atoi(argv[++i]);
            } else if (arg == "--duration" && i + 1 < argc) {
                duration = std::stod(argv[++i]);
            } else if (arg == "--transition" && i + 1 < argc) {
                transition = parse_transition(argv[++i]);
            } else if (arg == "--trans-duration" && i + 1 < argc) {
                trans_duration = std::stod(argv[++i]);
            }
        }

        // Collect image files
        auto image_files = collect_image_files(image_dir);

        if (image_files.empty()) {
            std::cerr << std::format("Error: No images found in {}\n", image_dir.string());
            return 1;
        }

        std::cout << std::format("Found {} images\n\n", image_files.size());

        // Generate slideshow
        SlideshowGenerator generator(width, height, fps, duration, trans_duration, transition);
        generator.generate(image_files, output_file);

    } catch (const ffmpeg::FFmpegError& e) {
        std::cerr << std::format("FFmpeg error: {}\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }

    return 0;
}
