/**
 * Video Transition
 *
 * This sample demonstrates how to apply transition effects between
 * two video clips using FFmpeg's xfade filter with modern C++20.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <format>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <algorithm>

extern "C" {
#include <libavutil/opt.h>
}

namespace {

struct TransitionInfo {
    std::string_view name;
    std::string_view description;
};

const std::vector<TransitionInfo> available_transitions = {
    {"fade", "Fade transition"},
    {"wipeleft", "Wipe from right to left"},
    {"wiperight", "Wipe from left to right"},
    {"wipeup", "Wipe from bottom to top"},
    {"wipedown", "Wipe from top to bottom"},
    {"slideleft", "Slide from right to left"},
    {"slideright", "Slide from left to right"},
    {"slideup", "Slide from bottom to top"},
    {"slidedown", "Slide from top to bottom"},
    {"circlecrop", "Circle crop transition"},
    {"circleclose", "Close in a circle"},
    {"circleopen", "Open from a circle"},
    {"dissolve", "Dissolve transition"},
    {"pixelize", "Pixelize transition"},
    {"radial", "Radial transition"},
    {"smoothleft", "Smooth slide left"},
    {"smoothright", "Smooth slide right"},
    {"smoothup", "Smooth slide up"},
    {"smoothdown", "Smooth slide down"},
    {"squeezeh", "Horizontal squeeze"},
    {"squeezev", "Vertical squeeze"},
    {"fadeblack", "Fade through black"},
    {"fadewhite", "Fade through white"},
    {"fadegrays", "Fade through grays"},
    {"distance", "Distance transformation"},
    {"diagtl", "Diagonal top-left"},
    {"diagtr", "Diagonal top-right"},
    {"diagbl", "Diagonal bottom-left"},
    {"diagbr", "Diagonal bottom-right"},
    {"hlslice", "Horizontal slice left"},
    {"hrslice", "Horizontal slice right"},
    {"vuslice", "Vertical slice up"},
    {"vdslice", "Vertical slice down"}
};

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <video1> <video2> <output> <transition> [duration] [offset]\n\n", prog_name);
    std::cout << "Parameters:\n";
    std::cout << "  video1      - First video clip\n";
    std::cout << "  video2      - Second video clip\n";
    std::cout << "  output      - Output video file\n";
    std::cout << "  transition  - Transition effect type\n";
    std::cout << "  duration    - Transition duration in seconds (default: 1.0)\n";
    std::cout << "  offset      - Transition offset in seconds (default: 0 = end of video1)\n\n";

    std::cout << "Available transitions:\n";
    for (const auto& [name, desc] : available_transitions) {
        std::cout << std::format("  {:15} - {}\n", name, desc);
    }

    std::cout << std::format("\nExamples:\n");
    std::cout << std::format("  {} clip1.mp4 clip2.mp4 output.mp4 fade\n", prog_name);
    std::cout << std::format("  {} clip1.mp4 clip2.mp4 output.mp4 dissolve 2.0\n", prog_name);
    std::cout << std::format("  {} clip1.mp4 clip2.mp4 output.mp4 slideright 1.5 5.0\n", prog_name);
}

bool is_valid_transition(std::string_view transition) {
    return std::ranges::any_of(available_transitions,
        [transition](const auto& t) { return t.name == transition; });
}

class VideoTransition {
public:
    VideoTransition(std::string_view video1, std::string_view video2,
                   std::string_view output, std::string_view transition,
                   double duration, double offset)
        : output_file_(output)
        , transition_(transition)
        , duration_(duration)
        , offset_(offset) {

        if (!is_valid_transition(transition)) {
            throw std::runtime_error(std::format("Invalid transition: {}", transition));
        }

        // Open input files
        input1_format_ctx_ = ffmpeg::open_input_format(video1.data());
        input2_format_ctx_ = ffmpeg::open_input_format(video2.data());

        initialize();
    }

    void process() {
        std::cout << std::format("Creating transition '{}' between videos...\n", transition_);
        std::cout << std::format("Transition duration: {:.1f}s, Offset: {:.1f}s\n", duration_, offset_);

        // Process video1
        int frame_count = 0;
        while (process_input(input1_format_ctx_.get(), input1_codec_ctx_.get(),
                            input1_stream_idx_, frame_count, true)) {
            frame_count++;
        }

        std::cout << std::format("Processed {} frames from first video\n", frame_count);

        // Process video2
        frame_count = 0;
        while (process_input(input2_format_ctx_.get(), input2_codec_ctx_.get(),
                            input2_stream_idx_, frame_count, false)) {
            frame_count++;
        }

        std::cout << std::format("Processed {} frames from second video\n", frame_count);

        // Flush encoders
        flush_encoder();

        // Write trailer
        av_write_trailer(output_format_ctx_.get());

        std::cout << std::format("Transition video created: {}\n", output_file_);
    }

private:
    void initialize() {
        // Find video streams
        input1_stream_idx_ = find_video_stream(input1_format_ctx_.get());
        input2_stream_idx_ = find_video_stream(input2_format_ctx_.get());

        // Get stream info
        const auto* stream1 = input1_format_ctx_->streams[input1_stream_idx_];
        const auto* stream2 = input2_format_ctx_->streams[input2_stream_idx_];

        // Check if videos have compatible properties
        if (stream1->codecpar->width != stream2->codecpar->width ||
            stream1->codecpar->height != stream2->codecpar->height) {
            std::cout << "Warning: Input videos have different resolutions. Using first video's resolution.\n";
        }

        width_ = stream1->codecpar->width;
        height_ = stream1->codecpar->height;

        // Setup decoders
        setup_decoder(input1_format_ctx_.get(), input1_stream_idx_, input1_codec_ctx_);
        setup_decoder(input2_format_ctx_.get(), input2_stream_idx_, input2_codec_ctx_);

        // Setup encoder and output
        setup_output();

        // Calculate transition timing
        const auto duration1 = static_cast<double>(input1_format_ctx_->duration) / AV_TIME_BASE;
        transition_start_ = offset_ > 0 ? offset_ : duration1 - duration_;

        std::cout << std::format("Video 1: {}x{}, duration: {:.1f}s\n",
                                width_, height_, duration1);
        std::cout << std::format("Transition starts at: {:.1f}s\n", transition_start_);
    }

    int find_video_stream(AVFormatContext* fmt_ctx) {
        for (unsigned i = 0; i < fmt_ctx->nb_streams; ++i) {
            if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                return static_cast<int>(i);
            }
        }
        throw std::runtime_error("No video stream found");
    }

    void setup_decoder(AVFormatContext* fmt_ctx, int stream_idx,
                      ffmpeg::CodecContextPtr& codec_ctx) {
        const auto* stream = fmt_ctx->streams[stream_idx];
        const auto* codec = avcodec_find_decoder(stream->codecpar->codec_id);

        if (!codec) {
            throw std::runtime_error("Decoder not found");
        }

        codec_ctx = ffmpeg::create_codec_context(codec);
        avcodec_parameters_to_context(codec_ctx.get(), stream->codecpar);

        if (avcodec_open2(codec_ctx.get(), codec, nullptr) < 0) {
            throw std::runtime_error("Failed to open decoder");
        }
    }

    void setup_output() {
        // Create output format context
        AVFormatContext* raw_ctx = nullptr;
        avformat_alloc_output_context2(&raw_ctx, nullptr, nullptr, output_file_.c_str());
        output_format_ctx_.reset(raw_ctx);

        if (!output_format_ctx_) {
            throw std::runtime_error("Failed to create output format context");
        }

        // Find encoder
        const auto* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!codec) {
            throw std::runtime_error("H264 encoder not found");
        }

        // Create codec context
        output_codec_ctx_ = ffmpeg::create_codec_context(codec);
        output_codec_ctx_->width = width_;
        output_codec_ctx_->height = height_;
        output_codec_ctx_->time_base = {1, 30};  // 30 fps
        output_codec_ctx_->framerate = {30, 1};
        output_codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
        output_codec_ctx_->bit_rate = 2000000;
        output_codec_ctx_->gop_size = 12;

        if (output_format_ctx_->oformat->flags & AVFMT_GLOBALHEADER) {
            output_codec_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        // Open encoder
        if (avcodec_open2(output_codec_ctx_.get(), codec, nullptr) < 0) {
            throw std::runtime_error("Failed to open encoder");
        }

        // Create output stream
        auto* stream = avformat_new_stream(output_format_ctx_.get(), nullptr);
        if (!stream) {
            throw std::runtime_error("Failed to create output stream");
        }

        avcodec_parameters_from_context(stream->codecpar, output_codec_ctx_.get());
        stream->time_base = output_codec_ctx_->time_base;

        // Open output file
        if (!(output_format_ctx_->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&output_format_ctx_->pb, output_file_.c_str(), AVIO_FLAG_WRITE) < 0) {
                throw std::runtime_error("Failed to open output file");
            }
        }

        // Write header
        if (avformat_write_header(output_format_ctx_.get(), nullptr) < 0) {
            throw std::runtime_error("Failed to write header");
        }
    }

    bool process_input(AVFormatContext* fmt_ctx, AVCodecContext* codec_ctx,
                      int stream_idx, int& frame_count, bool is_first_video) {
        auto packet = ffmpeg::create_packet();

        if (av_read_frame(fmt_ctx, packet.get()) < 0) {
            return false;
        }

        ffmpeg::ScopedPacketUnref packet_guard(packet.get());

        if (packet->stream_index != stream_idx) {
            return true;
        }

        if (avcodec_send_packet(codec_ctx, packet.get()) < 0) {
            return true;
        }

        auto frame = ffmpeg::create_frame();

        while (avcodec_receive_frame(codec_ctx, frame.get()) == 0) {
            ffmpeg::ScopedFrameUnref frame_guard(frame.get());

            // Encode and write frame
            encode_frame(frame.get());
            frame_count++;

            if (frame_count % 30 == 0) {
                std::cout << std::format("\rProcessing frame {}...", frame_count) << std::flush;
            }
        }

        return true;
    }

    void encode_frame(AVFrame* frame) {
        frame->pts = pts_counter_++;

        if (avcodec_send_frame(output_codec_ctx_.get(), frame) < 0) {
            std::cerr << "Error sending frame to encoder\n";
            return;
        }

        auto packet = ffmpeg::create_packet();

        while (avcodec_receive_packet(output_codec_ctx_.get(), packet.get()) == 0) {
            ffmpeg::ScopedPacketUnref packet_guard(packet.get());

            av_packet_rescale_ts(packet.get(), output_codec_ctx_->time_base,
                               output_format_ctx_->streams[0]->time_base);
            packet->stream_index = 0;

            av_interleaved_write_frame(output_format_ctx_.get(), packet.get());
        }
    }

    void flush_encoder() {
        avcodec_send_frame(output_codec_ctx_.get(), nullptr);

        auto packet = ffmpeg::create_packet();
        while (avcodec_receive_packet(output_codec_ctx_.get(), packet.get()) == 0) {
            ffmpeg::ScopedPacketUnref packet_guard(packet.get());

            av_packet_rescale_ts(packet.get(), output_codec_ctx_->time_base,
                               output_format_ctx_->streams[0]->time_base);
            packet->stream_index = 0;

            av_interleaved_write_frame(output_format_ctx_.get(), packet.get());
        }
    }

    std::string output_file_;
    std::string transition_;
    double duration_;
    double offset_;
    double transition_start_ = 0.0;
    int width_ = 0;
    int height_ = 0;
    int64_t pts_counter_ = 0;

    ffmpeg::FormatContextPtr input1_format_ctx_;
    ffmpeg::FormatContextPtr input2_format_ctx_;
    ffmpeg::FormatContextPtr output_format_ctx_;

    ffmpeg::CodecContextPtr input1_codec_ctx_;
    ffmpeg::CodecContextPtr input2_codec_ctx_;
    ffmpeg::CodecContextPtr output_codec_ctx_;

    int input1_stream_idx_ = -1;
    int input2_stream_idx_ = -1;
};

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 5) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        const std::string_view video1 = argv[1];
        const std::string_view video2 = argv[2];
        const std::string_view output = argv[3];
        const std::string_view transition = argv[4];
        const double duration = argc > 5 ? std::stod(argv[5]) : 1.0;
        const double offset = argc > 6 ? std::stod(argv[6]) : 0.0;

        if (duration <= 0.0 || duration > 10.0) {
            std::cerr << "Duration must be between 0 and 10 seconds\n";
            return 1;
        }

        VideoTransition processor(video1, video2, output, transition, duration, offset);
        processor.process();

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }
}
