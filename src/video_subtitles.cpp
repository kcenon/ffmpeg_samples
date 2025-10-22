/**
 * Video Subtitle Processor
 *
 * This sample demonstrates how to add, extract, and burn subtitles into video files
 * using modern C++20 and FFmpeg libraries.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <fstream>
#include <format>
#include <filesystem>
#include <vector>
#include <string>
#include <regex>

namespace fs = std::filesystem;

namespace {

struct SubtitleEntry {
    int index;
    int64_t start_pts;
    int64_t end_pts;
    std::string text;
};

class SubtitleExtractor {
public:
    explicit SubtitleExtractor(std::string_view input_file)
        : format_ctx_(ffmpeg::open_input_format(input_file.data())) {
    }

    void extract_to_srt(const fs::path& output_file) {
        std::cout << "Extracting Subtitles\n";
        std::cout << "====================\n\n";

        // Find subtitle stream
        const auto stream_idx = ffmpeg::find_stream_index(format_ctx_.get(), AVMEDIA_TYPE_SUBTITLE);
        if (!stream_idx) {
            throw ffmpeg::FFmpegError("No subtitle stream found");
        }
        subtitle_stream_index_ = *stream_idx;

        std::cout << std::format("Input: {}\n", format_ctx_->url);
        std::cout << std::format("Output: {}\n", output_file.string());
        std::cout << std::format("Subtitle stream: #{}\n\n", subtitle_stream_index_);

        // Setup decoder
        const auto* codecpar = format_ctx_->streams[subtitle_stream_index_]->codecpar;
        const auto* decoder = avcodec_find_decoder(codecpar->codec_id);
        if (!decoder) {
            throw ffmpeg::FFmpegError("Subtitle decoder not found");
        }

        codec_ctx_ = ffmpeg::create_codec_context(decoder);
        ffmpeg::check_error(
            avcodec_parameters_to_context(codec_ctx_.get(), codecpar),
            "copy codec parameters"
        );
        ffmpeg::check_error(
            avcodec_open2(codec_ctx_.get(), decoder, nullptr),
            "open decoder"
        );

        // Extract subtitles
        std::vector<SubtitleEntry> entries;
        auto packet = ffmpeg::create_packet();
        int subtitle_count = 0;

        std::cout << "Extracting subtitles...\n";

        while (av_read_frame(format_ctx_.get(), packet.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(packet.get());

            if (packet->stream_index != subtitle_stream_index_) {
                continue;
            }

            AVSubtitle subtitle;
            int got_subtitle = 0;

            const auto ret = avcodec_decode_subtitle2(codec_ctx_.get(), &subtitle,
                                                     &got_subtitle, packet.get());

            if (ret < 0 || !got_subtitle) {
                continue;
            }

            // Convert subtitle to SRT format
            for (unsigned int i = 0; i < subtitle.num_rects; ++i) {
                const auto* rect = subtitle.rects[i];

                if (rect->type == SUBTITLE_TEXT && rect->text) {
                    SubtitleEntry entry;
                    entry.index = ++subtitle_count;
                    entry.start_pts = packet->pts;
                    entry.end_pts = packet->pts + av_rescale_q(
                        subtitle.end_display_time,
                        AVRational{1, 1000},
                        format_ctx_->streams[subtitle_stream_index_]->time_base
                    );
                    entry.text = rect->text;
                    entries.push_back(entry);

                } else if (rect->type == SUBTITLE_ASS && rect->ass) {
                    // Parse ASS format
                    std::string ass_text = rect->ass;
                    const auto comma_pos = ass_text.find_last_of(',');
                    if (comma_pos != std::string::npos) {
                        SubtitleEntry entry;
                        entry.index = ++subtitle_count;
                        entry.start_pts = packet->pts;
                        entry.end_pts = packet->pts + av_rescale_q(
                            subtitle.end_display_time,
                            AVRational{1, 1000},
                            format_ctx_->streams[subtitle_stream_index_]->time_base
                        );
                        entry.text = ass_text.substr(comma_pos + 1);
                        entries.push_back(entry);
                    }
                }
            }

            avsubtitle_free(&subtitle);
        }

        std::cout << std::format("Extracted {} subtitle entries\n\n", subtitle_count);

        // Write SRT file
        write_srt_file(output_file, entries);

        std::cout << std::format("✓ Subtitles extracted successfully\n");
        std::cout << std::format("Output file: {}\n", output_file.string());
    }

private:
    void write_srt_file(const fs::path& output_file, const std::vector<SubtitleEntry>& entries) {
        std::ofstream srt_file(output_file);
        if (!srt_file.is_open()) {
            throw std::runtime_error(std::format("Failed to open output file: {}",
                                                 output_file.string()));
        }

        const auto* stream = format_ctx_->streams[subtitle_stream_index_];
        const auto time_base = stream->time_base;

        for (const auto& entry : entries) {
            // Convert PTS to time
            const auto start_ms = av_rescale_q(entry.start_pts, time_base, AVRational{1, 1000});
            const auto end_ms = av_rescale_q(entry.end_pts, time_base, AVRational{1, 1000});

            // Format: HH:MM:SS,mmm
            const auto format_time = [](int64_t ms) {
                const auto hours = ms / 3600000;
                const auto minutes = (ms % 3600000) / 60000;
                const auto seconds = (ms % 60000) / 1000;
                const auto millis = ms % 1000;
                return std::format("{:02d}:{:02d}:{:02d},{:03d}",
                                  hours, minutes, seconds, millis);
            };

            srt_file << entry.index << "\n";
            srt_file << format_time(start_ms) << " --> " << format_time(end_ms) << "\n";
            srt_file << entry.text << "\n\n";
        }
    }

    ffmpeg::FormatContextPtr format_ctx_;
    ffmpeg::CodecContextPtr codec_ctx_;
    int subtitle_stream_index_ = -1;
};

class SubtitleBurner {
public:
    SubtitleBurner(std::string_view input_video, std::string_view subtitle_file,
                   const fs::path& output_file)
        : input_video_(input_video)
        , subtitle_file_(subtitle_file)
        , output_file_(output_file)
        , format_ctx_(ffmpeg::open_input_format(input_video.data()))
        , packet_(ffmpeg::create_packet())
        , frame_(ffmpeg::create_frame())
        , filtered_frame_(ffmpeg::create_frame()) {

        initialize();
    }

    void burn() {
        std::cout << "Burning Subtitles into Video\n";
        std::cout << "=============================\n\n";
        std::cout << std::format("Input video: {}\n", input_video_);
        std::cout << std::format("Subtitle file: {}\n", subtitle_file_);
        std::cout << std::format("Output: {}\n", output_file_.string());
        std::cout << std::format("Resolution: {}x{}\n\n", codec_ctx_->width, codec_ctx_->height);

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
        encoder_ctx_->width = codec_ctx_->width;
        encoder_ctx_->height = codec_ctx_->height;
        encoder_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
        encoder_ctx_->time_base = codec_ctx_->time_base;
        encoder_ctx_->framerate = av_guess_frame_rate(format_ctx_.get(),
                                                      format_ctx_->streams[video_stream_index_],
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

        // Process video
        int frame_count = 0;
        std::cout << "Processing video with burned subtitles...\n";

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

                // Push frame through filter (burns subtitle)
                ffmpeg::check_error(
                    av_buffersrc_add_frame_flags(buffersrc_ctx_, frame_.get(),
                                               AV_BUFFERSRC_FLAG_KEEP_REF),
                    "feed frame to filter"
                );

                // Get filtered frame
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

                    // Encode frame
                    filtered_frame_->pict_type = AV_PICTURE_TYPE_NONE;
                    encode_write_frame(output_ctx.get(), out_stream);

                    ++frame_count;
                    if (frame_count % 30 == 0) {
                        std::cout << std::format("Processed {} frames\r", frame_count) << std::flush;
                    }
                }
            }
        }

        // Flush encoder
        flush_encoder(output_ctx.get(), out_stream);

        // Write trailer
        ffmpeg::check_error(
            av_write_trailer(output_ctx.get()),
            "write trailer"
        );

        std::cout << std::format("\n\nTotal frames: {}\n", frame_count);
        std::cout << std::format("✓ Subtitles burned successfully\n");
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

        // Initialize subtitle filter
        initialize_subtitle_filter();
    }

    void initialize_subtitle_filter() {
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

        // Create subtitle filter
        const auto subtitle_filter = std::format("subtitles='{}'", subtitle_file_);

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

        const auto ret = avfilter_graph_parse_ptr(filter_graph_.get(), subtitle_filter.c_str(),
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

            ffmpeg::check_error(
                av_interleaved_write_frame(output_ctx, encoded_packet.get()),
                "write frame"
            );
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

    std::string input_video_;
    std::string subtitle_file_;
    fs::path output_file_;
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
    std::cout << std::format("Usage: {} <command> [options]\n\n", prog_name);
    std::cout << "Commands:\n";
    std::cout << "  extract <input_video> <output_srt>\n";
    std::cout << "      Extract embedded subtitles to SRT file\n\n";
    std::cout << "  burn <input_video> <subtitle_file> <output_video>\n";
    std::cout << "      Burn subtitles into video (hardsub)\n\n";
    std::cout << "Examples:\n";
    std::cout << std::format("  {} extract video.mkv subtitles.srt\n", prog_name);
    std::cout << std::format("  {} burn video.mp4 subtitles.srt output.mp4\n", prog_name);
    std::cout << "\nSupported subtitle formats:\n";
    std::cout << "  - SRT (SubRip)\n";
    std::cout << "  - ASS/SSA (Advanced SubStation Alpha)\n";
    std::cout << "  - WebVTT\n";
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        const std::string_view command{argv[1]};

        if (command == "extract") {
            if (argc < 4) {
                std::cerr << "Error: extract command requires <input_video> <output_srt>\n";
                return 1;
            }
            const std::string_view input_video{argv[2]};
            const fs::path output_srt{argv[3]};

            SubtitleExtractor extractor(input_video);
            extractor.extract_to_srt(output_srt);

        } else if (command == "burn") {
            if (argc < 5) {
                std::cerr << "Error: burn command requires <input_video> <subtitle_file> <output_video>\n";
                return 1;
            }
            const std::string_view input_video{argv[2]};
            const std::string_view subtitle_file{argv[3]};
            const fs::path output_video{argv[4]};

            SubtitleBurner burner(input_video, subtitle_file, output_video);
            burner.burn();

        } else {
            std::cerr << std::format("Error: Unknown command '{}'\n", command);
            print_usage(argv[0]);
            return 1;
        }

    } catch (const ffmpeg::FFmpegError& e) {
        std::cerr << std::format("FFmpeg error: {}\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }

    return 0;
}
