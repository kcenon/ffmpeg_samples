# FFmpeg Multimedia Processing Samples

A collection of C++ sample applications demonstrating various video and audio processing capabilities using the FFmpeg library.

## Overview

This project contains ten sample applications that showcase different aspects of multimedia processing:

### Video Samples
1. **video_info** - Read and display video file metadata
2. **video_decoder** - Decode video frames and save as images
3. **video_encoder** - Encode generated frames into video files
4. **video_transcoder** - Convert videos between different formats and codecs
5. **video_filter** - Apply various video filters and effects

### Audio Samples
6. **audio_info** - Read and display audio file metadata
7. **audio_decoder** - Decode audio and save as WAV format
8. **audio_encoder** - Generate audio tones and encode to various formats
9. **audio_resampler** - Change audio sample rate and channel layout
10. **audio_mixer** - Mix two audio files with volume control

## Documentation

Comprehensive documentation is available in both English and Korean:

### English Documentation

**Video Processing:**
- [Video Info Guide](docs/en/video_info.md) - Reading video file information
- [Video Decoder Guide](docs/en/video_decoder.md) - Decoding frames to images
- [Video Encoder Guide](docs/en/video_encoder.md) - Encoding videos from frames
- [Video Transcoder Guide](docs/en/video_transcoder.md) - Converting video formats
- [Video Filter Guide](docs/en/video_filter.md) - Applying video filters and effects

**Audio Processing:**
- [Audio Samples Guide](docs/en/audio_samples.md) - Complete audio processing guide

**API Reference:**
- [FFmpeg API Reference](docs/en/ffmpeg_api.md) - Complete API documentation

### Korean Documentation (한국어 문서)

**비디오 처리:**
- [비디오 정보 가이드](docs/ko/video_info.md) - 비디오 파일 정보 읽기
- [비디오 디코더 가이드](docs/ko/video_decoder.md) - 프레임을 이미지로 디코딩
- [비디오 인코더 가이드](docs/ko/video_encoder.md) - 프레임에서 비디오 인코딩
- [비디오 트랜스코더 가이드](docs/ko/video_transcoder.md) - 비디오 포맷 변환
- [비디오 필터 가이드](docs/ko/video_filter.md) - 비디오 필터 및 효과 적용

**오디오 처리:**
- [오디오 샘플 가이드](docs/ko/audio_samples.md) - 완전한 오디오 처리 가이드

**API 참조:**
- [FFmpeg API 참조](docs/ko/ffmpeg_api.md) - 전체 API 문서

## Prerequisites

### macOS

```bash
brew install ffmpeg pkg-config cmake
```

### Ubuntu/Debian

```bash
sudo apt-get install ffmpeg libavcodec-dev libavformat-dev libavutil-dev \
                     libavfilter-dev libswscale-dev libswresample-dev \
                     pkg-config cmake build-essential
```

### Arch Linux

```bash
sudo pacman -S ffmpeg cmake pkgconf base-devel
```

## Building

```bash
cd ffmpeg_samples
mkdir -p build
cd build
cmake ..
make
```

The compiled executables will be in the `build` directory.

## Usage

### 1. Video Information Reader (video_info)

Display detailed information about a video file including codecs, resolution, frame rate, and duration.

```bash
./video_info <input_file>
```

**Example:**
```bash
./video_info sample.mp4
```

**Output:**
- File format and container information
- Duration and overall bitrate
- Stream information (video and audio)
- Video resolution, pixel format, frame rate
- Audio sample rate, channels, codec

### 2. Video Decoder (video_decoder)

Decode video frames and save them as PPM image files.

```bash
./video_decoder <input_file> <output_dir> [max_frames]
```

**Parameters:**
- `input_file` - Input video file path
- `output_dir` - Directory to save decoded frames
- `max_frames` - Maximum number of frames to decode (default: 10)

**Example:**
```bash
mkdir -p samples/frames
./video_decoder sample.mp4 samples/frames 100
```

**Features:**
- Decodes video frames to RGB format
- Saves frames as PPM images (easily viewable)
- Configurable frame limit
- Progress reporting

### 3. Video Encoder (video_encoder)

Generate animated test patterns and encode them into a video file.

```bash
./video_encoder <output_file> [num_frames] [width] [height] [fps]
```

**Parameters:**
- `output_file` - Output video file path
- `num_frames` - Number of frames to generate (default: 100)
- `width` - Video width in pixels (default: 1280)
- `height` - Video height in pixels (default: 720)
- `fps` - Frame rate (default: 30)

**Example:**
```bash
./video_encoder samples/test_video.mp4 300 1920 1080 60
```

**Features:**
- H.264 codec encoding
- Configurable resolution and frame rate
- Generates animated test patterns
- Progress reporting during encoding

### 4. Video Transcoder (video_transcoder)

Convert videos between different formats, resolutions, and bitrates.

```bash
./video_transcoder <input_file> <output_file> [width] [height] [bitrate] [fps]
```

**Parameters:**
- `input_file` - Input video file path
- `output_file` - Output video file path
- `width` - Output width in pixels (default: 1280)
- `height` - Output height in pixels (default: 720)
- `bitrate` - Output bitrate in bits per second (default: 2000000)
- `fps` - Output frame rate (default: 30)

**Example:**
```bash
./video_transcoder input.avi output.mp4 1920 1080 5000000 30
```

**Features:**
- Format conversion (AVI, MP4, MKV, etc.)
- Resolution scaling
- Bitrate adjustment
- Frame rate conversion
- Progress reporting

### 5. Video Filter (video_filter)

Apply various video filters and effects to video files.

```bash
./video_filter <input_file> <output_file> <filter_type>
```

**Available Filters:**
- `grayscale` - Convert to grayscale
- `blur` - Apply Gaussian blur
- `sharpen` - Sharpen the video
- `rotate` - Rotate 90 degrees clockwise
- `flip_h` - Flip horizontally
- `flip_v` - Flip vertically
- `brightness` - Increase brightness
- `contrast` - Increase contrast
- `edge` - Edge detection
- `negative` - Create negative image
- `custom` - Custom combined filters

**Examples:**
```bash
./video_filter input.mp4 output_gray.mp4 grayscale
./video_filter input.mp4 output_blur.mp4 blur
./video_filter input.mp4 output_edge.mp4 edge
```

**Features:**
- Multiple built-in filter presets
- FFmpeg filter graph API
- Easily extensible for custom filters
- Real-time progress reporting

## Sample Workflow

Here is a complete workflow example:

```bash
# 1. Generate a test video
./video_encoder samples/test.mp4 150 1280 720 30

# 2. Check video information
./video_info samples/test.mp4

# 3. Apply a filter effect
./video_filter samples/test.mp4 samples/filtered.mp4 blur

# 4. Transcode to different resolution
./video_transcoder samples/filtered.mp4 samples/final.mp4 640 480 1000000 24

# 5. Extract some frames as images
mkdir -p samples/frames
./video_decoder samples/final.mp4 samples/frames 10
```

## Project Structure

```
ffmpeg_samples/
├── CMakeLists.txt          # CMake build configuration
├── README.md               # This file
├── .gitignore             # Git ignore rules
├── src/                   # Source files
│   ├── video_info.cpp     # Video information reader
│   ├── video_decoder.cpp  # Video frame decoder
│   ├── video_encoder.cpp  # Video encoder
│   ├── video_transcoder.cpp # Video transcoder
│   └── video_filter.cpp   # Video filter application
├── include/               # Header files (if needed)
├── build/                 # Build directory (generated)
└── samples/               # Sample videos and output (user-created)
```

## FFmpeg API Overview

### Key Components Used

1. **libavformat** - Container format handling (MP4, AVI, MKV, etc.)
   - Opening/closing files
   - Reading/writing packets
   - Stream management

2. **libavcodec** - Codec encoding/decoding
   - H.264, H.265, VP9, etc.
   - Frame encoding/decoding
   - Codec parameter configuration

3. **libavutil** - Utility functions
   - Memory management
   - Error handling
   - Mathematical operations

4. **libavfilter** - Video filtering
   - Filter graph creation
   - Complex filter chains
   - Various video effects

5. **libswscale** - Video scaling and color conversion
   - Pixel format conversion
   - Resolution scaling
   - Color space transformation

6. **libswresample** - Audio resampling (used in transcoder)

## Common Issues and Solutions

### Issue: FFmpeg libraries not found

**Solution:**
```bash
# macOS
brew install ffmpeg pkg-config

# Ensure pkg-config can find FFmpeg
export PKG_CONFIG_PATH="/usr/local/opt/ffmpeg/lib/pkgconfig:$PKG_CONFIG_PATH"
```

### Issue: Output directory doesn't exist

**Solution:**
Create the output directory before running the decoder:
```bash
mkdir -p samples/frames
```

### Issue: Codec not found

**Solution:**
Make sure FFmpeg is compiled with the required codec support:
```bash
ffmpeg -codecs | grep h264
```

## Advanced Usage

### Custom Filters

To add custom filters, modify `video_filter.cpp` and add new filter descriptions in the `get_filter_description()` function:

```cpp
} else if (strcmp(filter_type, "myfilter") == 0) {
    return "your_filter_expression_here";
}
```

FFmpeg filter documentation: https://ffmpeg.org/ffmpeg-filters.html

### Combining Multiple Filters

You can chain multiple filters together using commas:

```cpp
return "hue=s=0,eq=brightness=0.2,unsharp=5:5:1.0";
```

## Performance Considerations

1. **Hardware Acceleration** - Consider using hardware encoders (e.g., VideoToolbox on macOS, NVENC on NVIDIA GPUs)
2. **Thread Count** - Increase encoder thread count for better performance
3. **Preset Selection** - Use faster presets for real-time processing
4. **Memory Management** - Properly release FFmpeg resources to prevent memory leaks

## Learning Resources

- [FFmpeg Official Documentation](https://ffmpeg.org/documentation.html)
- [FFmpeg API Documentation](https://ffmpeg.org/doxygen/trunk/index.html)
- [FFmpeg Wiki](https://trac.ffmpeg.org/wiki)

## License

This project is intended for educational purposes. FFmpeg itself is licensed under the LGPL or GPL depending on configuration.

## Contributing

Feel free to add more samples or improve existing ones. Common additions might include:

- Audio processing samples
- Subtitle handling
- Network streaming
- Hardware acceleration examples
- More complex filter combinations
- Multi-threaded processing examples

## Troubleshooting

For issues or questions:

1. Check FFmpeg installation: `ffmpeg -version`
2. Verify library paths: `pkg-config --libs libavcodec`
3. Ensure input files are valid: `ffprobe input.mp4`
4. Check build output for specific error messages

## Author

Created as a comprehensive FFmpeg learning resource for C++ developers.
