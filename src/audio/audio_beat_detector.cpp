/**
 * Audio Beat Detector
 *
 * This sample demonstrates how to detect beats and measure BPM (beats per minute)
 * in audio files using modern C++20 and FFmpeg libraries.
 *
 * Features:
 * - Automatic BPM detection
 * - Beat timestamp extraction
 * - Multiple detection methods (energy, spectral flux, onset detection)
 * - Beat map output
 * - Statistics and analysis
 */

#include "ffmpeg_wrappers.hpp"

#include <iostream>
#include <format>
#include <filesystem>
#include <string_view>
#include <vector>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <numeric>
#include <optional>

namespace fs = std::filesystem;

namespace {

enum class DetectionMethod {
    ENERGY,       // Energy-based detection
    SPECTRAL,     // Spectral flux detection
    ONSET,        // Onset detection using FFmpeg
    AUTO          // Automatic method selection
};

struct BeatInfo {
    double timestamp;     // Time in seconds
    double strength;      // Beat strength (0.0 to 1.0)
    double confidence;    // Detection confidence (0.0 to 1.0)
};

struct BPMAnalysis {
    double bpm;                    // Detected BPM
    double confidence;             // Detection confidence
    std::vector<BeatInfo> beats;   // All detected beats
    double avg_beat_interval;      // Average interval between beats
    double tempo_stability;        // How stable the tempo is (0.0 to 1.0)
};

struct DetectionParams {
    DetectionMethod method = DetectionMethod::AUTO;
    double sensitivity = 0.5;      // Detection sensitivity (0.0 to 1.0)
    double min_bpm = 60.0;         // Minimum BPM to detect
    double max_bpm = 200.0;        // Maximum BPM to detect
    double min_beat_interval = 0.3; // Minimum time between beats (seconds)
    bool export_beats = false;     // Export beat timestamps to file
    bool verbose = false;          // Print detailed analysis
};

DetectionMethod parse_method(std::string_view method_str) {
    if (method_str == "energy") return DetectionMethod::ENERGY;
    if (method_str == "spectral") return DetectionMethod::SPECTRAL;
    if (method_str == "onset") return DetectionMethod::ONSET;
    if (method_str == "auto") return DetectionMethod::AUTO;
    throw std::invalid_argument(std::format("Invalid detection method: {}", method_str));
}

class BeatDetector {
public:
    BeatDetector(std::string_view input_file, DetectionParams params)
        : input_file_(input_file)
        , params_(std::move(params))
        , format_ctx_(ffmpeg::open_input_format(input_file.data()))
        , packet_(ffmpeg::create_packet()) {

        initialize();
    }

    BPMAnalysis analyze() {
        print_header();

        // Determine detection method
        if (params_.method == DetectionMethod::AUTO) {
            params_.method = select_best_method();
        }

        std::cout << std::format("Detection method: {}\n\n", method_to_string(params_.method));

        // Process audio and detect beats
        switch (params_.method) {
            case DetectionMethod::ENERGY:
                return detect_beats_energy();
            case DetectionMethod::SPECTRAL:
                return detect_beats_spectral();
            case DetectionMethod::ONSET:
                return detect_beats_onset();
            default:
                return detect_beats_energy();
        }
    }

private:
    void initialize() {
        // Find audio stream
        const auto stream_idx = ffmpeg::find_stream_index(format_ctx_.get(), AVMEDIA_TYPE_AUDIO);
        if (!stream_idx) {
            throw ffmpeg::FFmpegError("No audio stream found");
        }
        audio_stream_index_ = *stream_idx;

        // Setup decoder
        const auto* codecpar = format_ctx_->streams[audio_stream_index_]->codecpar;
        const auto* decoder = avcodec_find_decoder(codecpar->codec_id);
        if (!decoder) {
            throw ffmpeg::FFmpegError("Audio decoder not found");
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

        // Get duration
        if (format_ctx_->duration != AV_NOPTS_VALUE) {
            duration_ = format_ctx_->duration / static_cast<double>(AV_TIME_BASE);
        }
    }

    void print_header() const {
        std::cout << "Audio Beat Detector\n";
        std::cout << "==================\n\n";
        std::cout << std::format("Input file: {}\n", input_file_);
        std::cout << std::format("Sample rate: {} Hz\n", codec_ctx_->sample_rate);
        std::cout << std::format("Channels: {}\n", codec_ctx_->ch_layout.nb_channels);
        if (duration_ > 0) {
            std::cout << std::format("Duration: {:.2f} seconds\n", duration_);
        }
        std::cout << std::format("BPM range: {:.0f} - {:.0f}\n", params_.min_bpm, params_.max_bpm);
        std::cout << std::format("Sensitivity: {:.0f}%\n", params_.sensitivity * 100);
    }

    DetectionMethod select_best_method() const {
        // Select method based on audio characteristics
        if (codec_ctx_->sample_rate >= 44100) {
            return DetectionMethod::ONSET; // Best for high quality audio
        } else {
            return DetectionMethod::ENERGY; // Better for lower quality
        }
    }

    std::string method_to_string(DetectionMethod method) const {
        switch (method) {
            case DetectionMethod::ENERGY: return "Energy-based";
            case DetectionMethod::SPECTRAL: return "Spectral flux";
            case DetectionMethod::ONSET: return "Onset detection";
            case DetectionMethod::AUTO: return "Automatic";
            default: return "Unknown";
        }
    }

    BPMAnalysis detect_beats_energy() {
        std::cout << "Analyzing audio energy...\n";

        // Initialize filter for energy calculation
        initialize_energy_filter();

        std::vector<double> energy_values;
        std::vector<double> timestamps;
        double current_time = 0.0;

        // Process all audio frames
        while (av_read_frame(format_ctx_.get(), packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(packet_.get());

            if (packet_->stream_index != audio_stream_index_) {
                continue;
            }

            if (avcodec_send_packet(codec_ctx_.get(), packet_.get()) < 0) {
                continue;
            }

            while (true) {
                auto frame = ffmpeg::create_frame();
                const auto recv_ret = avcodec_receive_frame(codec_ctx_.get(), frame.get());

                if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF) {
                    break;
                }
                if (recv_ret < 0) {
                    break;
                }

                ffmpeg::ScopedFrameUnref frame_guard(frame.get());

                // Calculate energy for this frame
                const double energy = calculate_frame_energy(frame.get());
                energy_values.push_back(energy);
                timestamps.push_back(current_time);

                const double frame_duration = frame->nb_samples /
                    static_cast<double>(codec_ctx_->sample_rate);
                current_time += frame_duration;
            }
        }

        if (energy_values.empty()) {
            throw ffmpeg::FFmpegError("No audio data processed");
        }

        std::cout << std::format("Processed {:.2f} seconds of audio\n\n", current_time);

        // Detect beats from energy values
        return detect_beats_from_energy(energy_values, timestamps);
    }

    BPMAnalysis detect_beats_spectral() {
        std::cout << "Analyzing spectral flux...\n";

        // Initialize spectral analysis filter
        initialize_spectral_filter();

        std::vector<std::vector<double>> spectrum_history;
        std::vector<double> flux_values;
        std::vector<double> timestamps;
        double current_time = 0.0;

        // Process all audio frames
        while (av_read_frame(format_ctx_.get(), packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(packet_.get());

            if (packet_->stream_index != audio_stream_index_) {
                continue;
            }

            if (avcodec_send_packet(codec_ctx_.get(), packet_.get()) < 0) {
                continue;
            }

            while (true) {
                auto frame = ffmpeg::create_frame();
                const auto recv_ret = avcodec_receive_frame(codec_ctx_.get(), frame.get());

                if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF) {
                    break;
                }
                if (recv_ret < 0) {
                    break;
                }

                ffmpeg::ScopedFrameUnref frame_guard(frame.get());

                // Calculate spectral flux
                const auto spectrum = calculate_spectrum(frame.get());

                if (!spectrum_history.empty()) {
                    const double flux = calculate_spectral_flux(spectrum_history.back(), spectrum);
                    flux_values.push_back(flux);
                    timestamps.push_back(current_time);
                }

                spectrum_history.push_back(spectrum);

                const double frame_duration = frame->nb_samples /
                    static_cast<double>(codec_ctx_->sample_rate);
                current_time += frame_duration;
            }
        }

        if (flux_values.empty()) {
            throw ffmpeg::FFmpegError("No spectral data processed");
        }

        std::cout << std::format("Processed {:.2f} seconds of audio\n\n", current_time);

        return detect_beats_from_flux(flux_values, timestamps);
    }

    BPMAnalysis detect_beats_onset() {
        std::cout << "Detecting onsets...\n";

        // Use FFmpeg's astats filter to detect audio onsets
        initialize_onset_filter();

        std::vector<BeatInfo> beats;
        double current_time = 0.0;
        double last_beat_time = -params_.min_beat_interval;

        // Process audio through onset detection filter
        while (av_read_frame(format_ctx_.get(), packet_.get()) >= 0) {
            ffmpeg::ScopedPacketUnref packet_guard(packet_.get());

            if (packet_->stream_index != audio_stream_index_) {
                continue;
            }

            if (avcodec_send_packet(codec_ctx_.get(), packet_.get()) < 0) {
                continue;
            }

            while (true) {
                auto frame = ffmpeg::create_frame();
                const auto recv_ret = avcodec_receive_frame(codec_ctx_.get(), frame.get());

                if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF) {
                    break;
                }
                if (recv_ret < 0) {
                    break;
                }

                ffmpeg::ScopedFrameUnref frame_guard(frame.get());

                // Push frame through filter
                av_buffersrc_add_frame_flags(buffersrc_ctx_, frame.get(),
                                           AV_BUFFERSRC_FLAG_KEEP_REF);

                // Get filtered frames
                auto filtered_frame = ffmpeg::create_frame();
                while (av_buffersink_get_frame(buffersink_ctx_, filtered_frame.get()) >= 0) {
                    ffmpeg::ScopedFrameUnref filtered_guard(filtered_frame.get());

                    // Analyze frame for onset
                    const double energy = calculate_frame_energy(filtered_frame.get());
                    const double threshold = params_.sensitivity * 0.3;

                    if (energy > threshold && (current_time - last_beat_time) >= params_.min_beat_interval) {
                        beats.push_back({current_time, energy, 0.8});
                        last_beat_time = current_time;
                    }

                    const double frame_duration = filtered_frame->nb_samples /
                        static_cast<double>(codec_ctx_->sample_rate);
                    current_time += frame_duration;
                }
            }
        }

        std::cout << std::format("Processed {:.2f} seconds of audio\n", current_time);
        std::cout << std::format("Detected {} potential beats\n\n", beats.size());

        return calculate_bpm_from_beats(beats);
    }

    void initialize_energy_filter() {
        // Simple passthrough for energy calculation
        const auto* abuffersrc = avfilter_get_by_name("abuffer");
        const auto* buffersink = avfilter_get_by_name("abuffersink");

        filter_graph_.reset(avfilter_graph_alloc());
        if (!filter_graph_) {
            throw ffmpeg::FFmpegError("Failed to allocate filter graph");
        }

        char ch_layout[64];
        av_channel_layout_describe(&codec_ctx_->ch_layout, ch_layout, sizeof(ch_layout));

        const auto args = std::format(
            "sample_rate={}:sample_fmt={}:channel_layout={}",
            codec_ctx_->sample_rate,
            av_get_sample_fmt_name(codec_ctx_->sample_fmt),
            ch_layout
        );

        ffmpeg::check_error(
            avfilter_graph_create_filter(&buffersrc_ctx_, abuffersrc, "in",
                                        args.c_str(), nullptr, filter_graph_.get()),
            "create buffer source"
        );

        ffmpeg::check_error(
            avfilter_graph_create_filter(&buffersink_ctx_, buffersink, "out",
                                        nullptr, nullptr, filter_graph_.get()),
            "create buffer sink"
        );

        ffmpeg::check_error(
            avfilter_link(buffersrc_ctx_, 0, buffersink_ctx_, 0),
            "link filters"
        );

        ffmpeg::check_error(
            avfilter_graph_config(filter_graph_.get(), nullptr),
            "configure filter graph"
        );
    }

    void initialize_spectral_filter() {
        initialize_energy_filter(); // Use same filter for now
    }

    void initialize_onset_filter() {
        const auto* abuffersrc = avfilter_get_by_name("abuffer");
        const auto* buffersink = avfilter_get_by_name("abuffersink");

        filter_graph_.reset(avfilter_graph_alloc());
        if (!filter_graph_) {
            throw ffmpeg::FFmpegError("Failed to allocate filter graph");
        }

        char ch_layout[64];
        av_channel_layout_describe(&codec_ctx_->ch_layout, ch_layout, sizeof(ch_layout));

        const auto args = std::format(
            "sample_rate={}:sample_fmt={}:channel_layout={}",
            codec_ctx_->sample_rate,
            av_get_sample_fmt_name(codec_ctx_->sample_fmt),
            ch_layout
        );

        ffmpeg::check_error(
            avfilter_graph_create_filter(&buffersrc_ctx_, abuffersrc, "in",
                                        args.c_str(), nullptr, filter_graph_.get()),
            "create buffer source"
        );

        ffmpeg::check_error(
            avfilter_graph_create_filter(&buffersink_ctx_, buffersink, "out",
                                        nullptr, nullptr, filter_graph_.get()),
            "create buffer sink"
        );

        // Create highpass filter to emphasize transients
        AVFilterContext* highpass_ctx = nullptr;
        const auto* highpass = avfilter_get_by_name("highpass");
        ffmpeg::check_error(
            avfilter_graph_create_filter(&highpass_ctx, highpass, "highpass",
                                        "f=200", nullptr, filter_graph_.get()),
            "create highpass filter"
        );

        ffmpeg::check_error(
            avfilter_link(buffersrc_ctx_, 0, highpass_ctx, 0),
            "link source to highpass"
        );

        ffmpeg::check_error(
            avfilter_link(highpass_ctx, 0, buffersink_ctx_, 0),
            "link highpass to sink"
        );

        ffmpeg::check_error(
            avfilter_graph_config(filter_graph_.get(), nullptr),
            "configure filter graph"
        );
    }

    double calculate_frame_energy(AVFrame* frame) const {
        double energy = 0.0;
        const int num_samples = frame->nb_samples;
        const int channels = frame->ch_layout.nb_channels;

        if (frame->format == AV_SAMPLE_FMT_FLTP) {
            for (int ch = 0; ch < channels; ++ch) {
                const auto* samples = reinterpret_cast<float*>(frame->data[ch]);
                for (int i = 0; i < num_samples; ++i) {
                    energy += samples[i] * samples[i];
                }
            }
        } else if (frame->format == AV_SAMPLE_FMT_S16P) {
            for (int ch = 0; ch < channels; ++ch) {
                const auto* samples = reinterpret_cast<int16_t*>(frame->data[ch]);
                for (int i = 0; i < num_samples; ++i) {
                    const float normalized = samples[i] / 32768.0f;
                    energy += normalized * normalized;
                }
            }
        }

        return std::sqrt(energy / (num_samples * channels));
    }

    std::vector<double> calculate_spectrum(AVFrame* frame) const {
        // Simplified spectrum calculation using energy in frequency bands
        const int num_bands = 32;
        std::vector<double> spectrum(num_bands, 0.0);

        const int num_samples = frame->nb_samples;
        const int channels = frame->ch_layout.nb_channels;
        const int band_size = num_samples / num_bands;

        if (frame->format == AV_SAMPLE_FMT_FLTP) {
            for (int ch = 0; ch < channels; ++ch) {
                const auto* samples = reinterpret_cast<float*>(frame->data[ch]);
                for (int band = 0; band < num_bands; ++band) {
                    double band_energy = 0.0;
                    const int start = band * band_size;
                    const int end = std::min(start + band_size, num_samples);

                    for (int i = start; i < end; ++i) {
                        band_energy += std::abs(samples[i]);
                    }
                    spectrum[band] += band_energy / (end - start);
                }
            }
        }

        // Normalize by channels
        for (auto& val : spectrum) {
            val /= channels;
        }

        return spectrum;
    }

    double calculate_spectral_flux(const std::vector<double>& prev_spectrum,
                                   const std::vector<double>& curr_spectrum) const {
        double flux = 0.0;
        const size_t size = std::min(prev_spectrum.size(), curr_spectrum.size());

        for (size_t i = 0; i < size; ++i) {
            const double diff = curr_spectrum[i] - prev_spectrum[i];
            if (diff > 0) {
                flux += diff * diff;
            }
        }

        return std::sqrt(flux);
    }

    BPMAnalysis detect_beats_from_energy(const std::vector<double>& energy_values,
                                        const std::vector<double>& timestamps) {
        // Calculate adaptive threshold
        const double mean_energy = std::accumulate(energy_values.begin(), energy_values.end(), 0.0)
                                  / energy_values.size();

        double variance = 0.0;
        for (const auto& e : energy_values) {
            variance += (e - mean_energy) * (e - mean_energy);
        }
        const double std_dev = std::sqrt(variance / energy_values.size());

        const double threshold = mean_energy + (params_.sensitivity * 2.0 * std_dev);

        std::cout << std::format("Energy threshold: {:.6f}\n", threshold);
        std::cout << "Detecting beats...\n";

        // Detect peaks above threshold
        std::vector<BeatInfo> beats;
        double last_beat_time = -params_.min_beat_interval;

        for (size_t i = 1; i < energy_values.size() - 1; ++i) {
            const bool is_peak = energy_values[i] > energy_values[i-1] &&
                               energy_values[i] > energy_values[i+1];
            const bool above_threshold = energy_values[i] > threshold;
            const bool min_interval_met = (timestamps[i] - last_beat_time) >= params_.min_beat_interval;

            if (is_peak && above_threshold && min_interval_met) {
                const double strength = (energy_values[i] - mean_energy) / (std_dev + 1e-10);
                const double confidence = std::clamp(strength / 3.0, 0.0, 1.0);

                beats.push_back({timestamps[i], strength, confidence});
                last_beat_time = timestamps[i];
            }
        }

        std::cout << std::format("Found {} beats\n\n", beats.size());

        return calculate_bpm_from_beats(beats);
    }

    BPMAnalysis detect_beats_from_flux(const std::vector<double>& flux_values,
                                      const std::vector<double>& timestamps) {
        // Calculate adaptive threshold for flux
        const double mean_flux = std::accumulate(flux_values.begin(), flux_values.end(), 0.0)
                               / flux_values.size();

        double variance = 0.0;
        for (const auto& f : flux_values) {
            variance += (f - mean_flux) * (f - mean_flux);
        }
        const double std_dev = std::sqrt(variance / flux_values.size());

        const double threshold = mean_flux + (params_.sensitivity * 1.5 * std_dev);

        std::cout << std::format("Flux threshold: {:.6f}\n", threshold);
        std::cout << "Detecting beats from spectral flux...\n";

        // Detect peaks
        std::vector<BeatInfo> beats;
        double last_beat_time = -params_.min_beat_interval;

        for (size_t i = 1; i < flux_values.size() - 1; ++i) {
            const bool is_peak = flux_values[i] > flux_values[i-1] &&
                               flux_values[i] > flux_values[i+1];
            const bool above_threshold = flux_values[i] > threshold;
            const bool min_interval_met = (timestamps[i] - last_beat_time) >= params_.min_beat_interval;

            if (is_peak && above_threshold && min_interval_met) {
                const double strength = (flux_values[i] - mean_flux) / (std_dev + 1e-10);
                const double confidence = std::clamp(strength / 3.0, 0.0, 1.0);

                beats.push_back({timestamps[i], strength, confidence});
                last_beat_time = timestamps[i];
            }
        }

        std::cout << std::format("Found {} beats\n\n", beats.size());

        return calculate_bpm_from_beats(beats);
    }

    BPMAnalysis calculate_bpm_from_beats(const std::vector<BeatInfo>& beats) {
        if (beats.size() < 2) {
            std::cout << "Warning: Not enough beats detected for BPM calculation\n";
            return {0.0, 0.0, beats, 0.0, 0.0};
        }

        // Calculate intervals between beats
        std::vector<double> intervals;
        for (size_t i = 1; i < beats.size(); ++i) {
            intervals.push_back(beats[i].timestamp - beats[i-1].timestamp);
        }

        // Calculate median interval (more robust than mean)
        std::sort(intervals.begin(), intervals.end());
        const double median_interval = intervals[intervals.size() / 2];

        // Calculate BPM from median interval
        const double bpm = 60.0 / median_interval;

        // Filter out intervals that are too far from median (outliers)
        std::vector<double> filtered_intervals;
        const double tolerance = median_interval * 0.3;

        for (const auto& interval : intervals) {
            if (std::abs(interval - median_interval) <= tolerance) {
                filtered_intervals.push_back(interval);
            }
        }

        // Calculate average of filtered intervals
        const double avg_interval = std::accumulate(filtered_intervals.begin(),
                                                   filtered_intervals.end(), 0.0)
                                  / filtered_intervals.size();

        // Calculate tempo stability
        double variance = 0.0;
        for (const auto& interval : filtered_intervals) {
            variance += (interval - avg_interval) * (interval - avg_interval);
        }
        const double std_dev = std::sqrt(variance / filtered_intervals.size());
        const double tempo_stability = 1.0 - std::min(std_dev / avg_interval, 1.0);

        // Calculate confidence based on number of beats and stability
        const double beat_count_factor = std::min(beats.size() / 20.0, 1.0);
        const double confidence = (tempo_stability * 0.7 + beat_count_factor * 0.3);

        // Clamp BPM to valid range
        const double clamped_bpm = std::clamp(bpm, params_.min_bpm, params_.max_bpm);

        return {clamped_bpm, confidence, beats, avg_interval, tempo_stability};
    }

    void print_analysis(const BPMAnalysis& analysis) const {
        std::cout << "Analysis Results\n";
        std::cout << "================\n\n";
        std::cout << std::format("Detected BPM: {:.1f}\n", analysis.bpm);
        std::cout << std::format("Confidence: {:.0f}%\n", analysis.confidence * 100);
        std::cout << std::format("Total beats detected: {}\n", analysis.beats.size());
        std::cout << std::format("Average beat interval: {:.3f} seconds\n", analysis.avg_beat_interval);
        std::cout << std::format("Tempo stability: {:.0f}%\n\n", analysis.tempo_stability * 100);

        if (params_.verbose && !analysis.beats.empty()) {
            std::cout << "Beat timestamps (first 20):\n";
            const size_t show_count = std::min(size_t(20), analysis.beats.size());
            for (size_t i = 0; i < show_count; ++i) {
                const auto& beat = analysis.beats[i];
                std::cout << std::format("  {:3d}. {:.3f}s (strength: {:.2f}, confidence: {:.0f}%)\n",
                    i + 1, beat.timestamp, beat.strength, beat.confidence * 100);
            }
            if (analysis.beats.size() > 20) {
                std::cout << std::format("  ... and {} more beats\n", analysis.beats.size() - 20);
            }
            std::cout << "\n";
        }
    }

    void export_beat_map(const BPMAnalysis& analysis, const fs::path& output_path) const {
        std::ofstream file(output_path);
        if (!file) {
            throw std::runtime_error(std::format("Failed to open output file: {}",
                                                output_path.string()));
        }

        file << "# Beat Map\n";
        file << std::format("# BPM: {:.1f}\n", analysis.bpm);
        file << std::format("# Confidence: {:.0f}%\n", analysis.confidence * 100);
        file << std::format("# Total beats: {}\n", analysis.beats.size());
        file << "#\n";
        file << "# Format: timestamp(s), strength, confidence\n";
        file << "#\n\n";

        for (const auto& beat : analysis.beats) {
            file << std::format("{:.6f},{:.4f},{:.4f}\n",
                beat.timestamp, beat.strength, beat.confidence);
        }

        std::cout << std::format("Beat map exported to: {}\n", output_path.string());
    }

    std::string input_file_;
    DetectionParams params_;
    int audio_stream_index_ = -1;
    double duration_ = 0.0;

    ffmpeg::FormatContextPtr format_ctx_;
    ffmpeg::CodecContextPtr codec_ctx_;
    ffmpeg::FilterGraphPtr filter_graph_;
    ffmpeg::PacketPtr packet_;

    AVFilterContext* buffersrc_ctx_ = nullptr;
    AVFilterContext* buffersink_ctx_ = nullptr;

    friend void run_beat_detection(std::string_view input_file,
                                   const std::optional<fs::path>& output_file,
                                   DetectionParams params);
};

void run_beat_detection(std::string_view input_file,
                       const std::optional<fs::path>& output_file,
                       DetectionParams params) {
    BeatDetector detector(input_file, params);
    const auto analysis = detector.analyze();

    detector.print_analysis(analysis);

    if (params.export_beats) {
        const auto beat_map_path = output_file.value_or(
            fs::path(input_file).stem().string() + "_beats.csv"
        );
        detector.export_beat_map(analysis, beat_map_path);
    }
}

void print_usage(std::string_view prog_name) {
    std::cout << std::format("Usage: {} <input_audio> [options]\n\n", prog_name);
    std::cout << "Options:\n";
    std::cout << "  -m, --method <method>     Detection method: energy, spectral, onset, auto (default: auto)\n";
    std::cout << "  -s, --sensitivity <0-1>   Detection sensitivity (default: 0.5)\n";
    std::cout << "  -b, --bpm-range <min-max> BPM range to detect (default: 60-200)\n";
    std::cout << "  -i, --min-interval <sec>  Minimum beat interval in seconds (default: 0.3)\n";
    std::cout << "  -e, --export [file]       Export beat timestamps to CSV file\n";
    std::cout << "  -v, --verbose             Print detailed analysis\n";
    std::cout << "  -h, --help                Show this help message\n\n";
    std::cout << "Detection Methods:\n";
    std::cout << "  energy    - Energy-based detection (fast, good for percussive music)\n";
    std::cout << "  spectral  - Spectral flux detection (slower, better for complex music)\n";
    std::cout << "  onset     - Onset detection (best quality, requires high sample rate)\n";
    std::cout << "  auto      - Automatically select best method\n\n";
    std::cout << "Examples:\n";
    std::cout << std::format("  {} music.mp3\n", prog_name);
    std::cout << std::format("  {} song.wav -m onset -s 0.7 -e beats.csv\n", prog_name);
    std::cout << std::format("  {} audio.flac -b 120-180 -v\n", prog_name);
    std::cout << std::format("  {} track.m4a -m energy -i 0.4 -e\n", prog_name);
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    // Check for help first
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    try {
        std::string_view input_file{argv[1]};
        DetectionParams params;
        std::optional<fs::path> output_file;

        // Parse command-line options
        for (int i = 2; i < argc; ++i) {
            const std::string_view arg{argv[i]};

            if (arg == "-m" || arg == "--method") {
                if (i + 1 >= argc) {
                    throw std::invalid_argument("Missing method argument");
                }
                params.method = parse_method(argv[++i]);
            } else if (arg == "-s" || arg == "--sensitivity") {
                if (i + 1 >= argc) {
                    throw std::invalid_argument("Missing sensitivity argument");
                }
                params.sensitivity = std::clamp(std::stod(argv[++i]), 0.0, 1.0);
            } else if (arg == "-b" || arg == "--bpm-range") {
                if (i + 1 >= argc) {
                    throw std::invalid_argument("Missing BPM range argument");
                }
                const std::string range{argv[++i]};
                const auto dash_pos = range.find('-');
                if (dash_pos == std::string::npos) {
                    throw std::invalid_argument("Invalid BPM range format (use min-max)");
                }
                params.min_bpm = std::stod(range.substr(0, dash_pos));
                params.max_bpm = std::stod(range.substr(dash_pos + 1));
            } else if (arg == "-i" || arg == "--min-interval") {
                if (i + 1 >= argc) {
                    throw std::invalid_argument("Missing min interval argument");
                }
                params.min_beat_interval = std::stod(argv[++i]);
            } else if (arg == "-e" || arg == "--export") {
                params.export_beats = true;
                if (i + 1 < argc && argv[i + 1][0] != '-') {
                    output_file = fs::path{argv[++i]};
                }
            } else if (arg == "-v" || arg == "--verbose") {
                params.verbose = true;
            } else {
                throw std::invalid_argument(std::format("Unknown option: {}", arg));
            }
        }

        run_beat_detection(input_file, output_file, params);

    } catch (const ffmpeg::FFmpegError& e) {
        std::cerr << std::format("FFmpeg error: {}\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }

    return 0;
}
