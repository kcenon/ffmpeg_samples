/**
 * Audio Noise Reduction
 *
 * This sample demonstrates how to apply noise reduction and audio enhancement
 * filters using modern C++20 and FFmpeg libraries.
 */

#include "ffmpeg_wrappers.hpp"

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <string_view>
#include <unordered_map>

namespace fs = std::filesystem;

namespace {

// WAV header structure
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

std::string_view get_filter_description(std::string_view preset) {
  static const std::unordered_map<std::string_view, std::string_view> presets =
      {{"light",
        "highpass=f=80,lowpass=f=15000,anlmdn=s=1:p=0.002:r=0.002:m=15"},
       {"medium", "highpass=f=100,lowpass=f=12000,anlmdn=s=3:p=0.004:r=0.004:m="
                  "15,volume=1.5"},
       {"heavy", "highpass=f=150,lowpass=f=10000,anlmdn=s=5:p=0.006:r=0.006:m="
                 "15,volume=2.0"},
       {"voice", "highpass=f=80,lowpass=f=8000,anlmdn=s=2:p=0.003:r=0.003:m=15,"
                 "loudnorm=I=-16:TP=-1.5:LRA=11"},
       {"music",
        "highpass=f=20,lowpass=f=18000,anlmdn=s=1:p=0.001:r=0.001:m=15"},
       {"podcast", "highpass=f=100,lowpass=f=10000,anlmdn=s=2:p=0.003:r=0.003:"
                   "m=15,loudnorm=I=-19:TP=-2:LRA=15,compand=attacks=0.3:"
                   "decays=0.8:points=-70/-70|-60/-20|-20/-10|0/-5|20/0"},
       {"denoise_only", "anlmdn=s=3:p=0.004:r=0.004:m=15"},
       {"normalize", "loudnorm=I=-16:TP=-1.5:LRA=11"},
       {"compress", "compand=attacks=0.3:decays=0.8:points=-80/-80|-45/-15|-27/"
                    "-9|-5/-4|0/-1|20/0"}};

  const auto it = presets.find(preset);
  return it != presets.end() ? it->second : "";
}

class AudioNoiseReducer {
public:
  AudioNoiseReducer(std::string_view input_file, const fs::path &output_file,
                    std::string_view filter_preset)
      : output_file_(output_file),
        filter_description_(get_filter_description(filter_preset)),
        format_ctx_(ffmpeg::open_input_format(input_file.data())),
        packet_(ffmpeg::create_packet()), frame_(ffmpeg::create_frame()),
        filtered_frame_(ffmpeg::create_frame()) {

    if (filter_description_.empty()) {
      throw std::invalid_argument(
          std::format("Unknown preset: {}", filter_preset));
    }

    initialize();
  }

  ~AudioNoiseReducer() {
    // Cleanup handled by RAII wrappers
  }

  void process() {
    std::cout << "Audio Noise Reduction\n";
    std::cout << "=====================\n\n";
    std::cout << std::format("Input: {}\n", format_ctx_->url);
    std::cout << std::format("Output: {}\n", output_file_.string());
    std::cout << std::format("Preset: {}\n", filter_description_);
    std::cout << std::format("Sample Rate: {} Hz\n", codec_ctx_->sample_rate);
    std::cout << std::format("Channels: {}\n\n",
                             codec_ctx_->ch_layout.nb_channels);

    // Open output file
    std::ofstream output_stream(output_file_, std::ios::binary);
    if (!output_stream.is_open()) {
      throw std::runtime_error(
          std::format("Failed to open output file: {}", output_file_.string()));
    }

    // Write placeholder WAV header
    write_wav_header(output_stream, out_sample_rate_, out_channels_, 0);

    uint32_t total_data_size = 0;
    int frame_count = 0;

    std::cout << "Processing audio...\n";

    // Process all frames
    while (av_read_frame(format_ctx_.get(), packet_.get()) >= 0) {
      ffmpeg::ScopedPacketUnref packet_guard(packet_.get());

      if (packet_->stream_index != audio_stream_index_) {
        continue;
      }

      const auto ret = avcodec_send_packet(codec_ctx_.get(), packet_.get());
      if (ret < 0) {
        continue;
      }

      while (true) {
        const auto recv_ret =
            avcodec_receive_frame(codec_ctx_.get(), frame_.get());

        if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF) {
          break;
        }

        if (recv_ret < 0) {
          std::cerr << "Error during decoding\n";
          break;
        }

        ffmpeg::ScopedFrameUnref frame_guard(frame_.get());

        // Push frame to filter graph
        ffmpeg::check_error(
            av_buffersrc_add_frame_flags(buffersrc_ctx_, frame_.get(),
                                         AV_BUFFERSRC_FLAG_KEEP_REF),
            "feed frame to filter graph");

        // Pull filtered frames
        while (true) {
          const auto filter_ret =
              av_buffersink_get_frame(buffersink_ctx_, filtered_frame_.get());

          if (filter_ret == AVERROR(EAGAIN) || filter_ret == AVERROR_EOF) {
            break;
          }

          if (filter_ret < 0) {
            std::cerr << "Error getting filtered frame\n";
            break;
          }

          ffmpeg::ScopedFrameUnref filtered_guard(filtered_frame_.get());

          // Write filtered audio
          total_data_size += write_frame(output_stream);
          ++frame_count;

          if (frame_count % 100 == 0) {
            std::cout << std::format("Processed {} frames\r", frame_count)
                      << std::flush;
          }
        }
      }
    }

    std::cout << std::format("\nTotal frames processed: {}\n", frame_count);
    std::cout << std::format("Output data size: {} bytes\n", total_data_size);

    // Update WAV header
    output_stream.seekp(0, std::ios::beg);
    write_wav_header(output_stream, out_sample_rate_, out_channels_,
                     total_data_size);

    std::cout << std::format("\n✓ Noise reduction completed successfully\n");
    std::cout << std::format("Output file: {}\n", output_file_.string());
  }

private:
  void initialize() {
    // Find audio stream
    const auto stream_idx =
        ffmpeg::find_stream_index(format_ctx_.get(), AVMEDIA_TYPE_AUDIO);
    if (!stream_idx) {
      throw ffmpeg::FFmpegError("No audio stream found");
    }
    audio_stream_index_ = *stream_idx;

    // Open decoder
    const auto *codecpar = format_ctx_->streams[audio_stream_index_]->codecpar;
    const auto *decoder = avcodec_find_decoder(codecpar->codec_id);
    if (!decoder) {
      throw ffmpeg::FFmpegError("Decoder not found");
    }

    codec_ctx_ = ffmpeg::create_codec_context(decoder);
    ffmpeg::check_error(
        avcodec_parameters_to_context(codec_ctx_.get(), codecpar),
        "copy decoder parameters");
    ffmpeg::check_error(avcodec_open2(codec_ctx_.get(), decoder, nullptr),
                        "open decoder");

    // Initialize filter graph
    initialize_filter();

    // Setup output parameters
    out_sample_rate_ = codec_ctx_->sample_rate;
    out_channels_ = codec_ctx_->ch_layout.nb_channels;

    // Setup resampler to convert to S16 for WAV output
    AVChannelLayout out_ch_layout;
    av_channel_layout_default(&out_ch_layout, out_channels_);

    ffmpeg::check_error(
        swr_alloc_set_opts2(&swr_ctx_raw_, &out_ch_layout, AV_SAMPLE_FMT_S16,
                            out_sample_rate_, &codec_ctx_->ch_layout,
                            codec_ctx_->sample_fmt, codec_ctx_->sample_rate, 0,
                            nullptr),
        "allocate resampler");

    swr_ctx_.reset(swr_ctx_raw_);
    swr_ctx_raw_ = nullptr;

    ffmpeg::check_error(swr_init(swr_ctx_.get()), "initialize resampler");

    // Allocate output buffer
    max_dst_nb_samples_ = av_rescale_rnd(4096, out_sample_rate_,
                                         codec_ctx_->sample_rate, AV_ROUND_UP);

    ffmpeg::check_error(av_samples_alloc_array_and_samples(
                            &dst_data_, &dst_linesize_, out_channels_,
                            max_dst_nb_samples_, AV_SAMPLE_FMT_S16, 0),
                        "allocate sample buffer");
  }

  void initialize_filter() {
    const auto *abuffersrc = avfilter_get_by_name("abuffer");
    const auto *abuffersink = avfilter_get_by_name("abuffersink");

    filter_graph_.reset(avfilter_graph_alloc());
    if (!filter_graph_) {
      throw ffmpeg::FFmpegError("Failed to allocate filter graph");
    }

    // Create buffer source
    char ch_layout[64];
    av_channel_layout_describe(&codec_ctx_->ch_layout, ch_layout,
                               sizeof(ch_layout));

    const auto args = std::format(
        "sample_rate={}:sample_fmt={}:channel_layout={}:time_base={}/{}",
        codec_ctx_->sample_rate, av_get_sample_fmt_name(codec_ctx_->sample_fmt),
        ch_layout, 1, codec_ctx_->sample_rate);

    ffmpeg::check_error(avfilter_graph_create_filter(
                            &buffersrc_ctx_, abuffersrc, "in", args.c_str(),
                            nullptr, filter_graph_.get()),
                        "create buffer source");

    // Create buffer sink
    ffmpeg::check_error(
        avfilter_graph_create_filter(&buffersink_ctx_, abuffersink, "out",
                                     nullptr, nullptr, filter_graph_.get()),
        "create buffer sink");

    // Set up filter graph endpoints
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();

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

    // Parse filter description
    const auto ret = avfilter_graph_parse_ptr(filter_graph_.get(),
                                              filter_description_.data(),
                                              &inputs, &outputs, nullptr);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    ffmpeg::check_error(ret, "parse filter graph");

    // Configure filter graph
    ffmpeg::check_error(avfilter_graph_config(filter_graph_.get(), nullptr),
                        "configure filter graph");
  }

  uint32_t write_frame(std::ofstream &output_stream) {
    const auto dst_nb_samples =
        av_rescale_rnd(swr_get_delay(swr_ctx_.get(), codec_ctx_->sample_rate) +
                           filtered_frame_->nb_samples,
                       out_sample_rate_, codec_ctx_->sample_rate, AV_ROUND_UP);

    if (dst_nb_samples > max_dst_nb_samples_) {
      av_freep(&dst_data_[0]);
      av_samples_alloc(dst_data_, &dst_linesize_, out_channels_, dst_nb_samples,
                       AV_SAMPLE_FMT_S16, 1);
      max_dst_nb_samples_ = dst_nb_samples;
    }

    const auto ret =
        swr_convert(swr_ctx_.get(), dst_data_, dst_nb_samples,
                    const_cast<const uint8_t **>(filtered_frame_->data),
                    filtered_frame_->nb_samples);

    if (ret <= 0) {
      return 0;
    }

    const auto dst_bufsize = av_samples_get_buffer_size(
        &dst_linesize_, out_channels_, ret, AV_SAMPLE_FMT_S16, 1);
    output_stream.write(reinterpret_cast<char *>(dst_data_[0]), dst_bufsize);

    return static_cast<uint32_t>(dst_bufsize);
  }

  fs::path output_file_;
  std::string_view filter_description_;
  int audio_stream_index_ = -1;
  int out_sample_rate_ = 44100;
  int out_channels_ = 2;
  int max_dst_nb_samples_ = 0;
  int dst_linesize_ = 0;
  uint8_t **dst_data_ = nullptr;

  ffmpeg::FormatContextPtr format_ctx_;
  ffmpeg::CodecContextPtr codec_ctx_;
  ffmpeg::FilterGraphPtr filter_graph_;
  ffmpeg::PacketPtr packet_;
  ffmpeg::FramePtr frame_;
  ffmpeg::FramePtr filtered_frame_;
  ffmpeg::SwrContextPtr swr_ctx_;
  SwrContext *swr_ctx_raw_ = nullptr;

  AVFilterContext *buffersrc_ctx_ = nullptr;
  AVFilterContext *buffersink_ctx_ = nullptr;
};

void print_usage(std::string_view prog_name) {
  std::cout << std::format("Usage: {} <input_file> <output_file> <preset>\n\n",
                           prog_name);
  std::cout << "Available Presets:\n";
  std::cout << "  light        - Light noise reduction, preserves quality\n";
  std::cout << "  medium       - Balanced noise reduction (default)\n";
  std::cout << "  heavy        - Aggressive noise reduction\n";
  std::cout << "  voice        - Optimized for voice recordings\n";
  std::cout << "  music        - Optimized for music\n";
  std::cout << "  podcast      - Full processing for podcasts (denoise + "
               "normalize + compress)\n";
  std::cout << "  denoise_only - Only apply denoising filter\n";
  std::cout << "  normalize    - Only apply loudness normalization\n";
  std::cout << "  compress     - Only apply dynamic range compression\n\n";
  std::cout << "Examples:\n";
  std::cout << std::format("  {} noisy_audio.mp3 clean_audio.wav voice\n",
                           prog_name);
  std::cout << std::format("  {} podcast.wav enhanced.wav podcast\n",
                           prog_name);
  std::cout << std::format("  {} music.flac cleaned.wav light\n", prog_name);
}

} // anonymous namespace

int main(int argc, char *argv[]) {
  if (argc < 4) {
    print_usage(argv[0]);
    return 1;
  }

  try {
    const std::string_view input_file{argv[1]};
    const fs::path output_file{argv[2]};
    const std::string_view preset{argv[3]};

    AudioNoiseReducer reducer(input_file, output_file, preset);
    reducer.process();

  } catch (const ffmpeg::FFmpegError &e) {
    std::cerr << std::format("FFmpeg error: {}\n", e.what());
    return 1;
  } catch (const std::exception &e) {
    std::cerr << std::format("Error: {}\n", e.what());
    return 1;
  }

  return 0;
}
