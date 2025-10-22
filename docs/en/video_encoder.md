# Video Encoder

## Overview

The `video_encoder` application demonstrates how to create video files from scratch by generating and encoding frames using FFmpeg libraries. It creates animated test patterns and encodes them to H.264/MP4 format.

## Usage

```bash
./video_encoder <output_file> [num_frames] [width] [height] [fps]
```

### Parameters

- `output_file` - Output video file path (required)
- `num_frames` - Number of frames to generate (optional, default: 100)
- `width` - Video width in pixels (optional, default: 1280)
- `height` - Video height in pixels (optional, default: 720)
- `fps` - Frame rate (optional, default: 30)

## Examples

```bash
# Generate default 720p video
./video_encoder output.mp4

# Generate 1080p 60fps video with 300 frames
./video_encoder output.mp4 300 1920 1080 60

# Generate 4K video
./video_encoder output_4k.mp4 200 3840 2160 30
```

## Features

- H.264 encoding with configurable presets
- Customizable resolution and frame rate
- Animated test pattern generation
- YUV420P pixel format (most compatible)
- Progress reporting
- Proper timestamp management

## Implementation Details

### Encoding Pipeline

1. Create output format context
2. Find H.264 encoder
3. Configure codec parameters (resolution, bitrate, GOP size)
4. Open codec and write header
5. Generate frames in YUV420P format
6. Encode frames to packets
7. Write packets to file
8. Flush encoder and write trailer

### Key Configuration

```cpp
codec_ctx->width = width;
codec_ctx->height = height;
codec_ctx->time_base = AVRational{1, fps};
codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
codec_ctx->bit_rate = 2000000;  // 2 Mbps
codec_ctx->gop_size = 10;        // I-frame every 10 frames
av_opt_set(codec_ctx->priv_data, "preset", "medium", 0);
```

## Use Cases

- Testing video processing pipelines
- Generating test content for validation
- Learning video encoding workflows
- Creating synthetic video datasets
- Performance benchmarking

## See Also

- [Video Decoder Documentation](video_decoder.md)
- [Video Transcoder Documentation](video_transcoder.md)
- [FFmpeg API Guide](ffmpeg_api.md)
