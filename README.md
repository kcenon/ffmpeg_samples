# FFmpeg Multimedia Processing Samples

A comprehensive collection of **35 modern C++20 sample applications** demonstrating video and audio processing using the FFmpeg library. Perfect for beginners and professionals alike!

## 🌟 What is this?

This project provides ready-to-use examples for common multimedia tasks:
- Convert videos between formats
- Extract audio from videos
- Create GIFs from videos
- Add subtitles and watermarks
- Apply audio compression and effects
- Generate thumbnails and waveforms
- Split audio by silence detection
- And much more!

## ✨ Key Features

- **🎓 Beginner Friendly** - Clear examples with detailed comments
- **⚡ Modern C++20** - Uses latest C++ features (RAII, smart pointers, std::format)
- **🛡️ Safe & Robust** - Automatic memory management, proper error handling
- **📚 35 Complete Samples** - Covering video, audio, and streaming
- **🌍 Bilingual Docs** - Full documentation in English and Korean
- **🔧 Production Ready** - Battle-tested code you can use in real projects

## 📋 Table of Contents

- [Quick Start](#-quick-start)
- [Sample Catalog](#-sample-catalog)
- [Installation Guide](#-installation-guide)
- [Usage Examples](#-usage-examples)
- [Project Structure](#-project-structure)
- [How It Works](#-how-it-works)
- [Troubleshooting](#-troubleshooting)
- [Contributing](#-contributing)

## 🚀 Quick Start

### 1. Install Dependencies

**macOS:**
```bash
brew install ffmpeg pkg-config cmake
```

**Ubuntu/Debian:**
```bash
sudo apt-get install ffmpeg libavcodec-dev libavformat-dev libavutil-dev \
                     libavfilter-dev libswscale-dev libswresample-dev \
                     pkg-config cmake build-essential
```

**Arch Linux:**
```bash
sudo pacman -S ffmpeg cmake pkgconf base-devel
```

### 2. Build the Project

```bash
# Clone the repository (if you haven't already)
cd ffmpeg_samples

# Create build directory
mkdir -p build
cd build

# Configure and build
cmake ..
make -j$(nproc)  # Use all CPU cores for faster build
```

### 3. Run Your First Sample

```bash
# Get video information
./video_info your_video.mp4

# Create a GIF from a video
./video_gif_creator your_video.mp4 output.gif -s 480x270 -r 15

# Extract keyframes as thumbnails
./video_keyframe_extract your_video.mp4 keyframes/ --thumbnails
```

## 📚 Sample Catalog

### 🎬 Video Processing (20 samples)

| Sample | Description | Difficulty |
|--------|-------------|------------|
| `video_info` | Display video metadata (codec, resolution, duration) | ⭐ Easy |
| `video_decoder` | Extract frames from video as images | ⭐ Easy |
| `video_encoder` | Create video from images/patterns | ⭐⭐ Medium |
| `video_transcoder` | Convert between formats (MP4, AVI, MKV) | ⭐⭐ Medium |
| `video_filter` | Apply filters (grayscale, blur, rotate, etc.) | ⭐⭐ Medium |
| `video_thumbnail` | Generate video thumbnails | ⭐ Easy |
| `video_metadata` | Edit video metadata tags | ⭐ Easy |
| `video_subtitles` | Extract/burn subtitles | ⭐⭐ Medium |
| `video_watermark` | Add text/image watermarks | ⭐⭐ Medium |
| `video_splitter` | Split/merge videos | ⭐⭐ Medium |
| `video_slideshow` | Create slideshow from images | ⭐⭐ Medium |
| `video_stabilization` | Stabilize shaky footage | ⭐⭐⭐ Advanced |
| `video_transition` | Apply transitions (fade, wipe, slide) | ⭐⭐⭐ Advanced |
| `video_concatenate` | Merge multiple videos | ⭐⭐ Medium |
| `subtitle_generator` | Generate SRT/VTT/ASS subtitles | ⭐⭐ Medium |
| `video_reverse` | Play video backwards | ⭐⭐ Medium |
| `video_crop_rotate` | Crop and rotate videos | ⭐⭐ Medium |
| `video_speed_control` | Change playback speed (slow-mo, fast) | ⭐⭐⭐ Advanced |
| `video_gif_creator` | Create optimized GIFs | ⭐⭐ Medium |
| `video_keyframe_extract` | Extract I-frames/keyframes | ⭐⭐ Medium |

### 🎵 Audio Processing (14 samples)

| Sample | Description | Difficulty |
|--------|-------------|------------|
| `audio_info` | Display audio metadata | ⭐ Easy |
| `audio_decoder` | Extract audio as WAV | ⭐ Easy |
| `audio_encoder` | Generate audio tones | ⭐⭐ Medium |
| `audio_resampler` | Change sample rate/channels | ⭐⭐ Medium |
| `audio_mixer` | Mix multiple audio files | ⭐⭐ Medium |
| `audio_noise_reduction` | Remove background noise | ⭐⭐⭐ Advanced |
| `audio_format_converter` | Convert audio formats | ⭐ Easy |
| `audio_spectrum` | Create spectrum visualization | ⭐⭐ Medium |
| `audio_equalizer` | Apply multi-band EQ | ⭐⭐⭐ Advanced |
| `audio_transition` | Crossfade between tracks | ⭐⭐ Medium |
| `audio_silence_detect` | Detect silent segments | ⭐⭐ Medium |
| `audio_waveform` | Create waveform visualization | ⭐⭐ Medium |
| `audio_compressor` | Dynamic range compression | ⭐⭐⭐ Advanced |
| `audio_splitter` | Split audio by silence detection | ⭐⭐ Medium |

### 📡 Streaming (1 sample)

| Sample | Description | Difficulty |
|--------|-------------|------------|
| `streaming_server` | Stream video over network | ⭐⭐⭐ Advanced |

## 💻 Installation Guide

### Prerequisites

Before you begin, ensure you have:
- **C++ Compiler** with C++20 support (GCC 10+, Clang 10+, or MSVC 2019+)
- **CMake** version 3.15 or higher
- **FFmpeg** libraries (4.0 or higher recommended)
- **pkg-config** for library detection

### Detailed Installation Steps

#### macOS

1. **Install Homebrew** (if not already installed):
   ```bash
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   ```

2. **Install dependencies**:
   ```bash
   brew install ffmpeg pkg-config cmake
   ```

3. **Verify installation**:
   ```bash
   ffmpeg -version
   cmake --version
   ```

#### Ubuntu/Debian

1. **Update package list**:
   ```bash
   sudo apt-get update
   ```

2. **Install dependencies**:
   ```bash
   sudo apt-get install -y \
       ffmpeg \
       libavcodec-dev \
       libavformat-dev \
       libavutil-dev \
       libavfilter-dev \
       libswscale-dev \
       libswresample-dev \
       pkg-config \
       cmake \
       build-essential
   ```

3. **Verify installation**:
   ```bash
   ffmpeg -version
   g++ --version
   cmake --version
   ```

#### Arch Linux

1. **Install dependencies**:
   ```bash
   sudo pacman -S ffmpeg cmake pkgconf base-devel
   ```

2. **Verify installation**:
   ```bash
   ffmpeg -version
   cmake --version
   ```

### Building the Project

```bash
# Navigate to project directory
cd ffmpeg_samples

# Create and enter build directory
mkdir -p build
cd build

# Configure the project
cmake ..

# Build all samples (use -j for parallel builds)
make -j$(nproc)

# Or build a specific sample
make video_info

# Optional: Install to system
sudo make install
```

## 📖 Usage Examples

### Example 1: Basic Video Information

Get detailed information about any video file:

```bash
./video_info movie.mp4
```

**Output:**
```
Format: mov,mp4,m4a,3gp,3g2,mj2
Duration: 120.5 seconds
Bit Rate: 5000000 bps
Video Stream #0:
  Codec: h264
  Resolution: 1920x1080
  Frame Rate: 30 fps
  Pixel Format: yuv420p
Audio Stream #1:
  Codec: aac
  Sample Rate: 48000 Hz
  Channels: 2 (stereo)
```

### Example 2: Create a GIF from Video

Convert a video clip to an optimized GIF:

```bash
# Basic usage - entire video
./video_gif_creator input.mp4 output.gif

# Custom size and frame rate
./video_gif_creator input.mp4 output.gif -s 640x360 -r 15

# Extract 5 seconds starting at 10s
./video_gif_creator input.mp4 output.gif -ss 10 -t 5

# High quality with more colors
./video_gif_creator input.mp4 output.gif --colors 256 -q 95
```

### Example 3: Speed Control

Change video playback speed:

```bash
# Half speed (slow motion)
./video_speed_control input.mp4 output.mp4 0.5

# Double speed (fast forward)
./video_speed_control input.mp4 output.mp4 2.0

# Slow video only, keep audio normal
./video_speed_control input.mp4 output.mp4 1.0 --video 0.5 --audio 1.0
```

### Example 4: Extract Keyframes

Extract keyframes for thumbnails or preview:

```bash
# Extract all keyframes as JPEG
./video_keyframe_extract video.mp4 keyframes/

# First 10 keyframes as PNG
./video_keyframe_extract video.mp4 frames/ -f png -n 10

# Every 5th keyframe with thumbnails
./video_keyframe_extract video.mp4 output/ -i 5 --thumbnails

# High quality with metadata
./video_keyframe_extract video.mp4 frames/ -q 95 --info
```

### Example 5: Audio Compression

Apply dynamic range compression to audio:

```bash
# Default compression
./audio_compressor input.wav output.wav

# Podcast preset
./audio_compressor audio.mp3 compressed.mp3 -p podcast

# Custom settings
./audio_compressor input.wav output.wav -t -15 -r 6 -a 10 -R 200

# Music mastering with makeup gain
./audio_compressor music.flac output.flac -p mastering -m 2
```

### Example 6: Video Concatenation

Merge multiple videos:

```bash
# Merge two videos
./video_concatenate output.mp4 video1.mp4 video2.mp4

# Merge multiple videos
./video_concatenate merged.mp4 clip1.mp4 clip2.mp4 clip3.mp4
```

### Example 7: Subtitle Generation

Create subtitle files:

```bash
# Manual entry mode
./subtitle_generator subtitles.srt manual

# Auto-generate from text file
./subtitle_generator output.vtt auto script.txt 3.0

# Generate template
./subtitle_generator captions.srt template dialogue.txt 0.0 2.5
```

### Example 8: Audio Waveform Visualization

Create audio waveform videos:

```bash
# Basic waveform video
./audio_waveform audio.mp3 waveform.mp4

# Stereo with custom colors
./audio_waveform audio.wav output.mp4 -m cline -c "red|green"

# Static waveform image
./audio_waveform audio.mp3 waveform.png --static -s 1920x1080

# Split channels
./audio_waveform input.wav output.mp4 --split --scale sqrt
```

### Example 9: Audio Splitting

Split audio files based on silence detection:

```bash
# Basic splitting
./audio_splitter audio.mp3

# Custom threshold and duration
./audio_splitter podcast.wav -t -35 -s 1.0 -m 5.0

# Custom output directory and prefix
./audio_splitter interview.m4a -o output -p part

# Fine-tuned splitting for quiet audio
./audio_splitter audio.mp3 -t -50 -s 0.3 -m 2.0 -o segments -p segment
```

## 🏗️ Project Structure

```
ffmpeg_samples/
├── CMakeLists.txt           # Build configuration
├── README.md                # This file
├── include/
│   └── ffmpeg_wrappers.hpp  # RAII wrappers for FFmpeg C API
├── src/
│   ├── video/               # Video processing samples
│   │   ├── video_info.cpp
│   │   ├── video_decoder.cpp
│   │   ├── video_encoder.cpp
│   │   ├── video_transcoder.cpp
│   │   ├── video_filter.cpp
│   │   ├── video_thumbnail.cpp
│   │   ├── video_metadata.cpp
│   │   ├── video_subtitles.cpp
│   │   ├── video_watermark.cpp
│   │   ├── video_splitter.cpp
│   │   ├── video_slideshow.cpp
│   │   ├── video_stabilization.cpp
│   │   ├── video_transition.cpp
│   │   ├── video_concatenate.cpp
│   │   ├── subtitle_generator.cpp
│   │   ├── video_reverse.cpp
│   │   ├── video_crop_rotate.cpp
│   │   ├── video_speed_control.cpp
│   │   ├── video_gif_creator.cpp
│   │   └── video_keyframe_extract.cpp
│   ├── audio/               # Audio processing samples
│   │   ├── audio_info.cpp
│   │   ├── audio_decoder.cpp
│   │   ├── audio_encoder.cpp
│   │   ├── audio_resampler.cpp
│   │   ├── audio_mixer.cpp
│   │   ├── audio_noise_reduction.cpp
│   │   ├── audio_format_converter.cpp
│   │   ├── audio_spectrum.cpp
│   │   ├── audio_equalizer.cpp
│   │   ├── audio_transition.cpp
│   │   ├── audio_silence_detect.cpp
│   │   ├── audio_waveform.cpp
│   │   ├── audio_compressor.cpp
│   │   └── audio_splitter.cpp
│   └── streaming/
│       └── streaming_server.cpp
├── docs/                    # Documentation
│   ├── en/                  # English documentation
│   └── ko/                  # Korean documentation
└── build/                   # Build output (created by CMake)
    ├── video_info
    ├── video_decoder
    ├── audio_compressor
    └── ... (all executables)
```

## 🔧 How It Works

### Architecture Overview

Each sample follows a consistent architecture:

1. **Command-line parsing** - Parse user input and validate parameters
2. **FFmpeg initialization** - Open input files and detect streams
3. **Decoder setup** - Configure decoder for input format
4. **Processing** - Apply filters, effects, or transformations
5. **Encoder setup** - Configure encoder for output format
6. **Output writing** - Write processed data to output file
7. **Cleanup** - Automatic resource cleanup via RAII

### Code Structure Example

Here's a simplified example of the typical structure:

```cpp
#include "ffmpeg_wrappers.hpp"

int main(int argc, char* argv[]) {
    try {
        // 1. Parse command line
        auto params = parse_arguments(argc, argv);

        // 2. Open input (RAII - auto cleanup)
        auto input = ffmpeg::open_input_format(input_file);

        // 3. Setup decoder
        auto decoder = setup_decoder(input);

        // 4. Setup encoder
        auto encoder = setup_encoder(output_file);

        // 5. Process frames
        while (auto frame = read_frame(input, decoder)) {
            auto processed = apply_processing(frame);
            write_frame(encoder, processed);
        }

        // 6. Automatic cleanup on scope exit

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
```

### RAII Wrappers

The project uses custom RAII wrappers to manage FFmpeg resources safely:

```cpp
// Smart pointers automatically free FFmpeg resources
ffmpeg::FormatContextPtr format_ctx_;    // AVFormatContext*
ffmpeg::CodecContextPtr codec_ctx_;      // AVCodecContext*
ffmpeg::FramePtr frame_;                 // AVFrame*
ffmpeg::PacketPtr packet_;               // AVPacket*
ffmpeg::SwsContextPtr sws_ctx_;          // SwsContext*
ffmpeg::FilterGraphPtr filter_graph_;    // AVFilterGraph*

// No manual cleanup needed - handled automatically!
```

### Key Concepts

#### 1. Containers vs Codecs

- **Container** (MP4, AVI, MKV) - The file format that holds video/audio
- **Codec** (H.264, VP9, AAC) - The compression algorithm

```bash
# Same codec, different containers
./video_transcoder input.mp4 output.avi  # H.264 in AVI
./video_transcoder input.mp4 output.mkv  # H.264 in MKV
```

#### 2. Streams

Videos typically have multiple streams:
- Video stream (pictures)
- Audio stream (sound)
- Subtitle stream (text)

#### 3. Frames and Packets

- **Frame** - Uncompressed data (ready to process)
- **Packet** - Compressed data (from file)

```
File → Packets → Decoder → Frames → Processing → Encoder → Packets → File
```

## 🐛 Troubleshooting

### Common Issues

#### 1. "command not found" when running samples

**Problem:** Executables not in PATH

**Solution:**
```bash
# Run from build directory
cd build
./video_info input.mp4

# Or add to PATH
export PATH="$PWD/build:$PATH"
```

#### 2. "Failed to open input file"

**Problem:** File doesn't exist or unsupported format

**Solution:**
```bash
# Check file exists
ls -lh input.mp4

# Test with ffmpeg directly
ffmpeg -i input.mp4

# Try different file
./video_info /path/to/video.mp4
```

#### 3. Build errors about missing FFmpeg headers

**Problem:** FFmpeg development libraries not installed

**Solution:**
```bash
# Ubuntu/Debian
sudo apt-get install libavcodec-dev libavformat-dev libavutil-dev

# macOS
brew install ffmpeg

# Verify
pkg-config --modversion libavcodec
```

#### 4. Codec not found errors

**Problem:** FFmpeg built without certain codec support

**Solution:**
```bash
# Check available encoders
ffmpeg -encoders | grep -i h264

# Use different codec
./video_encoder output.mp4  # Try different format
```

#### 5. Permission denied

**Problem:** No write permission for output directory

**Solution:**
```bash
# Check permissions
ls -la output_directory/

# Create directory with proper permissions
mkdir -p output_directory
chmod 755 output_directory
```

### Getting Help

If you encounter issues:

1. **Check sample usage**: Run without arguments for help
   ```bash
   ./video_info  # Shows usage
   ```

2. **Enable verbose FFmpeg logging**:
   ```bash
   export AV_LOG_FORCE_NOCOLOR=1
   export FFREPORT=file=ffmpeg_log.txt:level=32
   ```

3. **Test with FFmpeg command line**:
   ```bash
   ffmpeg -i input.mp4  # Test file reading
   ```

4. **Check FFmpeg version**:
   ```bash
   ffmpeg -version
   pkg-config --modversion libavcodec
   ```

## 📝 Common Use Cases

### Use Case 1: Create Social Media Content

```bash
# Instagram Stories (1080x1920)
./video_crop_rotate input.mp4 story.mp4 --crop 0:0:1080:1920 --rotate 90

# Instagram Feed (1080x1080)
./video_crop_rotate input.mp4 feed.mp4 --crop 0:140:1080:1080

# Twitter optimized
./video_transcoder input.mp4 twitter.mp4 1280 720 2000000 30
```

### Use Case 2: Podcast Production

```bash
# 1. Compress audio
./audio_compressor raw_audio.wav compressed.wav -p podcast

# 2. Remove noise
./audio_noise_reduction compressed.wav clean.wav -p podcast

# 3. Create waveform video
./audio_waveform clean.wav podcast_video.mp4 -m cline -c blue

# 4. Add subtitles
./subtitle_generator podcast.srt auto transcript.txt 3.0
```

### Use Case 3: Video Archive Conversion

```bash
# Batch convert old videos to modern format
for video in *.avi; do
    ./video_transcoder "$video" "${video%.avi}.mp4" 1920 1080 5000000 30
done
```

### Use Case 4: Create Video Preview

```bash
# Extract keyframes
./video_keyframe_extract long_video.mp4 keyframes/ -n 20 --thumbnails

# Create GIF preview
./video_gif_creator long_video.mp4 preview.gif -ss 30 -t 3 -s 480x270
```

## 🎓 Learning Resources

### For Beginners

1. **Start with simple samples**:
   - `video_info` - Learn about video properties
   - `video_decoder` - Understand frame extraction
   - `audio_info` - Learn about audio properties

2. **Progress to processing**:
   - `video_filter` - Apply visual effects
   - `audio_mixer` - Combine audio files
   - `video_gif_creator` - Create animations

3. **Advanced techniques**:
   - `video_transition` - Complex filter graphs
   - `audio_compressor` - Audio dynamics
   - `video_speed_control` - Temporal manipulation

### FFmpeg Resources

- [FFmpeg Official Documentation](https://ffmpeg.org/documentation.html)
- [FFmpeg Wiki](https://trac.ffmpeg.org/wiki)
- [FFmpeg Filters Documentation](https://ffmpeg.org/ffmpeg-filters.html)

### C++20 Resources

- [C++ Reference](https://en.cppreference.com/)
- [Modern C++ Features](https://github.com/AnthonyCalandra/modern-cpp-features)

## 🤝 Contributing

Contributions are welcome! Here's how you can help:

1. **Report bugs** - Open an issue with details
2. **Suggest features** - Describe new sample ideas
3. **Improve documentation** - Fix typos, add examples
4. **Submit code** - Follow the existing code style

### Code Style Guidelines

- Use Modern C++20 features
- Follow RAII principles
- Use smart pointers for FFmpeg resources
- Add comments for complex logic
- Include usage examples in comments

## 📄 License

This project is open source and available for educational and commercial use.

## 🙏 Acknowledgments

- FFmpeg team for the excellent multimedia library
- Contributors to this project
- Community for feedback and suggestions

## 📞 Support

For questions and support:
- Open an issue on GitHub
- Check existing documentation
- Review sample code comments

---

**Happy Multimedia Processing! 🎬🎵**
