/**
 * Audio Mixer
 *
 * This sample demonstrates how to mix two audio files together
 * using modern C++20 and FFmpeg libraries.
 */

#include "ffmpeg_wrappers.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

namespace {

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

class AudioDecoder {
public:
  AudioDecoder(std::string_view filename, int target_sample_rate,
               int target_channels)
      : format_ctx_(ffmpeg::open_input_format(filename.data())),
        packet_(ffmpeg::create_packet()), frame_(ffmpeg::create_frame()) {

    initialize(target_sample_rate, target_channels);
  }

  int read_samples(int16_t *buffer, int num_samples) {
    int samples_read = 0;

    while (samples_read < num_samples && !eof_) {
      auto ret = avcodec_receive_frame(codec_ctx_.get(), frame_.get());

      if (ret == AVERROR(EAGAIN)) {
        // Need more packets
        ret = av_read_frame(format_ctx_.get(), packet_.get());
        if (ret < 0) {
          eof_ = true;
          break;
        }

        if (packet_->stream_index == stream_index_) {
          avcodec_send_packet(codec_ctx_.get(), packet_.get());
        }
        av_packet_unref(packet_.get());
        continue;
      }

      if (ret == AVERROR_EOF || ret < 0) {
        eof_ = true;
        break;
      }

      // Resample
      auto *out_buf = reinterpret_cast<uint8_t *>(buffer + samples_read);
      const auto dst_nb_samples = num_samples - samples_read;

      ret = swr_convert(swr_ctx_.get(), &out_buf, dst_nb_samples,
                        const_cast<const uint8_t **>(frame_->data),
                        frame_->nb_samples);

      if (ret > 0) {
        samples_read += ret;
      }
    }

    return samples_read;
  }

  bool is_eof() const { return eof_; }

private:
  void initialize(int target_sample_rate, int target_channels) {
    // Find audio stream
    const auto stream_idx =
        ffmpeg::find_stream_index(format_ctx_.get(), AVMEDIA_TYPE_AUDIO);
    if (!stream_idx) {
      throw ffmpeg::FFmpegError("No audio stream found");
    }
    stream_index_ = *stream_idx;

    // Setup decoder
    const auto *codecpar = format_ctx_->streams[stream_index_]->codecpar;
    const auto *decoder = avcodec_find_decoder(codecpar->codec_id);
    if (!decoder) {
      throw ffmpeg::FFmpegError("Decoder not found");
    }

    codec_ctx_ = ffmpeg::create_codec_context(decoder);
    ffmpeg::check_error(
        avcodec_parameters_to_context(codec_ctx_.get(), codecpar),
        "copy codec parameters");
    ffmpeg::check_error(avcodec_open2(codec_ctx_.get(), decoder, nullptr),
                        "open decoder");

    // Setup resampler
    AVChannelLayout out_ch_layout;
    if (target_channels == 1) {
      out_ch_layout = AV_CHANNEL_LAYOUT_MONO;
    } else {
      out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    }

    ffmpeg::check_error(
        swr_alloc_set_opts2(&swr_ctx_raw_, &out_ch_layout, AV_SAMPLE_FMT_S16,
                            target_sample_rate, &codec_ctx_->ch_layout,
                            codec_ctx_->sample_fmt, codec_ctx_->sample_rate, 0,
                            nullptr),
        "allocate resampler");

    swr_ctx_.reset(swr_ctx_raw_);
    swr_ctx_raw_ = nullptr;

    ffmpeg::check_error(swr_init(swr_ctx_.get()), "initialize resampler");
  }

  ffmpeg::FormatContextPtr format_ctx_;
  ffmpeg::CodecContextPtr codec_ctx_;
  ffmpeg::SwrContextPtr swr_ctx_;
  ffmpeg::PacketPtr packet_;
  ffmpeg::FramePtr frame_;
  SwrContext *swr_ctx_raw_ = nullptr;
  int stream_index_ = -1;
  bool eof_ = false;
};

class AudioMixer {
public:
  AudioMixer(std::string_view input1, std::string_view input2,
             const fs::path &output, float volume1, float volume2)
      : output_file_(output), volume1_(std::clamp(volume1, 0.0f, 1.0f)),
        volume2_(std::clamp(volume2, 0.0f, 1.0f)),
        decoder1_(input1, target_sample_rate_, target_channels_),
        decoder2_(input2, target_sample_rate_, target_channels_),
        buffer1_(buffer_size_ * target_channels_),
        buffer2_(buffer_size_ * target_channels_),
        output_buffer_(buffer_size_ * target_channels_) {}

  void mix() {
    std::cout << "Audio Mixer\n";
    std::cout << "===========\n\n";
    std::cout << std::format("Output: {}\n", output_file_.string());
    std::cout << std::format("Output format: {}kHz, Stereo, 16-bit PCM\n",
                             target_sample_rate_ / 1000);
    std::cout << std::format("Volume 1: {:.2f}\n", volume1_);
    std::cout << std::format("Volume 2: {:.2f}\n\n", volume2_);

    // Open output file
    std::ofstream output_stream(output_file_, std::ios::binary);
    if (!output_stream.is_open()) {
      throw std::runtime_error(
          std::format("Failed to open output file: {}", output_file_.string()));
    }

    // Write placeholder WAV header
    write_wav_header(output_stream, target_sample_rate_, target_channels_, 0);

    uint32_t total_samples_written = 0;
    int iteration = 0;

    std::cout << "Mixing in progress...\n";

    // Mix audio
    while (!decoder1_.is_eof() || !decoder2_.is_eof()) {
      const auto samples1 =
          decoder1_.read_samples(buffer1_.data(), buffer_size_);
      const auto samples2 =
          decoder2_.read_samples(buffer2_.data(), buffer_size_);

      const auto max_samples = std::max(samples1, samples2);

      if (max_samples == 0) {
        break;
      }

      // Mix samples
      for (int i = 0; i < max_samples * target_channels_; ++i) {
        const auto sample1 =
            (i < samples1 * target_channels_) ? buffer1_[i] : int16_t{0};
        const auto sample2 =
            (i < samples2 * target_channels_) ? buffer2_[i] : int16_t{0};

        // Mix with volume control and clipping prevention
        auto mixed = static_cast<int32_t>(sample1 * volume1_) +
                     static_cast<int32_t>(sample2 * volume2_);

        // Clamp to prevent overflow
        mixed = std::clamp(mixed, static_cast<int32_t>(-32768),
                           static_cast<int32_t>(32767));

        output_buffer_[i] = static_cast<int16_t>(mixed);
      }

      // Write to file
      const auto bytes_to_write =
          max_samples * target_channels_ * sizeof(int16_t);
      output_stream.write(reinterpret_cast<char *>(output_buffer_.data()),
                          bytes_to_write);
      total_samples_written += max_samples;

      ++iteration;
      if (iteration % 100 == 0) {
        const auto seconds =
            total_samples_written / static_cast<double>(target_sample_rate_);
        std::cout << std::format("Mixed {:.2f} seconds\r", seconds)
                  << std::flush;
      }
    }

    const auto total_bytes =
        total_samples_written * target_channels_ * sizeof(int16_t);

    std::cout << std::format("\nTotal samples mixed: {}\n",
                             total_samples_written);
    std::cout << std::format("Duration: {:.2f} seconds\n",
                             total_samples_written /
                                 static_cast<double>(target_sample_rate_));
    std::cout << std::format("Output size: {} bytes\n", total_bytes);

    // Update WAV header
    output_stream.seekp(0, std::ios::beg);
    write_wav_header(output_stream, target_sample_rate_, target_channels_,
                     total_bytes);

    std::cout << std::format("\nMixing completed successfully!\n");
    std::cout << std::format("Output file: {}\n", output_file_.string());
  }

private:
  static constexpr int target_sample_rate_ = 44100;
  static constexpr int target_channels_ = 2;
  static constexpr int buffer_size_ = 4096;

  fs::path output_file_;
  float volume1_;
  float volume2_;

  AudioDecoder decoder1_;
  AudioDecoder decoder2_;

  std::vector<int16_t> buffer1_;
  std::vector<int16_t> buffer2_;
  std::vector<int16_t> output_buffer_;
};

} // anonymous namespace

int main(int argc, char *argv[]) {
  if (argc < 4) {
    std::cerr << std::format(
        "Usage: {} <input1> <input2> <output> [volume1] [volume2]\n", argv[0]);
    std::cerr << std::format(
        "Example: {} audio1.mp3 audio2.mp3 mixed.wav 0.5 0.5\n", argv[0]);
    std::cerr << "\nMixes two audio files together.\n";
    std::cerr << "Volume range: 0.0 to 1.0 (default: 0.5 for both)\n";
    std::cerr << "Output: WAV file, 44.1kHz, Stereo, 16-bit\n";
    return 1;
  }

  try {
    const std::string_view input1_filename{argv[1]};
    const std::string_view input2_filename{argv[2]};
    const fs::path output_filename{argv[3]};
    const float volume1 =
        argc > 4 ? static_cast<float>(std::atof(argv[4])) : 0.5f;
    const float volume2 =
        argc > 5 ? static_cast<float>(std::atof(argv[5])) : 0.5f;

    std::cout << "Audio Mixer\n";
    std::cout << "===========\n\n";
    std::cout << std::format("Input 1: {} (volume: {:.2f})\n", input1_filename,
                             volume1);
    std::cout << std::format("Input 2: {} (volume: {:.2f})\n", input2_filename,
                             volume2);

    AudioMixer mixer(input1_filename, input2_filename, output_filename, volume1,
                     volume2);
    mixer.mix();

  } catch (const ffmpeg::FFmpegError &e) {
    std::cerr << std::format("FFmpeg error: {}\n", e.what());
    return 1;
  } catch (const std::exception &e) {
    std::cerr << std::format("Error: {}\n", e.what());
    return 1;
  }

  return 0;
}
