/**
 * Complex Video Filter (Picture-in-Picture)
 *
 * This sample demonstrates how to use complex filter graphs with multiple
 * inputs. It takes two input videos and overlays the second one on top of the
 * first one (PiP).
 */

#include "ffmpeg_wrappers.hpp"

#include <format>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

class ComplexFilter {
public:
  ComplexFilter(std::string_view main_input, std::string_view pip_input,
                std::string_view output_file)
      : output_file_(output_file) {

    // Open inputs
    inputs_.push_back(std::make_unique<InputContext>(main_input));
    inputs_.push_back(std::make_unique<InputContext>(pip_input));

    initialize();
  }

  void process() {
    std::cout << "Processing Picture-in-Picture...\n";

    int frame_count = 0;
    size_t finished_inputs = 0;
    int64_t pts_counter = 0;

    while (finished_inputs < inputs_.size()) {
      // Read from each input
      for (size_t i = 0; i < inputs_.size(); ++i) {
        auto &input = inputs_[i];
        if (input->finished)
          continue;

        if (av_read_frame(input->fmt_ctx.get(), input->packet.get()) < 0) {
          input->finished = true;
          finished_inputs++;
          // Flush filter
          if (av_buffersrc_add_frame(input->buffersrc_ctx, nullptr) < 0) {
            std::cerr << "Error flushing filter graph\n";
          }
          continue;
        }

        ffmpeg::ScopedPacketUnref packet_guard(input->packet.get());

        if (input->packet->stream_index == input->video_stream_idx) {
          avcodec_send_packet(input->codec_ctx.get(), input->packet.get());

          while (true) {
            int ret = avcodec_receive_frame(input->codec_ctx.get(),
                                            input->frame.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
              break;
            if (ret < 0)
              break;

            ffmpeg::ScopedFrameUnref frame_guard(input->frame.get());

            // Feed to filter graph
            if (av_buffersrc_add_frame_flags(input->buffersrc_ctx,
                                             input->frame.get(),
                                             AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
              std::cerr << "Error feeding filter graph\n";
              break;
            }
          }
        }
      }

      // Pull from filter graph
      while (true) {
        int ret =
            av_buffersink_get_frame(buffersink_ctx_, filtered_frame_.get());
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
          break;
        if (ret < 0)
          break;

        ffmpeg::ScopedFrameUnref frame_guard(filtered_frame_.get());

        filtered_frame_->pts = pts_counter++;
        encode_frame(filtered_frame_.get());

        frame_count++;
        if (frame_count % 30 == 0) {
          std::cout << std::format("Processed {} frames\r", frame_count)
                    << std::flush;
        }
      }
    }

    flush_encoder();
    av_write_trailer(output_format_ctx_.get());
    std::cout << std::format("\nDone! Total frames: {}\n", frame_count);
  }

private:
  struct InputContext {
    ffmpeg::FormatContextPtr fmt_ctx;
    ffmpeg::CodecContextPtr codec_ctx;
    ffmpeg::PacketPtr packet;
    ffmpeg::FramePtr frame;
    int video_stream_idx = -1;
    AVFilterContext *buffersrc_ctx = nullptr;
    bool finished = false;

    InputContext(std::string_view filename)
        : fmt_ctx(ffmpeg::open_input_format(filename.data())),
          packet(ffmpeg::create_packet()), frame(ffmpeg::create_frame()) {

      auto idx = ffmpeg::find_stream_index(fmt_ctx.get(), AVMEDIA_TYPE_VIDEO);
      if (!idx)
        throw std::runtime_error("No video stream");
      video_stream_idx = *idx;

      auto *par = fmt_ctx->streams[video_stream_idx]->codecpar;
      auto *decoder = avcodec_find_decoder(par->codec_id);
      codec_ctx = ffmpeg::create_codec_context(decoder);
      avcodec_parameters_to_context(codec_ctx.get(), par);
      avcodec_open2(codec_ctx.get(), decoder, nullptr);
    }
  };

  void initialize() {
    // Filter graph
    filter_graph_.reset(avfilter_graph_alloc());

    // Create buffer sources for inputs
    for (size_t i = 0; i < inputs_.size(); ++i) {
      auto &input = inputs_[i];
      auto *par = input->fmt_ctx->streams[input->video_stream_idx]->codecpar;
      auto time_base =
          input->fmt_ctx->streams[input->video_stream_idx]->time_base;

      std::string args = std::format(
          "video_size={}x{}:pix_fmt={}:time_base={}/{}:pixel_aspect={}/{}",
          par->width, par->height, par->format, time_base.num, time_base.den,
          par->sample_aspect_ratio.num, par->sample_aspect_ratio.den);

      char name[32];
      snprintf(name, sizeof(name), "in%zu", i);

      const AVFilter *buffersrc = avfilter_get_by_name("buffer");
      ffmpeg::check_error(avfilter_graph_create_filter(
                              &input->buffersrc_ctx, buffersrc, name,
                              args.c_str(), nullptr, filter_graph_.get()),
                          "create buffer source");
    }

    // Create buffer sink
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");
    ffmpeg::check_error(
        avfilter_graph_create_filter(&buffersink_ctx_, buffersink, "out",
                                     nullptr, nullptr, filter_graph_.get()),
        "create buffer sink");

    // Filter description: Scale PiP to 1/4 size and overlay at 10,10
    // [in0] is main, [in1] is PiP
    std::string filter_desc = "[in1]scale=iw/4:ih/4[pip];"
                              "[in0][pip]overlay=10:10[out]";

    // Connect graph
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();

    // Inputs to the graph (which are outputs of our source filters)
    // We need a linked list for multiple inputs
    AVFilterInOut *curr = outputs;
    for (size_t i = 0; i < inputs_.size(); ++i) {
      char name[32];
      snprintf(name, sizeof(name), "in%zu", i);

      curr->name = av_strdup(name);
      curr->filter_ctx = inputs_[i]->buffersrc_ctx;
      curr->pad_idx = 0;

      if (i < inputs_.size() - 1) {
        curr->next = avfilter_inout_alloc();
        curr = curr->next;
      } else {
        curr->next = nullptr;
      }
    }

    // Output from the graph (input to our sink)
    inputs->name = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx_;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    ffmpeg::check_error(avfilter_graph_parse_ptr(filter_graph_.get(),
                                                 filter_desc.c_str(), &inputs,
                                                 &outputs, nullptr),
                        "parse filter graph");

    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    ffmpeg::check_error(avfilter_graph_config(filter_graph_.get(), nullptr),
                        "config graph");

    // Initialize output
    initialize_output();
  }

  void initialize_output() {
    avformat_alloc_output_context2(&raw_out_ctx_, nullptr, nullptr,
                                   output_file_.c_str());
    output_format_ctx_.reset(raw_out_ctx_);

    auto *encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    output_stream_ = avformat_new_stream(output_format_ctx_.get(), nullptr);

    output_codec_ctx_ = ffmpeg::create_codec_context(encoder);

    // Use main input dimensions
    output_codec_ctx_->width = inputs_[0]->codec_ctx->width;
    output_codec_ctx_->height = inputs_[0]->codec_ctx->height;
    output_codec_ctx_->time_base = {1, 30};
    output_codec_ctx_->framerate = {30, 1};
    output_codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
    output_codec_ctx_->bit_rate = 2000000;

    if (output_format_ctx_->oformat->flags & AVFMT_GLOBALHEADER)
      output_codec_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    avcodec_open2(output_codec_ctx_.get(), encoder, nullptr);
    avcodec_parameters_from_context(output_stream_->codecpar,
                                    output_codec_ctx_.get());
    output_stream_->time_base = output_codec_ctx_->time_base;

    if (!(output_format_ctx_->oformat->flags & AVFMT_NOFILE)) {
      avio_open(&output_format_ctx_->pb, output_file_.c_str(), AVIO_FLAG_WRITE);
    }
    ffmpeg::check_error(
        avformat_write_header(output_format_ctx_.get(), nullptr),
        "write header");
  }

  void encode_frame(AVFrame *frame) {
    auto packet = ffmpeg::create_packet();
    if (avcodec_send_frame(output_codec_ctx_.get(), frame) < 0)
      return;

    while (true) {
      int ret = avcodec_receive_packet(output_codec_ctx_.get(), packet.get());
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
  std::vector<std::unique_ptr<InputContext>> inputs_;

  ffmpeg::FilterGraphPtr filter_graph_;
  AVFilterContext *buffersink_ctx_ = nullptr;
  ffmpeg::FramePtr filtered_frame_ = ffmpeg::create_frame();

  ffmpeg::FormatContextPtr output_format_ctx_;
  AVFormatContext *raw_out_ctx_ = nullptr; // Helper for init
  ffmpeg::CodecContextPtr output_codec_ctx_;
  AVStream *output_stream_ = nullptr;
};

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 4) {
    std::cerr << std::format(
        "Usage: {} <main_video> <pip_video> <output_file>\n", argv[0]);
    return 1;
  }

  try {
    ComplexFilter filter(argv[1], argv[2], argv[3]);
    filter.process();
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }

  return 0;
}
