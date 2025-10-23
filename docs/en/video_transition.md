# Video Transition Guide

## Overview

The `video_transition` sample demonstrates how to apply smooth transition effects between two video clips using FFmpeg's powerful xfade filter. This is essential for creating professional-looking video compositions, montages, and presentations.

## Features

- **33+ Transition Effects** - Wide variety of professional transitions
- **Customizable Duration** - Control how long the transition lasts
- **Flexible Timing** - Choose when the transition occurs
- **Modern C++20** - RAII wrappers and structured bindings
- **Production Ready** - Proper error handling and resource management

## Usage

```bash
./video_transition <video1> <video2> <output> <transition> [duration] [offset]
```

### Parameters

- `video1` - First video clip (plays first)
- `video2` - Second video clip (plays after transition)
- `output` - Output video file path
- `transition` - Type of transition effect (see list below)
- `duration` - (Optional) Transition duration in seconds (default: 1.0)
- `offset` - (Optional) When transition starts in seconds (default: 0 = end of video1)

### Examples

**Basic fade transition:**
```bash
./video_transition clip1.mp4 clip2.mp4 output.mp4 fade
```

**Dissolve with 2-second duration:**
```bash
./video_transition intro.mp4 main.mp4 result.mp4 dissolve 2.0
```

**Slide right with custom timing:**
```bash
./video_transition part1.mp4 part2.mp4 final.mp4 slideright 1.5 5.0
```

## Available Transitions

### Basic Transitions

| Transition | Description |
|------------|-------------|
| `fade` | Classic fade transition |
| `dissolve` | Smooth dissolve effect |
| `fadeblack` | Fade through black |
| `fadewhite` | Fade through white |
| `fadegrays` | Fade through grayscale |

### Wipe Transitions

| Transition | Description |
|------------|-------------|
| `wipeleft` | Wipe from right to left |
| `wiperight` | Wipe from left to right |
| `wipeup` | Wipe from bottom to top |
| `wipedown` | Wipe from top to bottom |

### Slide Transitions

| Transition | Description |
|------------|-------------|
| `slideleft` | Slide from right to left |
| `slideright` | Slide from left to right |
| `slideup` | Slide from bottom to top |
| `slidedown` | Slide from top to bottom |
| `smoothleft` | Smooth slide left |
| `smoothright` | Smooth slide right |
| `smoothup` | Smooth slide up |
| `smoothdown` | Smooth slide down |

### Circular Transitions

| Transition | Description |
|------------|-------------|
| `circlecrop` | Circle crop transition |
| `circleclose` | Close in a circle |
| `circleopen` | Open from a circle |
| `radial` | Radial sweep transition |

### Diagonal Transitions

| Transition | Description |
|------------|-------------|
| `diagtl` | Diagonal from top-left |
| `diagtr` | Diagonal from top-right |
| `diagbl` | Diagonal from bottom-left |
| `diagbr` | Diagonal from bottom-right |

### Slice Transitions

| Transition | Description |
|------------|-------------|
| `hlslice` | Horizontal slice left |
| `hrslice` | Horizontal slice right |
| `vuslice` | Vertical slice up |
| `vdslice` | Vertical slice down |

### Special Effects

| Transition | Description |
|------------|-------------|
| `pixelize` | Pixelization effect |
| `squeezeh` | Horizontal squeeze |
| `squeezev` | Vertical squeeze |
| `distance` | Distance transformation |

## How It Works

### 1. Input Processing

The sample opens and analyzes both input videos:

```cpp
input1_format_ctx_ = ffmpeg::open_input_format(video1.data());
input2_format_ctx_ = ffmpeg::open_input_format(video2.data());
```

### 2. Video Stream Detection

Finds the video streams in both files:

```cpp
input1_stream_idx_ = find_video_stream(input1_format_ctx_.get());
input2_stream_idx_ = find_video_stream(input2_format_ctx_.get());
```

### 3. Decoder Setup

Sets up decoders for both input videos:

```cpp
setup_decoder(input1_format_ctx_.get(), input1_stream_idx_, input1_codec_ctx_);
setup_decoder(input2_format_ctx_.get(), input2_stream_idx_, input2_codec_ctx_);
```

### 4. Transition Processing

Applies the selected transition effect between the clips:

```cpp
// Process video1
while (process_input(input1_format_ctx_.get(), ...)) {
    // Decode and encode frames
}

// Process video2 with transition
while (process_input(input2_format_ctx_.get(), ...)) {
    // Apply transition and encode
}
```

### 5. Output Encoding

Encodes the final video with the transition:

```cpp
encode_frame(frame.get());
flush_encoder();
av_write_trailer(output_format_ctx_.get());
```

## Technical Details

### Video Requirements

- **Format**: Any FFmpeg-supported video format (MP4, AVI, MKV, etc.)
- **Resolution**: Videos can have different resolutions (first video's resolution is used)
- **Codec**: Any FFmpeg-supported codec
- **Frame Rate**: Automatic handling of different frame rates

### Output Specifications

- **Codec**: H.264 (libx264)
- **Container**: Determined by output file extension
- **Pixel Format**: YUV420P
- **Bitrate**: 2 Mbps (configurable in code)
- **Frame Rate**: 30 fps (configurable in code)

### Transition Timing

The transition timing is calculated as follows:

- **Default offset (0)**: Transition starts at `duration1 - transition_duration`
- **Custom offset**: Transition starts at specified time in first video

```cpp
const auto duration1 = static_cast<double>(input1_format_ctx_->duration) / AV_TIME_BASE;
transition_start_ = offset_ > 0 ? offset_ : duration1 - duration_;
```

## Common Use Cases

### 1. Video Montage

Create smooth transitions between different scenes:

```bash
./video_transition scene1.mp4 scene2.mp4 montage.mp4 dissolve 1.5
```

### 2. Presentation Videos

Transition between slides or sections:

```bash
./video_transition intro.mp4 content.mp4 presentation.mp4 slideright 1.0
```

### 3. Wedding Videos

Create elegant transitions between moments:

```bash
./video_transition ceremony.mp4 reception.mp4 wedding.mp4 fade 2.0
```

### 4. Marketing Videos

Professional transitions for promotional content:

```bash
./video_transition product_intro.mp4 features.mp4 promo.mp4 circleopen 1.0
```

## Performance Considerations

### Processing Time

- Transition processing is CPU-intensive
- Duration depends on video length, resolution, and transition complexity
- Consider using hardware acceleration for faster processing

### Memory Usage

- Two input videos are processed simultaneously
- Memory usage scales with video resolution
- Recommend at least 4GB RAM for HD videos

### Optimization Tips

1. **Reduce Resolution**: Process at lower resolution if final output allows
2. **Shorter Clips**: Trim videos to needed segments before transition
3. **Hardware Acceleration**: Use hardware encoders (VideoToolbox, NVENC) if available
4. **Bitrate Adjustment**: Lower bitrate for faster encoding if quality permits

## Troubleshooting

### Different Resolutions

If input videos have different resolutions, a warning is shown and the first video's resolution is used:

```
Warning: Input videos have different resolutions. Using first video's resolution.
```

Consider pre-scaling videos to the same resolution for best results.

### Invalid Transition

If an invalid transition name is provided:

```
Error: Invalid transition: xyz
```

Check the available transitions list and ensure correct spelling.

### Codec Not Found

If H.264 encoder is not available:

```
Error: H264 encoder not found
```

Ensure FFmpeg is compiled with libx264 support:

```bash
ffmpeg -codecs | grep h264
```

### Duration Validation

Transition duration must be between 0 and 10 seconds:

```
Duration must be between 0 and 10 seconds
```

## Code Structure

### Main Class: VideoTransition

```cpp
class VideoTransition {
public:
    VideoTransition(std::string_view video1, std::string_view video2,
                   std::string_view output, std::string_view transition,
                   double duration, double offset);

    void process();

private:
    void initialize();
    void setup_decoder(AVFormatContext* fmt_ctx, int stream_idx,
                      ffmpeg::CodecContextPtr& codec_ctx);
    void setup_output();
    bool process_input(AVFormatContext* fmt_ctx, AVCodecContext* codec_ctx,
                      int stream_idx, int& frame_count, bool is_first_video);
    void encode_frame(AVFrame* frame);
    void flush_encoder();
};
```

### Key Methods

- `initialize()` - Sets up input/output contexts and decoders
- `setup_decoder()` - Configures video decoder
- `setup_output()` - Configures H.264 encoder and output file
- `process_input()` - Decodes frames from input video
- `encode_frame()` - Encodes frame to output
- `flush_encoder()` - Flushes remaining frames

## Advanced Usage

### Custom Transition Parameters

You can modify the code to add custom parameters for specific transitions. For example, for the radial transition, you could add rotation angle control.

### Multiple Transitions

To create a video with multiple transitions, chain the tool:

```bash
# Create first transition
./video_transition clip1.mp4 clip2.mp4 temp1.mp4 fade 1.0

# Create second transition
./video_transition temp1.mp4 clip3.mp4 final.mp4 dissolve 1.5
```

### Batch Processing

Process multiple video pairs with a script:

```bash
#!/bin/bash
transitions=("fade" "dissolve" "slideright" "circleopen")
for i in {1..4}; do
    ./video_transition clip$i.mp4 clip$((i+1)).mp4 \
        output_${transitions[$i-1]}.mp4 ${transitions[$i-1]} 1.5
done
```

## FFmpeg Filter Reference

The xfade filter supports many additional parameters not exposed in this basic implementation:

- `offset` - Transition start time
- `duration` - Transition duration
- `transition` - Effect type
- Custom expressions for advanced effects

For more details, see: https://ffmpeg.org/ffmpeg-filters.html#xfade

## Related Samples

- **video_slideshow** - Create slideshows with transitions
- **video_splitter** - Split and merge videos
- **video_filter** - Apply video filters and effects

## Further Enhancements

Potential improvements for production use:

1. **Audio Handling** - Currently focuses on video; add audio crossfade
2. **Hardware Acceleration** - Add GPU encoding support
3. **Real-time Preview** - Show transition preview before processing
4. **Batch Mode** - Process multiple transitions in one run
5. **Custom Expressions** - Support for FFmpeg custom transition expressions
6. **Progress Bar** - More detailed progress indication
7. **Format Conversion** - Auto-convert incompatible videos

## Conclusion

The `video_transition` sample provides a solid foundation for creating professional video transitions. With 33+ built-in effects and flexible timing control, it's suitable for a wide range of video editing tasks.

For production applications, consider adding audio handling, hardware acceleration, and more sophisticated error recovery mechanisms.
