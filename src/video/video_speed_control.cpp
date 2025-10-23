/**
 * Video Speed Control
 *
 * This sample demonstrates how to change video playback speed
 * (slow motion, fast forward) using FFmpeg filters with modern C++20.
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

struct SpeedParams {
    double video_speed = 1.0;    // Video speed multiplier (0.5 = half speed, 2.0 = double speed)
    double audio_speed = 1.0;    // Audio speed multiplier
    bool video_only = false;     // Process video only
    bool audio_only = false;     // Process audio only
};

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <input> <output> <speed> [options]\n\n", prog_name);
    std::cout << "Speed:\n";
    std::cout << "  <speed>          Speed multiplier (0.25 to 4.0)\n";
    std::cout << "                   0.5  = half speed (slow motion)\n";
    std::cout << "                   1.0  = normal speed\n";
    std::cout << "                   2.0  = double speed (fast forward)\n\n";

    std::cout << "Options:\n";
    std::cout << "  --video-only     Change video speed only (audio at normal speed)\n";
    std::cout << "  --audio-only     Change audio speed only (video at normal speed)\n";
    std::cout << "  --video <speed>  Set different video speed\n";
    std::cout << "  --audio <speed>  Set different audio speed\n\n";

    std::cout << "Examples:\n";
    std::cout << std::format("  {} input.mp4 output.mp4 0.5\n", prog_name);
    std::cout << "    Create slow-motion video at half speed\n\n";

    std::cout << std::format("  {} input.mp4 output.mp4 2.0\n", prog_name);
    std::cout << "    Create fast-forward video at double speed\n\n";

    std::cout << std::format("  {} input.mp4 output.mp4 1.0 --video 0.5 --audio 1.0\n", prog_name);
    std::cout << "    Slow motion video with normal audio speed\n\n";

    std::cout << "Notes:\n";
    std::cout << "  - Speed range: 0.25x to 4.0x\n";
    std::cout << "  - Audio quality may degrade at extreme speeds\n";
    std::cout << "  - File size increases with slower speeds\n";
}

std::optional<SpeedParams> parse_arguments(int argc, char* argv[], double base_speed) {
    SpeedParams params;
    params.video_speed = base_speed;
    params.audio_speed = base_speed;

    for (int i = 4; i < argc; ++i) {
        const std::string_view arg = argv[i];

        if (arg == "--video-only") {
            params.video_only = true;
            params.audio_speed = 1.0;
        }
        else if (arg == "--audio-only") {
            params.audio_only = true;
            params.video_speed = 1.0;
        }
        else if (arg == "--video" && i + 1 < argc) {
            params.video_speed = std::stod(argv[++i]);
        }
        else if (arg == "--audio" && i + 1 < argc) {
            params.audio_speed = std::stod(argv[++i]);
        }
        else {
            std::cerr << std::format("Error: Unknown option '{}'\n", arg);
            return std::nullopt;
        }
    }

    // Validate speed range
    if (params.video_speed < 0.25 || params.video_speed > 4.0 ||
        params.audio_speed < 0.25 || params.audio_speed > 4.0) {
        std::cerr << "Error: Speed must be between 0.25 and 4.0\n";
        return std::nullopt;
    }

    return params;
}

class VideoSpeedControl {
public:
    VideoSpeedControl(std::string_view input_file, std::string_view output_file,
                     const SpeedParams& params)
        : output_file_(output_file)
        , params_(params)
        , input_format_ctx_(ffmpeg::open_input_format(input_file.data()))
        , input_packet_(ffmpeg::create_packet()) {

        initialize();
    }

    void process() {
        std::cout << "Video Speed Control\n";
        std::cout << "===================\n\n";
        std::cout << std::format("Video Speed: {:.2f}x\n", params_.video_speed);
        std::cout << std::format("Audio Speed: {:.2f}x\n", params_.audio_speed);
        std::cout << std::format("Output: {}\n\n", output_file_);

        std::cout << "Processing...\n";

        int video_frame_count = 0;
        int audio_frame_count = 0;

        while (av_read_frame(input_format_ctx_.get(), input_packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(input_packet_.get());

            if (has_video_ && input_packet_->stream_index == video_stream_index_) {
                process_video_packet(video_frame_count);
            }
            else if (has_audio_ && input_packet_->stream_index == audio_stream_index_) {
                process_audio_packet(audio_frame_count);
            }
        }

        // Flush decoders and encoders
        flush_video(video_frame_count);
        flush_audio(audio_frame_count);

        av_write_trailer(output_format_ctx_.get());

        std::cout << std::format("\nProcessing complete!\n");
        std::cout << std::format("Video frames: {}\n", video_frame_count);
        std::cout << std::format("Audio frames: {}\n", audio_frame_count);
        std::cout << std::format("Output: {}\n", output_file_);
    }

private:
    void initialize() {
        // Find streams
        video_stream_index_ = av_find_best_stream(
            input_format_ctx_.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        audio_stream_index_ = av_find_best_stream(
            input_format_ctx_.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

        has_video_ = video_stream_index_ >= 0;
        has_audio_ = audio_stream_index_ >= 0;

        if (!has_video_ && !has_audio_) {
            throw std::runtime_error("No video or audio stream found");
        }

        // Setup output format
        avformat_alloc_output_context2(&output_format_ctx_raw_, nullptr,
                                       nullptr, output_file_.c_str());
        if (!output_format_ctx_raw_) {
            throw std::runtime_error("Failed to create output format context");
        }
        output_format_ctx_.reset(output_format_ctx_raw_);

        // Initialize video if present
        if (has_video_) {
            setup_video();
        }

        // Initialize audio if present
        if (has_audio_) {
            setup_audio();
        }

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
    }

    void setup_video() {
        auto* input_stream = input_format_ctx_->streams[video_stream_index_];

        // Setup decoder
        const auto* decoder = avcodec_find_decoder(input_stream->codecpar->codec_id);
        if (!decoder) {
            throw std::runtime_error("Failed to find video decoder");
        }

        video_dec_ctx_ = ffmpeg::create_codec_context(decoder);
        avcodec_parameters_to_context(video_dec_ctx_.get(), input_stream->codecpar);

        if (avcodec_open2(video_dec_ctx_.get(), decoder, nullptr) < 0) {
            throw std::runtime_error("Failed to open video decoder");
        }

        // Setup filter graph
        setup_video_filter();

        // Setup encoder
        const auto* encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!encoder) {
            throw std::runtime_error("H.264 encoder not found");
        }

        video_enc_ctx_ = ffmpeg::create_codec_context(encoder);

        const auto* buffersink_params =
            reinterpret_cast<AVFilterContext*>(video_buffersink_ctx_)->inputs[0];

        video_enc_ctx_->width = buffersink_params->w;
        video_enc_ctx_->height = buffersink_params->h;
        video_enc_ctx_->pix_fmt = static_cast<AVPixelFormat>(buffersink_params->format);
        video_enc_ctx_->time_base = av_inv_q(av_buffersink_get_frame_rate(video_buffersink_ctx_));
        video_enc_ctx_->bit_rate = video_dec_ctx_->bit_rate;

        if (output_format_ctx_->oformat->flags & AVFMT_GLOBALHEADER) {
            video_enc_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        if (avcodec_open2(video_enc_ctx_.get(), encoder, nullptr) < 0) {
            throw std::runtime_error("Failed to open video encoder");
        }

        // Create output stream
        auto* out_stream = avformat_new_stream(output_format_ctx_.get(), nullptr);
        if (!out_stream) {
            throw std::runtime_error("Failed to create video output stream");
        }

        avcodec_parameters_from_context(out_stream->codecpar, video_enc_ctx_.get());
        out_stream->time_base = video_enc_ctx_->time_base;
        video_output_stream_index_ = out_stream->index;

        video_input_frame_ = ffmpeg::create_frame();
        video_filtered_frame_ = ffmpeg::create_frame();
        video_output_packet_ = ffmpeg::create_packet();
    }

    void setup_video_filter() {
        video_filter_graph_.reset(avfilter_graph_alloc());
        if (!video_filter_graph_) {
            throw std::runtime_error("Failed to allocate video filter graph");
        }

        // Create buffer source
        const auto* buffersrc = avfilter_get_by_name("buffer");
        if (!buffersrc) {
            throw std::runtime_error("Failed to find buffer filter");
        }

        const std::string args = std::format(
            "video_size={}x{}:pix_fmt={}:time_base={}/{}:pixel_aspect={}/{}",
            video_dec_ctx_->width, video_dec_ctx_->height,
            static_cast<int>(video_dec_ctx_->pix_fmt),
            video_dec_ctx_->time_base.num, video_dec_ctx_->time_base.den,
            video_dec_ctx_->sample_aspect_ratio.num,
            video_dec_ctx_->sample_aspect_ratio.den);

        if (avfilter_graph_create_filter(&video_buffersrc_ctx_, buffersrc, "in",
                                         args.c_str(), nullptr, video_filter_graph_.get()) < 0) {
            throw std::runtime_error("Failed to create video buffer source");
        }

        // Create buffer sink
        const auto* buffersink = avfilter_get_by_name("buffersink");
        if (!buffersink) {
            throw std::runtime_error("Failed to find buffersink filter");
        }

        if (avfilter_graph_create_filter(&video_buffersink_ctx_, buffersink, "out",
                                         nullptr, nullptr, video_filter_graph_.get()) < 0) {
            throw std::runtime_error("Failed to create video buffer sink");
        }

        // Build filter description (setpts for speed control)
        // setpts=PTS/speed (e.g., PTS/2 = double speed, PTS*2 = half speed)
        const std::string filter_desc = std::format("setpts={}*PTS", 1.0 / params_.video_speed);

        std::cout << std::format("Video filter: {}\n", filter_desc);

        // Parse filter description
        AVFilterInOut* outputs = avfilter_inout_alloc();
        AVFilterInOut* inputs = avfilter_inout_alloc();

        outputs->name = av_strdup("in");
        outputs->filter_ctx = video_buffersrc_ctx_;
        outputs->pad_idx = 0;
        outputs->next = nullptr;

        inputs->name = av_strdup("out");
        inputs->filter_ctx = video_buffersink_ctx_;
        inputs->pad_idx = 0;
        inputs->next = nullptr;

        if (avfilter_graph_parse_ptr(video_filter_graph_.get(), filter_desc.c_str(),
                                     &inputs, &outputs, nullptr) < 0) {
            avfilter_inout_free(&inputs);
            avfilter_inout_free(&outputs);
            throw std::runtime_error("Failed to parse video filter graph");
        }

        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);

        if (avfilter_graph_config(video_filter_graph_.get(), nullptr) < 0) {
            throw std::runtime_error("Failed to configure video filter graph");
        }
    }

    void setup_audio() {
        auto* input_stream = input_format_ctx_->streams[audio_stream_index_];

        // Setup decoder
        const auto* decoder = avcodec_find_decoder(input_stream->codecpar->codec_id);
        if (!decoder) {
            throw std::runtime_error("Failed to find audio decoder");
        }

        audio_dec_ctx_ = ffmpeg::create_codec_context(decoder);
        avcodec_parameters_to_context(audio_dec_ctx_.get(), input_stream->codecpar);

        if (avcodec_open2(audio_dec_ctx_.get(), decoder, nullptr) < 0) {
            throw std::runtime_error("Failed to open audio decoder");
        }

        // Setup filter graph
        setup_audio_filter();

        // Setup encoder
        const auto* encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!encoder) {
            throw std::runtime_error("AAC encoder not found");
        }

        audio_enc_ctx_ = ffmpeg::create_codec_context(encoder);

        audio_enc_ctx_->sample_rate = av_buffersink_get_sample_rate(audio_buffersink_ctx_);
        if (av_buffersink_get_ch_layout(audio_buffersink_ctx_, &audio_enc_ctx_->ch_layout) < 0) {
            throw std::runtime_error("Failed to get audio channel layout");
        }
        audio_enc_ctx_->sample_fmt = static_cast<AVSampleFormat>(
            av_buffersink_get_format(audio_buffersink_ctx_));
        audio_enc_ctx_->time_base = {1, audio_enc_ctx_->sample_rate};
        audio_enc_ctx_->bit_rate = 128000;

        if (output_format_ctx_->oformat->flags & AVFMT_GLOBALHEADER) {
            audio_enc_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        if (avcodec_open2(audio_enc_ctx_.get(), encoder, nullptr) < 0) {
            throw std::runtime_error("Failed to open audio encoder");
        }

        // Create output stream
        auto* out_stream = avformat_new_stream(output_format_ctx_.get(), nullptr);
        if (!out_stream) {
            throw std::runtime_error("Failed to create audio output stream");
        }

        avcodec_parameters_from_context(out_stream->codecpar, audio_enc_ctx_.get());
        out_stream->time_base = audio_enc_ctx_->time_base;
        audio_output_stream_index_ = out_stream->index;

        audio_input_frame_ = ffmpeg::create_frame();
        audio_filtered_frame_ = ffmpeg::create_frame();
        audio_output_packet_ = ffmpeg::create_packet();
    }

    void setup_audio_filter() {
        audio_filter_graph_.reset(avfilter_graph_alloc());
        if (!audio_filter_graph_) {
            throw std::runtime_error("Failed to allocate audio filter graph");
        }

        // Create buffer source
        const auto* buffersrc = avfilter_get_by_name("abuffer");
        if (!buffersrc) {
            throw std::runtime_error("Failed to find abuffer filter");
        }

        char ch_layout_str[64];
        av_channel_layout_describe(&audio_dec_ctx_->ch_layout, ch_layout_str, sizeof(ch_layout_str));

        const std::string args = std::format(
            "time_base={}/{}:sample_rate={}:sample_fmt={}:channel_layout={}",
            audio_dec_ctx_->time_base.num, audio_dec_ctx_->time_base.den,
            audio_dec_ctx_->sample_rate,
            av_get_sample_fmt_name(audio_dec_ctx_->sample_fmt),
            ch_layout_str);

        if (avfilter_graph_create_filter(&audio_buffersrc_ctx_, buffersrc, "in",
                                         args.c_str(), nullptr, audio_filter_graph_.get()) < 0) {
            throw std::runtime_error("Failed to create audio buffer source");
        }

        // Create buffer sink
        const auto* buffersink = avfilter_get_by_name("abuffersink");
        if (!buffersink) {
            throw std::runtime_error("Failed to find abuffersink filter");
        }

        if (avfilter_graph_create_filter(&audio_buffersink_ctx_, buffersink, "out",
                                         nullptr, nullptr, audio_filter_graph_.get()) < 0) {
            throw std::runtime_error("Failed to create audio buffer sink");
        }

        // Build filter description (atempo for speed control)
        // atempo has range 0.5 to 2.0, so we need to chain multiple filters for larger changes
        std::string filter_desc = build_atempo_filter(params_.audio_speed);

        std::cout << std::format("Audio filter: {}\n", filter_desc);

        // Parse filter description
        AVFilterInOut* outputs = avfilter_inout_alloc();
        AVFilterInOut* inputs = avfilter_inout_alloc();

        outputs->name = av_strdup("in");
        outputs->filter_ctx = audio_buffersrc_ctx_;
        outputs->pad_idx = 0;
        outputs->next = nullptr;

        inputs->name = av_strdup("out");
        inputs->filter_ctx = audio_buffersink_ctx_;
        inputs->pad_idx = 0;
        inputs->next = nullptr;

        if (avfilter_graph_parse_ptr(audio_filter_graph_.get(), filter_desc.c_str(),
                                     &inputs, &outputs, nullptr) < 0) {
            avfilter_inout_free(&inputs);
            avfilter_inout_free(&outputs);
            throw std::runtime_error("Failed to parse audio filter graph");
        }

        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);

        if (avfilter_graph_config(audio_filter_graph_.get(), nullptr) < 0) {
            throw std::runtime_error("Failed to configure audio filter graph");
        }
    }

    std::string build_atempo_filter(double speed) {
        if (std::abs(speed - 1.0) < 0.01) {
            return "anull";  // No change
        }

        // atempo supports 0.5 to 2.0 range
        // For values outside this range, chain multiple atempo filters
        std::string filter;
        double remaining = speed;

        while (remaining > 2.0) {
            if (!filter.empty()) filter += ",";
            filter += "atempo=2.0";
            remaining /= 2.0;
        }

        while (remaining < 0.5) {
            if (!filter.empty()) filter += ",";
            filter += "atempo=0.5";
            remaining /= 0.5;
        }

        if (std::abs(remaining - 1.0) >= 0.01) {
            if (!filter.empty()) filter += ",";
            filter += std::format("atempo={:.3f}", remaining);
        }

        return filter.empty() ? "anull" : filter;
    }

    void process_video_packet(int& frame_count) {
        if (avcodec_send_packet(video_dec_ctx_.get(), input_packet_.get()) < 0) {
            return;
        }

        while (avcodec_receive_frame(video_dec_ctx_.get(), video_input_frame_.get()) >= 0) {
            ffmpeg::ScopedFrameUnref frame_guard(video_input_frame_.get());

            // Push frame to filter
            if (av_buffersrc_add_frame_flags(video_buffersrc_ctx_, video_input_frame_.get(),
                                            AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                continue;
            }

            // Pull filtered frames
            while (av_buffersink_get_frame(video_buffersink_ctx_, video_filtered_frame_.get()) >= 0) {
                ffmpeg::ScopedFrameUnref filtered_guard(video_filtered_frame_.get());

                encode_video_frame();
                frame_count++;

                if (frame_count % 30 == 0) {
                    std::cout << std::format("\rVideo frames: {}", frame_count) << std::flush;
                }
            }
        }
    }

    void process_audio_packet(int& frame_count) {
        if (avcodec_send_packet(audio_dec_ctx_.get(), input_packet_.get()) < 0) {
            return;
        }

        while (avcodec_receive_frame(audio_dec_ctx_.get(), audio_input_frame_.get()) >= 0) {
            ffmpeg::ScopedFrameUnref frame_guard(audio_input_frame_.get());

            // Push frame to filter
            if (av_buffersrc_add_frame_flags(audio_buffersrc_ctx_, audio_input_frame_.get(),
                                            AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                continue;
            }

            // Pull filtered frames
            while (av_buffersink_get_frame(audio_buffersink_ctx_, audio_filtered_frame_.get()) >= 0) {
                ffmpeg::ScopedFrameUnref filtered_guard(audio_filtered_frame_.get());

                encode_audio_frame();
                frame_count++;
            }
        }
    }

    void encode_video_frame() {
        if (avcodec_send_frame(video_enc_ctx_.get(), video_filtered_frame_.get()) < 0) {
            return;
        }

        while (avcodec_receive_packet(video_enc_ctx_.get(), video_output_packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(video_output_packet_.get());

            video_output_packet_->stream_index = video_output_stream_index_;
            av_packet_rescale_ts(video_output_packet_.get(),
                               video_enc_ctx_->time_base,
                               output_format_ctx_->streams[video_output_stream_index_]->time_base);

            av_interleaved_write_frame(output_format_ctx_.get(), video_output_packet_.get());
        }
    }

    void encode_audio_frame() {
        if (avcodec_send_frame(audio_enc_ctx_.get(), audio_filtered_frame_.get()) < 0) {
            return;
        }

        while (avcodec_receive_packet(audio_enc_ctx_.get(), audio_output_packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(audio_output_packet_.get());

            audio_output_packet_->stream_index = audio_output_stream_index_;
            av_packet_rescale_ts(audio_output_packet_.get(),
                               audio_enc_ctx_->time_base,
                               output_format_ctx_->streams[audio_output_stream_index_]->time_base);

            av_interleaved_write_frame(output_format_ctx_.get(), audio_output_packet_.get());
        }
    }

    void flush_video(int& frame_count) {
        if (!has_video_) return;

        // Flush decoder
        avcodec_send_packet(video_dec_ctx_.get(), nullptr);
        while (avcodec_receive_frame(video_dec_ctx_.get(), video_input_frame_.get()) >= 0) {
            ffmpeg::ScopedFrameUnref frame_guard(video_input_frame_.get());

            if (av_buffersrc_add_frame_flags(video_buffersrc_ctx_, video_input_frame_.get(),
                                            AV_BUFFERSRC_FLAG_KEEP_REF) >= 0) {
                while (av_buffersink_get_frame(video_buffersink_ctx_, video_filtered_frame_.get()) >= 0) {
                    ffmpeg::ScopedFrameUnref filtered_guard(video_filtered_frame_.get());
                    encode_video_frame();
                    frame_count++;
                }
            }
        }

        // Flush filter
        if (av_buffersrc_add_frame_flags(video_buffersrc_ctx_, nullptr, 0) >= 0) {
            while (av_buffersink_get_frame(video_buffersink_ctx_, video_filtered_frame_.get()) >= 0) {
                ffmpeg::ScopedFrameUnref filtered_guard(video_filtered_frame_.get());
                encode_video_frame();
                frame_count++;
            }
        }

        // Flush encoder
        avcodec_send_frame(video_enc_ctx_.get(), nullptr);
        while (avcodec_receive_packet(video_enc_ctx_.get(), video_output_packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(video_output_packet_.get());

            video_output_packet_->stream_index = video_output_stream_index_;
            av_packet_rescale_ts(video_output_packet_.get(),
                               video_enc_ctx_->time_base,
                               output_format_ctx_->streams[video_output_stream_index_]->time_base);

            av_interleaved_write_frame(output_format_ctx_.get(), video_output_packet_.get());
        }
    }

    void flush_audio(int& frame_count) {
        if (!has_audio_) return;

        // Flush decoder
        avcodec_send_packet(audio_dec_ctx_.get(), nullptr);
        while (avcodec_receive_frame(audio_dec_ctx_.get(), audio_input_frame_.get()) >= 0) {
            ffmpeg::ScopedFrameUnref frame_guard(audio_input_frame_.get());

            if (av_buffersrc_add_frame_flags(audio_buffersrc_ctx_, audio_input_frame_.get(),
                                            AV_BUFFERSRC_FLAG_KEEP_REF) >= 0) {
                while (av_buffersink_get_frame(audio_buffersink_ctx_, audio_filtered_frame_.get()) >= 0) {
                    ffmpeg::ScopedFrameUnref filtered_guard(audio_filtered_frame_.get());
                    encode_audio_frame();
                    frame_count++;
                }
            }
        }

        // Flush filter
        if (av_buffersrc_add_frame_flags(audio_buffersrc_ctx_, nullptr, 0) >= 0) {
            while (av_buffersink_get_frame(audio_buffersink_ctx_, audio_filtered_frame_.get()) >= 0) {
                ffmpeg::ScopedFrameUnref filtered_guard(audio_filtered_frame_.get());
                encode_audio_frame();
                frame_count++;
            }
        }

        // Flush encoder
        avcodec_send_frame(audio_enc_ctx_.get(), nullptr);
        while (avcodec_receive_packet(audio_enc_ctx_.get(), audio_output_packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(audio_output_packet_.get());

            audio_output_packet_->stream_index = audio_output_stream_index_;
            av_packet_rescale_ts(audio_output_packet_.get(),
                               audio_enc_ctx_->time_base,
                               output_format_ctx_->streams[audio_output_stream_index_]->time_base);

            av_interleaved_write_frame(output_format_ctx_.get(), audio_output_packet_.get());
        }
    }

    std::string output_file_;
    SpeedParams params_;

    ffmpeg::FormatContextPtr input_format_ctx_;
    ffmpeg::PacketPtr input_packet_;

    // Video
    bool has_video_ = false;
    int video_stream_index_ = -1;
    int video_output_stream_index_ = -1;
    ffmpeg::CodecContextPtr video_dec_ctx_;
    ffmpeg::CodecContextPtr video_enc_ctx_;
    ffmpeg::FramePtr video_input_frame_;
    ffmpeg::FramePtr video_filtered_frame_;
    ffmpeg::PacketPtr video_output_packet_;
    ffmpeg::FilterGraphPtr video_filter_graph_;
    AVFilterContext* video_buffersrc_ctx_ = nullptr;
    AVFilterContext* video_buffersink_ctx_ = nullptr;

    // Audio
    bool has_audio_ = false;
    int audio_stream_index_ = -1;
    int audio_output_stream_index_ = -1;
    ffmpeg::CodecContextPtr audio_dec_ctx_;
    ffmpeg::CodecContextPtr audio_enc_ctx_;
    ffmpeg::FramePtr audio_input_frame_;
    ffmpeg::FramePtr audio_filtered_frame_;
    ffmpeg::PacketPtr audio_output_packet_;
    ffmpeg::FilterGraphPtr audio_filter_graph_;
    AVFilterContext* audio_buffersrc_ctx_ = nullptr;
    AVFilterContext* audio_buffersink_ctx_ = nullptr;

    // Output
    AVFormatContext* output_format_ctx_raw_ = nullptr;
    ffmpeg::FormatContextPtr output_format_ctx_;
};

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 4) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        const std::string input = argv[1];
        const std::string output = argv[2];
        const double speed = std::stod(argv[3]);

        if (speed < 0.25 || speed > 4.0) {
            std::cerr << "Error: Speed must be between 0.25 and 4.0\n";
            return 1;
        }

        const auto params = parse_arguments(argc, argv, speed);
        if (!params) {
            print_usage(argv[0]);
            return 1;
        }

        VideoSpeedControl processor(input, output, *params);
        processor.process();

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }
}
