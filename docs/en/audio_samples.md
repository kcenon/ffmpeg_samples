# Audio Processing Samples

## Overview

This document covers all audio processing sample applications included in the FFmpeg samples project. These applications demonstrate various audio manipulation techniques using FFmpeg libraries.

## Audio Info

### Purpose
Read and display audio file metadata and stream information.

### Usage
```bash
./audio_info <input_file>
```

### Example
```bash
./audio_info audio.mp3
./audio_info music.flac
```

### Features
- Display codec information
- Show sample rate and bit rate
- Display channel layout
- Show sample format
- Extract metadata (artist, title, album, etc.)
- Duration information

### Output Example
```
Audio File Information
======================================

File: audio.mp3
Format: MP2/3 (MPEG audio layer 2/3)
Duration: 00:03:45
Overall Bit Rate: 320 kbps
Number of Streams: 1

Audio Stream #0:
  Codec: MP3 (MPEG audio layer 3) (mp3)
  Sample Rate: 44100 Hz
  Channels: 2
  Channel Layout: stereo
  Sample Format: fltp
  Bit Rate: 320 kbps

Metadata
======================================
artist: Example Artist
title: Example Song
album: Example Album
date: 2024
```

## Audio Decoder

### Purpose
Decode audio files and save as WAV (PCM) format.

### Usage
```bash
./audio_decoder <input_file> <output_file>
```

### Example
```bash
./audio_decoder music.mp3 output.wav
./audio_decoder audio.aac output.wav
```

### Features
- Decode any FFmpeg-supported audio format
- Convert to standard WAV format
- 16-bit PCM output
- Stereo, 44.1kHz output
- Automatic resampling
- Progress reporting

### Technical Details
- Output format: WAV
- Sample format: 16-bit signed integer (S16)
- Sample rate: 44.1kHz
- Channels: Stereo (2)
- Uses libswresample for format conversion

## Audio Encoder

### Purpose
Generate audio (sine wave tone) and encode to various formats.

### Usage
```bash
./audio_encoder <output_file> [duration_seconds] [frequency_hz]
```

### Example
```bash
# Generate 5 seconds of 440Hz (A4 note)
./audio_encoder output.mp3 5 440

# Generate 10 seconds of 880Hz (A5 note)
./audio_encoder output.aac 10 880

# Generate different formats
./audio_encoder output.mp3   # MP3 format
./audio_encoder output.aac   # AAC format
./audio_encoder output.ogg   # Ogg Vorbis format
./audio_encoder output.flac  # FLAC format
```

### Features
- Generate sine wave tones
- Multiple output format support
- Configurable duration
- Configurable frequency
- Automatic codec selection
- Stereo output at 44.1kHz

### Supported Formats
- MP3 (using libmp3lame)
- AAC
- Ogg Vorbis (using libvorbis)
- FLAC
- M4A

### Use Cases
- Generate test tones
- Create audio test signals
- Audio system calibration
- Learning audio encoding

## Audio Resampler

### Purpose
Change audio sample rate and/or channel layout (stereo to mono or vice versa).

### Usage
```bash
./audio_resampler <input_file> <output_file> [sample_rate] [channels]
```

### Parameters
- `sample_rate` - Target sample rate in Hz (default: 44100)
- `channels` - Target channels: 1 (mono) or 2 (stereo) (default: 2)

### Examples
```bash
# Convert to 48kHz stereo
./audio_resampler input.mp3 output.wav 48000 2

# Convert to 22050Hz mono
./audio_resampler input.wav output.wav 22050 1

# Standard CD quality (44.1kHz stereo)
./audio_resampler input.flac output.wav 44100 2

# Downsampling for voice (16kHz mono)
./audio_resampler voice.wav voice_16k.wav 16000 1
```

### Features
- Sample rate conversion
- Channel layout conversion (mono/stereo)
- High-quality resampling using libswresample
- Automatic format conversion
- WAV output format
- Progress reporting

### Common Use Cases
- **48kHz → 44.1kHz**: Video audio to CD quality
- **Stereo → Mono**: Reduce file size, voice processing
- **44.1kHz → 16kHz**: Voice recognition preprocessing
- **Mono → Stereo**: Compatibility with stereo-only systems

### Technical Details
- Uses libswresample for high-quality resampling
- Output format: WAV, 16-bit PCM
- Supports any input sample rate
- Maintains audio quality during conversion

## Audio Mixer

### Purpose
Mix two audio files together with independent volume control.

### Usage
```bash
./audio_mixer <input1> <input2> <output> [volume1] [volume2]
```

### Parameters
- `volume1` - Volume for first input (0.0 to 1.0, default: 0.5)
- `volume2` - Volume for second input (0.0 to 1.0, default: 0.5)

### Examples
```bash
# Mix two files with equal volume
./audio_mixer music.mp3 voice.mp3 mixed.wav 0.5 0.5

# Music background with louder voice
./audio_mixer music.mp3 voice.mp3 mixed.wav 0.3 0.7

# Add sound effect to music
./audio_mixer music.mp3 effect.wav output.wav 0.8 0.4
```

### Features
- Mix two audio streams
- Independent volume control
- Automatic resampling to match sample rates
- Automatic channel layout matching
- Clipping prevention
- Progress reporting

### Technical Details
- Output format: WAV, 44.1kHz, Stereo, 16-bit
- Automatic format normalization
- Sample-by-sample mixing
- Overflow protection
- Zero-padding for different length inputs

### Use Cases
1. **Background Music**
   - Add music behind voice narration
   - Create podcast intros

2. **Sound Effects**
   - Layer sound effects over music
   - Create complex soundscapes

3. **Mixing Tracks**
   - Combine multiple audio sources
   - Create mashups

4. **Audio Ducking Simulation**
   - Lower music volume relative to voice
   - Example: `./audio_mixer music.mp3 voice.mp3 out.wav 0.3 0.8`

### Mixing Algorithm
```cpp
// For each sample:
mixed_sample = (input1_sample * volume1) + (input2_sample * volume2)

// With clipping prevention:
if (mixed_sample > 32767) mixed_sample = 32767
if (mixed_sample < -32768) mixed_sample = -32768
```

## Common Audio Workflows

### Workflow 1: Create Test Audio
```bash
# 1. Generate tone
./audio_encoder test_tone.mp3 10 440

# 2. Check audio info
./audio_info test_tone.mp3

# 3. Convert to WAV
./audio_decoder test_tone.mp3 test_tone.wav
```

### Workflow 2: Audio Format Conversion
```bash
# 1. Decode to WAV
./audio_decoder input.mp3 temp.wav

# 2. Resample if needed
./audio_resampler temp.wav resampled.wav 48000 2

# 3. Encode to desired format
# (Use external encoder or audio_encoder for synthesis)
```

### Workflow 3: Create Audio Mix
```bash
# 1. Generate background music
./audio_encoder music.mp3 30 220

# 2. Mix with voice
./audio_mixer music.mp3 voice.mp3 final.wav 0.4 0.8

# 3. Check result
./audio_info final.wav
```

### Workflow 4: Audio Preprocessing
```bash
# 1. Convert to mono for voice recognition
./audio_resampler input.wav mono.wav 16000 1

# 2. Check format
./audio_info mono.wav
```

## FFmpeg Audio Libraries Used

### libavformat
- Container format handling
- Reading audio files
- Writing audio files

### libavcodec
- Audio codec encoding/decoding
- MP3, AAC, Vorbis, FLAC support

### libswresample
- Sample rate conversion
- Channel layout conversion
- Sample format conversion

### libavutil
- Utility functions
- Channel layout definitions
- Sample format definitions

## Supported Audio Formats

### Input Formats
- MP3
- AAC, M4A
- WAV
- FLAC
- Ogg Vorbis
- WMA
- ALAC (Apple Lossless)
- And many more

### Output Formats
- WAV (decoder, resampler, mixer)
- MP3 (encoder)
- AAC (encoder)
- Ogg Vorbis (encoder)
- FLAC (encoder)

## Common Issues

### Issue: "Codec not found"
**Solution:**
```bash
# Check available codecs
ffmpeg -codecs | grep audio

# Install required libraries (macOS)
brew reinstall ffmpeg --with-libmp3lame --with-libvorbis
```

### Issue: "No audio stream found"
**Solution:**
- Verify file contains audio: `ffprobe file.mp4`
- File may be video-only or corrupted

### Issue: Output volume too low/high
**Solution:**
- Adjust mixer volumes (recommended: sum of volumes ≤ 1.0)
- Example: `./audio_mixer in1.mp3 in2.mp3 out.wav 0.4 0.4`

### Issue: Resampling quality
**Solution:**
- libswresample uses high-quality resampling by default
- For highest quality, use higher sample rates (96kHz)
- Avoid multiple resampling steps

## Performance Tips

1. **Use appropriate sample rates**
   - 44.1kHz for music
   - 48kHz for video
   - 16kHz for voice

2. **Mono vs Stereo**
   - Use mono for voice to reduce file size
   - Use stereo for music and effects

3. **Batch processing**
   - Process multiple files with shell scripts
   - Use parallel processing for independent files

4. **Format selection**
   - MP3: Good compression, universal support
   - AAC: Better quality than MP3 at same bitrate
   - FLAC: Lossless, larger files
   - Ogg Vorbis: Open format, good compression

## Additional Resources

- [FFmpeg Audio Filters](https://ffmpeg.org/ffmpeg-filters.html#Audio-Filters)
- [libswresample Documentation](https://ffmpeg.org/libswresample.html)
- [Audio Codec Documentation](https://ffmpeg.org/ffmpeg-codecs.html#Audio-Encoders)

## See Also

- [FFmpeg API Guide](ffmpeg_api.md)
- [Video Samples Documentation](../README.md)
- [Main README](../../README.md)
