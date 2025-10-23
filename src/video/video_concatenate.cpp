/**
 * Video Concatenate
 *
 * This sample demonstrates how to concatenate multiple video files
 * into a single output file using FFmpeg with modern C++20.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <format>
#include <string_view>
#include <vector>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <output> <video1> <video2> [video3...]\n\n", prog_name);
    std::cout << "Parameters:\n";
    std::cout << "  output   - Output video file\n";
    std::cout << "  video1+  - Two or more input video files to concatenate\n\n";
    std::cout << "Examples:\n";
    std::cout << std::format("  {} output.mp4 clip1.mp4 clip2.mp4\n", prog_name);
    std::cout << std::format("  {} final.mp4 intro.mp4 main.mp4 outro.mp4\n", prog_name);
    std::cout << "\nNote: All videos will be re-encoded to match the first video's format.\n";
}

class VideoConcatenate {
public:
    VideoConcatenate(std::string_view output, const std::vector<std::string>& inputs)
        : output_file_(output)
        , input_files_(inputs) {

        if (inputs.empty()) {
            throw std::runtime_error("No input files provided");
        }

        std::cout << std::format("Concatenating {} videos into {}\n",
                                inputs.size(), output);
    }

    void process() {
        // Open first input to get format parameters
        auto first_input = ffmpeg::open_input_format(input_files_[0].c_str());

        int video_stream_idx = -1;
        int audio_stream_idx = -1;

        for (unsigned i = 0; i < first_input->nb_streams; ++i) {
            if (first_input->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
                video_stream_idx == -1) {
                video_stream_idx = static_cast<int>(i);
            }
            else if (first_input->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
                     audio_stream_idx == -1) {
                audio_stream_idx = static_cast<int>(i);
            }
        }

        if (video_stream_idx == -1) {
            throw std::runtime_error("No video stream found in first input");
        }

        const auto* video_stream = first_input->streams[video_stream_idx];
        width_ = video_stream->codecpar->width;
        height_ = video_stream->codecpar->height;

        // Get frame rate from first video
        AVRational frame_rate = first_input->streams[video_stream_idx]->r_frame_rate;
        if (frame_rate.num == 0 || frame_rate.den == 0) {
            frame_rate = av_guess_frame_rate(first_input.get(),
                                             first_input->streams[video_stream_idx],
                                             nullptr);
        }
        frame_rate_ = frame_rate;

        has_audio_ = (audio_stream_idx != -1);

        std::cout << std::format("Output format: {}x{} @ {}/{} fps\n",
                                width_, height_, frame_rate_.num, frame_rate_.den);
        if (has_audio_) {
            std::cout << "Audio: enabled\n";
        }

        setup_output();

        // Process each input file
        for (size_t i = 0; i < input_files_.size(); ++i) {
            std::cout << std::format("\nProcessing video {}/{}: {}\n",
                                    i + 1, input_files_.size(), input_files_[i]);
            process_input(input_files_[i]);
        }

        // Flush encoders
        flush_encoders();

        // Write trailer
        av_write_trailer(output_format_ctx_.get());

        std::cout << std::format("\nConcatenation complete: {}\n", output_file_);
        std::cout << std::format("Total frames: {}\n", total_frames_);
    }

private:
    void setup_output() {
        AVFormatContext* raw_ctx = nullptr;
        avformat_alloc_output_context2(&raw_ctx, nullptr, nullptr, output_file_.c_str());
        output_format_ctx_.reset(raw_ctx);

        if (!output_format_ctx_) {
            throw std::runtime_error("Failed to create output format context");
        }

        // Setup video encoder
        const auto* video_codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!video_codec) {
            throw std::runtime_error("H264 encoder not found");
        }

        video_encoder_ctx_ = ffmpeg::create_codec_context(video_codec);
        video_encoder_ctx_->width = width_;
        video_encoder_ctx_->height = height_;
        video_encoder_ctx_->time_base = av_inv_q(frame_rate_);
        video_encoder_ctx_->framerate = frame_rate_;
        video_encoder_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
        video_encoder_ctx_->bit_rate = 2000000;
        video_encoder_ctx_->gop_size = 12;

        if (output_format_ctx_->oformat->flags & AVFMT_GLOBALHEADER) {
            video_encoder_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        if (avcodec_open2(video_encoder_ctx_.get(), video_codec, nullptr) < 0) {
            throw std::runtime_error("Failed to open video encoder");
        }

        auto* video_stream = avformat_new_stream(output_format_ctx_.get(), nullptr);
        if (!video_stream) {
            throw std::runtime_error("Failed to create video stream");
        }
        video_stream_idx_ = video_stream->index;

        avcodec_parameters_from_context(video_stream->codecpar, video_encoder_ctx_.get());
        video_stream->time_base = video_encoder_ctx_->time_base;

        // Setup audio encoder if needed
        if (has_audio_) {
            const auto* audio_codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
            if (!audio_codec) {
                std::cerr << "Warning: AAC encoder not found, audio will be skipped\n";
                has_audio_ = false;
            }
            else {
                audio_encoder_ctx_ = ffmpeg::create_codec_context(audio_codec);
                audio_encoder_ctx_->sample_rate = 44100;
                audio_encoder_ctx_->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
                audio_encoder_ctx_->sample_fmt = AV_SAMPLE_FMT_FLTP;
                audio_encoder_ctx_->bit_rate = 128000;
                audio_encoder_ctx_->time_base = {1, audio_encoder_ctx_->sample_rate};

                if (output_format_ctx_->oformat->flags & AVFMT_GLOBALHEADER) {
                    audio_encoder_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
                }

                if (avcodec_open2(audio_encoder_ctx_.get(), audio_codec, nullptr) < 0) {
                    std::cerr << "Warning: Failed to open audio encoder, audio will be skipped\n";
                    has_audio_ = false;
                }
                else {
                    auto* audio_stream = avformat_new_stream(output_format_ctx_.get(), nullptr);
                    if (!audio_stream) {
                        throw std::runtime_error("Failed to create audio stream");
                    }
                    audio_stream_idx_ = audio_stream->index;

                    avcodec_parameters_from_context(audio_stream->codecpar, audio_encoder_ctx_.get());
                    audio_stream->time_base = audio_encoder_ctx_->time_base;
                }
            }
        }

        // Open output file
        if (!(output_format_ctx_->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&output_format_ctx_->pb, output_file_.c_str(), AVIO_FLAG_WRITE) < 0) {
                throw std::runtime_error("Failed to open output file");
            }
        }

        if (avformat_write_header(output_format_ctx_.get(), nullptr) < 0) {
            throw std::runtime_error("Failed to write header");
        }
    }

    void process_input(const std::string& input_file) {
        auto input_ctx = ffmpeg::open_input_format(input_file.c_str());

        int in_video_idx = -1;
        int in_audio_idx = -1;

        for (unsigned i = 0; i < input_ctx->nb_streams; ++i) {
            if (input_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
                in_video_idx == -1) {
                in_video_idx = static_cast<int>(i);
            }
            else if (input_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
                     in_audio_idx == -1 && has_audio_) {
                in_audio_idx = static_cast<int>(i);
            }
        }

        // Setup video decoder
        ffmpeg::CodecContextPtr video_decoder_ctx;
        if (in_video_idx >= 0) {
            const auto* video_codec = avcodec_find_decoder(
                input_ctx->streams[in_video_idx]->codecpar->codec_id);
            if (!video_codec) {
                throw std::runtime_error("Video decoder not found");
            }

            video_decoder_ctx = ffmpeg::create_codec_context(video_codec);
            avcodec_parameters_to_context(video_decoder_ctx.get(),
                                         input_ctx->streams[in_video_idx]->codecpar);

            if (avcodec_open2(video_decoder_ctx.get(), video_codec, nullptr) < 0) {
                throw std::runtime_error("Failed to open video decoder");
            }
        }

        // Setup audio decoder if needed
        ffmpeg::CodecContextPtr audio_decoder_ctx;
        ffmpeg::SwrContextPtr swr_ctx;

        if (in_audio_idx >= 0 && has_audio_) {
            const auto* audio_codec = avcodec_find_decoder(
                input_ctx->streams[in_audio_idx]->codecpar->codec_id);
            if (audio_codec) {
                audio_decoder_ctx = ffmpeg::create_codec_context(audio_codec);
                avcodec_parameters_to_context(audio_decoder_ctx.get(),
                                             input_ctx->streams[in_audio_idx]->codecpar);

                if (avcodec_open2(audio_decoder_ctx.get(), audio_codec, nullptr) == 0) {
                    // Setup resampler
                    SwrContext* raw_swr = nullptr;
                    swr_alloc_set_opts2(&raw_swr,
                                       &audio_encoder_ctx_->ch_layout,
                                       audio_encoder_ctx_->sample_fmt,
                                       audio_encoder_ctx_->sample_rate,
                                       &audio_decoder_ctx->ch_layout,
                                       audio_decoder_ctx->sample_fmt,
                                       audio_decoder_ctx->sample_rate,
                                       0, nullptr);
                    if (raw_swr) {
                        swr_ctx.reset(raw_swr);
                        swr_init(swr_ctx.get());
                    }
                }
            }
        }

        // Read and process packets
        auto packet = ffmpeg::create_packet();
        auto frame = ffmpeg::create_frame();

        int frame_count = 0;

        while (av_read_frame(input_ctx.get(), packet.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(packet.get());

            if (packet->stream_index == in_video_idx && video_decoder_ctx) {
                if (avcodec_send_packet(video_decoder_ctx.get(), packet.get()) >= 0) {
                    while (avcodec_receive_frame(video_decoder_ctx.get(), frame.get()) == 0) {
                        ffmpeg::ScopedFrameUnref frame_guard(frame.get());

                        encode_video_frame(frame.get());
                        frame_count++;
                        total_frames_++;

                        if (frame_count % 30 == 0) {
                            std::cout << std::format("\rEncoded {} frames...", frame_count) << std::flush;
                        }
                    }
                }
            }
            else if (packet->stream_index == in_audio_idx && audio_decoder_ctx && has_audio_) {
                if (avcodec_send_packet(audio_decoder_ctx.get(), packet.get()) >= 0) {
                    while (avcodec_receive_frame(audio_decoder_ctx.get(), frame.get()) == 0) {
                        ffmpeg::ScopedFrameUnref frame_guard(frame.get());

                        if (swr_ctx) {
                            encode_audio_frame(frame.get(), swr_ctx.get());
                        }
                    }
                }
            }
        }

        // Flush decoders
        if (video_decoder_ctx) {
            avcodec_send_packet(video_decoder_ctx.get(), nullptr);
            while (avcodec_receive_frame(video_decoder_ctx.get(), frame.get()) == 0) {
                ffmpeg::ScopedFrameUnref frame_guard(frame.get());
                encode_video_frame(frame.get());
                frame_count++;
                total_frames_++;
            }
        }

        std::cout << std::format("\rEncoded {} frames\n", frame_count);
    }

    void encode_video_frame(AVFrame* frame) {
        frame->pts = video_pts_++;

        if (avcodec_send_frame(video_encoder_ctx_.get(), frame) < 0) {
            return;
        }

        auto packet = ffmpeg::create_packet();
        while (avcodec_receive_packet(video_encoder_ctx_.get(), packet.get()) == 0) {
            ffmpeg::ScopedPacketUnref packet_guard(packet.get());

            av_packet_rescale_ts(packet.get(), video_encoder_ctx_->time_base,
                               output_format_ctx_->streams[video_stream_idx_]->time_base);
            packet->stream_index = video_stream_idx_;

            av_interleaved_write_frame(output_format_ctx_.get(), packet.get());
        }
    }

    void encode_audio_frame(AVFrame* frame, SwrContext* swr_ctx) {
        auto resampled_frame = ffmpeg::create_frame();
        resampled_frame->format = audio_encoder_ctx_->sample_fmt;
        resampled_frame->ch_layout = audio_encoder_ctx_->ch_layout;
        resampled_frame->sample_rate = audio_encoder_ctx_->sample_rate;
        resampled_frame->nb_samples = audio_encoder_ctx_->frame_size > 0 ?
            audio_encoder_ctx_->frame_size : frame->nb_samples;

        av_frame_get_buffer(resampled_frame.get(), 0);

        swr_convert(swr_ctx,
                   resampled_frame->data, resampled_frame->nb_samples,
                   const_cast<const uint8_t**>(frame->data), frame->nb_samples);

        resampled_frame->pts = audio_pts_;
        audio_pts_ += resampled_frame->nb_samples;

        if (avcodec_send_frame(audio_encoder_ctx_.get(), resampled_frame.get()) < 0) {
            return;
        }

        auto packet = ffmpeg::create_packet();
        while (avcodec_receive_packet(audio_encoder_ctx_.get(), packet.get()) == 0) {
            ffmpeg::ScopedPacketUnref packet_guard(packet.get());

            av_packet_rescale_ts(packet.get(), audio_encoder_ctx_->time_base,
                               output_format_ctx_->streams[audio_stream_idx_]->time_base);
            packet->stream_index = audio_stream_idx_;

            av_interleaved_write_frame(output_format_ctx_.get(), packet.get());
        }
    }

    void flush_encoders() {
        // Flush video encoder
        avcodec_send_frame(video_encoder_ctx_.get(), nullptr);
        auto packet = ffmpeg::create_packet();

        while (avcodec_receive_packet(video_encoder_ctx_.get(), packet.get()) == 0) {
            ffmpeg::ScopedPacketUnref packet_guard(packet.get());

            av_packet_rescale_ts(packet.get(), video_encoder_ctx_->time_base,
                               output_format_ctx_->streams[video_stream_idx_]->time_base);
            packet->stream_index = video_stream_idx_;

            av_interleaved_write_frame(output_format_ctx_.get(), packet.get());
        }

        // Flush audio encoder
        if (has_audio_ && audio_encoder_ctx_) {
            avcodec_send_frame(audio_encoder_ctx_.get(), nullptr);

            while (avcodec_receive_packet(audio_encoder_ctx_.get(), packet.get()) == 0) {
                ffmpeg::ScopedPacketUnref packet_guard(packet.get());

                av_packet_rescale_ts(packet.get(), audio_encoder_ctx_->time_base,
                                   output_format_ctx_->streams[audio_stream_idx_]->time_base);
                packet->stream_index = audio_stream_idx_;

                av_interleaved_write_frame(output_format_ctx_.get(), packet.get());
            }
        }
    }

    std::string output_file_;
    std::vector<std::string> input_files_;

    int width_ = 0;
    int height_ = 0;
    AVRational frame_rate_ = {30, 1};
    bool has_audio_ = false;

    ffmpeg::FormatContextPtr output_format_ctx_;
    ffmpeg::CodecContextPtr video_encoder_ctx_;
    ffmpeg::CodecContextPtr audio_encoder_ctx_;

    int video_stream_idx_ = 0;
    int audio_stream_idx_ = 1;

    int64_t video_pts_ = 0;
    int64_t audio_pts_ = 0;
    int total_frames_ = 0;
};

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 4) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        const std::string output = argv[1];
        std::vector<std::string> inputs;

        for (int i = 2; i < argc; ++i) {
            if (!fs::exists(argv[i])) {
                std::cerr << std::format("Error: File not found: {}\n", argv[i]);
                return 1;
            }
            inputs.push_back(argv[i]);
        }

        VideoConcatenate concatenator(output, inputs);
        concatenator.process();

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }
}
