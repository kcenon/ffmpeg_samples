/**
 * Audio Compressor
 *
 * This sample demonstrates how to apply dynamic range compression
 * to audio using FFmpeg's acompressor filter with modern C++20.
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

struct CompressorParams {
    double threshold = -20.0;        // dB threshold (default: -20dB)
    double ratio = 4.0;              // Compression ratio (default: 4:1)
    double attack = 20.0;            // Attack time in ms (default: 20ms)
    double release = 250.0;          // Release time in ms (default: 250ms)
    double makeup = 0.0;             // Makeup gain in dB (default: 0dB)
    double knee = 2.8;               // Knee width in dB (default: 2.8dB)
    std::string preset;              // Preset name
};

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <input> <output> [options]\n\n", prog_name);
    std::cout << "Options:\n";
    std::cout << "  -t, --threshold <dB>     Threshold level in dB (default: -20)\n";
    std::cout << "  -r, --ratio <ratio>      Compression ratio (default: 4.0)\n";
    std::cout << "  -a, --attack <ms>        Attack time in milliseconds (default: 20)\n";
    std::cout << "  -R, --release <ms>       Release time in milliseconds (default: 250)\n";
    std::cout << "  -m, --makeup <dB>        Makeup gain in dB (default: 0)\n";
    std::cout << "  -k, --knee <dB>          Knee width in dB (default: 2.8)\n";
    std::cout << "  -p, --preset <name>      Use preset configuration\n\n";

    std::cout << "Presets:\n";
    std::cout << "  podcast     - Optimized for voice (threshold: -18dB, ratio: 3:1)\n";
    std::cout << "  broadcast   - Radio/broadcast standard (threshold: -12dB, ratio: 4:1)\n";
    std::cout << "  music       - Gentle music compression (threshold: -24dB, ratio: 2.5:1)\n";
    std::cout << "  mastering   - Mastering compression (threshold: -8dB, ratio: 1.5:1)\n";
    std::cout << "  heavy       - Heavy compression (threshold: -15dB, ratio: 8:1)\n";
    std::cout << "  limiter     - Hard limiting (threshold: -6dB, ratio: 20:1)\n\n";

    std::cout << "Examples:\n";
    std::cout << std::format("  {} input.wav output.wav\n", prog_name);
    std::cout << "    Apply default compression settings\n\n";

    std::cout << std::format("  {} audio.mp3 compressed.mp3 -p podcast\n", prog_name);
    std::cout << "    Use podcast preset\n\n";

    std::cout << std::format("  {} input.wav output.wav -t -15 -r 6 -a 10 -R 200\n", prog_name);
    std::cout << "    Custom settings: -15dB threshold, 6:1 ratio\n\n";

    std::cout << std::format("  {} music.flac output.flac -p mastering -m 2\n", prog_name);
    std::cout << "    Mastering preset with +2dB makeup gain\n\n";

    std::cout << "Notes:\n";
    std::cout << "  - Threshold: Level above which compression is applied\n";
    std::cout << "  - Ratio: Amount of compression (4:1 = 4dB in → 1dB out)\n";
    std::cout << "  - Attack: How quickly compressor responds to peaks\n";
    std::cout << "  - Release: How quickly compressor returns to normal\n";
    std::cout << "  - Makeup gain: Compensate for volume reduction\n";
    std::cout << "  - Knee: Smooth transition around threshold (soft/hard)\n";
}

std::optional<CompressorParams> parse_preset(std::string_view preset) {
    CompressorParams params;

    if (preset == "podcast") {
        params.threshold = -18.0;
        params.ratio = 3.0;
        params.attack = 15.0;
        params.release = 200.0;
        params.makeup = 3.0;
        params.knee = 2.0;
    }
    else if (preset == "broadcast") {
        params.threshold = -12.0;
        params.ratio = 4.0;
        params.attack = 10.0;
        params.release = 150.0;
        params.makeup = 4.0;
        params.knee = 1.5;
    }
    else if (preset == "music") {
        params.threshold = -24.0;
        params.ratio = 2.5;
        params.attack = 25.0;
        params.release = 300.0;
        params.makeup = 2.0;
        params.knee = 3.5;
    }
    else if (preset == "mastering") {
        params.threshold = -8.0;
        params.ratio = 1.5;
        params.attack = 30.0;
        params.release = 400.0;
        params.makeup = 0.0;
        params.knee = 4.0;
    }
    else if (preset == "heavy") {
        params.threshold = -15.0;
        params.ratio = 8.0;
        params.attack = 5.0;
        params.release = 100.0;
        params.makeup = 6.0;
        params.knee = 1.0;
    }
    else if (preset == "limiter") {
        params.threshold = -6.0;
        params.ratio = 20.0;
        params.attack = 0.5;
        params.release = 50.0;
        params.makeup = 3.0;
        params.knee = 0.5;
    }
    else {
        return std::nullopt;
    }

    params.preset = std::string(preset);
    return params;
}

std::optional<CompressorParams> parse_arguments(int argc, char* argv[]) {
    CompressorParams params;

    for (int i = 3; i < argc; ++i) {
        const std::string_view arg = argv[i];

        if ((arg == "-t" || arg == "--threshold") && i + 1 < argc) {
            params.threshold = std::stod(argv[++i]);
        }
        else if ((arg == "-r" || arg == "--ratio") && i + 1 < argc) {
            params.ratio = std::stod(argv[++i]);
        }
        else if ((arg == "-a" || arg == "--attack") && i + 1 < argc) {
            params.attack = std::stod(argv[++i]);
        }
        else if ((arg == "-R" || arg == "--release") && i + 1 < argc) {
            params.release = std::stod(argv[++i]);
        }
        else if ((arg == "-m" || arg == "--makeup") && i + 1 < argc) {
            params.makeup = std::stod(argv[++i]);
        }
        else if ((arg == "-k" || arg == "--knee") && i + 1 < argc) {
            params.knee = std::stod(argv[++i]);
        }
        else if ((arg == "-p" || arg == "--preset") && i + 1 < argc) {
            const auto preset_params = parse_preset(argv[++i]);
            if (!preset_params) {
                std::cerr << "Error: Unknown preset\n";
                return std::nullopt;
            }
            return preset_params;
        }
        else {
            std::cerr << std::format("Error: Unknown option '{}'\n", arg);
            return std::nullopt;
        }
    }

    return params;
}

class AudioCompressor {
public:
    AudioCompressor(std::string_view input_file, std::string_view output_file,
                   const CompressorParams& params)
        : input_file_(input_file)
        , output_file_(output_file)
        , params_(params)
        , input_format_ctx_(ffmpeg::open_input_format(input_file.data()))
        , input_packet_(ffmpeg::create_packet())
        , input_frame_(ffmpeg::create_frame())
        , filtered_frame_(ffmpeg::create_frame()) {

        initialize();
    }

    void process() {
        std::cout << "Audio Dynamic Range Compression\n";
        std::cout << "================================\n\n";
        std::cout << std::format("Input: {}\n", input_file_);
        std::cout << std::format("Output: {}\n", output_file_);

        if (!params_.preset.empty()) {
            std::cout << std::format("Preset: {}\n", params_.preset);
        }

        std::cout << std::format("\nCompressor Settings:\n");
        std::cout << std::format("  Threshold: {:.1f} dB\n", params_.threshold);
        std::cout << std::format("  Ratio: {:.1f}:1\n", params_.ratio);
        std::cout << std::format("  Attack: {:.1f} ms\n", params_.attack);
        std::cout << std::format("  Release: {:.1f} ms\n", params_.release);
        std::cout << std::format("  Makeup Gain: {:.1f} dB\n", params_.makeup);
        std::cout << std::format("  Knee: {:.1f} dB\n", params_.knee);
        std::cout << std::format("\nSample Rate: {} Hz\n", input_codec_ctx_->sample_rate);
        std::cout << std::format("Channels: {}\n\n", input_codec_ctx_->ch_layout.nb_channels);

        std::cout << "Processing...\n";

        int frame_count = 0;

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

                    encode_frame();
                    frame_count++;

                    if (frame_count % 100 == 0) {
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
                    encode_frame();
                    frame_count++;
                }
            }
        }

        // Flush filter
        if (av_buffersrc_add_frame_flags(buffersrc_ctx_, nullptr, 0) >= 0) {
            while (av_buffersink_get_frame(buffersink_ctx_, filtered_frame_.get()) >= 0) {
                ffmpeg::ScopedFrameUnref filtered_guard(filtered_frame_.get());
                encode_frame();
                frame_count++;
            }
        }

        // Flush encoder
        flush_encoder();
        av_write_trailer(output_format_ctx_.get());

        std::cout << std::format("\n\nComplete!\n");
        std::cout << std::format("Processed {} frames\n", frame_count);
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
        setup_output();

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
        const auto* buffersink = avfilter_get_by_name("abuffersink");
        if (!buffersink) {
            throw std::runtime_error("Failed to find abuffersink filter");
        }

        if (avfilter_graph_create_filter(&buffersink_ctx_, buffersink, "out",
                                         nullptr, nullptr, filter_graph_.get()) < 0) {
            throw std::runtime_error("Failed to create buffer sink");
        }

        // Build acompressor filter description
        const std::string filter_desc = std::format(
            "acompressor=threshold={}dB:ratio={}:attack={}:release={}:makeup={}:knee={}",
            params_.threshold, params_.ratio,
            params_.attack, params_.release,
            params_.makeup, params_.knee);

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

    void setup_output() {
        avformat_alloc_output_context2(&output_format_ctx_raw_, nullptr,
                                       nullptr, output_file_.c_str());
        if (!output_format_ctx_raw_) {
            throw std::runtime_error("Failed to create output format context");
        }
        output_format_ctx_.reset(output_format_ctx_raw_);

        // Determine output codec based on file extension
        AVCodecID codec_id = AV_CODEC_ID_AAC;  // Default
        if (output_file_.ends_with(".mp3")) {
            codec_id = AV_CODEC_ID_MP3;
        } else if (output_file_.ends_with(".wav")) {
            codec_id = AV_CODEC_ID_PCM_S16LE;
        } else if (output_file_.ends_with(".flac")) {
            codec_id = AV_CODEC_ID_FLAC;
        }

        const auto* encoder = avcodec_find_encoder(codec_id);
        if (!encoder) {
            throw std::runtime_error("Failed to find encoder");
        }

        output_codec_ctx_ = ffmpeg::create_codec_context(encoder);

        output_codec_ctx_->sample_rate = av_buffersink_get_sample_rate(buffersink_ctx_);
        if (av_buffersink_get_ch_layout(buffersink_ctx_, &output_codec_ctx_->ch_layout) < 0) {
            throw std::runtime_error("Failed to get channel layout");
        }

        output_codec_ctx_->sample_fmt = static_cast<AVSampleFormat>(
            av_buffersink_get_format(buffersink_ctx_));
        output_codec_ctx_->time_base = {1, output_codec_ctx_->sample_rate};

        if (codec_id == AV_CODEC_ID_AAC || codec_id == AV_CODEC_ID_MP3) {
            output_codec_ctx_->bit_rate = 192000;
        }

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

    void encode_frame() {
        if (avcodec_send_frame(output_codec_ctx_.get(), filtered_frame_.get()) < 0) {
            return;
        }

        while (avcodec_receive_packet(output_codec_ctx_.get(), output_packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(output_packet_.get());

            output_packet_->stream_index = 0;
            av_packet_rescale_ts(output_packet_.get(),
                               output_codec_ctx_->time_base,
                               output_format_ctx_->streams[0]->time_base);

            av_interleaved_write_frame(output_format_ctx_.get(), output_packet_.get());
        }
    }

    void flush_encoder() {
        avcodec_send_frame(output_codec_ctx_.get(), nullptr);

        while (avcodec_receive_packet(output_codec_ctx_.get(), output_packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(output_packet_.get());

            output_packet_->stream_index = 0;
            av_packet_rescale_ts(output_packet_.get(),
                               output_codec_ctx_->time_base,
                               output_format_ctx_->streams[0]->time_base);

            av_interleaved_write_frame(output_format_ctx_.get(), output_packet_.get());
        }
    }

    std::string input_file_;
    std::string output_file_;
    CompressorParams params_;

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

        AudioCompressor compressor(input, output, *params);
        compressor.process();

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }
}
