/**
 * Video Metadata Editor
 *
 * This sample demonstrates how to read and write metadata in video files
 * using modern C++20 and FFmpeg libraries.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <format>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace {

class MetadataReader {
public:
    explicit MetadataReader(std::string_view input_file)
        : format_ctx_(ffmpeg::open_input_format(input_file.data())) {
    }

    void display_all() const {
        std::cout << "==========================================\n";
        std::cout << "Video File Metadata\n";
        std::cout << "==========================================\n\n";

        std::cout << std::format("File: {}\n", format_ctx_->url);
        std::cout << std::format("Format: {}\n", format_ctx_->iformat->long_name);

        if (format_ctx_->duration != AV_NOPTS_VALUE) {
            const auto duration = format_ctx_->duration / static_cast<double>(AV_TIME_BASE);
            const auto hours = static_cast<int>(duration / 3600);
            const auto minutes = static_cast<int>((duration - hours * 3600) / 60);
            const auto seconds = static_cast<int>(duration - hours * 3600 - minutes * 60);
            std::cout << std::format("Duration: {:02}:{:02}:{:02}\n", hours, minutes, seconds);
        }

        if (format_ctx_->bit_rate > 0) {
            std::cout << std::format("Bitrate: {} kbps\n", format_ctx_->bit_rate / 1000);
        }

        std::cout << std::format("Streams: {}\n\n", format_ctx_->nb_streams);

        // Display metadata
        std::cout << "Metadata Tags:\n";
        std::cout << "----------------------------------------\n";

        if (auto tags = get_all_metadata(); !tags.empty()) {
            for (const auto& [key, value] : tags) {
                std::cout << std::format("  {:<20} : {}\n", key, value);
            }
        } else {
            std::cout << "  (No metadata tags found)\n";
        }

        std::cout << "\n";

        // Display stream information
        display_streams();
    }

    std::unordered_map<std::string, std::string> get_all_metadata() const {
        std::unordered_map<std::string, std::string> metadata;

        AVDictionaryEntry* tag = nullptr;
        while ((tag = av_dict_get(format_ctx_->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
            metadata[tag->key] = tag->value;
        }

        return metadata;
    }

    std::string get_metadata(std::string_view key) const {
        const auto* entry = av_dict_get(format_ctx_->metadata, key.data(), nullptr, 0);
        return entry ? entry->value : "";
    }

private:
    void display_streams() const {
        std::cout << "Stream Information:\n";
        std::cout << "----------------------------------------\n";

        for (unsigned int i = 0; i < format_ctx_->nb_streams; ++i) {
            const auto* stream = format_ctx_->streams[i];
            const auto* codecpar = stream->codecpar;
            const auto* codec = avcodec_find_decoder(codecpar->codec_id);

            std::cout << std::format("Stream #{}:\n", i);
            std::cout << std::format("  Type: {}\n", av_get_media_type_string(codecpar->codec_type));
            std::cout << std::format("  Codec: {}\n", codec ? codec->long_name : "unknown");

            if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                std::cout << std::format("  Resolution: {}x{}\n", codecpar->width, codecpar->height);
                if (stream->avg_frame_rate.den && stream->avg_frame_rate.num) {
                    const auto fps = av_q2d(stream->avg_frame_rate);
                    std::cout << std::format("  Frame Rate: {:.2f} fps\n", fps);
                }
            } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                std::cout << std::format("  Sample Rate: {} Hz\n", codecpar->sample_rate);
                std::cout << std::format("  Channels: {}\n", codecpar->ch_layout.nb_channels);
            }

            // Stream metadata
            AVDictionaryEntry* tag = nullptr;
            bool has_stream_metadata = false;
            while ((tag = av_dict_get(stream->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
                if (!has_stream_metadata) {
                    std::cout << "  Metadata:\n";
                    has_stream_metadata = true;
                }
                std::cout << std::format("    {}: {}\n", tag->key, tag->value);
            }

            std::cout << "\n";
        }
    }

    ffmpeg::FormatContextPtr format_ctx_;
};

class MetadataWriter {
public:
    MetadataWriter(std::string_view input_file, std::string_view output_file)
        : input_file_(input_file)
        , output_file_(output_file) {
    }

    void set_metadata(std::string_view key, std::string_view value) {
        metadata_updates_[std::string(key)] = std::string(value);
    }

    void remove_metadata(std::string_view key) {
        metadata_removals_.emplace_back(key);
    }

    void clear_all_metadata() {
        clear_all_ = true;
    }

    void apply() {
        std::cout << "Updating metadata...\n\n";

        // Open input
        auto input_ctx = ffmpeg::open_input_format(input_file_.data());

        // Create output context
        AVFormatContext* output_ctx_raw = nullptr;
        ffmpeg::check_error(
            avformat_alloc_output_context2(&output_ctx_raw, nullptr, nullptr, output_file_.data()),
            "allocate output context"
        );
        auto output_ctx = ffmpeg::FormatContextPtr(output_ctx_raw);

        // Copy streams
        for (unsigned int i = 0; i < input_ctx->nb_streams; ++i) {
            const auto* in_stream = input_ctx->streams[i];
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

            // Copy stream metadata
            av_dict_copy(&out_stream->metadata, in_stream->metadata, 0);
        }

        // Handle metadata updates
        if (clear_all_) {
            std::cout << "Clearing all metadata\n";
        } else {
            // Copy existing metadata
            av_dict_copy(&output_ctx->metadata, input_ctx->metadata, 0);

            // Remove specified keys
            for (const auto& key : metadata_removals_) {
                std::cout << std::format("Removing: {}\n", key);
                av_dict_set(&output_ctx->metadata, key.c_str(), nullptr, 0);
            }
        }

        // Apply new metadata
        for (const auto& [key, value] : metadata_updates_) {
            std::cout << std::format("Setting: {} = {}\n", key, value);
            av_dict_set(&output_ctx->metadata, key.c_str(), value.c_str(), 0);
        }

        std::cout << "\n";

        // Open output file
        if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
            ffmpeg::check_error(
                avio_open(&output_ctx->pb, output_file_.data(), AVIO_FLAG_WRITE),
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
        int packet_count = 0;

        std::cout << "Copying video data...\n";

        while (av_read_frame(input_ctx.get(), packet.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(packet.get());

            const auto* in_stream = input_ctx->streams[packet->stream_index];
            auto* out_stream = output_ctx->streams[packet->stream_index];

            // Rescale timestamps
            av_packet_rescale_ts(packet.get(), in_stream->time_base, out_stream->time_base);
            packet->pos = -1;

            ffmpeg::check_error(
                av_interleaved_write_frame(output_ctx.get(), packet.get()),
                "write packet"
            );

            if (++packet_count % 100 == 0) {
                std::cout << std::format("Processed {} packets\r", packet_count) << std::flush;
            }
        }

        std::cout << std::format("\nTotal packets: {}\n", packet_count);

        // Write trailer
        ffmpeg::check_error(
            av_write_trailer(output_ctx.get()),
            "write trailer"
        );

        std::cout << std::format("\n✓ Metadata updated successfully\n");
        std::cout << std::format("Output file: {}\n", output_file_);
    }

private:
    std::string input_file_;
    std::string output_file_;
    std::unordered_map<std::string, std::string> metadata_updates_;
    std::vector<std::string> metadata_removals_;
    bool clear_all_ = false;
};

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <command> <input_file> [options]\n\n", prog_name);
    std::cout << "Commands:\n";
    std::cout << "  show <input_file>\n";
    std::cout << "      Display all metadata\n\n";
    std::cout << "  get <input_file> <key>\n";
    std::cout << "      Get specific metadata value\n\n";
    std::cout << "  set <input_file> <output_file> <key> <value>\n";
    std::cout << "      Set metadata value\n\n";
    std::cout << "  remove <input_file> <output_file> <key>\n";
    std::cout << "      Remove metadata key\n\n";
    std::cout << "  clear <input_file> <output_file>\n";
    std::cout << "      Remove all metadata\n\n";
    std::cout << "Common metadata keys:\n";
    std::cout << "  title, artist, album, date, genre, comment, copyright,\n";
    std::cout << "  description, language, encoder, author, composer\n\n";
    std::cout << "Examples:\n";
    std::cout << std::format("  {} show video.mp4\n", prog_name);
    std::cout << std::format("  {} get video.mp4 title\n", prog_name);
    std::cout << std::format("  {} set video.mp4 output.mp4 title \"My Video\"\n", prog_name);
    std::cout << std::format("  {} remove video.mp4 output.mp4 comment\n", prog_name);
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        const std::string_view command{argv[1]};
        const std::string_view input_file{argv[2]};

        if (command == "show") {
            MetadataReader reader(input_file);
            reader.display_all();

        } else if (command == "get") {
            if (argc < 4) {
                std::cerr << "Error: get command requires <key>\n";
                return 1;
            }
            const std::string_view key{argv[3]};

            MetadataReader reader(input_file);
            const auto value = reader.get_metadata(key);

            if (!value.empty()) {
                std::cout << std::format("{}: {}\n", key, value);
            } else {
                std::cout << std::format("Metadata key '{}' not found\n", key);
            }

        } else if (command == "set") {
            if (argc < 6) {
                std::cerr << "Error: set command requires <output_file> <key> <value>\n";
                return 1;
            }
            const std::string_view output_file{argv[3]};
            const std::string_view key{argv[4]};
            const std::string_view value{argv[5]};

            MetadataWriter writer(input_file, output_file);
            writer.set_metadata(key, value);
            writer.apply();

        } else if (command == "remove") {
            if (argc < 5) {
                std::cerr << "Error: remove command requires <output_file> <key>\n";
                return 1;
            }
            const std::string_view output_file{argv[3]};
            const std::string_view key{argv[4]};

            MetadataWriter writer(input_file, output_file);
            writer.remove_metadata(key);
            writer.apply();

        } else if (command == "clear") {
            if (argc < 4) {
                std::cerr << "Error: clear command requires <output_file>\n";
                return 1;
            }
            const std::string_view output_file{argv[3]};

            MetadataWriter writer(input_file, output_file);
            writer.clear_all_metadata();
            writer.apply();

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
