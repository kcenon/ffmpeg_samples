# FFmpeg Multimedia Processing Samples

A comprehensive collection of **52 modern C++20 sample applications** demonstrating video and audio processing using the FFmpeg library. Perfect for beginners and professionals alike!

## 🌟 What is this?

This project provides ready-to-use examples for common multimedia tasks:
- Convert videos between formats
- Extract audio from videos
- Create GIFs from videos
- Add subtitles and watermarks
- Apply audio compression and effects
- Generate thumbnails and waveforms
- Split audio by silence detection
- Normalize audio levels (peak/loudness)
- Apply true peak limiting
- And much more!

## ✨ Key Features

- **🎓 Beginner Friendly** - Clear examples with detailed comments
- **⚡ Modern C++20** - Uses latest C++ features (RAII, smart pointers, std::format)
- **🛡️ Safe & Robust** - Automatic memory management, proper error handling
- **📚 52 Complete Samples** - Covering video, audio, and streaming
- **🌍 Bilingual Docs** - Full documentation in English and Korean
- **🔧 Production Ready** - Battle-tested code you can use in real projects
- **🔄 Auto-Discovery** - CMake automatically detects and builds all samples

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

### 🎵 Audio Processing (31 samples)

| Sample | Description | Difficulty |
|--------|-------------|------------|
| `audio_info` | Display audio metadata | ⭐ Easy |
| `audio_decoder` | Extract audio as WAV | ⭐ Easy |
| `audio_encoder` | Generate audio tones | ⭐⭐ Medium |
| `audio_resampler` | Change sample rate/channels | ⭐⭐ Medium |
| `audio_mixer` | Mix multiple audio files | ⭐⭐ Medium |
| `audio_mixer_advanced` | Multi-track mixing with pan, offset, and fades | ⭐⭐⭐ Advanced |
| `audio_noise_reduction` | Remove background noise | ⭐⭐⭐ Advanced |
| `audio_format_converter` | Convert audio formats | ⭐ Easy |
| `audio_spectrum` | Create spectrum visualization | ⭐⭐ Medium |
| `audio_equalizer` | Apply multi-band EQ | ⭐⭐⭐ Advanced |
| `audio_transition` | Crossfade between tracks | ⭐⭐ Medium |
| `audio_silence_detect` | Detect silent segments | ⭐⭐ Medium |
| `audio_waveform` | Create waveform visualization | ⭐⭐ Medium |
| `audio_compressor` | Dynamic range compression | ⭐⭐⭐ Advanced |
| `audio_splitter` | Split audio by silence detection | ⭐⭐ Medium |
| `audio_normalization` | Normalize audio levels (peak/LUFS) | ⭐⭐ Medium |
| `audio_limiter` | True peak limiting with lookahead | ⭐⭐⭐ Advanced |
| `audio_delay` | Delay/echo effects (simple, multi-tap, ping-pong) | ⭐⭐ Medium |
| `audio_pitch_shift` | Pitch shifting with tempo preservation | ⭐⭐⭐ Advanced |
| `audio_beat_detector` | BPM detection and beat timestamp extraction | ⭐⭐⭐ Advanced |
| `audio_mastering` | Complete mastering chain (EQ, compression, limiting) | ⭐⭐⭐⭐ Expert |
| `audio_reverse` | Reverse audio playback | ⭐ Easy |
| `audio_gate` | Noise gate for removing background noise | ⭐⭐ Medium |
| `audio_stereo_tool` | Stereo manipulation (width, swap, M/S processing) | ⭐⭐ Medium |
| `audio_ducking` | Automatic volume ducking (sidechain compression) | ⭐⭐⭐ Advanced |
| `audio_phaser` | Classic phaser effect for guitars and synths | ⭐⭐ Medium |
| `audio_tremolo` | Tremolo/vibrato amplitude modulation effect | ⭐⭐ Medium |
| `audio_chorus` | Chorus effect with multiple delayed voices | ⭐⭐ Medium |
| `audio_reverb` | Reverb/room simulation effects | ⭐⭐⭐ Advanced |
| `audio_distortion` | Distortion and overdrive effects | ⭐⭐ Medium |
| `audio_flanger` | Jet/flanging sweep effect | ⭐⭐ Medium |

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

### Example 10: Audio Normalization

Normalize audio levels for consistent loudness:

```bash
# Peak normalization to -1dB
./audio_normalization input.wav output.wav

# Loudness normalization for podcast
./audio_normalization audio.mp3 normalized.mp3 -m loudness -l -16

# Broadcast standard (EBU R128)
./audio_normalization podcast.wav output.wav -m loudness -l -23 -t -1.5

# Two-pass peak normalization
./audio_normalization music.flac output.flac -m peak -l -0.1 -d

# RMS normalization with statistics
./audio_normalization audio.wav out.wav -m rms -l -20 -s
```

### Example 11: Audio Limiting

Apply true peak limiting to prevent clipping:

```bash
# Basic mastering limiter
./audio_limiter input.wav output.wav

# Mastering preset
./audio_limiter audio.mp3 limited.mp3 -p mastering

# Custom settings with lookahead
./audio_limiter input.wav output.wav -t -2 -c -0.5 -l 10

# Podcast preset
./audio_limiter podcast.wav output.wav -p podcast

# Aggressive limiting for streaming
./audio_limiter music.flac output.flac -p aggressive
```

### Example 12: Audio Delay/Echo

Apply delay and echo effects:

```bash
# Basic delay effect
./audio_delay input.wav output.wav

# Slapback echo preset
./audio_delay audio.mp3 delayed.mp3 -p slap

# Custom delay settings
./audio_delay input.wav output.wav -d 250 -f 0.6 -x 0.3

# Vocal doubling effect
./audio_delay vocal.wav doubled.wav -p vocal

# Ping-pong delay for guitar
./audio_delay guitar.wav echo.wav -m pingpong -d 375 -f 0.4

# Tempo-synced delay at 120 BPM
./audio_delay music.flac output.flac -t 120 -f 0.5
```

### Example 13: Audio Pitch Shift

Shift pitch while preserving tempo:

```bash
# Shift up 2 semitones (whole step)
./audio_pitch_shift input.wav output.wav -s 2

# Shift down 3 semitones (minor third)
./audio_pitch_shift audio.mp3 shifted.mp3 -s -3

# Shift up one octave
./audio_pitch_shift vocal.wav higher.wav -p octave_up

# Apply deep voice effect
./audio_pitch_shift voice.wav deep.wav -p deep

# Shift without preserving tempo (chipmunk effect)
./audio_pitch_shift music.flac pitched.flac -s 5 -t
```

### Example 15: Beat Detection and BPM Analysis

Detect beats and measure BPM in audio files:

```bash
# Basic BPM detection (automatic method)
./audio_beat_detector music.mp3

# Using onset detection method (best quality)
./audio_beat_detector song.wav -m onset

# Export beat timestamps to CSV
./audio_beat_detector audio.flac -e beats.csv

# High sensitivity detection with verbose output
./audio_beat_detector track.m4a -s 0.7 -v -e

# Detect specific BPM range
./audio_beat_detector dance.mp3 -b 120-140

# Energy-based detection with custom interval
./audio_beat_detector music.wav -m energy -i 0.4 -e beat_map.csv
```

### Example 16: Advanced Multi-Track Mixing

Mix multiple audio tracks with individual control:

```bash
# Mix two tracks with equal volume
./audio_mixer_advanced output.wav -i track1.wav -i track2.wav

# Mix with volume and pan control
./audio_mixer_advanced output.wav \
  -i vocals.wav -v 1.2 -p 0.0 \
  -i guitar.wav -v 0.8 -p -0.5 \
  -i bass.wav -v 1.0 -p 0.5

# Mix with time offsets and fades
./audio_mixer_advanced output.wav \
  -i intro.wav -fi 2.0 \
  -i main.wav -s 3.0 \
  -i outro.wav -s 60.0 -fi 1.0 -fo 3.0

# Full featured mix with auto-gain
./audio_mixer_advanced output.wav --auto-gain \
  -i drums.wav -v 1.0 -p 0.0 \
  -i bass.wav -v 0.9 -p -0.2 \
  -i guitar.wav -v 0.7 -p 0.3 -s 2.0 -fi 1.0 \
  -i vocals.wav -v 1.1 -p 0.0 -s 4.0 -fi 0.5
```

### Example 17: Professional Audio Mastering

Apply complete mastering chain:

```bash
# Master for streaming platforms (default)
./audio_mastering input.wav output.wav

# Master for CD release
./audio_mastering music.flac mastered.flac -p cd

# Master for podcast with voice optimization
./audio_mastering podcast.wav output.wav -p podcast

# Custom EQ settings
./audio_mastering audio.wav output.wav --eq --bass -2 --mid 2 --treble 1

# Custom loudness target
./audio_mastering input.wav output.wav --target-lufs -16 --true-peak -1.5
```

### Example 18: Audio Reverse

Reverse audio playback:

```bash
# Reverse entire audio file
./audio_reverse input.wav reversed.wav

# Reverse specific time range (5s to 10s)
./audio_reverse audio.mp3 output.wav -r -s 5.0 -e 10.0

# Reverse first 3.5 seconds
./audio_reverse speech.wav backward.wav -r -s 0 -e 3.5
```

### Example 19: Noise Gate

Remove background noise with gate:

```bash
# Apply default noise gate
./audio_gate input.wav output.wav

# Use vocal preset
./audio_gate noisy_audio.mp3 clean.wav -p vocal

# Custom settings
./audio_gate recording.wav output.wav -t -35 -r 15 -a 5 -R 150

# Optimize for podcast
./audio_gate podcast.wav clean.wav -p podcast

# Fast gating for drums
./audio_gate drums.wav gated.wav -p drum
```

### Example 20: Stereo Manipulation

Various stereo operations:

```bash
# Make stereo image wider
./audio_stereo_tool stereo.wav wide.wav --width 1.5

# Convert stereo to mono
./audio_stereo_tool stereo.wav mono.wav --to-mono

# Convert mono to stereo
./audio_stereo_tool mono.wav stereo.wav --to-stereo

# Swap left and right channels
./audio_stereo_tool input.wav swapped.wav --swap

# Adjust balance (30% to left)
./audio_stereo_tool stereo.wav balanced.wav --balance -0.3

# Mid/Side processing to enhance stereo
./audio_stereo_tool music.wav enhanced.wav --mid-side --mid-gain 0 --side-gain 3

# Fix phase issues
./audio_stereo_tool stereo.wav corrected.wav --phase-invert-right
```

### Example 21: Audio Ducking (Sidechain)

Automatic background music reduction:

```bash
# Basic ducking
./audio_ducking music.wav voice.wav output.wav

# Podcast preset (gentle ducking)
./audio_ducking bgm.mp3 narration.wav output.wav -p podcast

# Voiceover for video
./audio_ducking background.wav speech.wav output.wav -p voiceover

# Radio-style ducking
./audio_ducking music.flac podcast.wav output.wav -p radio

# Custom settings
./audio_ducking music.wav voice.wav output.wav -t -25 -r 6 -a 5 -R 300
```

### Example 22: Phaser Effect

Apply classic phaser effect:

```bash
# Classic 70s phaser
./audio_phaser guitar.wav phased.wav -p classic

# Fast sweeping phaser
./audio_phaser input.wav output.wav -s 0.8 -f 0.6

# Psychedelic rock sound
./audio_phaser synth.wav phased.wav -p psychedelic

# Jet plane flanging effect
./audio_phaser music.flac phased.flac -p jet

# Fast phaser with triangle LFO
./audio_phaser audio.wav output.wav -s 1.5 --triangle
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
│   │   ├── audio_mixer_advanced.cpp
│   │   ├── audio_noise_reduction.cpp
│   │   ├── audio_format_converter.cpp
│   │   ├── audio_spectrum.cpp
│   │   ├── audio_equalizer.cpp
│   │   ├── audio_transition.cpp
│   │   ├── audio_silence_detect.cpp
│   │   ├── audio_waveform.cpp
│   │   ├── audio_compressor.cpp
│   │   ├── audio_splitter.cpp
│   │   ├── audio_normalization.cpp
│   │   ├── audio_limiter.cpp
│   │   ├── audio_delay.cpp
│   │   ├── audio_pitch_shift.cpp
│   │   ├── audio_beat_detector.cpp
│   │   ├── audio_mastering.cpp
│   │   ├── audio_reverse.cpp
│   │   ├── audio_gate.cpp
│   │   ├── audio_stereo_tool.cpp
│   │   ├── audio_ducking.cpp
│   │   └── audio_phaser.cpp
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
