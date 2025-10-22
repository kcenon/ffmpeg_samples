# Video Transcoder

## Overview

The `video_transcoder` application converts videos between different formats, codecs, resolutions, and bitrates. It combines decoding, scaling, and encoding in a single efficient pipeline.

## Usage

```bash
./video_transcoder <input_file> <output_file> [width] [height] [bitrate] [fps]
```

### Parameters

- `input_file` - Input video file path (required)
- `output_file` - Output video file path (required)
- `width` - Output width in pixels (optional, default: 1280)
- `height` - Output height in pixels (optional, default: 720)
- `bitrate` - Output bitrate in bps (optional, default: 2000000)
- `fps` - Output frame rate (optional, default: 30)

## Examples

```bash
# Convert to 720p
./video_transcoder input.avi output.mp4 1280 720

# Convert to 1080p with high bitrate
./video_transcoder input.mkv output.mp4 1920 1080 5000000 30

# Convert format only (keep resolution)
./video_transcoder input.avi output.mp4
```

## Features

- Format conversion (AVI, MP4, MKV, MOV, etc.)
- Resolution scaling (upscaling/downscaling)
- Bitrate adjustment
- Frame rate conversion
- Codec conversion (any to H.264)
- Quality presets (fast, medium, slow)
- Progress reporting

## Implementation Details

### Transcoding Pipeline

1. Open input file and find streams
2. Initialize input decoder
3. Create output file and encoder
4. Initialize swscale context for resolution change
5. Read packets from input
6. Decode frames
7. Scale frames to target resolution
8. Encode scaled frames
9. Write encoded packets to output

### Key Components

```cpp
// Decoder
input_codec_ctx = avcodec_alloc_context3(decoder);
avcodec_open2(input_codec_ctx, decoder, nullptr);

// Scaler
sws_ctx = sws_getContext(
    input_width, input_height, input_pix_fmt,
    output_width, output_height, AV_PIX_FMT_YUV420P,
    SWS_BICUBIC, nullptr, nullptr, nullptr
);

// Encoder
output_codec_ctx = avcodec_alloc_context3(encoder);
output_codec_ctx->bit_rate = bitrate;
avcodec_open2(output_codec_ctx, encoder, nullptr);
```

## Use Cases

- Format standardization
- Resolution normalization
- Bitrate optimization for streaming
- Codec migration
- Quality adjustment
- Frame rate conversion

## Performance Tips

- Use hardware acceleration when available
- Choose appropriate scaling algorithm
- Adjust encoder preset based on speed/quality needs
- Consider multi-pass encoding for best quality

## Common Issues

### Quality Loss
- Increase bitrate
- Use slower encoding preset
- Avoid upscaling low-quality sources

### Slow Performance
- Use faster preset (fast, veryfast)
- Reduce output resolution
- Enable hardware acceleration

## See Also

- [Video Decoder Documentation](video_decoder.md)
- [Video Encoder Documentation](video_encoder.md)
- [FFmpeg API Guide](ffmpeg_api.md)
