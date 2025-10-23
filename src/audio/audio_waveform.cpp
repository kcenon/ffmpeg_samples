/**
 * Audio Waveform Visualizer
 *
 * This sample demonstrates how to create audio waveform visualization videos
 * using FFmpeg's showwaves and showwavespic filters with modern C++20.
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <fstream>
#include <format>
#include <string>
#include <string_view>
#include <filesystem>
#include <optional>

extern "C" {
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

namespace fs = std::filesystem;

namespace {

struct WaveformParams {
    int width = 1280;               // Output width
    int height = 720;               // Output height
    int fps = 25;                   // Frame rate
    std::string mode = "p2p";       // Waveform mode
    std::string colors = "red";     // Waveform colors
    std::string scale = "lin";      // Scale mode (lin, log, sqrt, cbrt)
    bool split_channels = false;    // Split channels vertically
    bool static_image = false;      // Generate static waveform image
};

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <input> <output> [options]\n\n", prog_name);
    std::cout << "Options:\n";
    std::cout << "  -s, --size <WxH>         Output size (default: 1280x720)\n";
    std::cout << "  -r, --fps <fps>          Frame rate (default: 25)\n";
    std::cout << "  -m, --mode <mode>        Waveform mode (default: p2p)\n";
    std::cout << "                           point, line, p2p, cline\n";
    std::cout << "  -c, --colors <colors>    Colors for channels (default: red)\n";
    std::cout << "                           Examples: red, blue, \"red|green\", \"0xff0000|0x00ff00\"\n";
    std::cout << "  --scale <scale>          Scale mode: lin, log, sqrt, cbrt (default: lin)\n";
    std::cout << "  --split                  Split channels vertically\n";
    std::cout << "  --static                 Generate static waveform image (PNG)\n\n";

    std::cout << "Waveform Modes:\n";
    std::cout << "  point  - Individual sample points\n";
    std::cout << "  line   - Line connecting samples\n";
    std::cout << "  p2p    - Peak to peak (vertical lines)\n";
    std::cout << "  cline  - Centered line (best for music)\n\n";

    std::cout << "Examples:\n";
    std::cout << std::format("  {} audio.mp3 waveform.mp4\n", prog_name);
    std::cout << "    Create waveform video with default settings\n\n";

    std::cout << std::format("  {} audio.wav output.mp4 -m cline -c \"red|green\"\n", prog_name);
    std::cout << "    Centered line mode with red/green for stereo channels\n\n";

    std::cout << std::format("  {} audio.mp3 waveform.png --static -s 1920x1080\n", prog_name);
    std::cout << "    Generate static waveform image\n\n";

    std::cout << std::format("  {} input.wav output.mp4 --split --scale sqrt\n", prog_name);
    std::cout << "    Split channels with square root scale\n\n";

    std::cout << "Notes:\n";
    std::cout << "  - Static mode uses showwavespic (entire audio in one image)\n";
    std::cout << "  - Video mode uses showwaves (animated waveform)\n";
    std::cout << "  - Use multiple colors for multi-channel audio\n";
    std::cout << "  - Scale affects amplitude display\n";
}

std::optional<WaveformParams> parse_arguments(int argc, char* argv[]) {
    WaveformParams params;

    for (int i = 3; i < argc; ++i) {
        const std::string_view arg = argv[i];

        if ((arg == "-s" || arg == "--size") && i + 1 < argc) {
            const std::string size_str = argv[++i];
            const auto x_pos = size_str.find('x');
            if (x_pos != std::string::npos) {
                params.width = std::stoi(size_str.substr(0, x_pos));
                params.height = std::stoi(size_str.substr(x_pos + 1));
            } else {
                std::cerr << "Error: Invalid size format. Use WxH\n";
                return std::nullopt;
            }
        }
        else if ((arg == "-r" || arg == "--fps") && i + 1 < argc) {
            params.fps = std::stoi(argv[++i]);
        }
        else if ((arg == "-m" || arg == "--mode") && i + 1 < argc) {
            params.mode = argv[++i];
        }
        else if ((arg == "-c" || arg == "--colors") && i + 1 < argc) {
            params.colors = argv[++i];
        }
        else if (arg == "--scale" && i + 1 < argc) {
            params.scale = argv[++i];
        }
        else if (arg == "--split") {
            params.split_channels = true;
        }
        else if (arg == "--static") {
            params.static_image = true;
        }
        else {
            std::cerr << std::format("Error: Unknown option '{}'\n", arg);
            return std::nullopt;
        }
    }

    return params;
}

class AudioWaveformVisualizer {
public:
    AudioWaveformVisualizer(std::string_view input_file, std::string_view output_file,
                           const WaveformParams& params)
        : input_file_(input_file)
        , output_file_(output_file)
        , params_(params)
        , input_format_ctx_(ffmpeg::open_input_format(input_file.data()))
        , input_packet_(ffmpeg::create_packet())
        , input_frame_(ffmpeg::create_frame())
        , filtered_frame_(ffmpeg::create_frame()) {

        initialize();
    }

    void generate() {
        std::cout << "Audio Waveform Visualization\n";
        std::cout << "=============================\n\n";
        std::cout << std::format("Input: {}\n", input_file_);
        std::cout << std::format("Output: {}\n", output_file_);
        std::cout << std::format("Size: {}x{}\n", params_.width, params_.height);
        std::cout << std::format("Mode: {}\n", params_.mode);
        std::cout << std::format("Type: {}\n", params_.static_image ? "static image" : "video");
        std::cout << std::format("Sample Rate: {} Hz\n", input_codec_ctx_->sample_rate);
        std::cout << std::format("Channels: {}\n\n", input_codec_ctx_->ch_layout.nb_channels);

        std::cout << "Processing...\n";

        int frame_count = 0;
        int64_t video_pts = 0;

        while (av_read_frame(input_format_ctx_.get(), input_packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(input_packet_.get());

            if (input_packet_->stream_index != audio_stream_index_) {
                continue;
            }

            if (avcodec_send_packet(input_codec_ctx_.get(), input_packet_.get()) < 0) {
                continue;
            }

            while (avcodec_receive_frame(input_codec_ctx_.get(), input_frame_.get()) >= 0) {
                ffmpeg::ScopedFrameUnref frame_guard(input_frame_.get());

                // Push frame to filter
                if (av_buffersrc_add_frame_flags(buffersrc_ctx_, input_frame_.get(),
                                                AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                    continue;
                }

                // Pull filtered frames
                while (av_buffersink_get_frame(buffersink_ctx_, filtered_frame_.get()) >= 0) {
                    ffmpeg::ScopedFrameUnref filtered_guard(filtered_frame_.get());

                    if (!params_.static_image) {
                        filtered_frame_->pts = video_pts++;
                    }

                    encode_frame();
                    frame_count++;

                    if (!params_.static_image && frame_count % 25 == 0) {
                        std::cout << std::format("\rFrames: {}", frame_count) << std::flush;
                    }
                }
            }
        }

        // Flush decoder
        avcodec_send_packet(input_codec_ctx_.get(), nullptr);
        while (avcodec_receive_frame(input_codec_ctx_.get(), input_frame_.get()) >= 0) {
            ffmpeg::ScopedFrameUnref frame_guard(input_frame_.get());

            if (av_buffersrc_add_frame_flags(buffersrc_ctx_, input_frame_.get(),
                                            AV_BUFFERSRC_FLAG_KEEP_REF) >= 0) {
                while (av_buffersink_get_frame(buffersink_ctx_, filtered_frame_.get()) >= 0) {
                    ffmpeg::ScopedFrameUnref filtered_guard(filtered_frame_.get());

                    if (!params_.static_image) {
                        filtered_frame_->pts = video_pts++;
                    }

                    encode_frame();
                    frame_count++;
                }
            }
        }

        // Flush filter
        if (av_buffersrc_add_frame_flags(buffersrc_ctx_, nullptr, 0) >= 0) {
            while (av_buffersink_get_frame(buffersink_ctx_, filtered_frame_.get()) >= 0) {
                ffmpeg::ScopedFrameUnref filtered_guard(filtered_frame_.get());

                if (!params_.static_image) {
                    filtered_frame_->pts = video_pts++;
                }

                encode_frame();
                frame_count++;
            }
        }

        // Flush encoder
        flush_encoder();

        if (!params_.static_image) {
            av_write_trailer(output_format_ctx_.get());
        }

        std::cout << std::format("\n\nComplete!\n");
        std::cout << std::format("Generated {} frame(s)\n", frame_count);
        std::cout << std::format("Output: {}\n", output_file_);
    }

private:
    void initialize() {
        // Find audio stream
        audio_stream_index_ = av_find_best_stream(
            input_format_ctx_.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

        if (audio_stream_index_ < 0) {
            throw std::runtime_error("No audio stream found");
        }

        auto* input_stream = input_format_ctx_->streams[audio_stream_index_];

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

        // Setup filter graph
        setup_filter_graph();

        // Setup output
        if (params_.static_image) {
            setup_image_output();
        } else {
            setup_video_output();
        }

        output_packet_ = ffmpeg::create_packet();
    }

    void setup_filter_graph() {
        filter_graph_.reset(avfilter_graph_alloc());
        if (!filter_graph_) {
            throw std::runtime_error("Failed to allocate filter graph");
        }

        // Create buffer source
        const auto* buffersrc = avfilter_get_by_name("abuffer");
        if (!buffersrc) {
            throw std::runtime_error("Failed to find abuffer filter");
        }

        char ch_layout_str[64];
        av_channel_layout_describe(&input_codec_ctx_->ch_layout, ch_layout_str, sizeof(ch_layout_str));

        const std::string args = std::format(
            "time_base={}/{}:sample_rate={}:sample_fmt={}:channel_layout={}",
            input_codec_ctx_->time_base.num, input_codec_ctx_->time_base.den,
            input_codec_ctx_->sample_rate,
            av_get_sample_fmt_name(input_codec_ctx_->sample_fmt),
            ch_layout_str);

        if (avfilter_graph_create_filter(&buffersrc_ctx_, buffersrc, "in",
                                         args.c_str(), nullptr, filter_graph_.get()) < 0) {
            throw std::runtime_error("Failed to create buffer source");
        }

        // Create buffer sink
        const auto* buffersink = avfilter_get_by_name("buffersink");
        if (!buffersink) {
            throw std::runtime_error("Failed to find buffersink filter");
        }

        if (avfilter_graph_create_filter(&buffersink_ctx_, buffersink, "out",
                                         nullptr, nullptr, filter_graph_.get()) < 0) {
            throw std::runtime_error("Failed to create buffer sink");
        }

        // Build filter description
        std::string filter_desc;

        if (params_.static_image) {
            // Use showwavespic for static image
            filter_desc = std::format("showwavespic=s={}x{}:colors={}:scale={}",
                                     params_.width, params_.height,
                                     params_.colors, params_.scale);

            if (params_.split_channels) {
                filter_desc += ":split_channels=1";
            }
        } else {
            // Use showwaves for video
            filter_desc = std::format("showwaves=s={}x{}:mode={}:rate={}:colors={}:scale={}",
                                     params_.width, params_.height,
                                     params_.mode, params_.fps,
                                     params_.colors, params_.scale);

            if (params_.split_channels) {
                filter_desc += ":split_channels=1";
            }
        }

        std::cout << std::format("Filter: {}\n", filter_desc);

        // Parse filter description
        AVFilterInOut* outputs = avfilter_inout_alloc();
        AVFilterInOut* inputs = avfilter_inout_alloc();

        outputs->name = av_strdup("in");
        outputs->filter_ctx = buffersrc_ctx_;
        outputs->pad_idx = 0;
        outputs->next = nullptr;

        inputs->name = av_strdup("out");
        inputs->filter_ctx = buffersink_ctx_;
        inputs->pad_idx = 0;
        inputs->next = nullptr;

        if (avfilter_graph_parse_ptr(filter_graph_.get(), filter_desc.c_str(),
                                     &inputs, &outputs, nullptr) < 0) {
            avfilter_inout_free(&inputs);
            avfilter_inout_free(&outputs);
            throw std::runtime_error("Failed to parse filter graph");
        }

        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);

        if (avfilter_graph_config(filter_graph_.get(), nullptr) < 0) {
            throw std::runtime_error("Failed to configure filter graph");
        }
    }

    void setup_video_output() {
        avformat_alloc_output_context2(&output_format_ctx_raw_, nullptr,
                                       nullptr, output_file_.c_str());
        if (!output_format_ctx_raw_) {
            throw std::runtime_error("Failed to create output format context");
        }
        output_format_ctx_.reset(output_format_ctx_raw_);

        const auto* encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!encoder) {
            throw std::runtime_error("H.264 encoder not found");
        }

        output_codec_ctx_ = ffmpeg::create_codec_context(encoder);
        output_codec_ctx_->width = params_.width;
        output_codec_ctx_->height = params_.height;
        output_codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
        output_codec_ctx_->time_base = {1, params_.fps};
        output_codec_ctx_->framerate = {params_.fps, 1};
        output_codec_ctx_->bit_rate = 2000000;

        if (output_format_ctx_->oformat->flags & AVFMT_GLOBALHEADER) {
            output_codec_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        if (avcodec_open2(output_codec_ctx_.get(), encoder, nullptr) < 0) {
            throw std::runtime_error("Failed to open encoder");
        }

        auto* out_stream = avformat_new_stream(output_format_ctx_.get(), nullptr);
        if (!out_stream) {
            throw std::runtime_error("Failed to create output stream");
        }

        avcodec_parameters_from_context(out_stream->codecpar, output_codec_ctx_.get());
        out_stream->time_base = output_codec_ctx_->time_base;

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

    void setup_image_output() {
        // For PNG output, we don't need format context
        const auto* encoder = avcodec_find_encoder(AV_CODEC_ID_PNG);
        if (!encoder) {
            throw std::runtime_error("PNG encoder not found");
        }

        output_codec_ctx_ = ffmpeg::create_codec_context(encoder);
        output_codec_ctx_->width = params_.width;
        output_codec_ctx_->height = params_.height;
        output_codec_ctx_->pix_fmt = AV_PIX_FMT_RGB24;
        output_codec_ctx_->time_base = {1, 1};

        if (avcodec_open2(output_codec_ctx_.get(), encoder, nullptr) < 0) {
            throw std::runtime_error("Failed to open PNG encoder");
        }
    }

    void encode_frame() {
        if (avcodec_send_frame(output_codec_ctx_.get(), filtered_frame_.get()) < 0) {
            return;
        }

        while (avcodec_receive_packet(output_codec_ctx_.get(), output_packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(output_packet_.get());

            if (params_.static_image) {
                // Write PNG to file
                std::ofstream out(output_file_, std::ios::binary);
                if (out.is_open()) {
                    out.write(reinterpret_cast<const char*>(output_packet_->data),
                             output_packet_->size);
                    out.close();
                }
            } else {
                // Write to video stream
                output_packet_->stream_index = 0;
                av_packet_rescale_ts(output_packet_.get(),
                                   output_codec_ctx_->time_base,
                                   output_format_ctx_->streams[0]->time_base);

                av_interleaved_write_frame(output_format_ctx_.get(), output_packet_.get());
            }
        }
    }

    void flush_encoder() {
        avcodec_send_frame(output_codec_ctx_.get(), nullptr);

        while (avcodec_receive_packet(output_codec_ctx_.get(), output_packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(output_packet_.get());

            if (!params_.static_image) {
                output_packet_->stream_index = 0;
                av_packet_rescale_ts(output_packet_.get(),
                                   output_codec_ctx_->time_base,
                                   output_format_ctx_->streams[0]->time_base);

                av_interleaved_write_frame(output_format_ctx_.get(), output_packet_.get());
            }
        }
    }

    std::string input_file_;
    std::string output_file_;
    WaveformParams params_;

    ffmpeg::FormatContextPtr input_format_ctx_;
    ffmpeg::CodecContextPtr input_codec_ctx_;
    ffmpeg::PacketPtr input_packet_;
    ffmpeg::FramePtr input_frame_;
    ffmpeg::FramePtr filtered_frame_;

    ffmpeg::FilterGraphPtr filter_graph_;
    AVFilterContext* buffersrc_ctx_ = nullptr;
    AVFilterContext* buffersink_ctx_ = nullptr;

    AVFormatContext* output_format_ctx_raw_ = nullptr;
    ffmpeg::FormatContextPtr output_format_ctx_;
    ffmpeg::CodecContextPtr output_codec_ctx_;
    ffmpeg::PacketPtr output_packet_;

    int audio_stream_index_ = -1;
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

        const auto params = parse_arguments(argc, argv);
        if (!params) {
            print_usage(argv[0]);
            return 1;
        }

        AudioWaveformVisualizer visualizer(input, output, *params);
        visualizer.generate();

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }
}
