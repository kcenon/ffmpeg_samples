/**
 * Audio Resampler
 *
 * This sample demonstrates how to resample audio (change sample rate,
 * channel layout, or sample format) using modern C++20 and FFmpeg's
 * libswresample.
 */

#include "ffmpeg_wrappers.hpp"

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace {

// Simple WAV header structure
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

class AudioResampler {
public:
  AudioResampler(std::string_view input_file, const fs::path &output_file,
                 int target_sample_rate, int target_channels)
      : output_file_(output_file), target_sample_rate_(target_sample_rate),
        target_channels_(target_channels),
        format_ctx_(ffmpeg::open_input_format(input_file.data())),
        packet_(ffmpeg::create_packet()), frame_(ffmpeg::create_frame()) {

    if (target_channels < 1 || target_channels > 2) {
      throw std::invalid_argument("Channels must be 1 (mono) or 2 (stereo)");
    }

    initialize();
  }

  ~AudioResampler() {
    if (dst_data_) {
      av_freep(&dst_data_[0]);
      av_freep(&dst_data_);
    }
  }

  void resample() {
    // Print resampling information
    std::cout << "Audio Resampler\n";
    std::cout << "===============\n\n";
    std::cout << std::format("Input file: {}\n", format_ctx_->url);
    std::cout << std::format("Output file: {}\n\n", output_file_.string());

    std::cout << "Input format:\n";
    std::cout << std::format("  Sample rate: {} Hz\n", codec_ctx_->sample_rate);
    std::cout << std::format("  Channels: {}\n",
                             codec_ctx_->ch_layout.nb_channels);
    std::cout << std::format("  Sample format: {}\n\n",
                             av_get_sample_fmt_name(codec_ctx_->sample_fmt));

    std::cout << "Output format:\n";
    std::cout << std::format("  Sample rate: {} Hz\n", target_sample_rate_);
    std::cout << std::format("  Channels: {}\n", target_channels_);
    std::cout << "  Sample format: S16 (16-bit signed integer)\n\n";

    // Open output file
    std::ofstream output_stream(output_file_, std::ios::binary);
    if (!output_stream.is_open()) {
      throw std::runtime_error(
          std::format("Failed to open output file: {}", output_file_.string()));
    }

    // Write placeholder WAV header
    write_wav_header(output_stream, target_sample_rate_, target_channels_, 0);

    uint32_t total_data_size = 0;
    int frame_count = 0;

    std::cout << "Resampling in progress...\n";

    // Read and resample frames
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
          break;
        }

        ffmpeg::ScopedFrameUnref frame_guard(frame_.get());

        // Resample and write
        total_data_size += resample_and_write(output_stream);
        ++frame_count;

        if (frame_count % 100 == 0) {
          std::cout << std::format("Processed {} frames\r", frame_count)
                    << std::flush;
        }
      }
    }

    // Flush resampler
    total_data_size += flush_resampler(output_stream);

    std::cout << std::format("\nTotal frames processed: {}\n", frame_count);
    std::cout << std::format("Output data size: {} bytes\n", total_data_size);

    // Update WAV header
    output_stream.seekp(0, std::ios::beg);
    write_wav_header(output_stream, target_sample_rate_, target_channels_,
                     total_data_size);

    std::cout << std::format("\nResampling completed successfully!\n");
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

    // Get decoder
    const auto *codecpar = format_ctx_->streams[audio_stream_index_]->codecpar;
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
    if (target_channels_ == 1) {
      out_ch_layout = AV_CHANNEL_LAYOUT_MONO;
    } else {
      out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    }

    ffmpeg::check_error(
        swr_alloc_set_opts2(&swr_ctx_raw_, &out_ch_layout, AV_SAMPLE_FMT_S16,
                            target_sample_rate_, &codec_ctx_->ch_layout,
                            codec_ctx_->sample_fmt, codec_ctx_->sample_rate, 0,
                            nullptr),
        "allocate resampler");

    swr_ctx_.reset(swr_ctx_raw_);
    swr_ctx_raw_ = nullptr;

    ffmpeg::check_error(swr_init(swr_ctx_.get()), "initialize resampler");

    // Allocate output buffer
    max_dst_nb_samples_ = av_rescale_rnd(4096, target_sample_rate_,
                                         codec_ctx_->sample_rate, AV_ROUND_UP);

    ffmpeg::check_error(av_samples_alloc_array_and_samples(
                            &dst_data_, &dst_linesize_, target_channels_,
                            max_dst_nb_samples_, AV_SAMPLE_FMT_S16, 0),
                        "allocate sample buffer");
  }

  uint32_t resample_and_write(std::ofstream &output_stream) {
    const auto dst_nb_samples = av_rescale_rnd(
        swr_get_delay(swr_ctx_.get(), codec_ctx_->sample_rate) +
            frame_->nb_samples,
        target_sample_rate_, codec_ctx_->sample_rate, AV_ROUND_UP);

    if (dst_nb_samples > max_dst_nb_samples_) {
      av_freep(&dst_data_[0]);
      av_samples_alloc(dst_data_, &dst_linesize_, target_channels_,
                       dst_nb_samples, AV_SAMPLE_FMT_S16, 1);
      max_dst_nb_samples_ = dst_nb_samples;
    }

    const auto ret = swr_convert(swr_ctx_.get(), dst_data_, dst_nb_samples,
                                 const_cast<const uint8_t **>(frame_->data),
                                 frame_->nb_samples);

    if (ret <= 0) {
      return 0;
    }

    const auto dst_bufsize = av_samples_get_buffer_size(
        &dst_linesize_, target_channels_, ret, AV_SAMPLE_FMT_S16, 1);
    output_stream.write(reinterpret_cast<char *>(dst_data_[0]), dst_bufsize);

    return static_cast<uint32_t>(dst_bufsize);
  }

  uint32_t flush_resampler(std::ofstream &output_stream) {
    uint32_t total_flushed = 0;

    while (true) {
      const auto ret = swr_convert(swr_ctx_.get(), dst_data_,
                                   max_dst_nb_samples_, nullptr, 0);
      if (ret <= 0) {
        break;
      }

      const auto dst_bufsize = av_samples_get_buffer_size(
          &dst_linesize_, target_channels_, ret, AV_SAMPLE_FMT_S16, 1);
      output_stream.write(reinterpret_cast<char *>(dst_data_[0]), dst_bufsize);
      total_flushed += dst_bufsize;
    }

    return total_flushed;
  }

  fs::path output_file_;
  int target_sample_rate_;
  int target_channels_;
  int audio_stream_index_ = -1;
  int max_dst_nb_samples_ = 0;
  int dst_linesize_ = 0;
  uint8_t **dst_data_ = nullptr;

  ffmpeg::FormatContextPtr format_ctx_;
  ffmpeg::CodecContextPtr codec_ctx_;
  ffmpeg::PacketPtr packet_;
  ffmpeg::FramePtr frame_;
  ffmpeg::SwrContextPtr swr_ctx_;
  SwrContext *swr_ctx_raw_ = nullptr;
};

} // anonymous namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::cerr << std::format(
        "Usage: {} <input_file> <output_file> [sample_rate] [channels]\n",
        argv[0]);
    std::cerr << std::format("Example: {} input.mp3 output.wav 48000 1\n",
                             argv[0]);
    std::cerr << "\nDefault output: 44100 Hz, Stereo\n";
    std::cerr << "Channels: 1 (mono), 2 (stereo)\n";
    return 1;
  }

  try {
    const std::string_view input_filename{argv[1]};
    const fs::path output_filename{argv[2]};
    const int target_sample_rate = argc > 3 ? std::atoi(argv[3]) : 44100;
    const int target_channels = argc > 4 ? std::atoi(argv[4]) : 2;

    AudioResampler resampler(input_filename, output_filename,
                             target_sample_rate, target_channels);
    resampler.resample();

  } catch (const ffmpeg::FFmpegError &e) {
    std::cerr << std::format("FFmpeg error: {}\n", e.what());
    return 1;
  } catch (const std::exception &e) {
    std::cerr << std::format("Error: {}\n", e.what());
    return 1;
  }

  return 0;
}
