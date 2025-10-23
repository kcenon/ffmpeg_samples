/**
 * Video Reverse
 *
 * This sample demonstrates how to reverse a video (play backwards)
 * using FFmpeg with modern C++20.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <format>
#include <string_view>
#include <vector>
#include <deque>
#include <algorithm>

extern "C" {
#include <libswresample/swresample.h>
}

namespace {

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <input> <output> [options]\n\n", prog_name);
    std::cout << "Parameters:\n";
    std::cout << "  input    - Input video file\n";
    std::cout << "  output   - Output video file\n\n";
    std::cout << "Options:\n";
    std::cout << "  --video-only  - Reverse video only, discard audio\n";
    std::cout << "  --audio-only  - Reverse audio only, discard video\n\n";
    std::cout << "Examples:\n";
    std::cout << std::format("  {} input.mp4 reversed.mp4\n", prog_name);
    std::cout << std::format("  {} video.mp4 output.mp4 --video-only\n", prog_name);
    std::cout << "\nNote: Reversing video requires loading all frames into memory.\n";
    std::cout << "      Large videos may require significant RAM.\n";
}

class VideoReverse {
public:
    VideoReverse(std::string_view input, std::string_view output,
                bool video_only = false, bool audio_only = false)
        : input_file_(input)
        , output_file_(output)
        , video_only_(video_only)
        , audio_only_(audio_only) {

        if (video_only && audio_only) {
            throw std::runtime_error("Cannot specify both --video-only and --audio-only");
        }

        std::cout << std::format("Reversing video: {}\n", input);
        if (video_only) {
            std::cout << "Mode: Video only\n";
        } else if (audio_only) {
            std::cout << "Mode: Audio only\n";
        } else {
            std::cout << "Mode: Video and Audio\n";
        }
    }

    void process() {
        // Open input
        input_format_ctx_ = ffmpeg::open_input_format(input_file_.c_str());

        // Find streams
        find_streams();

        // Setup decoders
        setup_decoders();

        // Read all frames into memory
        std::cout << "\nReading frames...\n";
        read_all_frames();

        // Setup output
        std::cout << "\nSetting up output...\n";
        setup_output();

        // Write frames in reverse order
        std::cout << "\nWriting reversed video...\n";
        write_reversed_frames();

        // Flush encoders
        flush_encoders();

        // Write trailer
        av_write_trailer(output_format_ctx_.get());

        std::cout << std::format("\nReverse complete: {}\n", output_file_);
        if (!audio_only_) {
            std::cout << std::format("Total video frames: {}\n", video_frames_.size());
        }
        if (!video_only_ && has_audio_) {
            std::cout << std::format("Total audio frames: {}\n", audio_frames_.size());
        }
    }

private:
    void find_streams() {
        video_stream_idx_ = -1;
        audio_stream_idx_ = -1;

        for (unsigned i = 0; i < input_format_ctx_->nb_streams; ++i) {
            const auto codec_type = input_format_ctx_->streams[i]->codecpar->codec_type;
            if (codec_type == AVMEDIA_TYPE_VIDEO && video_stream_idx_ == -1 && !audio_only_) {
                video_stream_idx_ = static_cast<int>(i);
            }
            else if (codec_type == AVMEDIA_TYPE_AUDIO && audio_stream_idx_ == -1 && !video_only_) {
                audio_stream_idx_ = static_cast<int>(i);
            }
        }

        if (video_stream_idx_ == -1 && !audio_only_) {
            throw std::runtime_error("No video stream found");
        }

        has_audio_ = (audio_stream_idx_ != -1);
    }

    void setup_decoders() {
        // Setup video decoder
        if (video_stream_idx_ >= 0) {
            auto* stream = input_format_ctx_->streams[video_stream_idx_];
            const auto* codec = avcodec_find_decoder(stream->codecpar->codec_id);
            if (!codec) {
                throw std::runtime_error("Video decoder not found");
            }

            video_decoder_ctx_ = ffmpeg::create_codec_context(codec);
            avcodec_parameters_to_context(video_decoder_ctx_.get(), stream->codecpar);

            if (avcodec_open2(video_decoder_ctx_.get(), codec, nullptr) < 0) {
                throw std::runtime_error("Failed to open video decoder");
            }

            width_ = video_decoder_ctx_->width;
            height_ = video_decoder_ctx_->height;
            frame_rate_ = av_guess_frame_rate(input_format_ctx_.get(), stream, nullptr);

            std::cout << std::format("Video: {}x{} @ {}/{} fps\n",
                                    width_, height_, frame_rate_.num, frame_rate_.den);
        }

        // Setup audio decoder
        if (audio_stream_idx_ >= 0 && has_audio_) {
            const auto* stream = input_format_ctx_->streams[audio_stream_idx_];
            const auto* codec = avcodec_find_decoder(stream->codecpar->codec_id);
            if (!codec) {
                std::cerr << "Warning: Audio decoder not found, audio will be skipped\n";
                has_audio_ = false;
            }
            else {
                audio_decoder_ctx_ = ffmpeg::create_codec_context(codec);
                avcodec_parameters_to_context(audio_decoder_ctx_.get(), stream->codecpar);

                if (avcodec_open2(audio_decoder_ctx_.get(), codec, nullptr) < 0) {
                    std::cerr << "Warning: Failed to open audio decoder, audio will be skipped\n";
                    has_audio_ = false;
                }
                else {
                    std::cout << std::format("Audio: {} Hz, {} channels\n",
                                            audio_decoder_ctx_->sample_rate,
                                            audio_decoder_ctx_->ch_layout.nb_channels);
                }
            }
        }
    }

    void read_all_frames() {
        auto packet = ffmpeg::create_packet();
        auto frame = ffmpeg::create_frame();

        int video_count = 0;
        int audio_count = 0;

        while (av_read_frame(input_format_ctx_.get(), packet.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(packet.get());

            if (packet->stream_index == video_stream_idx_ && video_decoder_ctx_) {
                if (avcodec_send_packet(video_decoder_ctx_.get(), packet.get()) >= 0) {
                    while (avcodec_receive_frame(video_decoder_ctx_.get(), frame.get()) == 0) {
                        // Clone frame and store
                        auto cloned_frame = ffmpeg::create_frame();
                        av_frame_ref(cloned_frame.get(), frame.get());
                        video_frames_.push_back(std::move(cloned_frame));

                        video_count++;
                        if (video_count % 30 == 0) {
                            std::cout << std::format("\rRead {} video frames...", video_count) << std::flush;
                        }

                        av_frame_unref(frame.get());
                    }
                }
            }
            else if (packet->stream_index == audio_stream_idx_ && audio_decoder_ctx_) {
                if (avcodec_send_packet(audio_decoder_ctx_.get(), packet.get()) >= 0) {
                    while (avcodec_receive_frame(audio_decoder_ctx_.get(), frame.get()) == 0) {
                        // Clone frame and store
                        auto cloned_frame = ffmpeg::create_frame();
                        av_frame_ref(cloned_frame.get(), frame.get());
                        audio_frames_.push_front(std::move(cloned_frame));  // Reverse order

                        audio_count++;
                        av_frame_unref(frame.get());
                    }
                }
            }
        }

        // Flush decoders
        if (video_decoder_ctx_) {
            avcodec_send_packet(video_decoder_ctx_.get(), nullptr);
            while (avcodec_receive_frame(video_decoder_ctx_.get(), frame.get()) == 0) {
                auto cloned_frame = ffmpeg::create_frame();
                av_frame_ref(cloned_frame.get(), frame.get());
                video_frames_.push_back(std::move(cloned_frame));
                video_count++;
                av_frame_unref(frame.get());
            }
        }

        if (audio_decoder_ctx_) {
            avcodec_send_packet(audio_decoder_ctx_.get(), nullptr);
            while (avcodec_receive_frame(audio_decoder_ctx_.get(), frame.get()) == 0) {
                auto cloned_frame = ffmpeg::create_frame();
                av_frame_ref(cloned_frame.get(), frame.get());
                audio_frames_.push_front(std::move(cloned_frame));
                audio_count++;
                av_frame_unref(frame.get());
            }
        }

        std::cout << std::format("\rRead {} video frames, {} audio frames\n",
                                video_count, audio_count);
    }

    void setup_output() {
        AVFormatContext* raw_ctx = nullptr;
        avformat_alloc_output_context2(&raw_ctx, nullptr, nullptr, output_file_.c_str());
        output_format_ctx_.reset(raw_ctx);

        if (!output_format_ctx_) {
            throw std::runtime_error("Failed to create output format context");
        }

        // Setup video encoder
        if (video_stream_idx_ >= 0 && !audio_only_) {
            const auto* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
            if (!codec) {
                throw std::runtime_error("H264 encoder not found");
            }

            video_encoder_ctx_ = ffmpeg::create_codec_context(codec);
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

            if (avcodec_open2(video_encoder_ctx_.get(), codec, nullptr) < 0) {
                throw std::runtime_error("Failed to open video encoder");
            }

            auto* stream = avformat_new_stream(output_format_ctx_.get(), nullptr);
            if (!stream) {
                throw std::runtime_error("Failed to create video stream");
            }

            avcodec_parameters_from_context(stream->codecpar, video_encoder_ctx_.get());
            stream->time_base = video_encoder_ctx_->time_base;
            output_video_stream_idx_ = stream->index;
        }

        // Setup audio encoder
        if (has_audio_ && !video_only_) {
            const auto* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
            if (!codec) {
                std::cerr << "Warning: AAC encoder not found, audio will be skipped\n";
                has_audio_ = false;
            }
            else {
                audio_encoder_ctx_ = ffmpeg::create_codec_context(codec);
                audio_encoder_ctx_->sample_rate = audio_decoder_ctx_->sample_rate;
                audio_encoder_ctx_->ch_layout = audio_decoder_ctx_->ch_layout;
                audio_encoder_ctx_->sample_fmt = AV_SAMPLE_FMT_FLTP;
                audio_encoder_ctx_->bit_rate = 128000;
                audio_encoder_ctx_->time_base = {1, audio_encoder_ctx_->sample_rate};

                if (output_format_ctx_->oformat->flags & AVFMT_GLOBALHEADER) {
                    audio_encoder_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
                }

                if (avcodec_open2(audio_encoder_ctx_.get(), codec, nullptr) < 0) {
                    std::cerr << "Warning: Failed to open audio encoder, audio will be skipped\n";
                    has_audio_ = false;
                }
                else {
                    auto* stream = avformat_new_stream(output_format_ctx_.get(), nullptr);
                    if (!stream) {
                        throw std::runtime_error("Failed to create audio stream");
                    }

                    avcodec_parameters_from_context(stream->codecpar, audio_encoder_ctx_.get());
                    stream->time_base = audio_encoder_ctx_->time_base;
                    output_audio_stream_idx_ = stream->index;
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

    void write_reversed_frames() {
        // Write video frames in reverse
        if (!audio_only_ && !video_frames_.empty()) {
            std::cout << "Encoding video frames in reverse...\n";
            int64_t pts = 0;

            // Reverse the video frames vector
            std::ranges::reverse(video_frames_);

            int count = 0;
            for (auto& frame : video_frames_) {
                frame->pts = pts++;
                encode_video_frame(frame.get());

                count++;
                if (count % 30 == 0) {
                    std::cout << std::format("\rEncoded {} / {} frames...",
                                            count, video_frames_.size()) << std::flush;
                }
            }
            std::cout << std::format("\rEncoded {} frames\n", count);
        }

        // Write audio frames (already in reverse order from deque)
        if (!video_only_ && has_audio_ && !audio_frames_.empty()) {
            std::cout << "Encoding audio frames...\n";
            int64_t pts = 0;

            for (auto& frame : audio_frames_) {
                frame->pts = pts;
                pts += frame->nb_samples;
                encode_audio_frame(frame.get());
            }
        }
    }

    void encode_video_frame(AVFrame* frame) {
        if (avcodec_send_frame(video_encoder_ctx_.get(), frame) < 0) {
            return;
        }

        auto packet = ffmpeg::create_packet();
        while (avcodec_receive_packet(video_encoder_ctx_.get(), packet.get()) == 0) {
            ffmpeg::ScopedPacketUnref packet_guard(packet.get());

            av_packet_rescale_ts(packet.get(), video_encoder_ctx_->time_base,
                               output_format_ctx_->streams[output_video_stream_idx_]->time_base);
            packet->stream_index = output_video_stream_idx_;

            av_interleaved_write_frame(output_format_ctx_.get(), packet.get());
        }
    }

    void encode_audio_frame(AVFrame* frame) {
        if (avcodec_send_frame(audio_encoder_ctx_.get(), frame) < 0) {
            return;
        }

        auto packet = ffmpeg::create_packet();
        while (avcodec_receive_packet(audio_encoder_ctx_.get(), packet.get()) == 0) {
            ffmpeg::ScopedPacketUnref packet_guard(packet.get());

            av_packet_rescale_ts(packet.get(), audio_encoder_ctx_->time_base,
                               output_format_ctx_->streams[output_audio_stream_idx_]->time_base);
            packet->stream_index = output_audio_stream_idx_;

            av_interleaved_write_frame(output_format_ctx_.get(), packet.get());
        }
    }

    void flush_encoders() {
        // Flush video encoder
        if (video_encoder_ctx_) {
            avcodec_send_frame(video_encoder_ctx_.get(), nullptr);
            auto packet = ffmpeg::create_packet();

            while (avcodec_receive_packet(video_encoder_ctx_.get(), packet.get()) == 0) {
                ffmpeg::ScopedPacketUnref packet_guard(packet.get());

                av_packet_rescale_ts(packet.get(), video_encoder_ctx_->time_base,
                                   output_format_ctx_->streams[output_video_stream_idx_]->time_base);
                packet->stream_index = output_video_stream_idx_;

                av_interleaved_write_frame(output_format_ctx_.get(), packet.get());
            }
        }

        // Flush audio encoder
        if (has_audio_ && audio_encoder_ctx_) {
            avcodec_send_frame(audio_encoder_ctx_.get(), nullptr);
            auto packet = ffmpeg::create_packet();

            while (avcodec_receive_packet(audio_encoder_ctx_.get(), packet.get()) == 0) {
                ffmpeg::ScopedPacketUnref packet_guard(packet.get());

                av_packet_rescale_ts(packet.get(), audio_encoder_ctx_->time_base,
                                   output_format_ctx_->streams[output_audio_stream_idx_]->time_base);
                packet->stream_index = output_audio_stream_idx_;

                av_interleaved_write_frame(output_format_ctx_.get(), packet.get());
            }
        }
    }

    std::string input_file_;
    std::string output_file_;
    bool video_only_ = false;
    bool audio_only_ = false;
    bool has_audio_ = false;

    int width_ = 0;
    int height_ = 0;
    AVRational frame_rate_ = {30, 1};

    ffmpeg::FormatContextPtr input_format_ctx_;
    ffmpeg::FormatContextPtr output_format_ctx_;

    ffmpeg::CodecContextPtr video_decoder_ctx_;
    ffmpeg::CodecContextPtr audio_decoder_ctx_;
    ffmpeg::CodecContextPtr video_encoder_ctx_;
    ffmpeg::CodecContextPtr audio_encoder_ctx_;

    int video_stream_idx_ = -1;
    int audio_stream_idx_ = -1;
    int output_video_stream_idx_ = 0;
    int output_audio_stream_idx_ = 1;

    std::vector<ffmpeg::FramePtr> video_frames_;
    std::deque<ffmpeg::FramePtr> audio_frames_;
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

        bool video_only = false;
        bool audio_only = false;

        for (int i = 3; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--video-only") {
                video_only = true;
            }
            else if (arg == "--audio-only") {
                audio_only = true;
            }
        }

        VideoReverse reverser(input, output, video_only, audio_only);
        reverser.process();

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }
}
