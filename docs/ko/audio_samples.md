# 오디오 처리 샘플

## 개요

이 문서는 FFmpeg 샘플 프로젝트에 포함된 모든 오디오 처리 샘플 애플리케이션을 다룹니다. 이 애플리케이션들은 FFmpeg 라이브러리를 사용한 다양한 오디오 조작 기술을 보여줍니다.

## Audio Info (오디오 정보)

### 목적
오디오 파일 메타데이터 및 스트림 정보를 읽고 표시합니다.

### 사용법
```bash
./audio_info <입력_파일>
```

### 예제
```bash
./audio_info audio.mp3
./audio_info music.flac
```

### 기능
- 코덱 정보 표시
- 샘플 레이트 및 비트 레이트 표시
- 채널 레이아웃 표시
- 샘플 포맷 표시
- 메타데이터 추출 (아티스트, 제목, 앨범 등)
- 재생 시간 정보

## Audio Decoder (오디오 디코더)

### 목적
오디오 파일을 디코딩하여 WAV (PCM) 포맷으로 저장합니다.

### 사용법
```bash
./audio_decoder <입력_파일> <출력_파일>
```

### 예제
```bash
./audio_decoder music.mp3 output.wav
./audio_decoder audio.aac output.wav
```

### 기능
- FFmpeg가 지원하는 모든 오디오 포맷 디코딩
- 표준 WAV 포맷으로 변환
- 16비트 PCM 출력
- 스테레오, 44.1kHz 출력
- 자동 리샘플링
- 진행 상황 보고

### 기술 세부사항
- 출력 포맷: WAV
- 샘플 포맷: 16비트 부호있는 정수 (S16)
- 샘플 레이트: 44.1kHz
- 채널: 스테레오 (2)
- 포맷 변환에 libswresample 사용

## Audio Encoder (오디오 인코더)

### 목적
오디오 (사인파 톤)를 생성하고 다양한 포맷으로 인코딩합니다.

### 사용법
```bash
./audio_encoder <출력_파일> [재생시간_초] [주파수_Hz]
```

### 예제
```bash
# 440Hz (A4 음표) 5초 생성
./audio_encoder output.mp3 5 440

# 880Hz (A5 음표) 10초 생성
./audio_encoder output.aac 10 880

# 다양한 포맷 생성
./audio_encoder output.mp3   # MP3 포맷
./audio_encoder output.aac   # AAC 포맷
./audio_encoder output.ogg   # Ogg Vorbis 포맷
./audio_encoder output.flac  # FLAC 포맷
```

### 기능
- 사인파 톤 생성
- 다중 출력 포맷 지원
- 구성 가능한 재생 시간
- 구성 가능한 주파수
- 자동 코덱 선택
- 44.1kHz 스테레오 출력

### 지원 포맷
- MP3 (libmp3lame 사용)
- AAC
- Ogg Vorbis (libvorbis 사용)
- FLAC
- M4A

### 사용 사례
- 테스트 톤 생성
- 오디오 테스트 신호 생성
- 오디오 시스템 보정
- 오디오 인코딩 학습

## Audio Resampler (오디오 리샘플러)

### 목적
오디오 샘플 레이트 및/또는 채널 레이아웃 변경 (스테레오를 모노로 또는 그 반대).

### 사용법
```bash
./audio_resampler <입력_파일> <출력_파일> [샘플_레이트] [채널수]
```

### 매개변수
- `샘플_레이트` - 대상 샘플 레이트(Hz) (기본값: 44100)
- `채널수` - 대상 채널: 1 (모노) 또는 2 (스테레오) (기본값: 2)

### 예제
```bash
# 48kHz 스테레오로 변환
./audio_resampler input.mp3 output.wav 48000 2

# 22050Hz 모노로 변환
./audio_resampler input.wav output.wav 22050 1

# 표준 CD 품질 (44.1kHz 스테레오)
./audio_resampler input.flac output.wav 44100 2

# 음성용 다운샘플링 (16kHz 모노)
./audio_resampler voice.wav voice_16k.wav 16000 1
```

### 기능
- 샘플 레이트 변환
- 채널 레이아웃 변환 (모노/스테레오)
- libswresample을 사용한 고품질 리샘플링
- 자동 포맷 변환
- WAV 출력 포맷
- 진행 상황 보고

### 일반적인 사용 사례
- **48kHz → 44.1kHz**: 비디오 오디오를 CD 품질로
- **스테레오 → 모노**: 파일 크기 감소, 음성 처리
- **44.1kHz → 16kHz**: 음성 인식 전처리
- **모노 → 스테레오**: 스테레오 전용 시스템과의 호환성

### 기술 세부사항
- 고품질 리샘플링에 libswresample 사용
- 출력 포맷: WAV, 16비트 PCM
- 모든 입력 샘플 레이트 지원
- 변환 중 오디오 품질 유지

## Audio Mixer (오디오 믹서)

### 목적
두 오디오 파일을 독립적인 볼륨 제어로 믹싱합니다.

### 사용법
```bash
./audio_mixer <입력1> <입력2> <출력> [볼륨1] [볼륨2]
```

### 매개변수
- `볼륨1` - 첫 번째 입력의 볼륨 (0.0 ~ 1.0, 기본값: 0.5)
- `볼륨2` - 두 번째 입력의 볼륨 (0.0 ~ 1.0, 기본값: 0.5)

### 예제
```bash
# 동일한 볼륨으로 두 파일 믹싱
./audio_mixer music.mp3 voice.mp3 mixed.wav 0.5 0.5

# 음악 배경에 더 큰 음성
./audio_mixer music.mp3 voice.mp3 mixed.wav 0.3 0.7

# 음악에 효과음 추가
./audio_mixer music.mp3 effect.wav output.wav 0.8 0.4
```

### 기능
- 두 오디오 스트림 믹싱
- 독립적인 볼륨 제어
- 샘플 레이트 일치를 위한 자동 리샘플링
- 자동 채널 레이아웃 매칭
- 클리핑 방지
- 진행 상황 보고

### 기술 세부사항
- 출력 포맷: WAV, 44.1kHz, 스테레오, 16비트
- 자동 포맷 정규화
- 샘플별 믹싱
- 오버플로 보호
- 다른 길이 입력을 위한 제로 패딩

### 사용 사례
1. **배경 음악**
   - 음성 나레이션 뒤에 음악 추가
   - 팟캐스트 인트로 생성

2. **효과음**
   - 음악 위에 효과음 레이어링
   - 복잡한 사운드스케이프 생성

3. **트랙 믹싱**
   - 여러 오디오 소스 결합
   - 매시업 생성

4. **오디오 더킹 시뮬레이션**
   - 음성에 비해 음악 볼륨 낮추기
   - 예: `./audio_mixer music.mp3 voice.mp3 out.wav 0.3 0.8`

### 믹싱 알고리즘
```cpp
// 각 샘플에 대해:
mixed_sample = (input1_sample * volume1) + (input2_sample * volume2)

// 클리핑 방지:
if (mixed_sample > 32767) mixed_sample = 32767
if (mixed_sample < -32768) mixed_sample = -32768
```

## 일반적인 오디오 워크플로우

### 워크플로우 1: 테스트 오디오 생성
```bash
# 1. 톤 생성
./audio_encoder test_tone.mp3 10 440

# 2. 오디오 정보 확인
./audio_info test_tone.mp3

# 3. WAV로 변환
./audio_decoder test_tone.mp3 test_tone.wav
```

### 워크플로우 2: 오디오 포맷 변환
```bash
# 1. WAV로 디코딩
./audio_decoder input.mp3 temp.wav

# 2. 필요시 리샘플링
./audio_resampler temp.wav resampled.wav 48000 2

# 3. 원하는 포맷으로 인코딩
# (합성을 위해 외부 인코더 또는 audio_encoder 사용)
```

### 워크플로우 3: 오디오 믹스 생성
```bash
# 1. 배경 음악 생성
./audio_encoder music.mp3 30 220

# 2. 음성과 믹싱
./audio_mixer music.mp3 voice.mp3 final.wav 0.4 0.8

# 3. 결과 확인
./audio_info final.wav
```

### 워크플로우 4: 오디오 전처리
```bash
# 1. 음성 인식용 모노로 변환
./audio_resampler input.wav mono.wav 16000 1

# 2. 포맷 확인
./audio_info mono.wav
```

## 사용된 FFmpeg 오디오 라이브러리

### libavformat
- 컨테이너 포맷 처리
- 오디오 파일 읽기
- 오디오 파일 쓰기

### libavcodec
- 오디오 코덱 인코딩/디코딩
- MP3, AAC, Vorbis, FLAC 지원

### libswresample
- 샘플 레이트 변환
- 채널 레이아웃 변환
- 샘플 포맷 변환

### libavutil
- 유틸리티 함수
- 채널 레이아웃 정의
- 샘플 포맷 정의

## 지원 오디오 포맷

### 입력 포맷
- MP3
- AAC, M4A
- WAV
- FLAC
- Ogg Vorbis
- WMA
- ALAC (Apple Lossless)
- 그 외 다수

### 출력 포맷
- WAV (디코더, 리샘플러, 믹서)
- MP3 (인코더)
- AAC (인코더)
- Ogg Vorbis (인코더)
- FLAC (인코더)

## 일반적인 문제

### 문제: "Codec not found"
**해결책:**
```bash
# 사용 가능한 코덱 확인
ffmpeg -codecs | grep audio

# 필요한 라이브러리 설치 (macOS)
brew reinstall ffmpeg --with-libmp3lame --with-libvorbis
```

### 문제: "No audio stream found"
**해결책:**
- 파일에 오디오가 포함되어 있는지 확인: `ffprobe file.mp4`
- 파일이 비디오 전용이거나 손상되었을 수 있음

### 문제: 출력 볼륨이 너무 낮거나 높음
**해결책:**
- 믹서 볼륨 조정 (권장: 볼륨 합계 ≤ 1.0)
- 예: `./audio_mixer in1.mp3 in2.mp3 out.wav 0.4 0.4`

### 문제: 리샘플링 품질
**해결책:**
- libswresample은 기본적으로 고품질 리샘플링 사용
- 최고 품질을 위해 높은 샘플 레이트 사용 (96kHz)
- 여러 리샘플링 단계 피하기

## 성능 팁

1. **적절한 샘플 레이트 사용**
   - 음악: 44.1kHz
   - 비디오: 48kHz
   - 음성: 16kHz

2. **모노 vs 스테레오**
   - 음성에는 파일 크기 감소를 위해 모노 사용
   - 음악 및 효과에는 스테레오 사용

3. **일괄 처리**
   - 셸 스크립트로 여러 파일 처리
   - 독립적인 파일에 병렬 처리 사용

4. **포맷 선택**
   - MP3: 좋은 압축, 범용 지원
   - AAC: 동일한 비트레이트에서 MP3보다 나은 품질
   - FLAC: 무손실, 더 큰 파일
   - Ogg Vorbis: 오픈 포맷, 좋은 압축

## 추가 리소스

- [FFmpeg 오디오 필터](https://ffmpeg.org/ffmpeg-filters.html#Audio-Filters)
- [libswresample 문서](https://ffmpeg.org/libswresample.html)
- [오디오 코덱 문서](https://ffmpeg.org/ffmpeg-codecs.html#Audio-Encoders)

## 참조

- [FFmpeg API 가이드](ffmpeg_api.md)
- [비디오 샘플 문서](../README.md)
- [메인 README](../../README.md)
