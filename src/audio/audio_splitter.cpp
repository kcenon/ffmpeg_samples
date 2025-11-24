/**
 * Audio Splitter
 *
 * This sample demonstrates how to split audio files based on silence detection
 * using modern C++20 and FFmpeg libraries.
 */

#include "ffmpeg_wrappers.hpp"

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

extern "C" {
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

namespace fs = std::filesystem;

namespace {

struct SplitSegment {
  double start_time;  // Start time in seconds
  double end_time;    // End time in seconds
  int segment_number; // Segment index
};

struct SplitterParams {
  double noise_threshold = -40.0; // Noise threshold in dB (default: -40dB)
  double min_silence = 0.5;       // Minimum silence duration in seconds
  double min_segment = 1.0;       // Minimum segment duration in seconds
  std::string output_dir = "segments";
  std::string output_format = "wav";
  std::string output_prefix = "segment";
};

// Simple WAV header
struct WAVHeader {
  char riff_header[4] = {'R', 'I', 'F', 'F'};
  uint32_t wav_size;
  char wave_header[4] = {'W', 'A', 'V', 'E'};
  char fmt_header[4] = {'f', 'm', 't', ' '};
  uint32_t fmt_chunk_size = 16;
  uint16_t audio_format = 1;
  uint16_t num_channels;
  uint32_t sample_rate;
  uint32_t byte_rate;
  uint16_t block_align;
  uint16_t bits_per_sample;
  char data_header[4] = {'d', 'a', 't', 'a'};
  uint32_t data_bytes;
};

void write_wav_header(std::ofstream &file, int sample_rate, int channels,
                      uint32_t data_size) {
  WAVHeader header;
  header.num_channels = static_cast<uint16_t>(channels);
  header.sample_rate = static_cast<uint32_t>(sample_rate);
  header.bits_per_sample = 16;
  header.byte_rate = static_cast<uint32_t>(sample_rate * channels * 2);
  header.block_align = static_cast<uint16_t>(channels * 2);
  header.data_bytes = data_size;
  header.wav_size = 36 + data_size;

  file.write(reinterpret_cast<const char *>(&header), sizeof(WAVHeader));
}

void print_usage(std::string_view prog_name) {
  std::cout << std::format("Usage: {} <input> [options]\n\n", prog_name);
  std::cout << "Options:\n";
  std::cout << "  -t, --threshold <dB>       Silence threshold in dB (default: "
               "-40.0)\n";
  std::cout << "  -s, --silence <seconds>    Minimum silence duration "
               "(default: 0.5)\n";
  std::cout << "  -m, --min-length <seconds> Minimum segment duration "
               "(default: 1.0)\n";
  std::cout
      << "  -o, --output <directory>   Output directory (default: segments)\n";
  std::cout << "  -f, --format <format>      Output format: wav, mp3 (default: "
               "wav)\n";
  std::cout << "  -p, --prefix <prefix>      Output filename prefix (default: "
               "segment)\n\n";

  std::cout << "Examples:\n";
  std::cout << std::format("  {} audio.mp3\n", prog_name);
  std::cout << "    Split audio with default settings\n\n";

  std::cout << std::format("  {} podcast.wav -t -35 -s 1.0 -m 5.0\n",
                           prog_name);
  std::cout << "    Split podcast with custom thresholds\n\n";

  std::cout << std::format("  {} interview.m4a -o output -p part\n", prog_name);
  std::cout
      << "    Split and save to 'output' directory with prefix 'part'\n\n";

  std::cout << "Notes:\n";
  std::cout
      << "  - Lower threshold values (e.g., -50dB) detect quieter silence\n";
  std::cout << "  - Increase min-silence to avoid splitting on short pauses\n";
  std::cout << "  - Segments shorter than min-length are merged with adjacent "
               "segments\n";
  std::cout << "  - Output files are named: <prefix>_001.<format>, "
               "<prefix>_002.<format>, etc.\n";
}

std::optional<SplitterParams> parse_arguments(int argc, char *argv[]) {
  if (argc < 2) {
    return std::nullopt;
  }

  SplitterParams params;

  for (int i = 2; i < argc; ++i) {
    const std::string_view arg = argv[i];

    if ((arg == "-t" || arg == "--threshold") && i + 1 < argc) {
      params.noise_threshold = std::stod(argv[++i]);
    } else if ((arg == "-s" || arg == "--silence") && i + 1 < argc) {
      params.min_silence = std::stod(argv[++i]);
    } else if ((arg == "-m" || arg == "--min-length") && i + 1 < argc) {
      params.min_segment = std::stod(argv[++i]);
    } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
      params.output_dir = argv[++i];
    } else if ((arg == "-f" || arg == "--format") && i + 1 < argc) {
      params.output_format = argv[++i];
    } else if ((arg == "-p" || arg == "--prefix") && i + 1 < argc) {
      params.output_prefix = argv[++i];
    } else {
      std::cerr << std::format("Error: Unknown option '{}'\n", arg);
      return std::nullopt;
    }
  }

  return params;
}

class AudioSplitter {
public:
  AudioSplitter(std::string_view input_file, const SplitterParams &params)
      : input_file_(input_file), params_(params),
        input_format_ctx_(ffmpeg::open_input_format(input_file.data())),
        input_packet_(ffmpeg::create_packet()),
        input_frame_(ffmpeg::create_frame()) {

    initialize();
  }

  void split() {
    std::cout << "Audio Splitter\n";
    std::cout << "==============\n\n";
    std::cout << std::format("Input: {}\n", input_file_);
    std::cout << std::format("Output Directory: {}\n", params_.output_dir);
    std::cout << std::format("Output Format: {}\n", params_.output_format);
    std::cout << std::format("Silence Threshold: {:.1f} dB\n",
                             params_.noise_threshold);
    std::cout << std::format("Min Silence: {:.2f} seconds\n",
                             params_.min_silence);
    std::cout << std::format("Min Segment: {:.2f} seconds\n\n",
                             params_.min_segment);

    // Create output directory
    fs::create_directories(params_.output_dir);

    // Get total duration
    const double total_duration =
        static_cast<double>(input_format_ctx_->duration) / AV_TIME_BASE;
    std::cout << std::format("Total Duration: {:.2f} seconds\n\n",
                             total_duration);

    // Detect silence segments and split points
    std::cout << "Phase 1: Analyzing audio for silence...\n";
    auto split_points = detect_split_points();

    if (split_points.empty()) {
      std::cout << "No silence detected. Audio will not be split.\n";
      return;
    }

    std::cout << std::format("Found {} split points\n\n", split_points.size());

    // Create segments based on split points
    std::vector<SplitSegment> segments;
    double start_time = 0.0;
    int segment_number = 1;

    for (const auto split_point : split_points) {
      if (split_point - start_time >= params_.min_segment) {
        segments.push_back({start_time, split_point, segment_number++});
        start_time = split_point;
      }
    }

    // Add final segment
    if (total_duration - start_time >= params_.min_segment) {
      segments.push_back({start_time, total_duration, segment_number});
    }

    std::cout << std::format("Phase 2: Splitting into {} segments...\n\n",
                             segments.size());

    // Extract each segment
    for (const auto &segment : segments) {
      extract_segment(segment);
    }

    std::cout << "\nSplitting completed successfully!\n";
    std::cout << std::format("Created {} audio segments in: {}\n",
                             segments.size(), params_.output_dir);
  }

private:
  void initialize() {
    // Find audio stream
    audio_stream_index_ = av_find_best_stream(
        input_format_ctx_.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    if (audio_stream_index_ < 0) {
      throw std::runtime_error("Failed to find audio stream");
    }

    const auto *input_stream = input_format_ctx_->streams[audio_stream_index_];

    // Setup decoder
    const auto *decoder =
        avcodec_find_decoder(input_stream->codecpar->codec_id);
    if (!decoder) {
      throw std::runtime_error("Failed to find decoder");
    }

    input_codec_ctx_ = ffmpeg::create_codec_context(decoder);
    avcodec_parameters_to_context(input_codec_ctx_.get(),
                                  input_stream->codecpar);

    if (avcodec_open2(input_codec_ctx_.get(), decoder, nullptr) < 0) {
      throw std::runtime_error("Failed to open decoder");
    }

    // Setup resampler for PCM output
    AVChannelLayout out_ch_layout = input_codec_ctx_->ch_layout;

    SwrContext *swr_ctx_raw = nullptr;
    swr_alloc_set_opts2(&swr_ctx_raw, &out_ch_layout, AV_SAMPLE_FMT_S16,
                        input_codec_ctx_->sample_rate,
                        &input_codec_ctx_->ch_layout,
                        input_codec_ctx_->sample_fmt,
                        input_codec_ctx_->sample_rate, 0, nullptr);

    swr_ctx_.reset(swr_ctx_raw);
    swr_init(swr_ctx_.get());
  }

  std::vector<double> detect_split_points() {
    std::vector<double> split_points;

    // For simplicity, we'll split based on silence detection using a simple
    // approach In a real implementation, you would parse silencedetect filter
    // output Here we'll create segments at regular intervals as a demonstration

    // Reset file position
    av_seek_frame(input_format_ctx_.get(), audio_stream_index_, 0,
                  AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(input_codec_ctx_.get());

    const auto *stream = input_format_ctx_->streams[audio_stream_index_];
    std::vector<int16_t> audio_buffer;
    const int sample_rate = input_codec_ctx_->sample_rate;
    const int channels = input_codec_ctx_->ch_layout.nb_channels;

    // Read and analyze audio
    bool in_silence = false;
    double silence_start = 0.0;
    int silence_sample_count = 0;
    const int min_silence_samples =
        static_cast<int>(params_.min_silence * sample_rate);
    const int16_t silence_threshold_value = static_cast<int16_t>(
        32767.0 * std::pow(10.0, params_.noise_threshold / 20.0));

    while (av_read_frame(input_format_ctx_.get(), input_packet_.get()) >= 0) {
      ffmpeg::ScopedPacketUnref packet_guard(input_packet_.get());

      if (input_packet_->stream_index != audio_stream_index_) {
        continue;
      }

      if (avcodec_send_packet(input_codec_ctx_.get(), input_packet_.get()) <
          0) {
        continue;
      }

      while (avcodec_receive_frame(input_codec_ctx_.get(),
                                   input_frame_.get()) >= 0) {
        ffmpeg::ScopedFrameUnref frame_guard(input_frame_.get());

        // Convert to PCM
        const int max_samples =
            swr_get_out_samples(swr_ctx_.get(), input_frame_->nb_samples);
        audio_buffer.resize(max_samples * channels);

        auto *out_buf = reinterpret_cast<uint8_t *>(audio_buffer.data());
        const int converted_samples =
            swr_convert(swr_ctx_.get(), &out_buf, max_samples,
                        const_cast<const uint8_t **>(input_frame_->data),
                        input_frame_->nb_samples);

        // Analyze samples for silence
        for (int i = 0; i < converted_samples * channels; ++i) {
          const bool is_silent =
              std::abs(audio_buffer[i]) < silence_threshold_value;

          if (is_silent) {
            if (!in_silence) {
              in_silence = true;
              silence_start = static_cast<double>(input_frame_->pts *
                                                  stream->time_base.num) /
                              stream->time_base.den;
              silence_sample_count = 0;
            }
            silence_sample_count++;
          } else {
            if (in_silence && silence_sample_count >= min_silence_samples) {
              // Found a silence segment long enough to split
              const double split_time =
                  silence_start + (params_.min_silence / 2.0);
              split_points.push_back(split_time);
            }
            in_silence = false;
            silence_sample_count = 0;
          }
        }
      }
    }

    return split_points;
  }

  void extract_segment(const SplitSegment &segment) {
    const auto output_filename = std::format(
        "{}/{}_{:03d}.{}", params_.output_dir, params_.output_prefix,
        segment.segment_number, params_.output_format);

    std::cout << std::format("Extracting segment {}: {:.2f}s - {:.2f}s -> {}\n",
                             segment.segment_number, segment.start_time,
                             segment.end_time, output_filename);

    // Seek to start position
    const auto *stream = input_format_ctx_->streams[audio_stream_index_];
    const int64_t start_pts = static_cast<int64_t>(
        segment.start_time * stream->time_base.den / stream->time_base.num);

    av_seek_frame(input_format_ctx_.get(), audio_stream_index_, start_pts,
                  AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(input_codec_ctx_.get());

    // Open output file
    std::ofstream output_stream(output_filename, std::ios::binary);
    if (!output_stream.is_open()) {
      std::cerr << std::format("Failed to create output file: {}\n",
                               output_filename);
      return;
    }

    // Write WAV header (placeholder)
    const int sample_rate = input_codec_ctx_->sample_rate;
    const int channels = input_codec_ctx_->ch_layout.nb_channels;
    write_wav_header(output_stream, sample_rate, channels, 0);

    uint32_t total_bytes_written = 0;
    std::vector<int16_t> audio_buffer;

    // Read and write segment
    auto local_packet = ffmpeg::create_packet();
    auto local_frame = ffmpeg::create_frame();

    while (av_read_frame(input_format_ctx_.get(), local_packet.get()) >= 0) {
      ffmpeg::ScopedPacketUnref packet_guard(local_packet.get());

      if (local_packet->stream_index != audio_stream_index_) {
        continue;
      }

      if (avcodec_send_packet(input_codec_ctx_.get(), local_packet.get()) < 0) {
        continue;
      }

      while (avcodec_receive_frame(input_codec_ctx_.get(), local_frame.get()) >=
             0) {
        ffmpeg::ScopedFrameUnref frame_guard(local_frame.get());

        const double frame_time =
            static_cast<double>(local_frame->pts * stream->time_base.num) /
            stream->time_base.den;

        if (frame_time < segment.start_time) {
          continue;
        }

        if (frame_time >= segment.end_time) {
          goto segment_complete;
        }

        // Convert to PCM
        const int max_samples =
            swr_get_out_samples(swr_ctx_.get(), local_frame->nb_samples);
        audio_buffer.resize(max_samples * channels);

        auto *out_buf = reinterpret_cast<uint8_t *>(audio_buffer.data());
        const int converted_samples =
            swr_convert(swr_ctx_.get(), &out_buf, max_samples,
                        const_cast<const uint8_t **>(local_frame->data),
                        local_frame->nb_samples);

        if (converted_samples > 0) {
          const auto bytes_to_write =
              converted_samples * channels * sizeof(int16_t);
          output_stream.write(reinterpret_cast<char *>(audio_buffer.data()),
                              bytes_to_write);
          total_bytes_written += bytes_to_write;
        }
      }
    }

  segment_complete:
    // Update WAV header with actual size
    output_stream.seekp(0, std::ios::beg);
    write_wav_header(output_stream, sample_rate, channels, total_bytes_written);
    output_stream.close();

    std::cout << std::format(
        "  Created: {} ({:.2f}s, {} bytes)\n", output_filename,
        segment.end_time - segment.start_time, total_bytes_written);
  }

  std::string input_file_;
  SplitterParams params_;

  ffmpeg::FormatContextPtr input_format_ctx_;
  ffmpeg::CodecContextPtr input_codec_ctx_;
  ffmpeg::SwrContextPtr swr_ctx_;
  ffmpeg::PacketPtr input_packet_;
  ffmpeg::FramePtr input_frame_;

  int audio_stream_index_ = -1;
};

} // anonymous namespace

int main(int argc, char *argv[]) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  try {
    const auto params = parse_arguments(argc, argv);
    if (!params) {
      print_usage(argv[0]);
      return 1;
    }

    const std::string input = argv[1];

    AudioSplitter splitter(input, *params);
    splitter.split();

    return 0;
  } catch (const std::exception &e) {
    std::cerr << std::format("Error: {}\n", e.what());
    return 1;
  }
}
