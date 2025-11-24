/**
 * Hardware Accelerated Video Encoder
 *
 * This sample demonstrates how to encode video frames using hardware
 * acceleration (e.g., h264_videotoolbox on macOS, h264_nvenc on NVIDIA) using
 * modern C++20.
 */

#include "ffmpeg_wrappers.hpp"

#include <format>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

class VideoHWEncoder {
public:
  VideoHWEncoder(std::string_view input_file, std::string_view output_file,
                 std::string_view encoder_name, std::string_view hw_device_type)
      : output_file_(output_file), encoder_name_(encoder_name),
        input_format_ctx_(ffmpeg::open_input_format(input_file.data())),
        input_packet_(ffmpeg::create_packet()),
        input_frame_(ffmpeg::create_frame()), sw_frame_(ffmpeg::create_frame()),
        hw_frame_(ffmpeg::create_frame()) {

    // Find HW device type
    if (!hw_device_type.empty()) {
      hw_type_ = av_hwdevice_find_type_by_name(hw_device_type.data());
      if (hw_type_ == AV_HWDEVICE_TYPE_NONE) {
        std::cerr << std::format(
            "Warning: Device type '{}' not found. Trying to proceed without "
            "explicit device context.\n",
            hw_device_type);
      }
    }

    initialize();
  }

  void encode() {
    std::cout << "Encoding video...\n";
    std::cout << std::format("Input: {}\n", input_format_ctx_->url);
    std::cout << std::format("Output: {}\n", output_file_);
    std::cout << std::format("Encoder: {}\n", encoder_name_);

    int64_t pts_counter = 0;
    int frame_count = 0;

    while (av_read_frame(input_format_ctx_.get(), input_packet_.get()) >= 0) {
      ffmpeg::ScopedPacketUnref packet_guard(input_packet_.get());

      if (input_packet_->stream_index != video_stream_index_) {
        continue;
      }

      if (const auto ret =
              avcodec_send_packet(input_codec_ctx_.get(), input_packet_.get());
          ret < 0) {
        continue;
      }

      while (true) {
        const auto ret =
            avcodec_receive_frame(input_codec_ctx_.get(), input_frame_.get());

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
          break;
        }

        if (ret < 0) {
          std::cerr << "Error decoding video frame\n";
          break;
        }

        ffmpeg::ScopedFrameUnref frame_guard(input_frame_.get());

        // Upload to HW frame if needed
        AVFrame *frame_to_encode = input_frame_.get();
        ffmpeg::ScopedFrameUnref hw_frame_guard(nullptr);

        if (hw_device_ctx_) {
          if (av_hwframe_get_buffer(output_codec_ctx_->hw_frames_ctx,
                                    hw_frame_.get(), 0) < 0) {
            std::cerr << "Error allocating HW frame\n";
            break;
          }

          // Copy data to HW frame
          // Note: This is a simplified upload. Real usage might need sws_scale
          // if formats differ. For VideoToolbox, we often need to transfer
          // data.
          if (av_hwframe_transfer_data(hw_frame_.get(), input_frame_.get(), 0) <
              0) {
            std::cerr << "Error transferring data to GPU\n";
            av_frame_unref(hw_frame_.get());
            break;
          }

          hw_frame_->pts = input_frame_->pts;
          frame_to_encode = hw_frame_.get();
          // We need to unref hw_frame_ after encoding
        }

        frame_to_encode->pts = pts_counter++;
        encode_frame(frame_to_encode);

        if (frame_to_encode == hw_frame_.get()) {
          av_frame_unref(hw_frame_.get());
        }

        ++frame_count;
        if (frame_count % 30 == 0) {
          std::cout << std::format("Processed {} frames\r", frame_count)
                    << std::flush;
        }
      }
    }

    flush_encoder();

    ffmpeg::check_error(av_write_trailer(output_format_ctx_.get()),
                        "write trailer");

    std::cout << std::format("\nTotal frames encoded: {}\n", frame_count);
  }

private:
  void initialize() {
    // Input setup
    const auto stream_idx =
        ffmpeg::find_stream_index(input_format_ctx_.get(), AVMEDIA_TYPE_VIDEO);
    if (!stream_idx) {
      throw ffmpeg::FFmpegError("No video stream found");
    }
    video_stream_index_ = *stream_idx;

    const auto *in_codecpar =
        input_format_ctx_->streams[video_stream_index_]->codecpar;
    const auto *decoder = avcodec_find_decoder(in_codecpar->codec_id);
    if (!decoder)
      throw ffmpeg::FFmpegError("Decoder not found");

    input_codec_ctx_ = ffmpeg::create_codec_context(decoder);
    ffmpeg::check_error(
        avcodec_parameters_to_context(input_codec_ctx_.get(), in_codecpar),
        "copy decoder params");
    ffmpeg::check_error(avcodec_open2(input_codec_ctx_.get(), decoder, nullptr),
                        "open decoder");

    // Output setup
    AVFormatContext *raw_out_ctx = nullptr;
    ffmpeg::check_error(avformat_alloc_output_context2(&raw_out_ctx, nullptr,
                                                       nullptr,
                                                       output_file_.c_str()),
                        "alloc output context");
    output_format_ctx_.reset(raw_out_ctx);

    const auto *encoder = avcodec_find_encoder_by_name(encoder_name_.c_str());
    if (!encoder) {
      throw ffmpeg::FFmpegError(
          std::format("Encoder '{}' not found", encoder_name_));
    }

    output_stream_ = avformat_new_stream(output_format_ctx_.get(), nullptr);
    if (!output_stream_)
      throw ffmpeg::FFmpegError("Failed to create output stream");

    output_codec_ctx_ = ffmpeg::create_codec_context(encoder);

    // Configure encoder
    output_codec_ctx_->width = input_codec_ctx_->width;
    output_codec_ctx_->height = input_codec_ctx_->height;
    output_codec_ctx_->time_base = AVRational{1, 30};
    output_codec_ctx_->framerate = AVRational{30, 1};
    output_codec_ctx_->pix_fmt =
        AV_PIX_FMT_YUV420P; // Default, might need change for HW

    if (hw_type_ != AV_HWDEVICE_TYPE_NONE) {
      // Create HW device context
      AVBufferRef *hw_device_ctx = nullptr;
      ffmpeg::check_error(
          av_hwdevice_ctx_create(&hw_device_ctx, hw_type_, nullptr, nullptr, 0),
          "create hw device context");
      hw_device_ctx_.reset(hw_device_ctx);

      // Set HW frames context if needed (complex, skipping for basic sample
      // unless strictly required) For many HW encoders, just setting
      // hw_device_ctx and correct pix_fmt is enough. But often we need
      // hw_frames_ctx for zero-copy.

      // Find supported HW pixel format
      for (int i = 0;; i++) {
        const auto *config = avcodec_get_hw_config(encoder, i);
        if (!config)
          break;
        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            config->device_type == hw_type_) {
          output_codec_ctx_->pix_fmt = config->pix_fmt;
          break;
        }
      }

      output_codec_ctx_->hw_device_ctx = av_buffer_ref(hw_device_ctx_.get());

      // Create HW frames context
      AVBufferRef *hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx_.get());
      if (!hw_frames_ref)
        throw ffmpeg::FFmpegError("Failed to alloc hw frames ctx");

      auto *frames_ctx =
          reinterpret_cast<AVHWFramesContext *>(hw_frames_ref->data);
      frames_ctx->format = output_codec_ctx_->pix_fmt;
      frames_ctx->sw_format = AV_PIX_FMT_NV12; // Common for HW
      frames_ctx->width = output_codec_ctx_->width;
      frames_ctx->height = output_codec_ctx_->height;
      frames_ctx->initial_pool_size = 20;

      ffmpeg::check_error(av_hwframe_ctx_init(hw_frames_ref),
                          "init hw frames ctx");
      output_codec_ctx_->hw_frames_ctx =
          hw_frames_ref; // Takes ownership? No, ref counted.
                         // Actually avcodec_free_context will unref it.
    }

    output_codec_ctx_->bit_rate = 2000000;

    if (output_format_ctx_->oformat->flags & AVFMT_GLOBALHEADER) {
      output_codec_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    ffmpeg::check_error(
        avcodec_open2(output_codec_ctx_.get(), encoder, nullptr),
        "open encoder");
    ffmpeg::check_error(avcodec_parameters_from_context(
                            output_stream_->codecpar, output_codec_ctx_.get()),
                        "copy enc params");
    output_stream_->time_base = output_codec_ctx_->time_base;

    if (!(output_format_ctx_->oformat->flags & AVFMT_NOFILE)) {
      ffmpeg::check_error(avio_open(&output_format_ctx_->pb,
                                    output_file_.c_str(), AVIO_FLAG_WRITE),
                          "open output file");
    }
    ffmpeg::check_error(
        avformat_write_header(output_format_ctx_.get(), nullptr),
        "write header");
  }

  void encode_frame(AVFrame *frame) {
    auto packet = ffmpeg::create_packet();

    int ret = avcodec_send_frame(output_codec_ctx_.get(), frame);
    if (ret < 0)
      return;

    while (true) {
      ret = avcodec_receive_packet(output_codec_ctx_.get(), packet.get());
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        break;
      if (ret < 0)
        break;

      ffmpeg::ScopedPacketUnref packet_guard(packet.get());
      av_packet_rescale_ts(packet.get(), output_codec_ctx_->time_base,
                           output_stream_->time_base);
      packet->stream_index = 0;
      av_interleaved_write_frame(output_format_ctx_.get(), packet.get());
    }
  }

  void flush_encoder() { encode_frame(nullptr); }

  std::string output_file_;
  std::string encoder_name_;
  int video_stream_index_ = -1;
  enum AVHWDeviceType hw_type_ = AV_HWDEVICE_TYPE_NONE;

  ffmpeg::FormatContextPtr input_format_ctx_;
  ffmpeg::FormatContextPtr output_format_ctx_;
  ffmpeg::CodecContextPtr input_codec_ctx_;
  ffmpeg::CodecContextPtr output_codec_ctx_;
  ffmpeg::BufferRefPtr hw_device_ctx_;
  ffmpeg::PacketPtr input_packet_;
  ffmpeg::FramePtr input_frame_;
  ffmpeg::FramePtr sw_frame_;
  ffmpeg::FramePtr hw_frame_;
  AVStream *output_stream_ = nullptr;
};

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 4) {
    std::cerr << std::format(
        "Usage: {} <input_file> <output_file> <encoder_name> [device_type]\n",
        argv[0]);
    std::cerr << "Example: ... output.mp4 h264_videotoolbox videotoolbox\n";
    return 1;
  }

  try {
    VideoHWEncoder encoder(argv[1], argv[2], argv[3], argc > 4 ? argv[4] : "");
    encoder.encode();
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
