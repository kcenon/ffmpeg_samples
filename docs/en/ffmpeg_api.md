# FFmpeg API Guide

## Introduction

This guide provides comprehensive documentation for the FFmpeg API functions and structures used in the sample applications. FFmpeg is a powerful multimedia framework that provides libraries for encoding, decoding, transcoding, muxing, demuxing, filtering, and playing various audio and video formats.

## FFmpeg Library Overview

### Core Libraries

#### libavformat
Container format handling (muxing and demuxing).

**Purpose:**
- Reading and writing container formats (MP4, AVI, MKV, etc.)
- Managing streams within containers
- Handling metadata and chapters
- Network protocols support

**Key Structures:**
- `AVFormatContext` - Format I/O context
- `AVStream` - Stream information
- `AVIOContext` - I/O context for custom protocols

#### libavcodec
Codec encoding and decoding.

**Purpose:**
- Encoding and decoding audio/video streams
- Codec management and configuration
- Frame and packet handling
- Hardware acceleration support

**Key Structures:**
- `AVCodec` - Codec information
- `AVCodecContext` - Codec context with configuration
- `AVPacket` - Compressed data packet
- `AVFrame` - Decoded frame data

#### libavutil
Utility functions and structures.

**Purpose:**
- Memory management
- Mathematical operations
- Pixel format and sample format definitions
- Error handling
- Logging
- Timing and rational number operations

**Key Structures:**
- `AVRational` - Rational number (numerator/denominator)
- `AVDictionary` - Key-value dictionary
- `AVBuffer` - Reference-counted buffer

#### libavfilter
Video and audio filtering.

**Purpose:**
- Creating filter graphs
- Applying effects and transformations
- Complex processing pipelines
- Audio/video manipulation

**Key Structures:**
- `AVFilterGraph` - Filter graph
- `AVFilterContext` - Filter instance
- `AVFilter` - Filter definition
- `AVFilterInOut` - Filter input/output

#### libswscale
Image scaling and color space conversion.

**Purpose:**
- Scaling images
- Converting pixel formats
- Color space conversion
- Aspect ratio handling

**Key Structures:**
- `SwsContext` - Scaling context

#### libswresample
Audio resampling.

**Purpose:**
- Sample rate conversion
- Channel layout conversion
- Sample format conversion
- Audio mixing

**Key Structures:**
- `SwrContext` - Resampling context

## Common API Patterns

### Opening and Reading Files

```cpp
// Open input file
AVFormatContext* format_ctx = nullptr;
int ret = avformat_open_input(&format_ctx, filename, nullptr, nullptr);
if (ret < 0) {
    // Handle error
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
    std::cerr << "Error: " << errbuf << "\n";
    return -1;
}

// Find stream information
ret = avformat_find_stream_info(format_ctx, nullptr);
if (ret < 0) {
    avformat_close_input(&format_ctx);
    return -1;
}

// Find video stream
int video_stream_index = -1;
for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
    if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        video_stream_index = i;
        break;
    }
}

// Cleanup
avformat_close_input(&format_ctx);
```

### Setting Up Decoder

```cpp
// Get codec parameters
AVCodecParameters* codecpar = format_ctx->streams[video_stream_index]->codecpar;

// Find decoder
const AVCodec* decoder = avcodec_find_decoder(codecpar->codec_id);
if (!decoder) {
    std::cerr << "Decoder not found\n";
    return -1;
}

// Allocate codec context
AVCodecContext* codec_ctx = avcodec_alloc_context3(decoder);
if (!codec_ctx) {
    std::cerr << "Failed to allocate codec context\n";
    return -1;
}

// Copy parameters to context
ret = avcodec_parameters_to_context(codec_ctx, codecpar);
if (ret < 0) {
    avcodec_free_context(&codec_ctx);
    return -1;
}

// Open codec
ret = avcodec_open2(codec_ctx, decoder, nullptr);
if (ret < 0) {
    avcodec_free_context(&codec_ctx);
    return -1;
}

// Cleanup
avcodec_free_context(&codec_ctx);
```

### Decoding Frames

```cpp
AVPacket* packet = av_packet_alloc();
AVFrame* frame = av_frame_alloc();

// Read packets
while (av_read_frame(format_ctx, packet) >= 0) {
    if (packet->stream_index == video_stream_index) {
        // Send packet to decoder
        ret = avcodec_send_packet(codec_ctx, packet);
        if (ret < 0) {
            std::cerr << "Error sending packet\n";
            break;
        }

        // Receive decoded frames
        while (ret >= 0) {
            ret = avcodec_receive_frame(codec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                std::cerr << "Error receiving frame\n";
                break;
            }

            // Process frame
            // ...

            av_frame_unref(frame);
        }
    }
    av_packet_unref(packet);
}

// Cleanup
av_packet_free(&packet);
av_frame_free(&frame);
```

### Setting Up Encoder

```cpp
// Find encoder
const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
if (!encoder) {
    std::cerr << "Encoder not found\n";
    return -1;
}

// Allocate codec context
AVCodecContext* codec_ctx = avcodec_alloc_context3(encoder);
if (!codec_ctx) {
    std::cerr << "Failed to allocate codec context\n";
    return -1;
}

// Set encoding parameters
codec_ctx->width = 1280;
codec_ctx->height = 720;
codec_ctx->time_base = AVRational{1, 30};
codec_ctx->framerate = AVRational{30, 1};
codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
codec_ctx->bit_rate = 2000000;
codec_ctx->gop_size = 10;
codec_ctx->max_b_frames = 1;

// Set codec options
av_opt_set(codec_ctx->priv_data, "preset", "medium", 0);

// Open codec
ret = avcodec_open2(codec_ctx, encoder, nullptr);
if (ret < 0) {
    avcodec_free_context(&codec_ctx);
    return -1;
}

// Cleanup
avcodec_free_context(&codec_ctx);
```

### Encoding Frames

```cpp
AVFrame* frame = av_frame_alloc();
AVPacket* packet = av_packet_alloc();

// Allocate frame buffer
frame->format = codec_ctx->pix_fmt;
frame->width = codec_ctx->width;
frame->height = codec_ctx->height;
av_frame_get_buffer(frame, 0);

// Encode frames
for (int i = 0; i < num_frames; i++) {
    av_frame_make_writable(frame);

    // Fill frame data
    // ...

    frame->pts = i;

    // Send frame to encoder
    ret = avcodec_send_frame(codec_ctx, frame);
    if (ret < 0) {
        std::cerr << "Error sending frame\n";
        break;
    }

    // Receive encoded packets
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_ctx, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            std::cerr << "Error receiving packet\n";
            break;
        }

        // Write packet
        // ...

        av_packet_unref(packet);
    }
}

// Flush encoder
avcodec_send_frame(codec_ctx, nullptr);
while (avcodec_receive_packet(codec_ctx, packet) >= 0) {
    // Write packet
    av_packet_unref(packet);
}

// Cleanup
av_packet_free(&packet);
av_frame_free(&frame);
```

### Creating Output File

```cpp
AVFormatContext* output_format_ctx = nullptr;

// Allocate output context
avformat_alloc_output_context2(&output_format_ctx, nullptr, nullptr, filename);
if (!output_format_ctx) {
    std::cerr << "Could not create output context\n";
    return -1;
}

// Create stream
AVStream* stream = avformat_new_stream(output_format_ctx, nullptr);
if (!stream) {
    std::cerr << "Failed to create stream\n";
    avformat_free_context(output_format_ctx);
    return -1;
}

// Copy codec parameters
avcodec_parameters_from_context(stream->codecpar, codec_ctx);
stream->time_base = codec_ctx->time_base;

// Open output file
if (!(output_format_ctx->oformat->flags & AVFMT_NOFILE)) {
    ret = avio_open(&output_format_ctx->pb, filename, AVIO_FLAG_WRITE);
    if (ret < 0) {
        avformat_free_context(output_format_ctx);
        return -1;
    }
}

// Write header
ret = avformat_write_header(output_format_ctx, nullptr);
if (ret < 0) {
    avio_closep(&output_format_ctx->pb);
    avformat_free_context(output_format_ctx);
    return -1;
}

// Write packets...

// Write trailer
av_write_trailer(output_format_ctx);

// Cleanup
if (!(output_format_ctx->oformat->flags & AVFMT_NOFILE)) {
    avio_closep(&output_format_ctx->pb);
}
avformat_free_context(output_format_ctx);
```

### Image Scaling and Color Conversion

```cpp
// Create scaling context
SwsContext* sws_ctx = sws_getContext(
    src_width, src_height, src_pix_fmt,
    dst_width, dst_height, dst_pix_fmt,
    SWS_BILINEAR, nullptr, nullptr, nullptr
);

if (!sws_ctx) {
    std::cerr << "Failed to create scaling context\n";
    return -1;
}

// Allocate destination frame
AVFrame* dst_frame = av_frame_alloc();
dst_frame->format = dst_pix_fmt;
dst_frame->width = dst_width;
dst_frame->height = dst_height;
av_frame_get_buffer(dst_frame, 0);

// Scale frame
sws_scale(sws_ctx,
          src_frame->data, src_frame->linesize, 0, src_height,
          dst_frame->data, dst_frame->linesize);

// Cleanup
sws_freeContext(sws_ctx);
av_frame_free(&dst_frame);
```

### Filter Graph Setup

```cpp
AVFilterGraph* filter_graph = avfilter_graph_alloc();
AVFilterContext* buffersrc_ctx = nullptr;
AVFilterContext* buffersink_ctx = nullptr;

// Create buffer source
const AVFilter* buffersrc = avfilter_get_by_name("buffer");
char args[512];
snprintf(args, sizeof(args),
         "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
         width, height, pix_fmt,
         time_base.num, time_base.den,
         sar.num, sar.den);

avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                             args, nullptr, filter_graph);

// Create buffer sink
const AVFilter* buffersink = avfilter_get_by_name("buffersink");
avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                             nullptr, nullptr, filter_graph);

// Parse filter description
AVFilterInOut* outputs = avfilter_inout_alloc();
AVFilterInOut* inputs = avfilter_inout_alloc();

outputs->name = av_strdup("in");
outputs->filter_ctx = buffersrc_ctx;
inputs->name = av_strdup("out");
inputs->filter_ctx = buffersink_ctx;

avfilter_graph_parse_ptr(filter_graph, "scale=1280:720",
                         &inputs, &outputs, nullptr);

avfilter_graph_config(filter_graph, nullptr);

// Use filter
av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
av_buffersink_get_frame(buffersink_ctx, filtered_frame);

// Cleanup
avfilter_inout_free(&inputs);
avfilter_inout_free(&outputs);
avfilter_graph_free(&filter_graph);
```

## Memory Management

### Allocation and Deallocation

FFmpeg uses various allocation methods:

```cpp
// AVFormatContext
AVFormatContext* fmt_ctx = nullptr;
avformat_open_input(&fmt_ctx, filename, nullptr, nullptr);
avformat_close_input(&fmt_ctx);  // Also frees memory

// AVCodecContext
AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
avcodec_free_context(&codec_ctx);

// AVPacket
AVPacket* packet = av_packet_alloc();
av_packet_free(&packet);

// AVFrame
AVFrame* frame = av_frame_alloc();
av_frame_free(&frame);

// Generic memory
uint8_t* buffer = (uint8_t*)av_malloc(size);
av_free(buffer);
```

### Reference Counting

FFmpeg uses reference counting for frames and packets:

```cpp
// Increase reference count
AVFrame* ref_frame = av_frame_clone(frame);

// Decrease reference count
av_frame_unref(frame);

// For packets
av_packet_unref(packet);
```

## Error Handling

### Error Codes

```cpp
int ret = some_ffmpeg_function();
if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
    std::cerr << "Error: " << errbuf << "\n";
}
```

### Common Error Codes

- `AVERROR(EAGAIN)` - Output not available, retry
- `AVERROR_EOF` - End of file
- `AVERROR(ENOMEM)` - Out of memory
- `AVERROR(EINVAL)` - Invalid argument
- `AVERROR_DECODER_NOT_FOUND` - Decoder not found
- `AVERROR_ENCODER_NOT_FOUND` - Encoder not found

## Timing and Timestamps

### Time Base

```cpp
// Time base is a rational number (num/den)
AVRational time_base = stream->time_base;

// Convert timestamp to seconds
double seconds = timestamp * av_q2d(time_base);

// Convert seconds to timestamp
int64_t timestamp = seconds / av_q2d(time_base);
```

### Presentation Timestamp (PTS)

```cpp
// Each frame/packet has a PTS
frame->pts = frame_number;

// Rescale timestamps between different time bases
av_packet_rescale_ts(packet, src_time_base, dst_time_base);
```

## Pixel Formats

### Common Pixel Formats

- `AV_PIX_FMT_YUV420P` - Most common, planar YUV 4:2:0
- `AV_PIX_FMT_RGB24` - RGB 24-bit
- `AV_PIX_FMT_BGR24` - BGR 24-bit
- `AV_PIX_FMT_RGBA` - RGBA 32-bit
- `AV_PIX_FMT_NV12` - Semi-planar YUV 4:2:0

### Checking Format Support

```cpp
// Check if pixel format is supported by encoder
const enum AVPixelFormat* p = codec->pix_fmts;
while (*p != AV_PIX_FMT_NONE) {
    if (*p == pix_fmt) {
        // Supported
        break;
    }
    p++;
}
```

## Codec Options

### Setting Options

```cpp
// Set codec private options
av_opt_set(codec_ctx->priv_data, "preset", "slow", 0);
av_opt_set(codec_ctx->priv_data, "crf", "23", 0);

// Set integer options
av_opt_set_int(codec_ctx->priv_data, "option_name", value, 0);
```

### H.264 Presets

- `ultrafast` - Fastest encoding, lowest quality
- `superfast` - Very fast, low quality
- `veryfast` - Fast, acceptable quality
- `faster` - Faster than medium
- `fast` - Fast encoding
- `medium` - Default, balanced
- `slow` - Slower, better quality
- `slower` - Very slow, very good quality
- `veryslow` - Slowest, best quality

## Threading

### Enabling Multi-threading

```cpp
// Set thread count (0 = auto)
codec_ctx->thread_count = 0;

// Set thread type
codec_ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
```

## Hardware Acceleration

### Finding Hardware Decoders

```cpp
// Find hardware decoder
const AVCodec* codec = nullptr;
void* iter = nullptr;
while ((codec = av_codec_iterate(&iter))) {
    if (av_codec_is_decoder(codec) &&
        codec->id == codec_id &&
        (codec->capabilities & AV_CODEC_CAP_HARDWARE)) {
        // Found hardware decoder
        break;
    }
}
```

## Best Practices

1. **Always check return values** - FFmpeg functions return error codes
2. **Free allocated resources** - Prevent memory leaks
3. **Use reference counting** - For frames and packets
4. **Handle EAGAIN properly** - Retry operations when needed
5. **Flush decoders/encoders** - Send nullptr to flush remaining data
6. **Rescale timestamps** - When changing time bases
7. **Check codec support** - Verify pixel formats and features
8. **Use appropriate time base** - Match stream time base
9. **Set thread count** - Enable multi-threading for better performance
10. **Log errors properly** - Use av_strerror for error messages

## Additional Resources

- [Official FFmpeg Documentation](https://ffmpeg.org/documentation.html)
- [FFmpeg API Doxygen](https://ffmpeg.org/doxygen/trunk/index.html)
- [FFmpeg Wiki](https://trac.ffmpeg.org/wiki)
- [FFmpeg Examples](https://github.com/FFmpeg/FFmpeg/tree/master/doc/examples)

## See Also

- [Video Info Documentation](video_info.md)
- [Video Decoder Documentation](video_decoder.md)
- [Video Encoder Documentation](video_encoder.md)
- [Video Transcoder Documentation](video_transcoder.md)
- [Video Filter Documentation](video_filter.md)
- [Main README](../../README.md)
