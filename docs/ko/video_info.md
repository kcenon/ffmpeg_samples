# 비디오 정보 리더

## 개요

`video_info` 애플리케이션은 FFmpeg 라이브러리를 사용하여 비디오 파일의 포괄적인 메타데이터와 스트림 정보를 읽어 표시하는 명령줄 도구입니다. 비디오 및 오디오 스트림, 코덱, 포맷, 인코딩 매개변수에 대한 상세한 정보를 제공합니다.

## 기능

- 컨테이너 포맷 정보 표시
- 비디오 스트림 세부 정보 표시 (코덱, 해상도, 프레임 레이트, 비트레이트)
- 오디오 스트림 세부 정보 표시 (코덱, 샘플 레이트, 채널, 비트레이트)
- 재생 시간 및 타임스탬프 표시
- 다중 스트림 지원
- 읽기 쉬운 출력 형식

## 사용법

```bash
./video_info <입력_파일>
```

### 매개변수

- `입력_파일` - 분석할 비디오 파일 경로 (필수)

### 지원 포맷

FFmpeg가 지원하는 모든 포맷 포함:
- MP4 (H.264, H.265, VP9)
- AVI (다양한 코덱)
- MKV (Matroska)
- MOV (QuickTime)
- FLV (Flash Video)
- WebM
- MPEG/TS

## 출력 예시

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

## 코드 구조

### 주요 구성 요소

1. **avformat_open_input()** - 입력 파일 열기
2. **avformat_find_stream_info()** - 스트림 정보 검색
3. **스트림 반복** - 파일의 모든 스트림 순회
4. **print_stream_info()** - 스트림 세부 정보 포맷 및 표시를 위한 사용자 정의 함수

### 주요 FFmpeg 구조체

- `AVFormatContext` - 포맷 및 스트림 정보 포함
- `AVStream` - 단일 스트림(비디오, 오디오, 자막 등) 표현
- `AVCodecParameters` - 코덱별 매개변수 포함
- `AVCodec` - 코덱 정보

## 구현 세부사항

### 파일 열기

```cpp
AVFormatContext* format_ctx = nullptr;
int ret = avformat_open_input(&format_ctx, filename, nullptr, nullptr);
if (ret < 0) {
    // 에러 처리
}
```

### 스트림 정보 얻기

```cpp
ret = avformat_find_stream_info(format_ctx, nullptr);
if (ret < 0) {
    // 에러 처리
}
```

### 스트림 순회

```cpp
for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
    AVStream* stream = format_ctx->streams[i];
    AVCodecParameters* codecpar = stream->codecpar;

    if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        // 비디오 스트림 처리
    } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        // 오디오 스트림 처리
    }
}
```

### 재생 시간 계산

```cpp
// 파일 재생 시간(초 단위)
if (format_ctx->duration != AV_NOPTS_VALUE) {
    double duration = format_ctx->duration / static_cast<double>(AV_TIME_BASE);
}

// 스트림 재생 시간(초 단위)
if (stream->duration != AV_NOPTS_VALUE) {
    double duration = stream->duration * av_q2d(stream->time_base);
}
```

### 프레임 레이트 얻기

```cpp
if (stream->avg_frame_rate.den && stream->avg_frame_rate.num) {
    double fps = av_q2d(stream->avg_frame_rate);
}
```

## 사용 사례

1. **파일 검증** - 비디오 파일 무결성 및 포맷 확인
2. **스트림 분석** - 처리 전 인코딩 매개변수 분석
3. **포맷 감지** - 컨테이너 및 코덱 유형 식별
4. **품질 평가** - 해상도, 비트레이트, 인코딩 설정 확인
5. **호환성 테스트** - 변환 전 포맷 호환성 확인

## 일반적인 문제

### 문제: "Error opening input file"

**원인:**
- 파일이 존재하지 않음
- 파일 경로가 잘못됨
- 권한 부족
- 파일 손상

**해결책:**
- `ls -l`로 파일 경로 확인
- `chmod`로 파일 권한 확인
- `ffprobe` 명령으로 파일 테스트

### 문제: "Codec not found"

**원인:**
- 특정 코덱 지원 없이 FFmpeg 컴파일됨
- 독점 코덱 사용 불가

**해결책:**
- FFmpeg 코덱 지원 확인: `ffmpeg -codecs`
- 필요한 코덱으로 FFmpeg 재컴파일
- 대체 코덱 사용

## 성능 고려사항

- 매우 빠른 작동 (대부분의 파일에 대해 밀리초 단위)
- 최소 메모리 사용
- 실제 디코딩 수행 안 함 (메타데이터만)
- 대용량 파일에 안전

## 에러 처리

애플리케이션은 FFmpeg의 에러 보고 시스템 사용:

```cpp
char errbuf[AV_ERROR_MAX_STRING_SIZE];
av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
std::cerr << "Error: " << errbuf << "\n";
```

## 정리

적절한 리소스 관리:

```cpp
avformat_close_input(&format_ctx);
```

이는 관련된 모든 메모리를 자동으로 해제합니다.

## 관련 도구

- **ffprobe** - FFmpeg의 공식 미디어 정보 도구
- **mediainfo** - 대체 미디어 분석 도구
- **ExifTool** - 메타데이터 추출 도구

## 고급 사용법

### 스크립트 예제

```bash
#!/bin/bash
# 디렉토리의 모든 비디오 분석
for file in *.mp4; do
    echo "분석 중: $file"
    ./video_info "$file"
    echo "---"
done
```

### 통합 예제

```cpp
// 자신의 애플리케이션에서 video_info 출력 사용
std::string command = "./video_info " + filename;
FILE* pipe = popen(command.c_str(), "r");
// 출력 파싱
pclose(pipe);
```

## API 참조

### 사용된 함수

- `avformat_open_input()` - 입력 파일 열기
- `avformat_find_stream_info()` - 스트림 정보 검색
- `avformat_close_input()` - 리소스 닫고 해제
- `avcodec_find_decoder()` - ID로 코덱 찾기
- `av_get_media_type_string()` - 미디어 타입 이름 얻기
- `av_get_pix_fmt_name()` - 픽셀 포맷 이름 얻기
- `av_q2d()` - rational을 double로 변환
- `av_strerror()` - 에러 코드를 문자열로 변환

## 참조

- [비디오 디코더 문서](video_decoder.md)
- [FFmpeg API 가이드](ffmpeg_api.md)
- [메인 README](../../README.md)
