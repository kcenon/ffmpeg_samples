/**
 * Audio Mastering
 *
 * This sample demonstrates a complete audio mastering chain using
 * modern C++20 and FFmpeg filters. It combines multiple processing
 * stages to achieve professional mastering results.
 *
 * Mastering Chain:
 * 1. High-pass filter (DC offset removal)
 * 2. Equalization (tonal shaping)
 * 3. Multiband compression (dynamic processing)
 * 4. Loudness normalization (target LUFS)
 * 5. True peak limiting (final safety)
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
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

namespace fs = std::filesystem;

namespace {

// Mastering presets
enum class MasteringPreset {
    CUSTOM,
    STREAMING,      // -14 LUFS (Spotify, Apple Music)
    BROADCAST,      // -23 LUFS (EBU R128)
    CD,             // -9 LUFS (traditional CD mastering)
    PODCAST,        // -16 LUFS (podcast standard)
    YOUTUBE,        // -13 LUFS (YouTube optimization)
    AUDIOBOOK       // -18 LUFS (audiobook standard)
};

// Mastering parameters
struct MasteringParams {
    // Preset
    MasteringPreset preset = MasteringPreset::STREAMING;

    // High-pass filter
    bool enable_highpass = true;
    int highpass_freq = 30;         // Hz

    // EQ settings
    bool enable_eq = false;
    std::string eq_preset = "flat";
    double bass_gain = 0.0;         // dB (-12 to +12)
    double mid_gain = 0.0;          // dB
    double treble_gain = 0.0;       // dB

    // Compression
    bool enable_compression = true;
    double comp_threshold = -24.0;  // dB
    double comp_ratio = 2.0;
    double comp_attack = 20.0;      // ms
    double comp_release = 250.0;    // ms

    // Loudness normalization
    double target_lufs = -14.0;     // Target loudness in LUFS
    double max_true_peak = -1.0;    // dBTP

    // Limiter
    bool enable_limiter = true;
    double limiter_threshold = -1.0;    // dB
    double limiter_ceiling = -0.1;      // dB

    // Stereo processing
    bool enable_stereo_width = false;
    double stereo_width = 1.0;      // 0.0 (mono) to 2.0 (wide)

    // Analysis
    bool print_stats = false;
};

void apply_preset(MasteringParams& params, MasteringPreset preset) {
    params.preset = preset;

    switch (preset) {
        case MasteringPreset::STREAMING:
            params.target_lufs = -14.0;
            params.max_true_peak = -1.0;
            params.comp_threshold = -24.0;
            params.comp_ratio = 2.0;
            params.limiter_threshold = -1.0;
            params.limiter_ceiling = -0.1;
            break;

        case MasteringPreset::BROADCAST:
            params.target_lufs = -23.0;
            params.max_true_peak = -1.0;
            params.comp_threshold = -28.0;
            params.comp_ratio = 1.5;
            params.limiter_threshold = -2.0;
            params.limiter_ceiling = -1.0;
            break;

        case MasteringPreset::CD:
            params.target_lufs = -9.0;
            params.max_true_peak = -0.3;
            params.comp_threshold = -18.0;
            params.comp_ratio = 2.5;
            params.limiter_threshold = -0.5;
            params.limiter_ceiling = -0.1;
            break;

        case MasteringPreset::PODCAST:
            params.target_lufs = -16.0;
            params.max_true_peak = -1.0;
            params.comp_threshold = -20.0;
            params.comp_ratio = 3.0;
            params.limiter_threshold = -2.0;
            params.limiter_ceiling = -1.0;
            params.enable_eq = true;
            params.bass_gain = -2.0;    // Reduce rumble
            params.mid_gain = 2.0;      // Enhance voice
            break;

        case MasteringPreset::YOUTUBE:
            params.target_lufs = -13.0;
            params.max_true_peak = -1.0;
            params.comp_threshold = -22.0;
            params.comp_ratio = 2.5;
            params.limiter_threshold = -1.0;
            params.limiter_ceiling = -0.5;
            break;

        case MasteringPreset::AUDIOBOOK:
            params.target_lufs = -18.0;
            params.max_true_peak = -1.5;
            params.comp_threshold = -22.0;
            params.comp_ratio = 2.0;
            params.limiter_threshold = -3.0;
            params.limiter_ceiling = -1.5;
            params.enable_eq = true;
            params.bass_gain = -3.0;    // Reduce low end
            params.mid_gain = 3.0;      // Enhance voice clarity
            break;

        case MasteringPreset::CUSTOM:
            // Keep user settings
            break;
    }
}

std::string build_filter_chain(const MasteringParams& params,
                                int sample_rate,
                                const std::string& channel_layout) {
    std::vector<std::string> filters;

    // 1. High-pass filter (DC offset removal)
    if (params.enable_highpass) {
        filters.push_back(std::format("highpass=f={}:poles=2", params.highpass_freq));
    }

    // 2. EQ (3-band parametric)
    if (params.enable_eq) {
        if (params.bass_gain != 0.0) {
            // Low shelf at 100Hz
            filters.push_back(std::format("equalizer=f=100:t=s:w=1:g={}", params.bass_gain));
        }
        if (params.mid_gain != 0.0) {
            // Peak at 1kHz
            filters.push_back(std::format("equalizer=f=1000:t=q:w=2:g={}", params.mid_gain));
        }
        if (params.treble_gain != 0.0) {
            // High shelf at 8kHz
            filters.push_back(std::format("equalizer=f=8000:t=s:w=1:g={}", params.treble_gain));
        }
    }

    // 3. Compression
    if (params.enable_compression) {
        filters.push_back(std::format(
            "acompressor=threshold={}dB:ratio={}:attack={}:release={}:makeup=4dB",
            params.comp_threshold,
            params.comp_ratio,
            params.comp_attack,
            params.comp_release
        ));
    }

    // 4. Stereo width adjustment
    if (params.enable_stereo_width && params.stereo_width != 1.0) {
        filters.push_back(std::format("stereotools=mlev={}", params.stereo_width));
    }

    // 5. Loudness normalization (two-pass)
    filters.push_back(std::format(
        "loudnorm=I={}:TP={}:LRA=11:print_format=summary",
        params.target_lufs,
        params.max_true_peak
    ));

    // 6. Final limiting
    if (params.enable_limiter) {
        filters.push_back(std::format(
            "alimiter=limit={}dB:attack=5:release=50:level_in=1:level_out=1",
            params.limiter_ceiling
        ));
    }

    // Join all filters
    std::string filter_chain;
    for (size_t i = 0; i < filters.size(); ++i) {
        filter_chain += filters[i];
        if (i < filters.size() - 1) {
            filter_chain += ",";
        }
    }

    return filter_chain;
}

class AudioMastering {
public:
    AudioMastering(const fs::path& input_file,
                   const fs::path& output_file,
                   const MasteringParams& params)
        : input_file_(input_file)
        , output_file_(output_file)
        , params_(params)
        , format_ctx_(ffmpeg::open_input_format(input_file.string().c_str()))
        , packet_(ffmpeg::create_packet())
        , frame_(ffmpeg::create_frame())
        , filtered_frame_(ffmpeg::create_frame()) {

        initialize();
    }

    void process() {
        print_processing_info();

        // Open output file
        auto* output_codec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
        if (!output_codec) {
            throw ffmpeg::FFmpegError("PCM encoder not found");
        }

        // Setup encoder
        encoder_ctx_ = ffmpeg::create_codec_context(output_codec);
        encoder_ctx_->sample_rate = decoder_ctx_->sample_rate;
        encoder_ctx_->ch_layout = decoder_ctx_->ch_layout;
        encoder_ctx_->sample_fmt = AV_SAMPLE_FMT_S16;
        encoder_ctx_->time_base = {1, decoder_ctx_->sample_rate};

        ffmpeg::check_error(
            avcodec_open2(encoder_ctx_.get(), output_codec, nullptr),
            "open encoder"
        );

        // Create output format context
        AVFormatContext* out_fmt_ctx_raw = nullptr;
        ffmpeg::check_error(
            avformat_alloc_output_context2(&out_fmt_ctx_raw, nullptr,
                                          "wav", output_file_.string().c_str()),
            "allocate output context"
        );
        output_format_ctx_.reset(out_fmt_ctx_raw);

        // Create output stream
        auto* out_stream = avformat_new_stream(output_format_ctx_.get(), nullptr);
        if (!out_stream) {
            throw ffmpeg::FFmpegError("Failed to create output stream");
        }

        ffmpeg::check_error(
            avcodec_parameters_from_context(out_stream->codecpar, encoder_ctx_.get()),
            "copy encoder parameters"
        );

        // Open output file
        ffmpeg::check_error(
            avio_open(&output_format_ctx_->pb, output_file_.string().c_str(),
                     AVIO_FLAG_WRITE),
            "open output file"
        );

        ffmpeg::check_error(
            avformat_write_header(output_format_ctx_.get(), nullptr),
            "write output header"
        );

        // Process audio
        std::cout << "\nProcessing...\n";
        int64_t samples_processed = 0;
        int iteration = 0;

        // Decode, filter, and encode
        while (av_read_frame(format_ctx_.get(), packet_.get()) >= 0) {
            if (packet_->stream_index == audio_stream_index_) {
                ffmpeg::check_error(
                    avcodec_send_packet(decoder_ctx_.get(), packet_.get()),
                    "send packet to decoder"
                );

                while (avcodec_receive_frame(decoder_ctx_.get(), frame_.get()) >= 0) {
                    // Push frame to filter
                    ffmpeg::check_error(
                        av_buffersrc_add_frame_flags(buffersrc_ctx_, frame_.get(),
                                                    AV_BUFFERSRC_FLAG_KEEP_REF),
                        "feed filter graph"
                    );

                    // Pull filtered frames
                    while (av_buffersink_get_frame(buffersink_ctx_, filtered_frame_.get()) >= 0) {
                        // Encode filtered frame
                        encode_and_write_frame(filtered_frame_.get());
                        samples_processed += filtered_frame_->nb_samples;
                        av_frame_unref(filtered_frame_.get());

                        ++iteration;
                        if (iteration % 100 == 0) {
                            const auto seconds = samples_processed /
                                               static_cast<double>(decoder_ctx_->sample_rate);
                            std::cout << std::format("Processed: {:.2f}s\r", seconds)
                                     << std::flush;
                        }
                    }
                }
            }
            av_packet_unref(packet_.get());
        }

        // Flush filter graph
        ffmpeg::check_error(
            av_buffersrc_add_frame_flags(buffersrc_ctx_, nullptr, 0),
            "flush filter graph"
        );

        while (av_buffersink_get_frame(buffersink_ctx_, filtered_frame_.get()) >= 0) {
            encode_and_write_frame(filtered_frame_.get());
            av_frame_unref(filtered_frame_.get());
        }

        // Flush encoder
        flush_encoder();

        // Write trailer
        ffmpeg::check_error(
            av_write_trailer(output_format_ctx_.get()),
            "write output trailer"
        );

        // Close output file
        avio_closep(&output_format_ctx_->pb);

        const auto total_seconds = samples_processed /
                                  static_cast<double>(decoder_ctx_->sample_rate);

        std::cout << std::format("\n\nMastering completed!\n");
        std::cout << std::format("Duration: {:.2f} seconds\n", total_seconds);
        std::cout << std::format("Output: {}\n", output_file_.string());
    }

private:
    void initialize() {
        // Find audio stream
        const auto stream_idx = ffmpeg::find_stream_index(format_ctx_.get(),
                                                         AVMEDIA_TYPE_AUDIO);
        if (!stream_idx) {
            throw ffmpeg::FFmpegError("No audio stream found");
        }
        audio_stream_index_ = *stream_idx;

        // Setup decoder
        const auto* codecpar = format_ctx_->streams[audio_stream_index_]->codecpar;
        const auto* decoder = avcodec_find_decoder(codecpar->codec_id);
        if (!decoder) {
            throw ffmpeg::FFmpegError("Decoder not found");
        }

        decoder_ctx_ = ffmpeg::create_codec_context(decoder);
        ffmpeg::check_error(
            avcodec_parameters_to_context(decoder_ctx_.get(), codecpar),
            "copy codec parameters"
        );
        ffmpeg::check_error(
            avcodec_open2(decoder_ctx_.get(), decoder, nullptr),
            "open decoder"
        );

        // Setup filter graph
        setup_filter_graph();
    }

    void setup_filter_graph() {
        filter_graph_.reset(avfilter_graph_alloc());
        if (!filter_graph_) {
            throw ffmpeg::FFmpegError("Failed to allocate filter graph");
        }

        // Get channel layout string
        char ch_layout_str[64];
        av_channel_layout_describe(&decoder_ctx_->ch_layout, ch_layout_str,
                                   sizeof(ch_layout_str));

        // Create buffer source
        const auto* buffersrc = avfilter_get_by_name("abuffer");
        const auto args = std::format(
            "sample_rate={}:sample_fmt={}:channel_layout={}:time_base={}/{}",
            decoder_ctx_->sample_rate,
            av_get_sample_fmt_name(decoder_ctx_->sample_fmt),
            ch_layout_str,
            decoder_ctx_->time_base.num,
            decoder_ctx_->time_base.den
        );

        ffmpeg::check_error(
            avfilter_graph_create_filter(&buffersrc_ctx_, buffersrc, "in",
                                        args.c_str(), nullptr, filter_graph_.get()),
            "create buffer source"
        );

        // Create buffer sink
        const auto* buffersink = avfilter_get_by_name("abuffersink");
        ffmpeg::check_error(
            avfilter_graph_create_filter(&buffersink_ctx_, buffersink, "out",
                                        nullptr, nullptr, filter_graph_.get()),
            "create buffer sink"
        );

        // Build filter chain
        const auto filter_spec = build_filter_chain(params_,
                                                    decoder_ctx_->sample_rate,
                                                    ch_layout_str);

        // Parse filter chain
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

        ffmpeg::check_error(
            avfilter_graph_parse_ptr(filter_graph_.get(), filter_spec.c_str(),
                                    &inputs, &outputs, nullptr),
            "parse filter graph"
        );

        ffmpeg::check_error(
            avfilter_graph_config(filter_graph_.get(), nullptr),
            "configure filter graph"
        );

        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
    }

    void encode_and_write_frame(AVFrame* frame) {
        ffmpeg::check_error(
            avcodec_send_frame(encoder_ctx_.get(), frame),
            "send frame to encoder"
        );

        auto out_packet = ffmpeg::create_packet();
        while (avcodec_receive_packet(encoder_ctx_.get(), out_packet.get()) >= 0) {
            out_packet->stream_index = 0;
            av_packet_rescale_ts(out_packet.get(),
                               encoder_ctx_->time_base,
                               output_format_ctx_->streams[0]->time_base);

            ffmpeg::check_error(
                av_interleaved_write_frame(output_format_ctx_.get(), out_packet.get()),
                "write frame"
            );

            av_packet_unref(out_packet.get());
        }
    }

    void flush_encoder() {
        avcodec_send_frame(encoder_ctx_.get(), nullptr);

        auto out_packet = ffmpeg::create_packet();
        while (avcodec_receive_packet(encoder_ctx_.get(), out_packet.get()) >= 0) {
            out_packet->stream_index = 0;
            av_packet_rescale_ts(out_packet.get(),
                               encoder_ctx_->time_base,
                               output_format_ctx_->streams[0]->time_base);

            av_interleaved_write_frame(output_format_ctx_.get(), out_packet.get());
            av_packet_unref(out_packet.get());
        }
    }

    void print_processing_info() const {
        std::cout << "Audio Mastering\n";
        std::cout << "===============\n\n";
        std::cout << std::format("Input:  {}\n", input_file_.string());
        std::cout << std::format("Output: {}\n", output_file_.string());

        std::cout << "\nPreset: ";
        switch (params_.preset) {
            case MasteringPreset::STREAMING: std::cout << "Streaming (-14 LUFS)\n"; break;
            case MasteringPreset::BROADCAST: std::cout << "Broadcast (-23 LUFS, EBU R128)\n"; break;
            case MasteringPreset::CD: std::cout << "CD Mastering (-9 LUFS)\n"; break;
            case MasteringPreset::PODCAST: std::cout << "Podcast (-16 LUFS)\n"; break;
            case MasteringPreset::YOUTUBE: std::cout << "YouTube (-13 LUFS)\n"; break;
            case MasteringPreset::AUDIOBOOK: std::cout << "Audiobook (-18 LUFS)\n"; break;
            case MasteringPreset::CUSTOM: std::cout << "Custom\n"; break;
        }

        std::cout << "\nProcessing Chain:\n";
        if (params_.enable_highpass) {
            std::cout << std::format("  1. High-pass filter: {}Hz\n", params_.highpass_freq);
        }
        if (params_.enable_eq) {
            std::cout << "  2. Equalization:\n";
            std::cout << std::format("     - Bass: {:+.1f} dB\n", params_.bass_gain);
            std::cout << std::format("     - Mid:  {:+.1f} dB\n", params_.mid_gain);
            std::cout << std::format("     - Treble: {:+.1f} dB\n", params_.treble_gain);
        }
        if (params_.enable_compression) {
            std::cout << "  3. Compression:\n";
            std::cout << std::format("     - Threshold: {:.1f} dB\n", params_.comp_threshold);
            std::cout << std::format("     - Ratio: {:.1f}:1\n", params_.comp_ratio);
            std::cout << std::format("     - Attack: {:.1f} ms\n", params_.comp_attack);
            std::cout << std::format("     - Release: {:.1f} ms\n", params_.comp_release);
        }
        std::cout << "  4. Loudness normalization:\n";
        std::cout << std::format("     - Target: {:.1f} LUFS\n", params_.target_lufs);
        std::cout << std::format("     - True peak: {:.1f} dBTP\n", params_.max_true_peak);

        if (params_.enable_limiter) {
            std::cout << "  5. Final limiting:\n";
            std::cout << std::format("     - Ceiling: {:.1f} dB\n", params_.limiter_ceiling);
        }
    }

    fs::path input_file_;
    fs::path output_file_;
    MasteringParams params_;

    ffmpeg::FormatContextPtr format_ctx_;
    ffmpeg::FormatContextPtr output_format_ctx_;
    ffmpeg::CodecContextPtr decoder_ctx_;
    ffmpeg::CodecContextPtr encoder_ctx_;
    ffmpeg::PacketPtr packet_;
    ffmpeg::FramePtr frame_;
    ffmpeg::FramePtr filtered_frame_;
    ffmpeg::FilterGraphPtr filter_graph_;

    AVFilterContext* buffersrc_ctx_ = nullptr;
    AVFilterContext* buffersink_ctx_ = nullptr;
    int audio_stream_index_ = -1;
};

void print_usage(const char* prog_name) {
    std::cout << std::format("Usage: {} <input> <output> [options]\n\n", prog_name);
    std::cout << "Options:\n";
    std::cout << "  -p, --preset <name>       Mastering preset (default: streaming)\n";
    std::cout << "                              streaming  - -14 LUFS (Spotify, Apple Music)\n";
    std::cout << "                              broadcast  - -23 LUFS (EBU R128)\n";
    std::cout << "                              cd         - -9 LUFS (CD mastering)\n";
    std::cout << "                              podcast    - -16 LUFS (podcast)\n";
    std::cout << "                              youtube    - -13 LUFS (YouTube)\n";
    std::cout << "                              audiobook  - -18 LUFS (audiobook)\n";
    std::cout << "  --target-lufs <LUFS>      Target loudness (default: -14)\n";
    std::cout << "  --true-peak <dBTP>        True peak limit (default: -1.0)\n";
    std::cout << "  --eq                      Enable EQ\n";
    std::cout << "  --bass <dB>               Bass gain -12 to +12 (default: 0)\n";
    std::cout << "  --mid <dB>                Mid gain -12 to +12 (default: 0)\n";
    std::cout << "  --treble <dB>             Treble gain -12 to +12 (default: 0)\n";
    std::cout << "  --no-compression          Disable compression\n";
    std::cout << "  --no-limiter              Disable final limiter\n";
    std::cout << "  --stats                   Print detailed statistics\n\n";

    std::cout << "Examples:\n";
    std::cout << std::format("  {} input.wav output.wav\n", prog_name);
    std::cout << "    Master for streaming platforms (default)\n\n";

    std::cout << std::format("  {} music.flac mastered.flac -p cd\n", prog_name);
    std::cout << "    Master for CD release\n\n";

    std::cout << std::format("  {} podcast.wav output.wav -p podcast\n", prog_name);
    std::cout << "    Master for podcast with voice optimization\n\n";

    std::cout << std::format("  {} audio.wav output.wav --eq --bass -2 --mid 2 --treble 1\n", prog_name);
    std::cout << "    Custom EQ settings\n\n";

    std::cout << std::format("  {} input.wav output.wav --target-lufs -16 --true-peak -1.5\n", prog_name);
    std::cout << "    Custom loudness target\n\n";

    std::cout << "Target Levels:\n";
    std::cout << "  Streaming:   -14 LUFS (Spotify, Apple Music, Tidal)\n";
    std::cout << "  YouTube:     -13 LUFS\n";
    std::cout << "  Podcast:     -16 to -19 LUFS\n";
    std::cout << "  Broadcast:   -23 LUFS (EBU R128)\n";
    std::cout << "  CD:          -9 to -13 LUFS\n";
    std::cout << "  Audiobook:   -18 to -23 LUFS\n";
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        fs::path input_file{argv[1]};
        fs::path output_file{argv[2]};
        MasteringParams params;
        apply_preset(params, MasteringPreset::STREAMING);

        // Parse arguments
        for (int i = 3; i < argc; ++i) {
            std::string arg{argv[i]};

            if ((arg == "-p" || arg == "--preset") && i + 1 < argc) {
                std::string preset_name{argv[++i]};
                if (preset_name == "streaming") {
                    apply_preset(params, MasteringPreset::STREAMING);
                } else if (preset_name == "broadcast") {
                    apply_preset(params, MasteringPreset::BROADCAST);
                } else if (preset_name == "cd") {
                    apply_preset(params, MasteringPreset::CD);
                } else if (preset_name == "podcast") {
                    apply_preset(params, MasteringPreset::PODCAST);
                } else if (preset_name == "youtube") {
                    apply_preset(params, MasteringPreset::YOUTUBE);
                } else if (preset_name == "audiobook") {
                    apply_preset(params, MasteringPreset::AUDIOBOOK);
                } else {
                    std::cerr << std::format("Unknown preset: {}\n", preset_name);
                    return 1;
                }
            }
            else if (arg == "--target-lufs" && i + 1 < argc) {
                params.target_lufs = std::stod(argv[++i]);
            }
            else if (arg == "--true-peak" && i + 1 < argc) {
                params.max_true_peak = std::stod(argv[++i]);
            }
            else if (arg == "--eq") {
                params.enable_eq = true;
            }
            else if (arg == "--bass" && i + 1 < argc) {
                params.enable_eq = true;
                params.bass_gain = std::clamp(std::stod(argv[++i]), -12.0, 12.0);
            }
            else if (arg == "--mid" && i + 1 < argc) {
                params.enable_eq = true;
                params.mid_gain = std::clamp(std::stod(argv[++i]), -12.0, 12.0);
            }
            else if (arg == "--treble" && i + 1 < argc) {
                params.enable_eq = true;
                params.treble_gain = std::clamp(std::stod(argv[++i]), -12.0, 12.0);
            }
            else if (arg == "--no-compression") {
                params.enable_compression = false;
            }
            else if (arg == "--no-limiter") {
                params.enable_limiter = false;
            }
            else if (arg == "--stats") {
                params.print_stats = true;
            }
        }

        // Process
        AudioMastering mastering(input_file, output_file, params);
        mastering.process();

    } catch (const ffmpeg::FFmpegError& e) {
        std::cerr << std::format("FFmpeg error: {}\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }

    return 0;
}
