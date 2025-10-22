/**
 * Video Filter
 *
 * This sample demonstrates how to apply various video filters
 * using FFmpeg's filter graph API.
 */

#include <iostream>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
}

class VideoFilter {
private:
    AVFormatContext* input_format_ctx;
    AVFormatContext* output_format_ctx;
    AVCodecContext* input_codec_ctx;
    AVCodecContext* output_codec_ctx;
    AVFilterGraph* filter_graph;
    AVFilterContext* buffersrc_ctx;
    AVFilterContext* buffersink_ctx;
    int video_stream_index;

public:
    VideoFilter()
        : input_format_ctx(nullptr), output_format_ctx(nullptr),
          input_codec_ctx(nullptr), output_codec_ctx(nullptr),
          filter_graph(nullptr), buffersrc_ctx(nullptr),
          buffersink_ctx(nullptr), video_stream_index(-1) {}

    ~VideoFilter() {
        cleanup();
    }

    bool open_input(const char* filename) {
        int ret = avformat_open_input(&input_format_ctx, filename, nullptr, nullptr);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            std::cerr << "Error opening input file: " << errbuf << "\n";
            return false;
        }

        if (avformat_find_stream_info(input_format_ctx, nullptr) < 0) {
            std::cerr << "Error finding stream information\n";
            return false;
        }

        // Find video stream
        for (unsigned int i = 0; i < input_format_ctx->nb_streams; i++) {
            if (input_format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                video_stream_index = i;
                break;
            }
        }

        if (video_stream_index == -1) {
            std::cerr << "No video stream found\n";
            return false;
        }

        // Open decoder
        AVCodecParameters* codecpar = input_format_ctx->streams[video_stream_index]->codecpar;
        const AVCodec* decoder = avcodec_find_decoder(codecpar->codec_id);
        if (!decoder) {
            std::cerr << "Decoder not found\n";
            return false;
        }

        input_codec_ctx = avcodec_alloc_context3(decoder);
        if (!input_codec_ctx) {
            std::cerr << "Failed to allocate decoder context\n";
            return false;
        }

        if (avcodec_parameters_to_context(input_codec_ctx, codecpar) < 0) {
            std::cerr << "Failed to copy codec parameters\n";
            return false;
        }

        if (avcodec_open2(input_codec_ctx, decoder, nullptr) < 0) {
            std::cerr << "Failed to open decoder\n";
            return false;
        }

        return true;
    }

    bool init_filter(const char* filter_description) {
        char args[512];
        int ret;

        const AVFilter* buffersrc = avfilter_get_by_name("buffer");
        const AVFilter* buffersink = avfilter_get_by_name("buffersink");
        AVFilterInOut* outputs = avfilter_inout_alloc();
        AVFilterInOut* inputs = avfilter_inout_alloc();

        filter_graph = avfilter_graph_alloc();
        if (!outputs || !inputs || !filter_graph) {
            std::cerr << "Failed to allocate filter graph\n";
            return false;
        }

        // Buffer video source
        AVRational time_base = input_format_ctx->streams[video_stream_index]->time_base;
        snprintf(args, sizeof(args),
                "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                input_codec_ctx->width, input_codec_ctx->height,
                input_codec_ctx->pix_fmt,
                time_base.num, time_base.den,
                input_codec_ctx->sample_aspect_ratio.num,
                input_codec_ctx->sample_aspect_ratio.den);

        ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                                          args, nullptr, filter_graph);
        if (ret < 0) {
            std::cerr << "Cannot create buffer source\n";
            avfilter_inout_free(&inputs);
            avfilter_inout_free(&outputs);
            return false;
        }

        // Buffer video sink
        ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                                          nullptr, nullptr, filter_graph);
        if (ret < 0) {
            std::cerr << "Cannot create buffer sink\n";
            avfilter_inout_free(&inputs);
            avfilter_inout_free(&outputs);
            return false;
        }

        enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };
        ret = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts,
                                  AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            std::cerr << "Cannot set output pixel format\n";
            avfilter_inout_free(&inputs);
            avfilter_inout_free(&outputs);
            return false;
        }

        // Set endpoints for the filter graph
        outputs->name = av_strdup("in");
        outputs->filter_ctx = buffersrc_ctx;
        outputs->pad_idx = 0;
        outputs->next = nullptr;

        inputs->name = av_strdup("out");
        inputs->filter_ctx = buffersink_ctx;
        inputs->pad_idx = 0;
        inputs->next = nullptr;

        // Parse filter description
        ret = avfilter_graph_parse_ptr(filter_graph, filter_description,
                                       &inputs, &outputs, nullptr);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            std::cerr << "Error parsing filter graph: " << errbuf << "\n";
            avfilter_inout_free(&inputs);
            avfilter_inout_free(&outputs);
            return false;
        }

        ret = avfilter_graph_config(filter_graph, nullptr);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            std::cerr << "Error configuring filter graph: " << errbuf << "\n";
            avfilter_inout_free(&inputs);
            avfilter_inout_free(&outputs);
            return false;
        }

        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);

        return true;
    }

    bool create_output(const char* filename) {
        avformat_alloc_output_context2(&output_format_ctx, nullptr, nullptr, filename);
        if (!output_format_ctx) {
            std::cerr << "Could not create output context\n";
            return false;
        }

        // Get filtered frame properties
        AVFrame* temp_frame = av_frame_alloc();
        if (av_buffersink_get_frame(buffersink_ctx, temp_frame) >= 0) {
            av_frame_unref(temp_frame);
        }
        av_frame_free(&temp_frame);

        // Create encoder
        const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!encoder) {
            std::cerr << "H264 encoder not found\n";
            return false;
        }

        AVStream* out_stream = avformat_new_stream(output_format_ctx, nullptr);
        if (!out_stream) {
            std::cerr << "Failed to create output stream\n";
            return false;
        }

        output_codec_ctx = avcodec_alloc_context3(encoder);
        if (!output_codec_ctx) {
            std::cerr << "Failed to allocate encoder context\n";
            return false;
        }

        // Set encoder parameters
        output_codec_ctx->width = input_codec_ctx->width;
        output_codec_ctx->height = input_codec_ctx->height;
        output_codec_ctx->time_base = AVRational{1, 30};
        output_codec_ctx->framerate = AVRational{30, 1};
        output_codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        output_codec_ctx->bit_rate = 2000000;
        output_codec_ctx->gop_size = 10;
        output_codec_ctx->max_b_frames = 1;

        av_opt_set(output_codec_ctx->priv_data, "preset", "medium", 0);

        if (output_format_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
            output_codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        if (avcodec_open2(output_codec_ctx, encoder, nullptr) < 0) {
            std::cerr << "Failed to open encoder\n";
            return false;
        }

        avcodec_parameters_from_context(out_stream->codecpar, output_codec_ctx);
        out_stream->time_base = output_codec_ctx->time_base;

        // Open output file
        if (!(output_format_ctx->oformat->flags & AVFMT_NOFILE)) {
            int ret = avio_open(&output_format_ctx->pb, filename, AVIO_FLAG_WRITE);
            if (ret < 0) {
                char errbuf[AV_ERROR_MAX_STRING_SIZE];
                av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
                std::cerr << "Failed to open output file: " << errbuf << "\n";
                return false;
            }
        }

        // Write header
        int ret = avformat_write_header(output_format_ctx, nullptr);
        if (ret < 0) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
            std::cerr << "Error writing header: " << errbuf << "\n";
            return false;
        }

        return true;
    }

    bool process() {
        AVPacket* packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        AVFrame* filtered_frame = av_frame_alloc();

        if (!packet || !frame || !filtered_frame) {
            std::cerr << "Failed to allocate packet or frames\n";
            av_packet_free(&packet);
            av_frame_free(&frame);
            av_frame_free(&filtered_frame);
            return false;
        }

        int64_t pts_counter = 0;
        int frame_count = 0;

        std::cout << "Processing video with filters...\n";

        while (av_read_frame(input_format_ctx, packet) >= 0) {
            if (packet->stream_index == video_stream_index) {
                int ret = avcodec_send_packet(input_codec_ctx, packet);
                if (ret < 0) {
                    av_packet_unref(packet);
                    continue;
                }

                while (ret >= 0) {
                    ret = avcodec_receive_frame(input_codec_ctx, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    } else if (ret < 0) {
                        std::cerr << "Error decoding frame\n";
                        break;
                    }

                    // Push frame to filter graph
                    if (av_buffersrc_add_frame_flags(buffersrc_ctx, frame,
                                                     AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                        std::cerr << "Error feeding frame to filter graph\n";
                        break;
                    }

                    // Pull filtered frames
                    while (true) {
                        ret = av_buffersink_get_frame(buffersink_ctx, filtered_frame);
                        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                            break;
                        } else if (ret < 0) {
                            std::cerr << "Error getting filtered frame\n";
                            break;
                        }

                        filtered_frame->pts = pts_counter++;
                        encode_frame(filtered_frame);
                        av_frame_unref(filtered_frame);
                        frame_count++;

                        if (frame_count % 30 == 0) {
                            std::cout << "Processed " << frame_count << " frames\r" << std::flush;
                        }
                    }

                    av_frame_unref(frame);
                }
            }
            av_packet_unref(packet);
        }

        std::cout << "\nTotal frames processed: " << frame_count << "\n";

        // Flush encoder
        flush_encoder();

        // Write trailer
        av_write_trailer(output_format_ctx);

        av_packet_free(&packet);
        av_frame_free(&frame);
        av_frame_free(&filtered_frame);

        return true;
    }

    void encode_frame(AVFrame* frame) {
        AVPacket* packet = av_packet_alloc();

        int ret = avcodec_send_frame(output_codec_ctx, frame);
        if (ret < 0) {
            av_packet_free(&packet);
            return;
        }

        while (ret >= 0) {
            ret = avcodec_receive_packet(output_codec_ctx, packet);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                break;
            }

            av_packet_rescale_ts(packet, output_codec_ctx->time_base,
                                output_format_ctx->streams[0]->time_base);
            packet->stream_index = 0;
            av_interleaved_write_frame(output_format_ctx, packet);
            av_packet_unref(packet);
        }

        av_packet_free(&packet);
    }

    void flush_encoder() {
        AVPacket* packet = av_packet_alloc();
        avcodec_send_frame(output_codec_ctx, nullptr);

        int ret;
        while ((ret = avcodec_receive_packet(output_codec_ctx, packet)) >= 0) {
            av_packet_rescale_ts(packet, output_codec_ctx->time_base,
                                output_format_ctx->streams[0]->time_base);
            packet->stream_index = 0;
            av_interleaved_write_frame(output_format_ctx, packet);
            av_packet_unref(packet);
        }

        av_packet_free(&packet);
    }

    void cleanup() {
        if (filter_graph) avfilter_graph_free(&filter_graph);
        if (input_codec_ctx) avcodec_free_context(&input_codec_ctx);
        if (output_codec_ctx) avcodec_free_context(&output_codec_ctx);
        if (input_format_ctx) avformat_close_input(&input_format_ctx);
        if (output_format_ctx) {
            if (!(output_format_ctx->oformat->flags & AVFMT_NOFILE)) {
                avio_closep(&output_format_ctx->pb);
            }
            avformat_free_context(output_format_ctx);
        }
    }
};

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " <input_file> <output_file> <filter_type>\n\n";
    std::cout << "Available filter types:\n";
    std::cout << "  grayscale    - Convert to grayscale\n";
    std::cout << "  blur         - Apply Gaussian blur\n";
    std::cout << "  sharpen      - Apply sharpening\n";
    std::cout << "  rotate       - Rotate 90 degrees clockwise\n";
    std::cout << "  flip_h       - Flip horizontally\n";
    std::cout << "  flip_v       - Flip vertically\n";
    std::cout << "  brightness   - Increase brightness\n";
    std::cout << "  contrast     - Increase contrast\n";
    std::cout << "  edge         - Edge detection\n";
    std::cout << "  negative     - Negative image\n";
    std::cout << "  custom       - Custom filter (you can modify the code)\n";
    std::cout << "\nExample: " << prog_name << " input.mp4 output.mp4 grayscale\n";
}

const char* get_filter_description(const char* filter_type) {
    if (strcmp(filter_type, "grayscale") == 0) {
        return "hue=s=0";
    } else if (strcmp(filter_type, "blur") == 0) {
        return "gblur=sigma=5";
    } else if (strcmp(filter_type, "sharpen") == 0) {
        return "unsharp=5:5:1.0:5:5:0.0";
    } else if (strcmp(filter_type, "rotate") == 0) {
        return "transpose=1";
    } else if (strcmp(filter_type, "flip_h") == 0) {
        return "hflip";
    } else if (strcmp(filter_type, "flip_v") == 0) {
        return "vflip";
    } else if (strcmp(filter_type, "brightness") == 0) {
        return "eq=brightness=0.2";
    } else if (strcmp(filter_type, "contrast") == 0) {
        return "eq=contrast=1.5";
    } else if (strcmp(filter_type, "edge") == 0) {
        return "edgedetect=low=0.1:high=0.4";
    } else if (strcmp(filter_type, "negative") == 0) {
        return "negate";
    } else if (strcmp(filter_type, "custom") == 0) {
        // Combine multiple filters
        return "eq=brightness=0.1:contrast=1.2,hue=s=1.2";
    }

    return nullptr;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        print_usage(argv[0]);
        return 1;
    }

    const char* input_filename = argv[1];
    const char* output_filename = argv[2];
    const char* filter_type = argv[3];

    const char* filter_description = get_filter_description(filter_type);
    if (!filter_description) {
        std::cerr << "Unknown filter type: " << filter_type << "\n\n";
        print_usage(argv[0]);
        return 1;
    }

    std::cout << "FFmpeg Video Filter\n";
    std::cout << "===================\n";
    std::cout << "Input: " << input_filename << "\n";
    std::cout << "Output: " << output_filename << "\n";
    std::cout << "Filter: " << filter_type << "\n";
    std::cout << "Filter description: " << filter_description << "\n\n";

    VideoFilter video_filter;

    if (!video_filter.open_input(input_filename)) {
        std::cerr << "Failed to open input file\n";
        return 1;
    }

    if (!video_filter.init_filter(filter_description)) {
        std::cerr << "Failed to initialize filter\n";
        return 1;
    }

    if (!video_filter.create_output(output_filename)) {
        std::cerr << "Failed to create output file\n";
        return 1;
    }

    if (!video_filter.process()) {
        std::cerr << "Processing failed\n";
        return 1;
    }

    std::cout << "Filtering completed successfully!\n";
    std::cout << "Output file: " << output_filename << "\n";

    return 0;
}
