# FFmpeg Multimedia Processing Samples

A collection of modern C++20 sample applications demonstrating various video and audio processing capabilities using the FFmpeg library.

## Key Features

- ✨ **Modern C++20** - Uses RAII wrappers, std::span, std::string_view, and structured bindings
- 🛡️ **Exception Safety** - Automatic resource management with smart pointers
- 📚 **Comprehensive** - 22 samples covering video and audio processing
- 🌍 **Bilingual Docs** - Complete documentation in English and Korean
- 🎯 **Production Ready** - Proper error handling and resource management

## Overview

This project contains twenty-two sample applications that showcase different aspects of multimedia processing:

### Video Samples
1. **video_info** - Read and display video file metadata
2. **video_decoder** - Decode video frames and save as images
3. **video_encoder** - Encode generated frames into video files
4. **video_transcoder** - Convert videos between different formats and codecs
5. **video_filter** - Apply various video filters and effects
6. **video_thumbnail** - Generate video thumbnails and preview images
7. **video_metadata** - Edit and manage video metadata tags
8. **video_subtitles** - Extract and burn subtitles into videos
9. **video_watermark** - Add image or text watermarks to videos
10. **video_splitter** - Split videos into segments or merge multiple videos
11. **video_slideshow** - Create slideshows from image collections
12. **video_stabilization** - Stabilize shaky video footage
13. **streaming_server** - Stream videos over network protocols

### Audio Samples
14. **audio_info** - Read and display audio file metadata
15. **audio_decoder** - Decode audio and save as WAV format
16. **audio_encoder** - Generate audio tones and encode to various formats
17. **audio_resampler** - Change audio sample rate and channel layout
18. **audio_mixer** - Mix two audio files with volume control
19. **audio_noise_reduction** - Apply noise reduction and audio enhancement
20. **audio_format_converter** - Convert audio files between different formats
21. **audio_spectrum** - Create audio spectrum visualizations
22. **audio_equalizer** - Apply multi-band equalization

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

### 10. Video Subtitles (video_subtitles)

Extract embedded subtitles or burn subtitles into video files.

```bash
./video_subtitles <command> [options]
```

**Commands:**

**Extract** - Extract embedded subtitles to SRT file:
```bash
./video_subtitles extract video.mkv subtitles.srt
```

**Burn** - Burn subtitles into video (hardsub):
```bash
./video_subtitles burn video.mp4 subtitles.srt output.mp4
```

**Supported Subtitle Formats:**
- SRT (SubRip)
- ASS/SSA (Advanced SubStation Alpha)
- WebVTT

**Features:**
- Extract subtitles from video containers
- Burn subtitles permanently into video
- Support for multiple subtitle formats
- Preserves video quality during extraction
- Automatic subtitle positioning

### 11. Video Watermark (video_watermark)

Add image or text watermarks to video files.

```bash
./video_watermark <command> <input_video> <output_video> [options]
```

**Commands:**

**Image Watermark:**
```bash
./video_watermark image video.mp4 output.mp4 logo.png bottom_right 0.7
./video_watermark image video.mp4 output.mp4 watermark.png top_left 1.0
```

**Text Watermark:**
```bash
./video_watermark text video.mp4 output.mp4 "Copyright 2024" bottom_left 24 white 0.8
./video_watermark text video.mp4 output.mp4 "MyChannel" top_right 32 yellow 0.9
```

**Positions:**
- `top_left`, `top_right`, `bottom_left`, `bottom_right`, `center`

**Parameters:**
- For image: `<watermark_image> <position> [opacity]`
- For text: `<text> <position> [font_size] [color] [opacity]`

**Features:**
- Image and text watermark support
- Flexible positioning (5 positions)
- Configurable opacity/transparency
- Font size and color customization for text
- Maintains video quality

### 12. Audio Spectrum Visualizer (audio_spectrum)

Create spectrum visualization videos from audio files.

```bash
./audio_spectrum <input_audio> <output_video> <mode> [width] [height] [fps]
```

**Visualization Modes:**
- `spectrum` - Frequency spectrum visualization (default)
- `waveform` - Waveform display with colors
- `showcqt` - Constant Q Transform spectrum
- `showfreqs` - Frequency bars visualization
- `showwaves` - Multi-style waveform display

**Examples:**
```bash
# Basic spectrum visualization
./audio_spectrum music.mp3 spectrum.mp4 spectrum

# High-quality waveform at 60fps
./audio_spectrum audio.wav waveform.mp4 waveform 1920 1080 60

# CQT spectrum visualization
./audio_spectrum song.flac visual.mp4 showcqt 1280 720 30
```

**Parameters:**
- `width` - Video width in pixels (default: 1280)
- `height` - Video height in pixels (default: 720)
- `fps` - Frame rate (default: 30)

**Features:**
- Five different visualization modes
- Customizable resolution and frame rate
- Professional-quality output
- Perfect for music videos
- Real-time processing

### 13. Audio Equalizer (audio_equalizer)

Apply multi-band equalization to audio files.

```bash
./audio_equalizer <input_file> <output_file> <mode> [options]
```

**Modes:**

**Preset Mode** - Use predefined equalizer presets:
```bash
./audio_equalizer input.mp3 output.wav preset bass_boost
./audio_equalizer music.flac enhanced.wav preset vocal
```

**Custom Mode** - Create custom equalizer:
```bash
./audio_equalizer audio.wav custom.wav custom 100,5,2 1000,3,2 5000,-2,2
```

**Available Presets:**
- `flat` - No equalization (bypass)
- `bass_boost` - Enhanced bass frequencies
- `treble_boost` - Enhanced treble frequencies
- `vocal` - Optimized for vocals
- `classical` - Classical music preset
- `rock` - Rock music preset
- `jazz` - Jazz music preset
- `pop` - Pop music preset
- `electronic` - Electronic/EDM music preset
- `acoustic` - Acoustic instruments preset

**Custom Band Format:**
- `frequency(Hz),gain(dB),width(octaves)`
- Example: `100,5,2` = 100Hz frequency, +5dB gain, 2 octave width

**Examples:**
```bash
# Use bass boost preset
./audio_equalizer input.mp3 output.wav preset bass_boost

# Custom 3-band equalizer
./audio_equalizer music.wav custom.wav custom 100,6,1 1000,2,2 10000,-3,1

# Vocal enhancement
./audio_equalizer podcast.mp3 enhanced.wav preset vocal
```

**Features:**
- 10 professional presets
- Custom multi-band equalizer
- Precise frequency control
- Configurable gain per band
- High-quality processing
- WAV output format

### 14. Video Splitter (video_splitter)

Split videos into segments or merge multiple videos.

```bash
./video_splitter <command> [options]
```

**Commands:**

**Split by Time** - Split at specific time ranges:
```bash
./video_splitter split_time video.mp4 segments 0,30 30,60 60,90
```

**Split by Duration** - Split into equal segments:
```bash
./video_splitter split_duration video.mp4 segments 60
```

**Merge Videos** - Combine multiple videos:
```bash
./video_splitter merge output.mp4 part1.mp4 part2.mp4 part3.mp4
```

**Examples:**
```bash
# Split video into 3 segments
./video_splitter split_time video.mp4 parts 0,120 120,240 240,360

# Split into 30-second segments
./video_splitter split_duration long_video.mp4 clips 30

# Merge multiple clips
./video_splitter merge final.mp4 clip1.mp4 clip2.mp4 clip3.mp4
```

**Features:**
- Precise time-based splitting
- Duration-based automatic segmentation
- Lossless segment extraction
- Fast merging without re-encoding
- Preserves video/audio quality

### 15. Video Slideshow (video_slideshow)

Create video slideshows from image collections.

```bash
./video_slideshow <output_video> <image_dir> [options]
```

**Options:**
- `--width <pixels>` - Video width (default: 1920)
- `--height <pixels>` - Video height (default: 1080)
- `--fps <rate>` - Frame rate (default: 30)
- `--duration <seconds>` - Duration per image (default: 3.0)
- `--transition <type>` - Transition effect (default: fade)
- `--trans-duration <sec>` - Transition duration (default: 1.0)

**Transition Types:**
- `none`, `fade`, `slide_left`, `slide_right`, `zoom_in`, `zoom_out`

**Examples:**
```bash
# Basic slideshow
./video_slideshow slideshow.mp4 photos/

# Custom resolution and duration
./video_slideshow output.mp4 images/ --width 1280 --height 720 --duration 5

# With zoom-in transition at 60fps
./video_slideshow video.mp4 pics/ --transition zoom_in --fps 60

# Long duration with fade
./video_slideshow memories.mp4 vacation/ --duration 10 --transition fade
```

**Features:**
- Automatic image collection from directory
- Multiple transition effects
- Customizable resolution and frame rate
- Variable image display duration
- Supports JPG, PNG, BMP, TIFF formats

### 16. Video Stabilization (video_stabilization)

Stabilize shaky video footage using motion analysis.

```bash
./video_stabilization <input_video> <output_video> [options]
```

**Options:**
- `--smoothing <value>` - Smoothing strength (1-100, default: 10)
  * Higher values = smoother but less responsive
- `--shakiness <value>` - Shakiness detection (1-10, default: 5)
  * Higher values = detect more motion
- `--stats` - Show stabilization statistics

**Examples:**
```bash
# Basic stabilization
./video_stabilization shaky.mp4 stable.mp4

# High smoothing for very shaky footage
./video_stabilization input.mp4 output.mp4 --smoothing 20 --shakiness 8

# With statistics
./video_stabilization video.mp4 stabilized.mp4 --smoothing 15 --stats
```

**Process:**
1. **Motion Detection** - Analyzes camera movement
2. **Transform Calculation** - Computes stabilization transforms
3. **Video Stabilization** - Applies smooth camera motion

**Features:**
- Two-pass stabilization (detect + transform)
- Configurable smoothing strength
- Adjustable shakiness detection
- Automatic crop and zoom
- Preserves original video quality

**Note:** Requires FFmpeg compiled with vidstab support.

### 17. Streaming Server (streaming_server)

Stream videos over network protocols.

```bash
./streaming_server <input_file> <output_url> [options]
```

**Options:**
- `--format <fmt>` - Output format (auto-detect if not specified)
- `--loop` - Loop the video continuously

**Supported Formats:**
- `flv` - Flash Video (for RTMP/HTTP)
- `mpegts` - MPEG Transport Stream (for UDP/HTTP)
- `hls` - HTTP Live Streaming
- `dash` - MPEG-DASH

**Examples:**

**HTTP Streaming:**
```bash
./streaming_server video.mp4 http://localhost:8080/stream.flv --format flv
```

**RTMP Streaming** (requires RTMP server):
```bash
./streaming_server video.mp4 rtmp://localhost/live/stream --format flv
```

**UDP Streaming:**
```bash
./streaming_server video.mp4 udp://239.1.1.1:1234 --format mpegts
```

**HLS Output:**
```bash
./streaming_server video.mp4 stream.m3u8 --format hls
```

**Loop Streaming:**
```bash
./streaming_server video.mp4 http://localhost:8080/stream --loop
```

**Client Playback:**
```bash
# Using ffplay
ffplay http://localhost:8080/stream.flv

# Using VLC
vlc http://localhost:8080/stream.flv
```

**Features:**
- Multiple streaming protocols (RTMP, HTTP, UDP, HLS, DASH)
- Real-time streaming with timing control
- Loop mode for continuous playback
- Auto-format detection
- Network streaming optimization

**Requirements:**
- RTMP: Requires RTMP server (e.g., nginx-rtmp)
- HTTP: Requires HTTP server accepting streams
- UDP: Network must allow multicast

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
│   ├── video_subtitles.cpp     # Subtitle processor
│   ├── video_watermark.cpp     # Watermark processor
│   ├── video_splitter.cpp      # Video splitter/merger
│   ├── video_slideshow.cpp     # Slideshow generator
│   ├── video_stabilization.cpp # Video stabilization
│   ├── streaming_server.cpp    # Streaming server
│   ├── audio_info.cpp          # Audio information reader
│   ├── audio_decoder.cpp       # Audio decoder
│   ├── audio_encoder.cpp       # Audio encoder
│   ├── audio_resampler.cpp     # Audio resampler
│   ├── audio_mixer.cpp         # Audio mixer
│   ├── audio_noise_reduction.cpp    # Noise reduction
│   ├── audio_format_converter.cpp   # Format converter
│   ├── audio_spectrum.cpp      # Spectrum visualizer
│   └── audio_equalizer.cpp     # Multi-band equalizer
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

### Phase 1 ✅ (Completed)
- **video_thumbnail** - Thumbnail generator
- **video_metadata** - Metadata editor
- **audio_noise_reduction** - Noise reduction
- **audio_format_converter** - Format converter

### Phase 2 ✅ (Completed)
- **video_subtitles** - Subtitle processing and embedding
- **video_watermark** - Watermark addition and positioning
- **audio_spectrum** - Audio spectrum visualization
- **audio_equalizer** - Multi-band equalizer

### Phase 3 ✅ (Completed)
- **video_splitter** - Video splitting and merging
- **video_slideshow** - Slideshow generator from images
- **video_stabilization** - Video stabilization
- **streaming_server** - Basic streaming server

## Project Completion

All planned samples have been successfully implemented! This project now provides:
- **22 comprehensive samples** covering all major multimedia processing tasks
- **Modern C++20** implementation with RAII and smart pointers
- **Production-ready code** with proper error handling
- **Complete documentation** in English and Korean

## Contributing

Feel free to add more samples or improve existing ones. Potential additions might include:

- Hardware acceleration examples (NVENC, VideoToolbox, VA-API)
- More complex filter combinations and custom filters
- Multi-threaded processing optimization
- Additional streaming protocols
- Advanced audio/video analysis tools

## Troubleshooting

For issues or questions:

1. Check FFmpeg installation: `ffmpeg -version`
2. Verify library paths: `pkg-config --libs libavcodec`
3. Ensure input files are valid: `ffprobe input.mp4`
4. Check build output for specific error messages

## Author

Created as a comprehensive FFmpeg learning resource for C++ developers.
