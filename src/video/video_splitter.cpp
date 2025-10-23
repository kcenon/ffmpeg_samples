/**
 * Video Splitter and Merger
 *
 * This sample demonstrates how to split videos into segments and merge
 * multiple video files using modern C++20 and FFmpeg libraries.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <format>
#include <filesystem>
#include <vector>
#include <string>
#include <fstream>

namespace fs = std::filesystem;

namespace {

struct TimeRange {
    double start_seconds;
    double end_seconds;
};

class VideoSplitter {
public:
    VideoSplitter(std::string_view input_file)
        : input_file_(input_file)
        , format_ctx_(ffmpeg::open_input_format(input_file.data()))
        , packet_(ffmpeg::create_packet()) {

        initialize();
    }

    void split_by_time(const std::vector<TimeRange>& ranges, const fs::path& output_dir) {
        std::cout << "Splitting Video by Time Ranges\n";
        std::cout << "===============================\n\n";
        std::cout << std::format("Input: {}\n", input_file_);
        std::cout << std::format("Output directory: {}\n", output_dir.string());
        std::cout << std::format("Number of segments: {}\n\n", ranges.size());

        fs::create_directories(output_dir);

        for (size_t i = 0; i < ranges.size(); ++i) {
            const auto& range = ranges[i];
            const auto output_file = output_dir / std::format("segment_{:03d}.mp4", i + 1);

            std::cout << std::format("Segment {} [{:.2f}s - {:.2f}s]\n",
                                    i + 1, range.start_seconds, range.end_seconds);

            extract_segment(range.start_seconds, range.end_seconds, output_file);
        }

        std::cout << std::format("\n✓ Split completed successfully\n");
        std::cout << std::format("Output directory: {}\n", output_dir.string());
    }

    void split_by_duration(double segment_duration, const fs::path& output_dir) {
        const auto total_duration = get_duration();
        const auto num_segments = static_cast<int>(std::ceil(total_duration / segment_duration));

        std::cout << "Splitting Video by Duration\n";
        std::cout << "===========================\n\n";
        std::cout << std::format("Input: {}\n", input_file_);
        std::cout << std::format("Total duration: {:.2f} seconds\n", total_duration);
        std::cout << std::format("Segment duration: {:.2f} seconds\n", segment_duration);
        std::cout << std::format("Number of segments: {}\n\n", num_segments);

        fs::create_directories(output_dir);

        for (int i = 0; i < num_segments; ++i) {
            const auto start_time = i * segment_duration;
            const auto end_time = std::min((i + 1) * segment_duration, total_duration);
            const auto output_file = output_dir / std::format("segment_{:03d}.mp4", i + 1);

            std::cout << std::format("Segment {} [{:.2f}s - {:.2f}s]\n",
                                    i + 1, start_time, end_time);

            extract_segment(start_time, end_time, output_file);
        }

        std::cout << std::format("\n✓ Split completed successfully\n");
        std::cout << std::format("Output directory: {}\n", output_dir.string());
    }

private:
    void initialize() {
        // Find video stream
        const auto stream_idx = ffmpeg::find_stream_index(format_ctx_.get(), AVMEDIA_TYPE_VIDEO);
        if (!stream_idx) {
            throw ffmpeg::FFmpegError("No video stream found");
        }
        video_stream_index_ = *stream_idx;

        // Find audio stream (optional)
        const auto audio_idx = ffmpeg::find_stream_index(format_ctx_.get(), AVMEDIA_TYPE_AUDIO);
        if (audio_idx) {
            audio_stream_index_ = *audio_idx;
        }
    }

    double get_duration() const {
        return format_ctx_->duration / static_cast<double>(AV_TIME_BASE);
    }

    void extract_segment(double start_seconds, double end_seconds, const fs::path& output_file) {
        // Reopen input for each segment
        auto segment_ctx = ffmpeg::open_input_format(input_file_.c_str());

        // Seek to start time
        const auto start_ts = static_cast<int64_t>(start_seconds * AV_TIME_BASE);
        av_seek_frame(segment_ctx.get(), -1, start_ts, AVSEEK_FLAG_BACKWARD);

        // Create output context
        AVFormatContext* output_ctx_raw = nullptr;
        ffmpeg::check_error(
            avformat_alloc_output_context2(&output_ctx_raw, nullptr, nullptr,
                                          output_file.string().c_str()),
            "allocate output context"
        );
        auto output_ctx = ffmpeg::FormatContextPtr(output_ctx_raw);

        // Copy streams
        std::vector<int> stream_mapping(segment_ctx->nb_streams, -1);
        int stream_index = 0;

        for (unsigned int i = 0; i < segment_ctx->nb_streams; ++i) {
            const auto* in_stream = segment_ctx->streams[i];
            const auto codec_type = in_stream->codecpar->codec_type;

            // Only copy video and audio streams
            if (codec_type != AVMEDIA_TYPE_VIDEO && codec_type != AVMEDIA_TYPE_AUDIO) {
                continue;
            }

            auto* out_stream = avformat_new_stream(output_ctx.get(), nullptr);
            if (!out_stream) {
                throw ffmpeg::FFmpegError("Failed to create output stream");
            }

            ffmpeg::check_error(
                avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar),
                "copy codec parameters"
            );

            out_stream->codecpar->codec_tag = 0;
            out_stream->time_base = in_stream->time_base;

            stream_mapping[i] = stream_index++;
        }

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

        // Copy packets
        auto packet = ffmpeg::create_packet();
        const auto end_ts = static_cast<int64_t>(end_seconds * AV_TIME_BASE);
        int64_t first_pts = AV_NOPTS_VALUE;

        while (av_read_frame(segment_ctx.get(), packet.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(packet.get());

            const auto stream_idx = packet->stream_index;
            if (stream_mapping[stream_idx] < 0) {
                continue;
            }

            const auto* in_stream = segment_ctx->streams[stream_idx];
            const auto pts_time = av_rescale_q(packet->pts, in_stream->time_base,
                                              AVRational{1, AV_TIME_BASE});

            // Check if we've reached the end time
            if (pts_time >= end_ts) {
                break;
            }

            // Record first PTS for adjustment
            if (first_pts == AV_NOPTS_VALUE && packet->pts != AV_NOPTS_VALUE) {
                first_pts = packet->pts;
            }

            auto* out_stream = output_ctx->streams[stream_mapping[stream_idx]];

            // Adjust timestamps to start from 0
            if (packet->pts != AV_NOPTS_VALUE) {
                packet->pts -= first_pts;
            }
            if (packet->dts != AV_NOPTS_VALUE) {
                packet->dts -= first_pts;
            }

            // Rescale timestamps
            av_packet_rescale_ts(packet.get(), in_stream->time_base, out_stream->time_base);
            packet->stream_index = stream_mapping[stream_idx];
            packet->pos = -1;

            ffmpeg::check_error(
                av_interleaved_write_frame(output_ctx.get(), packet.get()),
                "write packet"
            );
        }

        // Write trailer
        av_write_trailer(output_ctx.get());
    }

    std::string input_file_;
    int video_stream_index_ = -1;
    int audio_stream_index_ = -1;

    ffmpeg::FormatContextPtr format_ctx_;
    ffmpeg::PacketPtr packet_;
};

class VideoMerger {
public:
    void merge(const std::vector<std::string>& input_files, const fs::path& output_file) {
        std::cout << "Merging Videos\n";
        std::cout << "==============\n\n";
        std::cout << std::format("Number of inputs: {}\n", input_files.size());
        std::cout << std::format("Output: {}\n\n", output_file.string());

        if (input_files.empty()) {
            throw std::invalid_argument("No input files provided");
        }

        // Method 1: Create concat demuxer list file
        const auto list_file = fs::temp_directory_path() / "concat_list.txt";
        create_concat_list(input_files, list_file);

        // Open concat demuxer
        auto concat_ctx = open_concat_demuxer(list_file);

        // Find streams
        const auto video_idx = ffmpeg::find_stream_index(concat_ctx.get(), AVMEDIA_TYPE_VIDEO);
        const auto audio_idx = ffmpeg::find_stream_index(concat_ctx.get(), AVMEDIA_TYPE_AUDIO);

        if (!video_idx) {
            throw ffmpeg::FFmpegError("No video stream found");
        }

        // Create output context
        AVFormatContext* output_ctx_raw = nullptr;
        ffmpeg::check_error(
            avformat_alloc_output_context2(&output_ctx_raw, nullptr, nullptr,
                                          output_file.string().c_str()),
            "allocate output context"
        );
        auto output_ctx = ffmpeg::FormatContextPtr(output_ctx_raw);

        // Copy streams
        std::vector<int> stream_mapping(concat_ctx->nb_streams, -1);
        int stream_index = 0;

        for (unsigned int i = 0; i < concat_ctx->nb_streams; ++i) {
            const auto* in_stream = concat_ctx->streams[i];
            const auto codec_type = in_stream->codecpar->codec_type;

            if (codec_type != AVMEDIA_TYPE_VIDEO && codec_type != AVMEDIA_TYPE_AUDIO) {
                continue;
            }

            auto* out_stream = avformat_new_stream(output_ctx.get(), nullptr);
            if (!out_stream) {
                throw ffmpeg::FFmpegError("Failed to create output stream");
            }

            ffmpeg::check_error(
                avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar),
                "copy codec parameters"
            );

            out_stream->codecpar->codec_tag = 0;
            out_stream->time_base = in_stream->time_base;

            stream_mapping[i] = stream_index++;

            std::cout << std::format("Input {} ({})\n", i,
                                    av_get_media_type_string(codec_type));
        }

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

        // Copy packets
        std::cout << "\nMerging...\n";
        auto packet = ffmpeg::create_packet();
        int packet_count = 0;

        while (av_read_frame(concat_ctx.get(), packet.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(packet.get());

            const auto stream_idx = packet->stream_index;
            if (stream_mapping[stream_idx] < 0) {
                continue;
            }

            const auto* in_stream = concat_ctx->streams[stream_idx];
            auto* out_stream = output_ctx->streams[stream_mapping[stream_idx]];

            // Rescale timestamps
            av_packet_rescale_ts(packet.get(), in_stream->time_base, out_stream->time_base);
            packet->stream_index = stream_mapping[stream_idx];
            packet->pos = -1;

            ffmpeg::check_error(
                av_interleaved_write_frame(output_ctx.get(), packet.get()),
                "write packet"
            );

            if (++packet_count % 100 == 0) {
                std::cout << std::format("Processed {} packets\r", packet_count) << std::flush;
            }
        }

        // Write trailer
        av_write_trailer(output_ctx.get());

        // Cleanup
        fs::remove(list_file);

        std::cout << std::format("\n\nTotal packets: {}\n", packet_count);
        std::cout << std::format("✓ Merge completed successfully\n");
        std::cout << std::format("Output file: {}\n", output_file.string());
    }

private:
    void create_concat_list(const std::vector<std::string>& input_files,
                           const fs::path& list_file) {
        std::ofstream list(list_file);
        if (!list.is_open()) {
            throw std::runtime_error(std::format("Failed to create list file: {}",
                                                 list_file.string()));
        }

        for (const auto& file : input_files) {
            // Escape single quotes in file path
            std::string escaped_path = file;
            size_t pos = 0;
            while ((pos = escaped_path.find('\'', pos)) != std::string::npos) {
                escaped_path.replace(pos, 1, "'\\''");
                pos += 4;
            }
            list << std::format("file '{}'\n", escaped_path);
        }
    }

    ffmpeg::FormatContextPtr open_concat_demuxer(const fs::path& list_file) {
        AVFormatContext* ctx_raw = nullptr;

        AVDictionary* options = nullptr;
        av_dict_set(&options, "safe", "0", 0);

        const auto ret = avformat_open_input(&ctx_raw,
                                            std::format("concat:{}", list_file.string()).c_str(),
                                            nullptr, &options);

        av_dict_free(&options);

        if (ret < 0) {
            // Try alternative concat protocol
            AVInputFormat* concat_fmt = const_cast<AVInputFormat*>(
                av_find_input_format("concat")
            );

            if (!concat_fmt) {
                throw ffmpeg::FFmpegError("Concat demuxer not found");
            }

            ffmpeg::check_error(
                avformat_open_input(&ctx_raw, list_file.string().c_str(),
                                  concat_fmt, nullptr),
                "open concat demuxer"
            );
        }

        auto ctx = ffmpeg::FormatContextPtr(ctx_raw);

        ffmpeg::check_error(
            avformat_find_stream_info(ctx.get(), nullptr),
            "find stream info"
        );

        return ctx;
    }
};

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <command> [options]\n\n", prog_name);
    std::cout << "Commands:\n\n";
    std::cout << "  split_time <input> <output_dir> <start1>,<end1> <start2>,<end2> ...\n";
    std::cout << "      Split video by specific time ranges\n\n";
    std::cout << "  split_duration <input> <output_dir> <segment_duration>\n";
    std::cout << "      Split video into equal duration segments\n\n";
    std::cout << "  merge <output> <input1> <input2> <input3> ...\n";
    std::cout << "      Merge multiple videos into one\n\n";
    std::cout << "Examples:\n";
    std::cout << std::format("  {} split_time video.mp4 segments 0,30 30,60 60,90\n", prog_name);
    std::cout << std::format("  {} split_duration video.mp4 segments 60\n", prog_name);
    std::cout << std::format("  {} merge output.mp4 part1.mp4 part2.mp4 part3.mp4\n", prog_name);
    std::cout << "\nTime format: seconds (e.g., 30.5 for 30.5 seconds)\n";
}

TimeRange parse_time_range(std::string_view range_str) {
    const auto comma_pos = range_str.find(',');
    if (comma_pos == std::string_view::npos) {
        throw std::invalid_argument(std::format("Invalid time range format: {}", range_str));
    }

    TimeRange range;
    range.start_seconds = std::stod(std::string(range_str.substr(0, comma_pos)));
    range.end_seconds = std::stod(std::string(range_str.substr(comma_pos + 1)));

    if (range.start_seconds >= range.end_seconds) {
        throw std::invalid_argument("Start time must be less than end time");
    }

    return range;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        const std::string_view command{argv[1]};

        if (command == "split_time") {
            if (argc < 5) {
                std::cerr << "Error: split_time requires <input> <output_dir> <time_ranges...>\n";
                return 1;
            }

            const std::string_view input_file{argv[2]};
            const fs::path output_dir{argv[3]};

            std::vector<TimeRange> ranges;
            for (int i = 4; i < argc; ++i) {
                ranges.push_back(parse_time_range(argv[i]));
            }

            VideoSplitter splitter(input_file);
            splitter.split_by_time(ranges, output_dir);

        } else if (command == "split_duration") {
            if (argc < 5) {
                std::cerr << "Error: split_duration requires <input> <output_dir> <duration>\n";
                return 1;
            }

            const std::string_view input_file{argv[2]};
            const fs::path output_dir{argv[3]};
            const double duration = std::stod(argv[4]);

            VideoSplitter splitter(input_file);
            splitter.split_by_duration(duration, output_dir);

        } else if (command == "merge") {
            if (argc < 4) {
                std::cerr << "Error: merge requires <output> <input1> <input2> ...\n";
                return 1;
            }

            const fs::path output_file{argv[2]};
            std::vector<std::string> input_files;

            for (int i = 3; i < argc; ++i) {
                input_files.emplace_back(argv[i]);
            }

            VideoMerger merger;
            merger.merge(input_files, output_file);

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
