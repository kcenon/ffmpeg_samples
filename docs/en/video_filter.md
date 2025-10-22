# Video Filter

## Overview

The `video_filter` application demonstrates the powerful capabilities of FFmpeg's filter graph API by applying various video filters and effects to video streams. This application serves as both a practical tool for video processing and an educational resource for understanding FFmpeg's filtering system.

The filter graph API is one of FFmpeg's most versatile features, allowing complex video processing pipelines to be constructed from simple, composable filter elements. This sample includes multiple pre-configured filter presets ranging from basic color adjustments to advanced edge detection, and can be easily extended to support custom filter combinations.

### Key Capabilities

- Real-time video filtering with multiple effect types
- Filter graph construction and management
- Frame-by-frame processing
- Support for filter chaining and complex pipelines
- Configurable filter parameters
- Progress monitoring during processing

## Usage

```bash
./video_filter <input_file> <output_file> <filter_type>
```

### Parameters

- `input_file` - Input video file path (required)
- `output_file` - Output video file path (required)
- `filter_type` - Filter preset to apply (required)

## Available Filters

| Filter | Description | Effect |
|--------|-------------|--------|
| `grayscale` | Convert to grayscale | Removes color |
| `blur` | Gaussian blur | Softens image |
| `sharpen` | Sharpen filter | Enhances edges |
| `rotate` | Rotate 90° clockwise | Rotates video |
| `flip_h` | Flip horizontal | Mirror horizontally |
| `flip_v` | Flip vertical | Mirror vertically |
| `brightness` | Increase brightness | Brightens image |
| `contrast` | Increase contrast | Enhances contrast |
| `edge` | Edge detection | Detects edges |
| `negative` | Negative image | Inverts colors |
| `custom` | Custom combined filters | Multiple effects |

## Examples

```bash
# Convert to grayscale
./video_filter input.mp4 output_gray.mp4 grayscale

# Apply blur effect
./video_filter input.mp4 output_blur.mp4 blur

# Edge detection
./video_filter input.mp4 output_edge.mp4 edge

# Sharpen video
./video_filter input.mp4 output_sharp.mp4 sharpen

# Rotate 90 degrees
./video_filter input.mp4 output_rotated.mp4 rotate
```

## Implementation Details

### Filter Graph API

The application uses FFmpeg's filter graph system:

1. Create filter graph
2. Create buffer source (input)
3. Create buffer sink (output)
4. Parse filter description
5. Configure graph
6. Push frames to source
7. Pull filtered frames from sink

### Filter Description Syntax

Filters use FFmpeg filter expressions:
```
# Single filter
hue=s=0

# Chained filters (comma-separated)
eq=brightness=0.1:contrast=1.2,hue=s=1.2

# Complex filters with parameters
gblur=sigma=5
```

### Adding Custom Filters

Modify `get_filter_description()` function:

```cpp
else if (strcmp(filter_type, "myfilter") == 0) {
    return "your_filter_expression";
}
```

## Filter Examples

### Color Adjustments

```cpp
// Brightness
"eq=brightness=0.2"

// Contrast
"eq=contrast=1.5"

// Saturation
"eq=saturation=1.5"

// Hue shift
"hue=h=90"
```

### Spatial Filters

```cpp
// Gaussian blur
"gblur=sigma=5"

// Box blur
"boxblur=2:1"

// Sharpen
"unsharp=5:5:1.0:5:5:0.0"
```

### Geometric Transforms

```cpp
// Rotate 90° clockwise
"transpose=1"

// Flip horizontal
"hflip"

// Flip vertical
"vflip"

// Scale
"scale=1280:720"
```

### Effects

```cpp
// Edge detection
"edgedetect=low=0.1:high=0.4"

// Negative
"negate"

// Grayscale
"hue=s=0"

// Vignette
"vignette=PI/4"
```

## Use Cases

1. **Video Enhancement**
   - Color correction
   - Sharpening
   - Noise reduction

2. **Creative Effects**
   - Artistic filters
   - Color grading
   - Special effects

3. **Analysis**
   - Edge detection
   - Motion detection
   - Quality assessment

4. **Preprocessing**
   - Normalization
   - Format conversion
   - Deinterlacing

## Performance Considerations

- Some filters are GPU-accelerated
- Complex filter chains may be slow
- Consider resolution when applying filters
- Test with short clips first

## Common Issues

### Filter Not Found
Check available filters:
```bash
ffmpeg -filters
```

### Syntax Error
Verify filter syntax:
```bash
ffmpeg -h filter=<filter_name>
```

### Performance Issues
- Simplify filter chain
- Reduce resolution
- Use faster algorithms

## Advanced Usage

### Multiple Filters

Combine filters with commas:
```cpp
"hue=s=0,eq=brightness=0.2,unsharp=5:5:1.0"
```

### Conditional Filtering

Apply filters based on conditions:
```cpp
"select='gt(scene,0.4)',hue=s=0"
```

### Time-based Effects

```cpp
"fade=in:0:30"  // Fade in over 30 frames
```

## Filter Documentation

Complete filter reference:
https://ffmpeg.org/ffmpeg-filters.html

## See Also

- [Video Transcoder Documentation](video_transcoder.md)
- [FFmpeg API Guide](ffmpeg_api.md)
- [Main README](../../README.md)
