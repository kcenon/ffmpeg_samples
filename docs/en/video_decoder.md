# Video Decoder

## Overview

The `video_decoder` application demonstrates how to decode video frames from various video formats and save them as individual PPM (Portable Pixmap) image files. This tool is essential for frame-by-frame video analysis, thumbnail generation, and understanding video decoding workflows.

## Features

- Decode video frames from any FFmpeg-supported format
- Convert frames to RGB format
- Save frames as PPM images
- Configurable frame extraction limit
- Progress reporting during decoding
- Automatic pixel format conversion
- Support for various video codecs

## Usage

```bash
./video_decoder <input_file> <output_dir> [max_frames]
```

### Parameters

- `input_file` - Input video file path (required)
- `output_dir` - Directory where decoded frames will be saved (required)
- `max_frames` - Maximum number of frames to decode (optional, default: 10)

### Prerequisites

Create the output directory before running:
```bash
mkdir -p output_frames
```

## Examples

### Basic Usage

```bash
# Decode first 10 frames
./video_decoder video.mp4 frames/

# Decode first 100 frames
./video_decoder video.mp4 frames/ 100

# Decode from different formats
./video_decoder video.avi frames/ 50
./video_decoder video.mkv frames/ 25
```

### Batch Processing

```bash
#!/bin/bash
# Decode frames from multiple videos
for video in *.mp4; do
    dirname="${video%.mp4}_frames"
    mkdir -p "$dirname"
    ./video_decoder "$video" "$dirname" 30
done
```

## Output Format

Frames are saved in PPM format with sequential numbering:
- `frame_0.ppm`
- `frame_1.ppm`
- `frame_2.ppm`
- ...

### Why PPM Format?

PPM (Portable Pixmap) is chosen because:
- Simple uncompressed format
- No external libraries required for writing
- Easy to read and process
- Widely supported by image viewers
- Human-readable header

### Converting PPM Files

Convert to other formats using ImageMagick:
```bash
# Convert to PNG
convert frame_0.ppm frame_0.png

# Convert all frames to JPEG
mogrify -format jpg *.ppm

# Create animated GIF
convert -delay 10 frame_*.ppm animation.gif
```

## Implementation Details

### Decoding Pipeline

1. **Open Input File**
   ```cpp
   avformat_open_input(&format_ctx, filename, nullptr, nullptr);
   ```

2. **Find Stream Information**
   ```cpp
   avformat_find_stream_info(format_ctx, nullptr);
   ```

3. **Find Video Stream**
   ```cpp
   for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
       if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
           video_stream_index = i;
           break;
       }
   }
   ```

4. **Initialize Decoder**
   ```cpp
   codec = avcodec_find_decoder(codecpar->codec_id);
   codec_ctx = avcodec_alloc_context3(codec);
   avcodec_parameters_to_context(codec_ctx, codecpar);
   avcodec_open2(codec_ctx, codec, nullptr);
   ```

5. **Initialize Color Conversion**
   ```cpp
   sws_ctx = sws_getContext(
       codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
       codec_ctx->width, codec_ctx->height, AV_PIX_FMT_RGB24,
       SWS_BILINEAR, nullptr, nullptr, nullptr
   );
   ```

6. **Read and Decode Packets**
   ```cpp
   while (av_read_frame(format_ctx, packet) >= 0) {
       if (packet->stream_index == video_stream_index) {
           avcodec_send_packet(codec_ctx, packet);
           while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
               // Process frame
           }
       }
       av_packet_unref(packet);
   }
   ```

7. **Convert Color Space**
   ```cpp
   sws_scale(sws_ctx, frame->data, frame->linesize, 0,
             codec_ctx->height, frame_rgb->data, frame_rgb->linesize);
   ```

8. **Save Frame**
   ```cpp
   save_frame_as_ppm(frame_rgb, width, height, frame_number, output_dir);
   ```

### Memory Management

The decoder properly manages FFmpeg resources:

```cpp
// Allocate
packet = av_packet_alloc();
frame = av_frame_alloc();
frame_rgb = av_frame_alloc();
buffer = av_malloc(num_bytes);

// Use...

// Free
av_packet_free(&packet);
av_frame_free(&frame);
av_frame_free(&frame_rgb);
av_free(buffer);
sws_freeContext(sws_ctx);
avcodec_free_context(&codec_ctx);
avformat_close_input(&format_ctx);
```

## Pixel Format Conversion

### Supported Input Formats

The decoder handles various pixel formats:
- YUV420P (most common)
- YUV422P
- YUV444P
- RGB24, BGR24
- NV12, NV21
- And many more...

### Scaling Algorithms

Available in `sws_getContext()`:
- `SWS_FAST_BILINEAR` - Fast but lower quality
- `SWS_BILINEAR` - Good balance (used in sample)
- `SWS_BICUBIC` - Higher quality, slower
- `SWS_LANCZOS` - Highest quality, slowest

## Performance Considerations

### Speed Optimization

1. **Limit Frame Count**
   - Only decode needed frames
   - Skip unnecessary frames with `av_seek_frame()`

2. **Hardware Acceleration**
   - Use hardware decoders when available
   - VideoToolbox (macOS), NVDEC (NVIDIA), etc.

3. **Threading**
   - FFmpeg automatic multi-threading
   - Set `codec_ctx->thread_count`

### Memory Usage

Typical memory per frame:
- 1920x1080 RGB24: ~6 MB
- 3840x2160 RGB24: ~24 MB

Memory usage = frame_size × buffered_frames

## Common Issues

### Issue: "No video stream found"

**Causes:**
- Audio-only file
- Corrupted video stream

**Solution:**
```bash
# Check streams with ffprobe
ffprobe input.mp4
```

### Issue: "Codec not found"

**Causes:**
- Unsupported codec
- Missing codec library

**Solution:**
```bash
# Check available decoders
ffmpeg -decoders | grep <codec_name>
```

### Issue: Output directory error

**Causes:**
- Directory doesn't exist
- No write permission

**Solution:**
```bash
mkdir -p output_dir
chmod 755 output_dir
```

### Issue: Out of memory

**Causes:**
- Too many frames at once
- High resolution video

**Solution:**
- Reduce `max_frames` parameter
- Process in batches
- Use compressed output format

## Advanced Usage

### Extracting Specific Frames

Modify the code to extract specific frame numbers:

```cpp
std::vector<int> target_frames = {0, 10, 25, 50, 100};
if (std::find(target_frames.begin(), target_frames.end(),
              frame_count) != target_frames.end()) {
    save_frame_as_ppm(frame_rgb, width, height, frame_count, output_dir);
}
```

### Seeking to Specific Time

```cpp
// Seek to 30 seconds
int64_t timestamp = 30 * AV_TIME_BASE;
av_seek_frame(format_ctx, -1, timestamp, AVSEEK_FLAG_BACKWARD);
```

### Frame Rate Control

Extract every Nth frame:

```cpp
if (frame_count % N == 0) {
    save_frame_as_ppm(frame_rgb, width, height,
                      frame_count / N, output_dir);
}
```

## Use Cases

1. **Thumbnail Generation**
   - Extract key frames for video previews
   - Create thumbnail galleries

2. **Video Analysis**
   - Frame-by-frame quality inspection
   - Motion analysis
   - Scene detection

3. **Computer Vision**
   - Training data preparation
   - Object detection datasets
   - Image preprocessing

4. **Animation Creation**
   - Extract frames for editing
   - Create sprite sheets
   - Generate frame sequences

5. **Quality Assurance**
   - Visual inspection of encoding artifacts
   - Compliance checking
   - Format verification

## Integration Examples

### Python Integration

```python
import subprocess
import os

def extract_frames(video_path, output_dir, max_frames=10):
    os.makedirs(output_dir, exist_ok=True)
    subprocess.run([
        './video_decoder',
        video_path,
        output_dir,
        str(max_frames)
    ])
```

### C++ Integration

```cpp
#include <cstdlib>

void process_video(const std::string& video_path) {
    std::string command = "./video_decoder " +
                         video_path + " frames/ 50";
    system(command.c_str());
}
```

## API Reference

### Key FFmpeg Functions

- `avformat_open_input()` - Open media file
- `avformat_find_stream_info()` - Read stream information
- `avcodec_find_decoder()` - Find decoder for codec
- `avcodec_alloc_context3()` - Allocate codec context
- `avcodec_parameters_to_context()` - Copy parameters
- `avcodec_open2()` - Initialize codec
- `av_read_frame()` - Read next packet
- `avcodec_send_packet()` - Send packet to decoder
- `avcodec_receive_frame()` - Receive decoded frame
- `sws_getContext()` - Initialize scaler
- `sws_scale()` - Scale and convert frame
- `av_frame_alloc()` - Allocate frame
- `av_packet_alloc()` - Allocate packet
- `av_image_get_buffer_size()` - Calculate buffer size
- `av_image_fill_arrays()` - Fill image plane pointers

## Troubleshooting

### Debugging Tips

1. **Enable FFmpeg Logging**
   ```cpp
   av_log_set_level(AV_LOG_DEBUG);
   ```

2. **Check Decoder Status**
   ```cpp
   if (ret < 0) {
       char errbuf[AV_ERROR_MAX_STRING_SIZE];
       av_strerror(ret, errbuf, sizeof(errbuf));
       std::cerr << "Error: " << errbuf << std::endl;
   }
   ```

3. **Verify Output Files**
   ```bash
   ls -lh frames/
   file frames/frame_0.ppm
   ```

## See Also

- [Video Encoder Documentation](video_encoder.md)
- [Video Filter Documentation](video_filter.md)
- [FFmpeg API Guide](ffmpeg_api.md)
- [Main README](../../README.md)
