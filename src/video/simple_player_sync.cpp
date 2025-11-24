/**
 * Simple Player Sync
 *
 * This sample demonstrates the core logic of Audio/Video synchronization.
 * It decodes both audio and video streams and synchronizes video to the audio
 * clock (Master Clock).
 *
 * Note: This sample simulates rendering by printing to the console to avoid
 * dependencies on external graphics libraries like SDL or GLFW.
 */

#include "ffmpeg_wrappers.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <format>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

namespace {

// Thread-safe queue for frames
template <typename T> class SafeQueue {
public:
  void push(T value) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(std::move(value));
    cond_.notify_one();
  }

  bool pop(T &value) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (queue_.empty())
      return false;
    value = std::move(queue_.front());
    queue_.pop();
    return true;
  }

  bool wait_and_pop(T &value) {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this] { return !queue_.empty() || stop_; });
    if (queue_.empty())
      return false;
    value = std::move(queue_.front());
    queue_.pop();
    return true;
  }

  void stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
    cond_.notify_all();
  }

  size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

private:
  std::queue<T> queue_;
  mutable std::mutex mutex_;
  std::condition_variable cond_;
  bool stop_ = false;
};

struct DecodedFrame {
  ffmpeg::FramePtr frame;
  double pts; // Presentation timestamp in seconds
};

class Player {
public:
  Player(std::string_view input_file)
      : format_ctx_(ffmpeg::open_input_format(input_file.data())) {

    initialize();
  }

  void play() {
    std::cout << "Starting playback simulation...\n";

    start_time_ = std::chrono::steady_clock::now();

    std::thread video_thread(&Player::video_thread_func, this);
    std::thread audio_thread(&Player::audio_thread_func, this);
    std::thread demuxer_thread(&Player::demuxer_thread_func, this);

    if (video_thread.joinable())
      video_thread.join();
    if (audio_thread.joinable())
      audio_thread.join();
    if (demuxer_thread.joinable())
      demuxer_thread.join();

    std::cout << "Playback finished.\n";
  }

private:
  void initialize() {
    // Find streams
    if (auto idx =
            ffmpeg::find_stream_index(format_ctx_.get(), AVMEDIA_TYPE_VIDEO)) {
      video_stream_idx_ = *idx;
      open_codec_context(video_stream_idx_, video_codec_ctx_);
    }

    if (auto idx =
            ffmpeg::find_stream_index(format_ctx_.get(), AVMEDIA_TYPE_AUDIO)) {
      audio_stream_idx_ = *idx;
      open_codec_context(audio_stream_idx_, audio_codec_ctx_);
    }

    if (video_stream_idx_ == -1 && audio_stream_idx_ == -1) {
      throw std::runtime_error("No audio or video streams found");
    }
  }

  void open_codec_context(int stream_idx, ffmpeg::CodecContextPtr &ctx) {
    auto *par = format_ctx_->streams[stream_idx]->codecpar;
    auto *decoder = avcodec_find_decoder(par->codec_id);
    if (!decoder)
      return;

    ctx = ffmpeg::create_codec_context(decoder);
    avcodec_parameters_to_context(ctx.get(), par);
    avcodec_open2(ctx.get(), decoder, nullptr);
  }

  void demuxer_thread_func() {
    auto packet = ffmpeg::create_packet();

    while (!stop_threads_) {
      if (video_queue_.size() > 10 || audio_queue_.size() > 20) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }

      if (av_read_frame(format_ctx_.get(), packet.get()) < 0) {
        stop_threads_ = true;
        video_queue_.stop();
        audio_queue_.stop();
        break;
      }

      // Duplicate packet because queue takes ownership
      auto packet_copy = ffmpeg::create_packet();
      av_packet_ref(packet_copy.get(), packet.get());
      ffmpeg::ScopedPacketUnref packet_guard(packet.get());

      if (packet_copy->stream_index == video_stream_idx_) {
        video_queue_.push(std::move(packet_copy));
      } else if (packet_copy->stream_index == audio_stream_idx_) {
        audio_queue_.push(std::move(packet_copy));
      }
    }
  }

  void video_thread_func() {
    ffmpeg::PacketPtr packet;
    auto frame = ffmpeg::create_frame();

    while (video_queue_.wait_and_pop(packet)) {
      ffmpeg::ScopedPacketUnref packet_guard(packet.get());
      avcodec_send_packet(video_codec_ctx_.get(), packet.get());

      while (true) {
        int ret = avcodec_receive_frame(video_codec_ctx_.get(), frame.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
          break;
        if (ret < 0)
          break;

        double pts = 0;
        if (frame->pts != AV_NOPTS_VALUE) {
          pts = frame->pts *
                av_q2d(format_ctx_->streams[video_stream_idx_]->time_base);
        } else {
          frame->pts = frame->best_effort_timestamp;
          pts = frame->pts *
                av_q2d(format_ctx_->streams[video_stream_idx_]->time_base);
        }

        synchronize_video(pts);

        // "Render"
        std::cout << std::format(
            "Rendered Video Frame: PTS={:.3f} (Clock={:.3f})\n", pts,
            get_master_clock());

        ffmpeg::ScopedFrameUnref frame_guard(frame.get());
      }
    }
  }

  void audio_thread_func() {
    ffmpeg::PacketPtr packet;
    auto frame = ffmpeg::create_frame();

    while (audio_queue_.wait_and_pop(packet)) {
      ffmpeg::ScopedPacketUnref packet_guard(packet.get());
      avcodec_send_packet(audio_codec_ctx_.get(), packet.get());

      while (true) {
        int ret = avcodec_receive_frame(audio_codec_ctx_.get(), frame.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
          break;
        if (ret < 0)
          break;

        double pts = 0;
        if (frame->pts != AV_NOPTS_VALUE) {
          pts = frame->pts *
                av_q2d(format_ctx_->streams[audio_stream_idx_]->time_base);
        }

        // Update master clock
        set_master_clock(pts);

        // Simulate audio playback duration
        double duration =
            frame->nb_samples / static_cast<double>(frame->sample_rate);
        std::this_thread::sleep_for(std::chrono::duration<double>(duration));

        ffmpeg::ScopedFrameUnref frame_guard(frame.get());
      }
    }
  }

  void synchronize_video(double pts) {
    double diff = pts - get_master_clock();

    // If video is ahead, wait
    if (diff > 0) {
      std::this_thread::sleep_for(std::chrono::duration<double>(diff));
    }
    // If video is behind, we should drop frame (skip rendering), but here we
    // just print warning
    else if (diff < -0.1) {
      std::cout << "Video lagging behind!\n";
    }
  }

  double get_master_clock() {
    // In a real player, we'd interpolate based on last PTS + time elapsed
    return audio_clock_.load();
  }

  void set_master_clock(double pts) { audio_clock_.store(pts); }

  ffmpeg::FormatContextPtr format_ctx_;
  ffmpeg::CodecContextPtr video_codec_ctx_;
  ffmpeg::CodecContextPtr audio_codec_ctx_;

  int video_stream_idx_ = -1;
  int audio_stream_idx_ = -1;

  SafeQueue<ffmpeg::PacketPtr> video_queue_;
  SafeQueue<ffmpeg::PacketPtr> audio_queue_;

  std::atomic<bool> stop_threads_{false};
  std::atomic<double> audio_clock_{0.0};
  std::chrono::steady_clock::time_point start_time_;
};

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << std::format("Usage: {} <input_file>\n", argv[0]);
    return 1;
  }

  try {
    Player player(argv[1]);
    player.play();
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
