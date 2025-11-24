/**
 * Audio Format Converter
 *
 * This sample demonstrates how to convert audio files between different formats
 * with configurable quality settings using modern C++20 and FFmpeg libraries.
 */

#include "ffmpeg_wrappers.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <iostream>
#include <string_view>
#include <unordered_map>

namespace fs = std::filesystem;

namespace {

struct CodecInfo {
  AVCodecID codec_id;
  const char *name;
  int default_bitrate;
  int default_sample_rate;
};

const std::unordered_map<std::string_view, CodecInfo> format_codecs = {
    {".mp3", {AV_CODEC_ID_MP3, "libmp3lame", 192000, 44100}},
    {".aac", {AV_CODEC_ID_AAC, "aac", 128000, 48000}},
    {".m4a", {AV_CODEC_ID_AAC, "aac", 128000, 48000}},
    {".ogg", {AV_CODEC_ID_VORBIS, "libvorbis", 128000, 48000}},
    {".opus", {AV_CODEC_ID_OPUS, "libopus", 128000, 48000}},
    {".flac", {AV_CODEC_ID_FLAC, "flac", 0, 48000}},          // Lossless
    {".wav", {AV_CODEC_ID_PCM_S16LE, "pcm_s16le", 0, 48000}}, // PCM
    {".wma", {AV_CODEC_ID_WMAV2, "wmav2", 128000, 44100}}};

CodecInfo get_codec_info(std::string_view filename) {
  const auto ext = fs::path(filename).extension().string();
  const auto it = format_codecs.find(ext);

  if (it != format_codecs.end()) {
    return it->second;
  }

  // Default to AAC
  return {AV_CODEC_ID_AAC, "aac", 128000, 48000};
}

class AudioFormatConverter {
public:
  AudioFormatConverter(std::string_view input_file, const fs::path &output_file,
                       int bitrate = 0, int sample_rate = 0, int channels = 0)
      : output_file_(output_file), target_bitrate_(bitrate),
        target_sample_rate_(sample_rate), target_channels_(channels),
        format_ctx_(ffmpeg::open_input_format(input_file.data())),
        input_packet_(ffmpeg::create_packet()),
        input_frame_(ffmpeg::create_frame()),
        output_frame_(ffmpeg::create_frame()) {

    initialize();
  }

  void convert() {
    std::cout << "Audio Format Converter\n";
    std::cout << "======================\n\n";

    std::cout << "Input:\n";
    std::cout << std::format("  File: {}\n", format_ctx_->url);
    std::cout << std::format("  Codec: {}\n", input_codec_->long_name);
    std::cout << std::format("  Sample Rate: {} Hz\n",
                             input_codec_ctx_->sample_rate);
    std::cout << std::format("  Channels: {}\n",
                             input_codec_ctx_->ch_layout.nb_channels);
    std::cout << std::format("  Bitrate: {} kbps\n",
                             input_codec_ctx_->bit_rate / 1000);

    std::cout << "\nOutput:\n";
    std::cout << std::format("  File: {}\n", output_file_.string());
    std::cout << std::format("  Codec: {}\n", output_codec_->long_name);
    std::cout << std::format("  Sample Rate: {} Hz\n",
                             output_codec_ctx_->sample_rate);
    std::cout << std::format("  Channels: {}\n",
                             output_codec_ctx_->ch_layout.nb_channels);

    if (output_codec_ctx_->bit_rate > 0) {
      std::cout << std::format("  Bitrate: {} kbps\n",
                               output_codec_ctx_->bit_rate / 1000);
    } else {
      std::cout << "  Bitrate: Lossless\n";
    }

    std::cout << "\nConverting audio...\n";

    int frame_count = 0;
    int64_t total_samples = 0;

    // Process all frames
    while (av_read_frame(format_ctx_.get(), input_packet_.get()) >= 0) {
      ffmpeg::ScopedPacketUnref packet_guard(input_packet_.get());

      if (input_packet_->stream_index != audio_stream_index_) {
        continue;
      }

      const auto ret =
          avcodec_send_packet(input_codec_ctx_.get(), input_packet_.get());
      if (ret < 0) {
        continue;
      }

      while (true) {
        const auto recv_ret =
            avcodec_receive_frame(input_codec_ctx_.get(), input_frame_.get());

        if (recv_ret == AVERROR(EAGAIN) || recv_ret == AVERROR_EOF) {
          break;
        }

        if (recv_ret < 0) {
          std::cerr << "Error during decoding\n";
          break;
        }

        ffmpeg::ScopedFrameUnref frame_guard(input_frame_.get());

        // Resample if needed
        encode_frame();

        ++frame_count;
        total_samples += input_frame_->nb_samples;

        if (frame_count % 100 == 0) {
          const auto seconds =
              total_samples /
              static_cast<double>(input_codec_ctx_->sample_rate);
          std::cout << std::format("Processed {:.2f} seconds\r", seconds)
                    << std::flush;
        }
      }
    }

    // Flush encoder
    flush_encoder();

    // Write trailer
    ffmpeg::check_error(av_write_trailer(output_format_ctx_.get()),
                        "write trailer");

    const auto duration =
        total_samples / static_cast<double>(input_codec_ctx_->sample_rate);
    std::cout << std::format("\n\n✓ Conversion completed successfully\n");
    std::cout << std::format("Processed {} frames ({:.2f} seconds)\n",
                             frame_count, duration);
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

    // Open input decoder
    const auto *input_codecpar =
        format_ctx_->streams[audio_stream_index_]->codecpar;
    input_codec_ = avcodec_find_decoder(input_codecpar->codec_id);
    if (!input_codec_) {
      throw ffmpeg::FFmpegError("Input decoder not found");
    }

    input_codec_ctx_ = ffmpeg::create_codec_context(input_codec_);
    ffmpeg::check_error(
        avcodec_parameters_to_context(input_codec_ctx_.get(), input_codecpar),
        "copy input codec parameters");
    ffmpeg::check_error(
        avcodec_open2(input_codec_ctx_.get(), input_codec_, nullptr),
        "open input decoder");

    // Determine output codec
    const auto codec_info = get_codec_info(output_file_.string());

    output_codec_ = avcodec_find_encoder_by_name(codec_info.name);
    if (!output_codec_) {
      output_codec_ = avcodec_find_encoder(codec_info.codec_id);
    }
    if (!output_codec_) {
      throw ffmpeg::FFmpegError(
          std::format("Output codec '{}' not found", codec_info.name));
    }

    // Create output format context
    AVFormatContext *output_ctx_raw = nullptr;
    ffmpeg::check_error(avformat_alloc_output_context2(&output_ctx_raw, nullptr,
                                                       nullptr,
                                                       output_file_.c_str()),
                        "allocate output context");
    output_format_ctx_.reset(output_ctx_raw);

    // Create output stream
    output_stream_ = avformat_new_stream(output_format_ctx_.get(), nullptr);
    if (!output_stream_) {
      throw ffmpeg::FFmpegError("Failed to create output stream");
    }

    // Configure output codec
    output_codec_ctx_ = ffmpeg::create_codec_context(output_codec_);

    output_codec_ctx_->codec_id = codec_info.codec_id;
    output_codec_ctx_->codec_type = AVMEDIA_TYPE_AUDIO;

    // Set sample rate
    if (target_sample_rate_ > 0) {
      output_codec_ctx_->sample_rate = target_sample_rate_;
    } else {
      output_codec_ctx_->sample_rate = codec_info.default_sample_rate;
    }

    // Set channels
    if (target_channels_ > 0) {
      av_channel_layout_default(&output_codec_ctx_->ch_layout,
                                target_channels_);
    } else {
      av_channel_layout_copy(&output_codec_ctx_->ch_layout,
                             &input_codec_ctx_->ch_layout);
    }

    // Set bitrate
    if (target_bitrate_ > 0) {
      output_codec_ctx_->bit_rate = target_bitrate_;
    } else if (codec_info.default_bitrate > 0) {
      output_codec_ctx_->bit_rate = codec_info.default_bitrate;
    }

// Select sample format
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    output_codec_ctx_->sample_fmt = output_codec_->sample_fmts
                                        ? output_codec_->sample_fmts[0]
                                        : AV_SAMPLE_FMT_FLTP;
#pragma clang diagnostic pop

    output_codec_ctx_->time_base =
        AVRational{1, output_codec_ctx_->sample_rate};

    // Global headers
    if (output_format_ctx_->oformat->flags & AVFMT_GLOBALHEADER) {
      output_codec_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    // Open output encoder
    ffmpeg::check_error(
        avcodec_open2(output_codec_ctx_.get(), output_codec_, nullptr),
        "open output encoder");

    // Copy codec parameters
    ffmpeg::check_error(avcodec_parameters_from_context(
                            output_stream_->codecpar, output_codec_ctx_.get()),
                        "copy output codec parameters");

    output_stream_->time_base = output_codec_ctx_->time_base;

    // Open output file
    if (!(output_format_ctx_->oformat->flags & AVFMT_NOFILE)) {
      ffmpeg::check_error(avio_open(&output_format_ctx_->pb,
                                    output_file_.c_str(), AVIO_FLAG_WRITE),
                          "open output file");
    }

    // Write header
    ffmpeg::check_error(
        avformat_write_header(output_format_ctx_.get(), nullptr),
        "write header");

    // Setup resampler
    ffmpeg::check_error(
        swr_alloc_set_opts2(
            &swr_ctx_raw_, &output_codec_ctx_->ch_layout,
            output_codec_ctx_->sample_fmt, output_codec_ctx_->sample_rate,
            &input_codec_ctx_->ch_layout, input_codec_ctx_->sample_fmt,
            input_codec_ctx_->sample_rate, 0, nullptr),
        "allocate resampler");

    swr_ctx_.reset(swr_ctx_raw_);
    swr_ctx_raw_ = nullptr;

    ffmpeg::check_error(swr_init(swr_ctx_.get()), "initialize resampler");
  }

  void encode_frame() {
    // Calculate output samples
    const auto delay =
        swr_get_delay(swr_ctx_.get(), input_codec_ctx_->sample_rate);
    const auto dst_nb_samples = av_rescale_rnd(
        delay + input_frame_->nb_samples, output_codec_ctx_->sample_rate,
        input_codec_ctx_->sample_rate, AV_ROUND_UP);

    // Allocate output frame if needed
    if (!output_frame_->data[0] ||
        output_frame_->nb_samples != dst_nb_samples) {
      av_frame_unref(output_frame_.get());

      output_frame_->format = output_codec_ctx_->sample_fmt;
      output_frame_->ch_layout = output_codec_ctx_->ch_layout;
      output_frame_->sample_rate = output_codec_ctx_->sample_rate;
      output_frame_->nb_samples = dst_nb_samples;

      ffmpeg::check_error(av_frame_get_buffer(output_frame_.get(), 0),
                          "allocate output frame buffer");
    }

    // Resample
    const auto ret =
        swr_convert(swr_ctx_.get(), output_frame_->data, dst_nb_samples,
                    const_cast<const uint8_t **>(input_frame_->data),
                    input_frame_->nb_samples);

    if (ret < 0) {
      return;
    }

    output_frame_->nb_samples = ret;
    output_frame_->pts = av_rescale_q(
        samples_count_, AVRational{1, input_codec_ctx_->sample_rate},
        output_codec_ctx_->time_base);
    samples_count_ += input_frame_->nb_samples;

    // Encode
    ffmpeg::check_error(
        avcodec_send_frame(output_codec_ctx_.get(), output_frame_.get()),
        "send frame to encoder");

    write_packets();
  }

  void flush_encoder() {
    avcodec_send_frame(output_codec_ctx_.get(), nullptr);
    write_packets();
  }

  void write_packets() {
    auto packet = ffmpeg::create_packet();

    while (true) {
      const auto ret =
          avcodec_receive_packet(output_codec_ctx_.get(), packet.get());

      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        break;
      }

      if (ret < 0) {
        throw ffmpeg::FFmpegError(ret);
      }

      ffmpeg::ScopedPacketUnref packet_guard(packet.get());

      av_packet_rescale_ts(packet.get(), output_codec_ctx_->time_base,
                           output_stream_->time_base);
      packet->stream_index = output_stream_->index;

      ffmpeg::check_error(
          av_interleaved_write_frame(output_format_ctx_.get(), packet.get()),
          "write packet");
    }
  }

  fs::path output_file_;
  int target_bitrate_;
  int target_sample_rate_;
  int target_channels_;
  int audio_stream_index_ = -1;
  int64_t samples_count_ = 0;

  ffmpeg::FormatContextPtr format_ctx_;
  ffmpeg::FormatContextPtr output_format_ctx_;
  ffmpeg::CodecContextPtr input_codec_ctx_;
  ffmpeg::CodecContextPtr output_codec_ctx_;
  ffmpeg::PacketPtr input_packet_;
  ffmpeg::FramePtr input_frame_;
  ffmpeg::FramePtr output_frame_;
  ffmpeg::SwrContextPtr swr_ctx_;
  SwrContext *swr_ctx_raw_ = nullptr;

  const AVCodec *input_codec_ = nullptr;
  const AVCodec *output_codec_ = nullptr;
  AVStream *output_stream_ = nullptr;
};

void print_usage(std::string_view prog_name) {
  std::cout << std::format("Usage: {} <input_file> <output_file> [options]\n\n",
                           prog_name);
  std::cout << "Options:\n";
  std::cout << "  -b <bitrate>      Output bitrate in bps (e.g., 192000 for "
               "192kbps)\n";
  std::cout << "  -r <sample_rate>  Output sample rate in Hz (e.g., 48000)\n";
  std::cout << "  -c <channels>     Output channels (1=mono, 2=stereo)\n\n";
  std::cout << "Supported formats:\n";
  std::cout << "  .mp3, .aac, .m4a, .ogg, .opus, .flac, .wav, .wma\n\n";
  std::cout << "Examples:\n";
  std::cout << std::format("  {} input.flac output.mp3\n", prog_name);
  std::cout << std::format("  {} input.wav output.aac -b 256000\n", prog_name);
  std::cout << std::format("  {} input.mp3 output.opus -r 48000 -c 2\n",
                           prog_name);
  std::cout << std::format("  {} music.flac music.mp3 -b 320000 -r 44100\n",
                           prog_name);
}

} // anonymous namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    print_usage(argv[0]);
    return 1;
  }

  try {
    const std::string_view input_file{argv[1]};
    const fs::path output_file{argv[2]};

    int bitrate = 0;
    int sample_rate = 0;
    int channels = 0;

    // Parse options
    for (int i = 3; i < argc; i += 2) {
      if (i + 1 >= argc)
        break;

      const std::string_view option{argv[i]};
      const std::string_view value{argv[i + 1]};

      if (option == "-b") {
        bitrate = std::atoi(value.data());
      } else if (option == "-r") {
        sample_rate = std::atoi(value.data());
      } else if (option == "-c") {
        channels = std::atoi(value.data());
      }
    }

    AudioFormatConverter converter(input_file, output_file, bitrate,
                                   sample_rate, channels);
    converter.convert();

  } catch (const ffmpeg::FFmpegError &e) {
    std::cerr << std::format("FFmpeg error: {}\n", e.what());
    return 1;
  } catch (const std::exception &e) {
    std::cerr << std::format("Error: {}\n", e.what());
    return 1;
  }

  return 0;
}
