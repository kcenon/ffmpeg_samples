# FFmpeg Multimedia Processing Samples

A collection of modern C++20 sample applications demonstrating various video and audio processing capabilities using the FFmpeg library.

## Key Features

- ✨ **Modern C++20** - Uses RAII wrappers, std::span, std::string_view, and structured bindings
- 🛡️ **Exception Safety** - Automatic resource management with smart pointers
- 📚 **Comprehensive** - 14 samples covering video and audio processing
- 🌍 **Bilingual Docs** - Complete documentation in English and Korean
- 🎯 **Production Ready** - Proper error handling and resource management

## Overview

This project contains fourteen sample applications that showcase different aspects of multimedia processing:

### Video Samples
1. **video_info** - Read and display video file metadata
2. **video_decoder** - Decode video frames and save as images
3. **video_encoder** - Encode generated frames into video files
4. **video_transcoder** - Convert videos between different formats and codecs
5. **video_filter** - Apply various video filters and effects
6. **video_thumbnail** - Generate video thumbnails and preview images
7. **video_metadata** - Edit and manage video metadata tags

### Audio Samples
8. **audio_info** - Read and display audio file metadata
9. **audio_decoder** - Decode audio and save as WAV format
10. **audio_encoder** - Generate audio tones and encode to various formats
11. **audio_resampler** - Change audio sample rate and channel layout
12. **audio_mixer** - Mix two audio files with volume control
13. **audio_noise_reduction** - Apply noise reduction and audio enhancement
14. **audio_format_converter** - Convert audio files between different formats

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

### 6. Video Thumbnail Generator (video_thumbnail)

Generate thumbnail images from video files with multiple modes.

```bash
./video_thumbnail <input_video> <mode> [options]
```

**Modes:**

**Time Mode** - Extract frame at specific timestamp:
```bash
./video_thumbnail video.mp4 time 30.5 thumb.jpg 90
```

**Grid Mode** - Generate multiple thumbnails:
```bash
./video_thumbnail video.mp4 grid 10 thumbnails 85
```

**Best Mode** - Automatically find and save best frame:
```bash
./video_thumbnail video.mp4 best thumbnail.jpg 95
```

**Parameters:**
- `input_video` - Input video file path
- `mode` - One of: time, grid, best
- For time mode: `<seconds> <output_file> [quality]`
- For grid mode: `<count> <output_dir> [quality]`
- For best mode: `<output_file> [quality]`
- `quality` - JPEG quality 1-100 (default: 85)

**Features:**
- Multiple extraction modes
- JPEG and PNG output support
- Quality control for JPEG
- Automatic frame quality analysis
- Grid generation for video preview

### 7. Video Metadata Editor (video_metadata)

Read and edit video file metadata without re-encoding.

```bash
./video_metadata <command> <input_file> [options]
```

**Commands:**

**Show** - Display all metadata:
```bash
./video_metadata show video.mp4
```

**Get** - Get specific metadata value:
```bash
./video_metadata get video.mp4 title
```

**Set** - Set metadata value:
```bash
./video_metadata set video.mp4 output.mp4 title "My Video"
./video_metadata set video.mp4 output.mp4 artist "Artist Name"
```

**Remove** - Remove metadata key:
```bash
./video_metadata remove video.mp4 output.mp4 comment
```

**Clear** - Remove all metadata:
```bash
./video_metadata clear video.mp4 output.mp4
```

**Common Metadata Keys:**
- title, artist, album, date, genre, comment
- copyright, description, language, encoder
- author, composer

**Features:**
- Fast metadata updates (no re-encoding)
- Preserves video and audio quality
- Supports all standard metadata keys
- Display stream information
- Copy protection preservation

## Audio Samples Usage

### 8. Audio Noise Reduction (audio_noise_reduction)

Apply noise reduction and audio enhancement filters.

```bash
./audio_noise_reduction <input_file> <output_file> <preset>
```

**Available Presets:**
- `light` - Light noise reduction, preserves quality
- `medium` - Balanced noise reduction
- `heavy` - Aggressive noise reduction
- `voice` - Optimized for voice recordings
- `music` - Optimized for music
- `podcast` - Full processing (denoise + normalize + compress)
- `denoise_only` - Only apply denoising filter
- `normalize` - Only apply loudness normalization
- `compress` - Only apply dynamic range compression

**Examples:**
```bash
./audio_noise_reduction noisy_audio.mp3 clean_audio.wav voice
./audio_noise_reduction podcast.wav enhanced.wav podcast
./audio_noise_reduction music.flac cleaned.wav light
```

**Features:**
- Multiple noise reduction presets
- Loudness normalization
- Dynamic range compression
- High-pass and low-pass filtering
- Optimized for different content types
- WAV output format

### 9. Audio Format Converter (audio_format_converter)

Convert audio files between different formats with quality control.

```bash
./audio_format_converter <input_file> <output_file> [bitrate] [sample_rate] [channels]
```

**Supported Formats:**
- MP3 (.mp3) - MPEG Audio Layer 3
- AAC (.aac, .m4a) - Advanced Audio Coding
- OGG (.ogg) - Ogg Vorbis
- Opus (.opus) - Opus codec
- FLAC (.flac) - Free Lossless Audio Codec
- WAV (.wav) - Waveform Audio File
- WMA (.wma) - Windows Media Audio

**Parameters:**
- `input_file` - Input audio file
- `output_file` - Output file (format determined by extension)
- `bitrate` - Target bitrate in bps (0 = default, optional)
- `sample_rate` - Target sample rate in Hz (0 = default, optional)
- `channels` - Number of channels (0 = default, optional)

**Examples:**
```bash
# Convert MP3 to FLAC (lossless)
./audio_format_converter input.mp3 output.flac

# Convert WAV to MP3 with specific bitrate
./audio_format_converter input.wav output.mp3 320000

# Convert to AAC with custom settings
./audio_format_converter input.flac output.m4a 256000 48000 2

# Convert to Opus (efficient for voice)
./audio_format_converter podcast.wav podcast.opus 96000 48000 1
```

**Features:**
- Support for 8 popular audio formats
- Configurable bitrate, sample rate, channels
- Automatic format detection from extension
- Lossless conversion support (FLAC, WAV)
- High-quality resampling
- Metadata preservation

## Sample Workflows

### Video Processing Workflow

Here is a complete video processing workflow:

```bash
# 1. Generate a test video
./video_encoder samples/test.mp4 150 1280 720 30

# 2. Check video information
./video_info samples/test.mp4

# 3. Add metadata
./video_metadata set samples/test.mp4 samples/test_meta.mp4 title "Test Video" artist "FFmpeg Samples"

# 4. Generate thumbnails
mkdir -p samples/thumbnails
./video_thumbnail samples/test_meta.mp4 grid 5 samples/thumbnails 85

# 5. Apply a filter effect
./video_filter samples/test_meta.mp4 samples/filtered.mp4 blur

# 6. Transcode to different resolution
./video_transcoder samples/filtered.mp4 samples/final.mp4 640 480 1000000 24

# 7. Extract some frames as images
mkdir -p samples/frames
./video_decoder samples/final.mp4 samples/frames 10
```

### Audio Processing Workflow

Here is a complete audio processing workflow:

```bash
# 1. Check audio information
./audio_info samples/input.mp3

# 2. Apply noise reduction
./audio_noise_reduction samples/input.mp3 samples/cleaned.wav voice

# 3. Convert format with quality settings
./audio_format_converter samples/cleaned.wav samples/output.m4a 256000 48000 2

# 4. Mix with background music
./audio_mixer samples/output.m4a samples/music.mp3 samples/mixed.wav 0.8 0.2

# 5. Resample for different use case
./audio_resampler samples/mixed.wav samples/final.wav 44100 2
```

## Project Structure

```
ffmpeg_samples/
├── CMakeLists.txt               # CMake build configuration
├── README.md                    # This file
├── .gitignore                  # Git ignore rules
├── src/                        # Source files
│   ├── video_info.cpp          # Video information reader
│   ├── video_decoder.cpp       # Video frame decoder
│   ├── video_encoder.cpp       # Video encoder
│   ├── video_transcoder.cpp    # Video transcoder
│   ├── video_filter.cpp        # Video filter application
│   ├── video_thumbnail.cpp     # Thumbnail generator
│   ├── video_metadata.cpp      # Metadata editor
│   ├── audio_info.cpp          # Audio information reader
│   ├── audio_decoder.cpp       # Audio decoder
│   ├── audio_encoder.cpp       # Audio encoder
│   ├── audio_resampler.cpp     # Audio resampler
│   ├── audio_mixer.cpp         # Audio mixer
│   ├── audio_noise_reduction.cpp    # Noise reduction
│   └── audio_format_converter.cpp   # Format converter
├── include/                    # Header files
│   └── ffmpeg_wrappers.hpp    # FFmpeg RAII wrappers
├── build/                      # Build directory (generated)
└── samples/                    # Sample videos and output (user-created)
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

## Roadmap

### Phase 2 (Medium-term)
- **video_subtitles** - Subtitle processing and embedding
- **video_watermark** - Watermark addition and positioning
- **audio_spectrum** - Audio spectrum visualization
- **audio_equalizer** - Multi-band equalizer

### Phase 3 (Long-term)
- **video_splitter** - Video splitting and merging
- **video_slideshow** - Slideshow generator from images
- **video_stabilization** - Video stabilization
- **streaming_server** - Basic streaming server

## Contributing

Feel free to add more samples or improve existing ones. Common additions might include:

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
