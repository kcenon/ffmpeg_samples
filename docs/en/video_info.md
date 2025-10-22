# Video Information Reader

## Overview

The `video_info` application is a command-line tool that reads and displays comprehensive metadata and stream information from video files using FFmpeg libraries. It provides detailed insights into video and audio streams, codecs, formats, and encoding parameters.

## Features

- Display container format information
- Show video stream details (codec, resolution, frame rate, bitrate)
- Show audio stream details (codec, sample rate, channels, bitrate)
- Display duration and timestamps
- Support for multiple streams
- Human-readable output format

## Usage

```bash
./video_info <input_file>
```

### Parameters

- `input_file` - Path to the video file to analyze (required)

### Supported Formats

All formats supported by FFmpeg, including:
- MP4 (H.264, H.265, VP9)
- AVI (various codecs)
- MKV (Matroska)
- MOV (QuickTime)
- FLV (Flash Video)
- WebM
- MPEG/TS

## Example Output

```
File: sample.mp4
Format: QuickTime / MOV
Duration: 00:05:23
Overall Bit Rate: 5420 kbps
Number of Streams: 2

Stream #0:
  Type: video
  Codec: h264
  Resolution: 1920x1080
  Pixel Format: yuv420p
  Frame Rate: 29.97 fps
  Bit Rate: 5000 kbps
  Duration: 323.45 seconds

Stream #1:
  Type: audio
  Codec: aac
  Sample Rate: 48000 Hz
  Channels: 2
  Bit Rate: 192 kbps
  Duration: 323.45 seconds
```

## Code Structure

### Main Components

1. **avformat_open_input()** - Opens the input file
2. **avformat_find_stream_info()** - Retrieves stream information
3. **Stream iteration** - Loops through all streams in the file
4. **print_stream_info()** - Custom function to format and display stream details

### Key FFmpeg Structures

- `AVFormatContext` - Contains format and stream information
- `AVStream` - Represents a single stream (video, audio, subtitle, etc.)
- `AVCodecParameters` - Contains codec-specific parameters
- `AVCodec` - Codec information

## Implementation Details

### Opening a File

```cpp
AVFormatContext* format_ctx = nullptr;
int ret = avformat_open_input(&format_ctx, filename, nullptr, nullptr);
if (ret < 0) {
    // Handle error
}
```

### Getting Stream Information

```cpp
ret = avformat_find_stream_info(format_ctx, nullptr);
if (ret < 0) {
    // Handle error
}
```

### Iterating Through Streams

```cpp
for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
    AVStream* stream = format_ctx->streams[i];
    AVCodecParameters* codecpar = stream->codecpar;

    if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        // Process video stream
    } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        // Process audio stream
    }
}
```

### Calculating Duration

```cpp
// File duration in seconds
if (format_ctx->duration != AV_NOPTS_VALUE) {
    double duration = format_ctx->duration / static_cast<double>(AV_TIME_BASE);
}

// Stream duration in seconds
if (stream->duration != AV_NOPTS_VALUE) {
    double duration = stream->duration * av_q2d(stream->time_base);
}
```

### Getting Frame Rate

```cpp
if (stream->avg_frame_rate.den && stream->avg_frame_rate.num) {
    double fps = av_q2d(stream->avg_frame_rate);
}
```

## Use Cases

1. **File Verification** - Verify video file integrity and format
2. **Stream Analysis** - Analyze encoding parameters before processing
3. **Format Detection** - Identify container and codec types
4. **Quality Assessment** - Check resolution, bitrate, and encoding settings
5. **Compatibility Testing** - Verify format compatibility before conversion

## Common Issues

### Issue: "Error opening input file"

**Causes:**
- File does not exist
- File path is incorrect
- Insufficient permissions
- Corrupted file

**Solutions:**
- Verify file path with `ls -l`
- Check file permissions with `chmod`
- Test file with `ffprobe` command

### Issue: "Codec not found"

**Causes:**
- FFmpeg compiled without specific codec support
- Proprietary codec not available

**Solutions:**
- Check FFmpeg codec support: `ffmpeg -codecs`
- Recompile FFmpeg with required codecs
- Use alternative codec

## Performance Considerations

- Very fast operation (milliseconds for most files)
- Minimal memory usage
- No actual decoding performed (metadata only)
- Safe for large files

## Error Handling

The application uses FFmpeg's error reporting system:

```cpp
char errbuf[AV_ERROR_MAX_STRING_SIZE];
av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
std::cerr << "Error: " << errbuf << "\n";
```

## Cleanup

Proper resource management:

```cpp
avformat_close_input(&format_ctx);
```

This automatically frees all associated memory.

## Related Tools

- **ffprobe** - FFmpeg's official media information tool
- **mediainfo** - Alternative media analysis tool
- **ExifTool** - Metadata extraction tool

## Advanced Usage

### Scripting Example

```bash
#!/bin/bash
# Analyze all videos in a directory
for file in *.mp4; do
    echo "Analyzing: $file"
    ./video_info "$file"
    echo "---"
done
```

### Integration Example

```cpp
// Use video_info output in your own application
std::string command = "./video_info " + filename;
FILE* pipe = popen(command.c_str(), "r");
// Parse output
pclose(pipe);
```

## API Reference

### Functions Used

- `avformat_open_input()` - Open input file
- `avformat_find_stream_info()` - Retrieve stream info
- `avformat_close_input()` - Close and free resources
- `avcodec_find_decoder()` - Find codec by ID
- `av_get_media_type_string()` - Get media type name
- `av_get_pix_fmt_name()` - Get pixel format name
- `av_q2d()` - Convert rational to double
- `av_strerror()` - Convert error code to string

## See Also

- [Video Decoder Documentation](video_decoder.md)
- [FFmpeg API Guide](ffmpeg_api.md)
- [Main README](../../README.md)
