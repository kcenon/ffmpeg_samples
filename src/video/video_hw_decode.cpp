/**
 * Hardware Accelerated Video Decoder
 *
 * This sample demonstrates how to decode video frames using hardware
 * acceleration (e.g., VideoToolbox on macOS, NVDEC on Linux/Windows) and save
 * them as PPM images.
 */

#include "ffmpeg_wrappers.hpp"

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

namespace {

void save_frame_as_ppm(const AVFrame &frame, int width, int height,
                       int frame_number, const fs::path &output_dir) {
  const auto filename =
      output_dir / std::format("frame_hw_{}.ppm", frame_number);

  std::ofstream file(filename, std::ios::binary);
  if (!file) {
    throw std::runtime_error(
        std::format("Failed to open output file: {}", filename.string()));
  }

  // Write PPM header
  file << std::format("P6\n{} {}\n255\n", width, height);

  // Write pixel data
  for (int y = 0; y < height; ++y) {
    file.write(
        reinterpret_cast<const char *>(frame.data[0] + y * frame.linesize[0]),
        width * 3);
  }

  std::cout << std::format("Saved frame {} to {}\n", frame_number,
                           filename.string());
}

class VideoHWDecoder {
public:
  VideoHWDecoder(std::string_view input_file, const fs::path &output_dir,
                 std::string_view hw_device_type)
      : output_dir_(output_dir),
        format_ctx_(ffmpeg::open_input_format(input_file.data())),
        packet_(ffmpeg::create_packet()), frame_(ffmpeg::create_frame()),
        sw_frame_(ffmpeg::create_frame()), frame_rgb_(ffmpeg::create_frame()) {

    // Find HW device type
    hw_type_ = av_hwdevice_find_type_by_name(hw_device_type.data());
    if (hw_type_ == AV_HWDEVICE_TYPE_NONE) {
      // List available devices
      std::string available;
      enum AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;
      while ((type = av_hwdevice_iterate_types(type)) !=
             AV_HWDEVICE_TYPE_NONE) {
        if (!available.empty())
          available += ", ";
        available += av_hwdevice_get_type_name(type);
      }
      throw ffmpeg::FFmpegError(std::format(
          "Device type '{}' is not supported. Available devices: {}",
          hw_device_type, available));
    }

    initialize();
  }

  void decode() {
    std::cout << std::format("Decoding video from {}\n", format_ctx_->url);
    std::cout << std::format("Using hardware device: {}\n",
                             av_hwdevice_get_type_name(hw_type_));

    int frame_count = 0;

    while (av_read_frame(format_ctx_.get(), packet_.get()) >= 0) {
      ffmpeg::ScopedPacketUnref packet_guard(packet_.get());

      if (packet_->stream_index != video_stream_index_) {
        continue;
      }

      if (const auto ret = avcodec_send_packet(codec_ctx_.get(), packet_.get());
          ret < 0) {
        std::cerr << "Error sending packet to decoder\n";
        break;
      }

      while (true) {
        const auto ret = avcodec_receive_frame(codec_ctx_.get(), frame_.get());

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
          break;
        }

        if (ret < 0) {
          std::cerr << "Error during decoding\n";
          return;
        }

        ffmpeg::ScopedFrameUnref frame_guard(frame_.get());

        // Check if frame is hardware accelerated
        AVFrame *final_frame = frame_.get();
        ffmpeg::ScopedFrameUnref sw_frame_guard(nullptr);

        if (frame_->format == hw_pix_fmt_) {
          // Retrieve data from GPU to CPU
          if (av_hwframe_transfer_data(sw_frame_.get(), frame_.get(), 0) < 0) {
            std::cerr << "Error transferring data to system memory\n";
            continue;
          }
          final_frame = sw_frame_.get();
          // Manually manage sw_frame unref since we reused the object
          // In a real loop we might want new objects or proper reset
          // Here we use a guard that we manually attach? No, ScopedFrameUnref
          // takes pointer. Let's just manually unref at end of scope or use a
          // temporary guard. Actually, sw_frame_ is a unique_ptr, so we
          // shouldn't unref it manually if we want to reuse it? Wait,
          // av_hwframe_transfer_data allocates buffers in sw_frame. We need to
          // unref it after processing.
        }

        // Initialize scaler if needed (only once or on format change)
        if (!sws_ctx_ || final_frame->width != last_width_ ||
            final_frame->height != last_height_ ||
            final_frame->format != last_format_) {

          last_width_ = final_frame->width;
          last_height_ = final_frame->height;
          last_format_ = final_frame->format;

          sws_ctx_.reset(
              sws_getContext(last_width_, last_height_,
                             static_cast<AVPixelFormat>(last_format_),
                             last_width_, last_height_, AV_PIX_FMT_RGB24,
                             SWS_BILINEAR, nullptr, nullptr, nullptr));

          // Re-allocate RGB frame
          frame_rgb_->format = AV_PIX_FMT_RGB24;
          frame_rgb_->width = last_width_;
          frame_rgb_->height = last_height_;
          av_frame_get_buffer(frame_rgb_.get(), 0);
        }

        // Convert to RGB
        sws_scale(sws_ctx_.get(), final_frame->data, final_frame->linesize, 0,
                  final_frame->height, frame_rgb_->data, frame_rgb_->linesize);

        // Save frame
        save_frame_as_ppm(*frame_rgb_, final_frame->width, final_frame->height,
                          frame_count++, output_dir_);

        // Clean up SW frame if used
        if (final_frame == sw_frame_.get()) {
          av_frame_unref(sw_frame_.get());
        }

        if (frame_count >= 10) { // Limit to 10 frames for demo
          std::cout << "Decoded 10 frames, stopping.\n";
          return;
        }
      }
    }
  }

private:
  static enum AVPixelFormat get_hw_format(AVCodecContext *ctx,
                                          const enum AVPixelFormat *pix_fmts) {
    const enum AVPixelFormat *p;
    auto *self = static_cast<VideoHWDecoder *>(ctx->opaque);

    for (p = pix_fmts; *p != -1; p++) {
      if (*p == self->hw_pix_fmt_) {
        return *p;
      }
    }

    std::cerr << "Failed to get HW surface format.\n";
    return AV_PIX_FMT_NONE;
  }

  void initialize() {
    // Find video stream
    const auto stream_idx =
        ffmpeg::find_stream_index(format_ctx_.get(), AVMEDIA_TYPE_VIDEO);
    if (!stream_idx) {
      throw ffmpeg::FFmpegError("No video stream found");
    }
    video_stream_index_ = *stream_idx;

    // Find decoder
    const auto *codecpar = format_ctx_->streams[video_stream_index_]->codecpar;
    const auto *decoder = avcodec_find_decoder(codecpar->codec_id);
    if (!decoder) {
      throw ffmpeg::FFmpegError("Decoder not found");
    }

    // Search for HW configuration
    bool hw_config_found = false;
    for (int i = 0;; i++) {
      const auto *config = avcodec_get_hw_config(decoder, i);
      if (!config)
        break;

      if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
          config->device_type == hw_type_) {
        hw_pix_fmt_ = config->pix_fmt;
        hw_config_found = true;
        break;
      }
    }

    if (!hw_config_found) {
      throw ffmpeg::FFmpegError(
          std::format("Decoder {} does not support device type {}.",
                      decoder->name, av_hwdevice_get_type_name(hw_type_)));
    }

    // Create codec context
    codec_ctx_ = ffmpeg::create_codec_context(decoder);
    ffmpeg::check_error(
        avcodec_parameters_to_context(codec_ctx_.get(), codecpar),
        "copy codec parameters");

    codec_ctx_->opaque = this;
    codec_ctx_->get_format = get_hw_format;

    // Create HW device context
    AVBufferRef *hw_device_ctx = nullptr;
    ffmpeg::check_error(
        av_hwdevice_ctx_create(&hw_device_ctx, hw_type_, nullptr, nullptr, 0),
        "create hw device context");
    hw_device_ctx_.reset(hw_device_ctx);
    codec_ctx_->hw_device_ctx = av_buffer_ref(hw_device_ctx_.get());

    // Open codec
    ffmpeg::check_error(avcodec_open2(codec_ctx_.get(), decoder, nullptr),
                        "open codec");
  }

  fs::path output_dir_;
  enum AVHWDeviceType hw_type_;
  enum AVPixelFormat hw_pix_fmt_ = AV_PIX_FMT_NONE;
  int video_stream_index_ = -1;

  // State for scaler
  int last_width_ = -1;
  int last_height_ = -1;
  int last_format_ = -1;

  ffmpeg::FormatContextPtr format_ctx_;
  ffmpeg::CodecContextPtr codec_ctx_;
  ffmpeg::BufferRefPtr hw_device_ctx_;
  ffmpeg::PacketPtr packet_;
  ffmpeg::FramePtr frame_;
  ffmpeg::FramePtr sw_frame_;
  ffmpeg::FramePtr frame_rgb_;
  ffmpeg::SwsContextPtr sws_ctx_;
};

} // anonymous namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::cerr << std::format(
        "Usage: {} <input_file> <output_dir> [device_type]\n", argv[0]);
    std::cerr << "Common device types: videotoolbox (macOS), cuda (NVIDIA), "
                 "vaapi (Intel/AMD), dxva2 (Windows)\n";
    return 1;
  }

  try {
    const std::string_view input_filename{argv[1]};
    const fs::path output_dir{argv[2]};
    const std::string_view device_type = argc > 3 ? argv[3] : "videotoolbox";

    fs::create_directories(output_dir);

    VideoHWDecoder decoder(input_filename, output_dir, device_type);
    decoder.decode();

  } catch (const ffmpeg::FFmpegError &e) {
    std::cerr << std::format("FFmpeg error: {}\n", e.what());
    return 1;
  } catch (const std::exception &e) {
    std::cerr << std::format("Error: {}\n", e.what());
    return 1;
  }

  return 0;
}
