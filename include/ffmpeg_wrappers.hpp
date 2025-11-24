/**
 * Modern C++ RAII wrappers for FFmpeg resources
 *
 * This header provides smart pointer-like wrappers for FFmpeg structures
 * to ensure proper resource management and exception safety.
 */

#pragma once

#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace ffmpeg {

// Custom deleters for FFmpeg resources
struct AVFormatContextDeleter {
  void operator()(AVFormatContext *ctx) const {
    if (ctx) {
      avformat_close_input(&ctx);
    }
  }
};

struct AVCodecContextDeleter {
  void operator()(AVCodecContext *ctx) const {
    if (ctx) {
      avcodec_free_context(&ctx);
    }
  }
};

struct AVFrameDeleter {
  void operator()(AVFrame *frame) const {
    if (frame) {
      av_frame_free(&frame);
    }
  }
};

struct AVPacketDeleter {
  void operator()(AVPacket *packet) const {
    if (packet) {
      av_packet_free(&packet);
    }
  }
};

struct SwsContextDeleter {
  void operator()(SwsContext *ctx) const {
    if (ctx) {
      sws_freeContext(ctx);
    }
  }
};

struct SwrContextDeleter {
  void operator()(SwrContext *ctx) const {
    if (ctx) {
      swr_free(&ctx);
    }
  }
};

struct AVFilterGraphDeleter {
  void operator()(AVFilterGraph *graph) const {
    if (graph) {
      avfilter_graph_free(&graph);
    }
  }
};

struct AVBufferRefDeleter {
  void operator()(AVBufferRef *ref) const {
    if (ref) {
      av_buffer_unref(&ref);
    }
  }
};

struct AVIOContextDeleter {
  void operator()(AVIOContext *ctx) const {
    if (ctx) {
      avio_closep(&ctx);
    }
  }
};

// Smart pointer type aliases
using FormatContextPtr =
    std::unique_ptr<AVFormatContext, AVFormatContextDeleter>;
using CodecContextPtr = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;
using FramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;
using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;
using SwrContextPtr = std::unique_ptr<SwrContext, SwrContextDeleter>;
using FilterGraphPtr = std::unique_ptr<AVFilterGraph, AVFilterGraphDeleter>;
using BufferRefPtr = std::unique_ptr<AVBufferRef, AVBufferRefDeleter>;

// Helper functions
inline std::string get_error_string(int error_code) {
  char errbuf[AV_ERROR_MAX_STRING_SIZE];
  av_strerror(error_code, errbuf, sizeof(errbuf));
  return std::string(errbuf);
}

// Error handling
class FFmpegError : public std::runtime_error {
public:
  explicit FFmpegError(int error_code)
      : std::runtime_error(get_error_string(error_code)),
        error_code_(error_code) {}

  explicit FFmpegError(std::string_view message)
      : std::runtime_error(std::string(message)), error_code_(0) {}

  [[nodiscard]] int error_code() const noexcept { return error_code_; }

private:
  int error_code_;
};

inline void check_error(int ret, std::string_view message) {
  if (ret < 0) {
    throw FFmpegError(
        std::format("{}: {} ({})", message, get_error_string(ret), ret));
  }
}

// Factory functions
inline FormatContextPtr open_input_format(const char *filename) {
  AVFormatContext *raw_ctx = nullptr;
  int ret = avformat_open_input(&raw_ctx, filename, nullptr, nullptr);
  if (ret < 0) {
    throw FFmpegError(ret);
  }

  ret = avformat_find_stream_info(raw_ctx, nullptr);
  if (ret < 0) {
    avformat_close_input(&raw_ctx);
    throw FFmpegError(ret);
  }

  return FormatContextPtr(raw_ctx);
}

inline FormatContextPtr create_output_format(const char *filename) {
  AVFormatContext *raw_ctx = nullptr;
  int ret =
      avformat_alloc_output_context2(&raw_ctx, nullptr, nullptr, filename);
  if (ret < 0 || !raw_ctx) {
    throw FFmpegError("Failed to create output format context");
  }
  return FormatContextPtr(raw_ctx);
}

inline CodecContextPtr create_codec_context(const AVCodec *codec) {
  AVCodecContext *raw_ctx = avcodec_alloc_context3(codec);
  if (!raw_ctx) {
    throw FFmpegError("Failed to allocate codec context");
  }
  return CodecContextPtr(raw_ctx);
}

inline FramePtr create_frame() {
  AVFrame *raw_frame = av_frame_alloc();
  if (!raw_frame) {
    throw FFmpegError("Failed to allocate frame");
  }
  return FramePtr(raw_frame);
}

inline PacketPtr create_packet() {
  AVPacket *raw_packet = av_packet_alloc();
  if (!raw_packet) {
    throw FFmpegError("Failed to allocate packet");
  }
  return PacketPtr(raw_packet);
}

// Stream finding helper
inline std::optional<int> find_stream_index(const AVFormatContext *ctx,
                                            AVMediaType type) noexcept {

  for (unsigned int i = 0; i < ctx->nb_streams; ++i) {
    if (ctx->streams[i]->codecpar->codec_type == type) {
      return static_cast<int>(i);
    }
  }
  return std::nullopt;
}

// RAII wrapper for output file context
class OutputContext {
public:
  explicit OutputContext(const char *filename)
      : format_ctx_(create_output_format(filename)), filename_(filename) {}

  ~OutputContext() {
    if (format_ctx_ && header_written_) {
      av_write_trailer(format_ctx_.get());
    }
  }

  // Delete copy operations
  OutputContext(const OutputContext &) = delete;
  OutputContext &operator=(const OutputContext &) = delete;

  // Enable move operations
  OutputContext(OutputContext &&) noexcept = default;
  OutputContext &operator=(OutputContext &&) noexcept = default;

  void open() {
    if (!(format_ctx_->oformat->flags & AVFMT_NOFILE)) {
      int ret = avio_open(&format_ctx_->pb, filename_.c_str(), AVIO_FLAG_WRITE);
      check_error(ret, "open output file");
    }
  }

  void write_header() {
    int ret = avformat_write_header(format_ctx_.get(), nullptr);
    check_error(ret, "write header");
    header_written_ = true;
  }

  AVFormatContext *get() noexcept { return format_ctx_.get(); }
  const AVFormatContext *get() const noexcept { return format_ctx_.get(); }

private:
  FormatContextPtr format_ctx_;
  std::string filename_;
  bool header_written_ = false;
};

// Scoped frame unref
class ScopedFrameUnref {
public:
  explicit ScopedFrameUnref(AVFrame *frame) : frame_(frame) {}
  ~ScopedFrameUnref() {
    if (frame_) {
      av_frame_unref(frame_);
    }
  }

  ScopedFrameUnref(const ScopedFrameUnref &) = delete;
  ScopedFrameUnref &operator=(const ScopedFrameUnref &) = delete;

private:
  AVFrame *frame_;
};

// Scoped packet unref
class ScopedPacketUnref {
public:
  explicit ScopedPacketUnref(AVPacket *packet) : packet_(packet) {}
  ~ScopedPacketUnref() {
    if (packet_) {
      av_packet_unref(packet_);
    }
  }

  ScopedPacketUnref(const ScopedPacketUnref &) = delete;
  ScopedPacketUnref &operator=(const ScopedPacketUnref &) = delete;

private:
  AVPacket *packet_;
};

} // namespace ffmpeg
