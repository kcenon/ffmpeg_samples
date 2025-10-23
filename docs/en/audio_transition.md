# Audio Transition Guide

## Overview

The `audio_transition` sample demonstrates how to apply smooth crossfade transitions between two audio clips using FFmpeg's audio processing capabilities. Crossfading is essential for creating seamless audio compositions, DJ mixes, podcasts, and professional audio productions.

## Features

- **16 Crossfade Curves** - Various mathematical curves for different fade characteristics
- **Customizable Duration** - Control how long the crossfade lasts
- **Overlap Mode** - Choose between overlapping or sequential playback
- **Modern C++20** - RAII wrappers and structured bindings
- **Production Ready** - Proper error handling and resource management
- **WAV Output** - Standard uncompressed audio format

## Usage

```bash
./audio_transition <audio1> <audio2> <output> <curve> [duration] [overlap]
```

### Parameters

- `audio1` - First audio clip (plays first)
- `audio2` - Second audio clip (plays after transition)
- `output` - Output audio file path (WAV format)
- `curve` - Type of crossfade curve (see list below)
- `duration` - (Optional) Crossfade duration in seconds (default: 2.0)
- `overlap` - (Optional) Overlap mode: 0=none, 1=overlap (default: 1)

### Examples

**Basic linear crossfade:**
```bash
./audio_transition music1.mp3 music2.mp3 output.wav tri
```

**Quarter sine wave with 3-second fade:**
```bash
./audio_transition audio1.wav audio2.wav result.wav qsin 3.0
```

**Exponential fade with overlap:**
```bash
./audio_transition clip1.flac clip2.flac final.wav exp 1.5 1
```

**No overlap, sequential playback:**
```bash
./audio_transition intro.mp3 main.mp3 podcast.wav tri 2.0 0
```

## Available Crossfade Curves

### Linear Curves

| Curve | Name | Description |
|-------|------|-------------|
| `tri` | Triangular | Simple linear crossfade (constant power) |

### Sine-Based Curves

| Curve | Name | Description |
|-------|------|-------------|
| `qsin` | Quarter Sine | Smooth quarter sine wave fade |
| `esin` | Exponential Sine | Exponential sine curve |
| `hsin` | Half Sine | Half sine wave (S-curve) |
| `iqsin` | Inverted Quarter Sine | Inverted quarter sine |
| `ihsin` | Inverted Half Sine | Inverted half sine |

### Polynomial Curves

| Curve | Name | Description |
|-------|------|-------------|
| `qua` | Quadratic | Quadratic curve (x²) |
| `cub` | Cubic | Cubic curve (x³) |
| `par` | Parabola | Parabolic curve |
| `ipar` | Inverted Parabola | Inverted parabolic curve |

### Root Curves

| Curve | Name | Description |
|-------|------|-------------|
| `squ` | Square Root | Square root curve (√x) |
| `cbr` | Cubic Root | Cubic root curve (∛x) |

### Advanced Curves

| Curve | Name | Description |
|-------|------|-------------|
| `log` | Logarithmic | Logarithmic fade |
| `exp` | Exponential | Exponential fade |
| `dese` | Double Exponential Smootherstep | Very smooth double exponential |
| `desi` | Double Exponential Sigmoid | Sigmoid-shaped double exponential |

## How It Works

### 1. Audio Decoding

The sample opens and decodes both input audio files:

```cpp
decoder1_ = std::make_unique<AudioDecoder>(audio1, sample_rate, channels);
decoder2_ = std::make_unique<AudioDecoder>(audio2, sample_rate, channels);
```

### 2. Resampling

Both audio streams are resampled to a common format (44.1kHz, stereo, 16-bit):

```cpp
swr_alloc_set_opts2(&raw_swr,
                   &out_ch_layout,
                   AV_SAMPLE_FMT_S16,
                   target_sample_rate,
                   &codec_ctx_->ch_layout,
                   codec_ctx_->sample_fmt,
                   codec_ctx_->sample_rate,
                   0, nullptr);
```

### 3. Crossfade Processing

The transition is applied using the selected curve:

```cpp
for (int i = 0; i < fade_samples; ++i) {
    const double t = static_cast<double>(i) / fade_samples;
    const double fade_out = apply_curve(1.0 - t);
    const double fade_in = apply_curve(t);

    for (int ch = 0; ch < channels_; ++ch) {
        const int idx = i * channels_ + ch;
        const auto sample1 = static_cast<double>(buffer1[idx]) * fade_out;
        const auto sample2 = static_cast<double>(buffer2[idx]) * fade_in;
        const auto mixed = static_cast<int16_t>(sample1 + sample2);
        buffer1[idx] = mixed;
    }
}
```

### 4. WAV Output

The final audio is written to a WAV file with proper headers:

```cpp
write_wav_header(output, sample_rate_, channels_, total_bytes);
```

## Technical Details

### Audio Requirements

- **Format**: Any FFmpeg-supported audio format (MP3, WAV, FLAC, AAC, OGG, etc.)
- **Sample Rate**: Automatically resampled to 44.1kHz
- **Channels**: Automatically converted to stereo
- **Bit Depth**: 16-bit PCM output

### Output Specifications

- **Format**: WAV (Waveform Audio File Format)
- **Sample Rate**: 44.1 kHz
- **Channels**: 2 (Stereo)
- **Bit Depth**: 16-bit signed integer
- **Encoding**: PCM (uncompressed)

### Overlap Modes

#### Overlap Mode (overlap=1, default)

The crossfade occurs at the end of the first audio and beginning of the second:

```
Audio 1: [==========FADE]
Audio 2:           [FADE==========]
Result:  [==========XXXX==========]
         where X = crossfaded region
```

Total duration = duration1 + duration2 - crossfade_duration

#### No Overlap Mode (overlap=0)

The crossfade is inserted between the two audio files:

```
Audio 1: [==========]FADE
Audio 2:            FADE[==========]
Result:  [==========XXXX==========]
```

Total duration = duration1 + duration2 + crossfade_duration

## Common Use Cases

### 1. DJ Mixes

Create smooth transitions between songs:

```bash
./audio_transition track1.mp3 track2.mp3 mix.wav qsin 4.0
```

### 2. Podcast Production

Blend intro music with main content:

```bash
./audio_transition intro_music.mp3 podcast_content.wav output.wav hsin 2.0
```

### 3. Audio Books

Smooth transitions between chapters:

```bash
./audio_transition chapter1.mp3 chapter2.mp3 book.wav tri 1.0
```

### 4. Music Production

Seamless track transitions for albums:

```bash
./audio_transition song1.flac song2.flac album.wav esin 3.0
```

### 5. Radio Production

Professional station IDs and jingles:

```bash
./audio_transition jingle.wav program.mp3 broadcast.wav exp 1.5
```

## Curve Selection Guide

### When to Use Each Curve

**Linear (tri)**
- General purpose crossfading
- Equal power fade
- Music mixing
- Fast and simple

**Quarter Sine (qsin)**
- Smooth, natural transitions
- Recommended for most music
- Professional DJ mixes
- Subtle fade effect

**Half Sine (hsin)**
- Very smooth S-curve
- Cinematic audio transitions
- High-quality productions
- Minimal artifacts

**Exponential (exp)**
- Quick initial fade
- Dramatic transitions
- Special effects
- Dynamic emphasis changes

**Logarithmic (log)**
- Slow start, fast finish
- Perceived loudness matching
- Speech transitions
- Volume-matched fades

**Cubic/Quadratic**
- Custom fade characteristics
- Artistic control
- Experimental effects
- Non-standard transitions

**Double Exponential (dese/desi)**
- Ultra-smooth transitions
- Audiophile-quality fades
- Mastering applications
- Premium productions

## Performance Considerations

### Processing Time

- Crossfade processing is relatively fast
- Duration depends on audio length and crossfade duration
- Typical processing time: real-time or faster

### Memory Usage

- Two audio buffers in memory simultaneously
- Crossfade buffer size based on duration
- Minimal memory footprint for typical use cases

### Quality Considerations

1. **Sample Rate**: 44.1kHz provides good quality for most uses
2. **Bit Depth**: 16-bit is sufficient for most applications
3. **Curve Selection**: Affects perceived quality significantly
4. **Duration**: Longer crossfades are smoother but more noticeable

## Troubleshooting

### Different Sample Rates

Input files with different sample rates are automatically resampled to 44.1kHz. This is transparent and requires no user intervention.

### Different Channel Counts

Mono files are converted to stereo. Stereo files remain stereo. Multi-channel files are downmixed to stereo.

### Invalid Curve Name

If an invalid curve name is provided, the tool defaults to linear (tri) fade:

```cpp
return t;  // Default to linear
```

### Duration Validation

Crossfade duration must be between 0 and 10 seconds:

```
Duration must be between 0 and 10 seconds
```

### File Format Issues

If input files cannot be decoded:

```
Error: No audio stream found
Error: Decoder not found
Error: Failed to open decoder
```

Ensure files are valid audio files supported by FFmpeg.

## Code Structure

### Main Class: AudioTransition

```cpp
class AudioTransition {
public:
    AudioTransition(std::string_view audio1, std::string_view audio2,
                   std::string_view output, std::string_view curve,
                   double duration, int overlap);

    void process();

private:
    uint32_t process_first_audio(std::ofstream& output);
    uint32_t process_crossfade(std::ofstream& output);
    uint32_t process_second_audio(std::ofstream& output);
    double apply_curve(double t) const;
};
```

### Helper Class: AudioDecoder

```cpp
class AudioDecoder {
public:
    AudioDecoder(std::string_view filename, int target_sample_rate, int target_channels);

    int read_samples(int16_t* buffer, int num_samples);
    bool is_eof() const;
    int get_sample_rate() const;
    int get_channels() const;
    double get_duration() const;

private:
    void initialize(int target_sample_rate, int target_channels);
};
```

### Key Methods

- `process()` - Main processing loop
- `process_first_audio()` - Writes first audio before crossfade
- `process_crossfade()` - Applies crossfade transition
- `process_second_audio()` - Writes second audio after crossfade
- `apply_curve()` - Mathematical curve functions

## Mathematical Curves

The curves are implemented as mathematical functions:

```cpp
// Linear
t

// Quarter Sine
sin(t * π/2)

// Half Sine
(1 - cos(t * π)) / 2

// Exponential
exp(t * 4 - 4)

// Logarithmic
log₁₀(t * 9 + 1)

// Quadratic
t²

// Cubic
t³

// Square Root
√t

// Cubic Root
∛t
```

## Advanced Usage

### Batch Processing

Process multiple transitions in a script:

```bash
#!/bin/bash
tracks=("track1.mp3" "track2.mp3" "track3.mp3" "track4.mp3")
for i in {0..2}; do
    ./audio_transition "${tracks[$i]}" "${tracks[$i+1]}" \
        "transition_$i.wav" qsin 3.0
done
```

### Chain Transitions

Create a complete mix from multiple tracks:

```bash
# First transition
./audio_transition track1.mp3 track2.mp3 temp1.wav qsin 3.0

# Second transition
./audio_transition temp1.wav track3.mp3 temp2.wav qsin 3.0

# Final transition
./audio_transition temp2.wav track4.mp3 final_mix.wav qsin 3.0
```

### Different Curves for Different Moods

```bash
# Energetic transition
./audio_transition upbeat1.mp3 upbeat2.mp3 energetic.wav exp 1.0

# Smooth transition
./audio_transition ambient1.mp3 ambient2.mp3 smooth.wav hsin 4.0

# Quick cut
./audio_transition rock1.mp3 rock2.mp3 quick.wav tri 0.5
```

## Comparison with Video Transition

While `video_transition` handles visual crossfades, `audio_transition` focuses specifically on audio:

| Feature | Video Transition | Audio Transition |
|---------|-----------------|------------------|
| Input | Video files | Audio files |
| Output | Video file | WAV file |
| Effects | 33+ visual transitions | 16 crossfade curves |
| Processing | Frame-by-frame | Sample-by-sample |
| Duration | Typically 1-2s | Typically 2-4s |

## Related Samples

- **audio_mixer** - Mix two audio files with volume control
- **audio_format_converter** - Convert between audio formats
- **video_transition** - Apply visual transitions between videos

## Further Enhancements

Potential improvements for production use:

1. **Parametric EQ** - Frequency-based crossfading
2. **Multiple Formats** - Support for various output formats (MP3, FLAC, etc.)
3. **BPM Detection** - Beat-matched crossfading for music
4. **Auto-Gain** - Automatic volume normalization
5. **Preview Mode** - Listen to crossfade before processing
6. **Metadata Preservation** - Copy tags from source files
7. **Multi-Track** - Support for more than two inputs
8. **Real-time Processing** - Live crossfading capability

## Best Practices

1. **Match Loudness**: Normalize audio levels before crossfading
2. **Choose Appropriate Duration**:
   - Music: 2-4 seconds
   - Speech: 1-2 seconds
   - Sound effects: 0.5-1 second
3. **Test Different Curves**: Each curve has unique characteristics
4. **Use Overlap Mode**: Usually sounds more natural than sequential
5. **Consider Context**: Dramatic vs. subtle transitions

## Conclusion

The `audio_transition` sample provides professional-quality crossfading between audio clips with extensive control over the fade characteristics. With 16 different curves and customizable parameters, it's suitable for a wide range of audio production tasks.

For production applications, consider adding format conversion, metadata handling, and automatic gain control for enhanced functionality.
