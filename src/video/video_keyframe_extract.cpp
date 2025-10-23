/**
 * Video Keyframe Extractor
 *
 * This sample demonstrates how to extract I-frames (keyframes) from videos
 * for thumbnail generation, fast seeking, and video analysis using modern C++20.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <fstream>
#include <format>
#include <string>
#include <string_view>
#include <filesystem>
#include <optional>
#include <vector>

extern "C" {
#include <libswscale/swscale.h>
}

namespace fs = std::filesystem;

namespace {

struct ExtractParams {
    std::string output_dir;
    std::string format = "jpg";      // Output format (jpg, png, bmp)
    int quality = 85;                // JPEG quality (1-100)
    int max_frames = 0;              // Maximum frames to extract (0 = all)
    int interval = 1;                // Extract every Nth keyframe
    bool thumbnails = false;         // Generate thumbnails
    int thumb_width = 160;           // Thumbnail width
    int thumb_height = 90;           // Thumbnail height
    bool info_file = false;          // Generate info file with timestamps
};

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <input> <output_dir> [options]\n\n", prog_name);
    std::cout << "Options:\n";
    std::cout << "  -f, --format <fmt>       Output format: jpg, png, bmp (default: jpg)\n";
    std::cout << "  -q, --quality <1-100>    JPEG quality (default: 85)\n";
    std::cout << "  -n, --max <count>        Maximum keyframes to extract (default: all)\n";
    std::cout << "  -i, --interval <n>       Extract every Nth keyframe (default: 1)\n";
    std::cout << "  --thumbnails             Also generate thumbnails (160x90)\n";
    std::cout << "  --thumb-size <WxH>       Thumbnail size (default: 160x90)\n";
    std::cout << "  --info                   Generate info file with timestamps\n\n";

    std::cout << "Examples:\n";
    std::cout << std::format("  {} video.mp4 keyframes\n", prog_name);
    std::cout << "    Extract all keyframes to 'keyframes' directory as JPEG\n\n";

    std::cout << std::format("  {} video.mp4 frames -f png -n 10\n", prog_name);
    std::cout << "    Extract first 10 keyframes as PNG\n\n";

    std::cout << std::format("  {} video.mp4 output -i 5 --thumbnails\n", prog_name);
    std::cout << "    Extract every 5th keyframe with thumbnails\n\n";

    std::cout << std::format("  {} video.mp4 frames --info -q 95\n", prog_name);
    std::cout << "    Extract keyframes with timestamp info, high quality\n\n";

    std::cout << "Notes:\n";
    std::cout << "  - Keyframes (I-frames) are full frames, not predicted\n";
    std::cout << "  - Useful for thumbnails, previews, and fast seeking\n";
    std::cout << "  - JPEG is smaller, PNG is lossless\n";
    std::cout << "  - Info file contains frame number and timestamp\n";
}

std::optional<ExtractParams> parse_arguments(int argc, char* argv[]) {
    if (argc < 3) {
        return std::nullopt;
    }

    ExtractParams params;
    params.output_dir = argv[2];

    for (int i = 3; i < argc; ++i) {
        const std::string_view arg = argv[i];

        if ((arg == "-f" || arg == "--format") && i + 1 < argc) {
            params.format = argv[++i];
            if (params.format != "jpg" && params.format != "png" && params.format != "bmp") {
                std::cerr << "Error: Format must be jpg, png, or bmp\n";
                return std::nullopt;
            }
        }
        else if ((arg == "-q" || arg == "--quality") && i + 1 < argc) {
            params.quality = std::stoi(argv[++i]);
            if (params.quality < 1 || params.quality > 100) {
                std::cerr << "Error: Quality must be between 1 and 100\n";
                return std::nullopt;
            }
        }
        else if ((arg == "-n" || arg == "--max") && i + 1 < argc) {
            params.max_frames = std::stoi(argv[++i]);
        }
        else if ((arg == "-i" || arg == "--interval") && i + 1 < argc) {
            params.interval = std::stoi(argv[++i]);
            if (params.interval < 1) {
                std::cerr << "Error: Interval must be at least 1\n";
                return std::nullopt;
            }
        }
        else if (arg == "--thumbnails") {
            params.thumbnails = true;
        }
        else if (arg == "--thumb-size" && i + 1 < argc) {
            const std::string size_str = argv[++i];
            const auto x_pos = size_str.find('x');
            if (x_pos != std::string::npos) {
                params.thumb_width = std::stoi(size_str.substr(0, x_pos));
                params.thumb_height = std::stoi(size_str.substr(x_pos + 1));
            } else {
                std::cerr << "Error: Invalid thumbnail size format. Use WxH\n";
                return std::nullopt;
            }
        }
        else if (arg == "--info") {
            params.info_file = true;
        }
        else {
            std::cerr << std::format("Error: Unknown option '{}'\n", arg);
            return std::nullopt;
        }
    }

    return params;
}

class VideoKeyframeExtractor {
public:
    VideoKeyframeExtractor(std::string_view input_file, const ExtractParams& params)
        : input_file_(input_file)
        , params_(params)
        , input_format_ctx_(ffmpeg::open_input_format(input_file.data()))
        , input_packet_(ffmpeg::create_packet())
        , input_frame_(ffmpeg::create_frame())
        , rgb_frame_(ffmpeg::create_frame()) {

        initialize();
    }

    void extract() {
        std::cout << "Keyframe Extraction\n";
        std::cout << "===================\n\n";
        std::cout << std::format("Input: {}\n", input_file_);
        std::cout << std::format("Output: {}/\n", params_.output_dir);
        std::cout << std::format("Format: {}\n", params_.format);
        std::cout << std::format("Interval: every {} keyframe(s)\n", params_.interval);
        if (params_.max_frames > 0) {
            std::cout << std::format("Max frames: {}\n", params_.max_frames);
        }
        std::cout << std::format("Thumbnails: {}\n", params_.thumbnails ? "enabled" : "disabled");
        std::cout << "\n";

        // Create output directory
        fs::create_directories(params_.output_dir);

        if (params_.thumbnails) {
            fs::create_directories(fs::path(params_.output_dir) / "thumbnails");
        }

        // Open info file if requested
        std::ofstream info_file;
        if (params_.info_file) {
            const auto info_path = fs::path(params_.output_dir) / "keyframes_info.txt";
            info_file.open(info_path);
            if (info_file.is_open()) {
                info_file << "Keyframe Extraction Information\n";
                info_file << std::format("Video: {}\n", input_file_);
                info_file << std::format("Format: {}\n\n", params_.format);
                info_file << "Frame_Number,Timestamp(s),Filename\n";
            }
        }

        int keyframe_count = 0;
        int extracted_count = 0;
        int frame_number = 0;

        auto* stream = input_format_ctx_->streams[video_stream_index_];

        while (av_read_frame(input_format_ctx_.get(), input_packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(input_packet_.get());

            if (input_packet_->stream_index != video_stream_index_) {
                continue;
            }

            if (avcodec_send_packet(input_codec_ctx_.get(), input_packet_.get()) < 0) {
                continue;
            }

            while (avcodec_receive_frame(input_codec_ctx_.get(), input_frame_.get()) >= 0) {
                ffmpeg::ScopedFrameUnref frame_guard(input_frame_.get());

                frame_number++;

                // Check if this is a keyframe (I-frame)
                if (input_frame_->flags & AV_FRAME_FLAG_KEY) {
                    keyframe_count++;

                    // Check interval
                    if (keyframe_count % params_.interval != 0) {
                        continue;
                    }

                    // Check max frames limit
                    if (params_.max_frames > 0 && extracted_count >= params_.max_frames) {
                        break;
                    }

                    // Calculate timestamp
                    const double timestamp = input_frame_->pts * av_q2d(stream->time_base);

                    // Save full-size frame
                    const std::string filename = std::format("keyframe_{:06d}.{}",
                                                            extracted_count + 1, params_.format);
                    const auto output_path = fs::path(params_.output_dir) / filename;

                    save_frame(input_frame_.get(), output_path.string(), false);

                    // Save thumbnail if requested
                    if (params_.thumbnails) {
                        const std::string thumb_filename = std::format("keyframe_{:06d}.{}",
                                                                      extracted_count + 1, params_.format);
                        const auto thumb_path = fs::path(params_.output_dir) / "thumbnails" / thumb_filename;

                        save_frame(input_frame_.get(), thumb_path.string(), true);
                    }

                    // Write to info file
                    if (info_file.is_open()) {
                        info_file << std::format("{},{:.3f},{}\n",
                                                frame_number, timestamp, filename);
                    }

                    extracted_count++;

                    if (extracted_count % 10 == 0) {
                        std::cout << std::format("\rExtracted {} keyframes", extracted_count) << std::flush;
                    }
                }
            }

            if (params_.max_frames > 0 && extracted_count >= params_.max_frames) {
                break;
            }
        }

        if (info_file.is_open()) {
            info_file.close();
        }

        std::cout << std::format("\n\nExtraction complete!\n");
        std::cout << std::format("Total keyframes found: {}\n", keyframe_count);
        std::cout << std::format("Keyframes extracted: {}\n", extracted_count);
        std::cout << std::format("Output directory: {}\n", params_.output_dir);

        if (params_.info_file) {
            std::cout << std::format("Info file: {}/keyframes_info.txt\n", params_.output_dir);
        }
    }

private:
    void initialize() {
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

        // Allocate RGB frame buffer
        rgb_frame_->format = AV_PIX_FMT_RGB24;
        rgb_frame_->width = input_codec_ctx_->width;
        rgb_frame_->height = input_codec_ctx_->height;
        av_frame_get_buffer(rgb_frame_.get(), 0);

        // Setup scaler for full-size
        sws_ctx_.reset(sws_getContext(
            input_codec_ctx_->width, input_codec_ctx_->height, input_codec_ctx_->pix_fmt,
            input_codec_ctx_->width, input_codec_ctx_->height, AV_PIX_FMT_RGB24,
            SWS_BILINEAR, nullptr, nullptr, nullptr));

        if (!sws_ctx_) {
            throw std::runtime_error("Failed to create scaler context");
        }

        // Setup scaler for thumbnails if needed
        if (params_.thumbnails) {
            thumb_frame_ = ffmpeg::create_frame();
            thumb_frame_->format = AV_PIX_FMT_RGB24;
            thumb_frame_->width = params_.thumb_width;
            thumb_frame_->height = params_.thumb_height;
            av_frame_get_buffer(thumb_frame_.get(), 0);

            thumb_sws_ctx_.reset(sws_getContext(
                input_codec_ctx_->width, input_codec_ctx_->height, input_codec_ctx_->pix_fmt,
                params_.thumb_width, params_.thumb_height, AV_PIX_FMT_RGB24,
                SWS_BILINEAR, nullptr, nullptr, nullptr));

            if (!thumb_sws_ctx_) {
                throw std::runtime_error("Failed to create thumbnail scaler context");
            }
        }
    }

    void save_frame(AVFrame* frame, const std::string& filename, bool thumbnail) {
        AVFrame* target_frame;
        SwsContext* sws_context;

        if (thumbnail && thumb_frame_) {
            target_frame = thumb_frame_.get();
            sws_context = thumb_sws_ctx_.get();
        } else {
            target_frame = rgb_frame_.get();
            sws_context = sws_ctx_.get();
        }

        // Convert to RGB
        sws_scale(sws_context,
                 frame->data, frame->linesize,
                 0, input_codec_ctx_->height,
                 target_frame->data, target_frame->linesize);

        // Save based on format
        if (params_.format == "jpg") {
            save_jpeg(target_frame, filename);
        } else if (params_.format == "png") {
            save_png(target_frame, filename);
        } else if (params_.format == "bmp") {
            save_bmp(target_frame, filename);
        }
    }

    void save_jpeg(AVFrame* frame, const std::string& filename) {
        // Setup JPEG encoder
        const auto* encoder = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
        if (!encoder) {
            std::cerr << "JPEG encoder not found\n";
            return;
        }

        auto codec_ctx = ffmpeg::create_codec_context(encoder);
        codec_ctx->pix_fmt = AV_PIX_FMT_YUVJ420P;
        codec_ctx->width = frame->width;
        codec_ctx->height = frame->height;
        codec_ctx->time_base = {1, 25};

        AVDictionary* opts = nullptr;
        av_dict_set(&opts, "qscale:v", std::to_string(31 - (params_.quality * 31 / 100)).c_str(), 0);

        if (avcodec_open2(codec_ctx.get(), encoder, &opts) < 0) {
            av_dict_free(&opts);
            return;
        }
        av_dict_free(&opts);

        // Convert RGB to YUV420P
        auto yuv_frame = ffmpeg::create_frame();
        yuv_frame->format = AV_PIX_FMT_YUVJ420P;
        yuv_frame->width = frame->width;
        yuv_frame->height = frame->height;
        av_frame_get_buffer(yuv_frame.get(), 0);

        auto yuv_sws_ctx = ffmpeg::SwsContextPtr(sws_getContext(
            frame->width, frame->height, AV_PIX_FMT_RGB24,
            frame->width, frame->height, AV_PIX_FMT_YUVJ420P,
            SWS_BILINEAR, nullptr, nullptr, nullptr));

        sws_scale(yuv_sws_ctx.get(),
                 frame->data, frame->linesize,
                 0, frame->height,
                 yuv_frame->data, yuv_frame->linesize);

        yuv_frame->pts = 0;

        // Encode
        auto packet = ffmpeg::create_packet();

        if (avcodec_send_frame(codec_ctx.get(), yuv_frame.get()) >= 0) {
            if (avcodec_receive_packet(codec_ctx.get(), packet.get()) >= 0) {
                // Write to file
                std::ofstream out(filename, std::ios::binary);
                if (out.is_open()) {
                    out.write(reinterpret_cast<const char*>(packet->data), packet->size);
                    out.close();
                }
            }
        }
    }

    void save_png(AVFrame* frame, const std::string& filename) {
        // Setup PNG encoder
        const auto* encoder = avcodec_find_encoder(AV_CODEC_ID_PNG);
        if (!encoder) {
            std::cerr << "PNG encoder not found\n";
            return;
        }

        auto codec_ctx = ffmpeg::create_codec_context(encoder);
        codec_ctx->pix_fmt = AV_PIX_FMT_RGB24;
        codec_ctx->width = frame->width;
        codec_ctx->height = frame->height;
        codec_ctx->time_base = {1, 25};

        if (avcodec_open2(codec_ctx.get(), encoder, nullptr) < 0) {
            return;
        }

        frame->pts = 0;

        auto packet = ffmpeg::create_packet();

        if (avcodec_send_frame(codec_ctx.get(), frame) >= 0) {
            if (avcodec_receive_packet(codec_ctx.get(), packet.get()) >= 0) {
                std::ofstream out(filename, std::ios::binary);
                if (out.is_open()) {
                    out.write(reinterpret_cast<const char*>(packet->data), packet->size);
                    out.close();
                }
            }
        }
    }

    void save_bmp(AVFrame* frame, const std::string& filename) {
        // Setup BMP encoder
        const auto* encoder = avcodec_find_encoder(AV_CODEC_ID_BMP);
        if (!encoder) {
            std::cerr << "BMP encoder not found\n";
            return;
        }

        auto codec_ctx = ffmpeg::create_codec_context(encoder);
        codec_ctx->pix_fmt = AV_PIX_FMT_RGB24;
        codec_ctx->width = frame->width;
        codec_ctx->height = frame->height;
        codec_ctx->time_base = {1, 25};

        if (avcodec_open2(codec_ctx.get(), encoder, nullptr) < 0) {
            return;
        }

        frame->pts = 0;

        auto packet = ffmpeg::create_packet();

        if (avcodec_send_frame(codec_ctx.get(), frame) >= 0) {
            if (avcodec_receive_packet(codec_ctx.get(), packet.get()) >= 0) {
                std::ofstream out(filename, std::ios::binary);
                if (out.is_open()) {
                    out.write(reinterpret_cast<const char*>(packet->data), packet->size);
                    out.close();
                }
            }
        }
    }

    std::string input_file_;
    ExtractParams params_;

    ffmpeg::FormatContextPtr input_format_ctx_;
    ffmpeg::CodecContextPtr input_codec_ctx_;
    ffmpeg::PacketPtr input_packet_;
    ffmpeg::FramePtr input_frame_;
    ffmpeg::FramePtr rgb_frame_;
    ffmpeg::FramePtr thumb_frame_;

    ffmpeg::SwsContextPtr sws_ctx_;
    ffmpeg::SwsContextPtr thumb_sws_ctx_;

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

        const auto params = parse_arguments(argc, argv);
        if (!params) {
            print_usage(argv[0]);
            return 1;
        }

        VideoKeyframeExtractor extractor(input, *params);
        extractor.extract();

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }
}
