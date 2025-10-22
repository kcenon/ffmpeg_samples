# FFmpeg API 가이드

## 소개

이 가이드는 샘플 애플리케이션에서 사용되는 FFmpeg API 함수와 구조체에 대한 포괄적인 문서를 제공합니다. FFmpeg는 다양한 오디오 및 비디오 포맷의 인코딩, 디코딩, 트랜스코딩, 먹싱, 디먹싱, 필터링 및 재생을 위한 라이브러리를 제공하는 강력한 멀티미디어 프레임워크입니다.

## FFmpeg 라이브러리 개요

### 핵심 라이브러리

#### libavformat
컨테이너 포맷 처리 (먹싱 및 디먹싱).

**목적:**
- 컨테이너 포맷 읽기 및 쓰기 (MP4, AVI, MKV 등)
- 컨테이너 내 스트림 관리
- 메타데이터 및 챕터 처리
- 네트워크 프로토콜 지원

**주요 구조체:**
- `AVFormatContext` - 포맷 I/O 컨텍스트
- `AVStream` - 스트림 정보
- `AVIOContext` - 사용자 정의 프로토콜을 위한 I/O 컨텍스트

#### libavcodec
코덱 인코딩 및 디코딩.

**목적:**
- 오디오/비디오 스트림 인코딩 및 디코딩
- 코덱 관리 및 구성
- 프레임 및 패킷 처리
- 하드웨어 가속 지원

**주요 구조체:**
- `AVCodec` - 코덱 정보
- `AVCodecContext` - 구성이 포함된 코덱 컨텍스트
- `AVPacket` - 압축된 데이터 패킷
- `AVFrame` - 디코딩된 프레임 데이터

#### libavutil
유틸리티 함수 및 구조체.

**목적:**
- 메모리 관리
- 수학 연산
- 픽셀 포맷 및 샘플 포맷 정의
- 에러 처리
- 로깅
- 타이밍 및 유리수 연산

**주요 구조체:**
- `AVRational` - 유리수 (분자/분모)
- `AVDictionary` - 키-값 사전
- `AVBuffer` - 참조 카운팅 버퍼

#### libavfilter
비디오 및 오디오 필터링.

**목적:**
- 필터 그래프 생성
- 효과 및 변환 적용
- 복잡한 처리 파이프라인
- 오디오/비디오 조작

**주요 구조체:**
- `AVFilterGraph` - 필터 그래프
- `AVFilterContext` - 필터 인스턴스
- `AVFilter` - 필터 정의
- `AVFilterInOut` - 필터 입력/출력

#### libswscale
이미지 스케일링 및 색 공간 변환.

**목적:**
- 이미지 스케일링
- 픽셀 포맷 변환
- 색 공간 변환
- 종횡비 처리

**주요 구조체:**
- `SwsContext` - 스케일링 컨텍스트

#### libswresample
오디오 리샘플링.

**목적:**
- 샘플 레이트 변환
- 채널 레이아웃 변환
- 샘플 포맷 변환
- 오디오 믹싱

**주요 구조체:**
- `SwrContext` - 리샘플링 컨텍스트

## 일반적인 API 패턴

### 파일 열기 및 읽기

```cpp
// 입력 파일 열기
AVFormatContext* format_ctx = nullptr;
int ret = avformat_open_input(&format_ctx, filename, nullptr, nullptr);
if (ret < 0) {
    // 에러 처리
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
    std::cerr << "Error: " << errbuf << "\n";
    return -1;
}

// 스트림 정보 찾기
ret = avformat_find_stream_info(format_ctx, nullptr);
if (ret < 0) {
    avformat_close_input(&format_ctx);
    return -1;
}

// 비디오 스트림 찾기
int video_stream_index = -1;
for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
    if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        video_stream_index = i;
        break;
    }
}

// 정리
avformat_close_input(&format_ctx);
```

### 디코더 설정

```cpp
// 코덱 매개변수 가져오기
AVCodecParameters* codecpar = format_ctx->streams[video_stream_index]->codecpar;

// 디코더 찾기
const AVCodec* decoder = avcodec_find_decoder(codecpar->codec_id);
if (!decoder) {
    std::cerr << "Decoder not found\n";
    return -1;
}

// 코덱 컨텍스트 할당
AVCodecContext* codec_ctx = avcodec_alloc_context3(decoder);
if (!codec_ctx) {
    std::cerr << "Failed to allocate codec context\n";
    return -1;
}

// 매개변수를 컨텍스트로 복사
ret = avcodec_parameters_to_context(codec_ctx, codecpar);
if (ret < 0) {
    avcodec_free_context(&codec_ctx);
    return -1;
}

// 코덱 열기
ret = avcodec_open2(codec_ctx, decoder, nullptr);
if (ret < 0) {
    avcodec_free_context(&codec_ctx);
    return -1;
}

// 정리
avcodec_free_context(&codec_ctx);
```

### 프레임 디코딩

```cpp
AVPacket* packet = av_packet_alloc();
AVFrame* frame = av_frame_alloc();

// 패킷 읽기
while (av_read_frame(format_ctx, packet) >= 0) {
    if (packet->stream_index == video_stream_index) {
        // 디코더에 패킷 전송
        ret = avcodec_send_packet(codec_ctx, packet);
        if (ret < 0) {
            std::cerr << "Error sending packet\n";
            break;
        }

        // 디코딩된 프레임 수신
        while (ret >= 0) {
            ret = avcodec_receive_frame(codec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                std::cerr << "Error receiving frame\n";
                break;
            }

            // 프레임 처리
            // ...

            av_frame_unref(frame);
        }
    }
    av_packet_unref(packet);
}

// 정리
av_packet_free(&packet);
av_frame_free(&frame);
```

## 메모리 관리

### 할당 및 해제

FFmpeg는 다양한 할당 방법을 사용합니다:

```cpp
// AVFormatContext
AVFormatContext* fmt_ctx = nullptr;
avformat_open_input(&fmt_ctx, filename, nullptr, nullptr);
avformat_close_input(&fmt_ctx);  // 메모리도 해제

// AVCodecContext
AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
avcodec_free_context(&codec_ctx);

// AVPacket
AVPacket* packet = av_packet_alloc();
av_packet_free(&packet);

// AVFrame
AVFrame* frame = av_frame_alloc();
av_frame_free(&frame);

// 일반 메모리
uint8_t* buffer = (uint8_t*)av_malloc(size);
av_free(buffer);
```

### 참조 카운팅

FFmpeg는 프레임과 패킷에 참조 카운팅을 사용합니다:

```cpp
// 참조 카운트 증가
AVFrame* ref_frame = av_frame_clone(frame);

// 참조 카운트 감소
av_frame_unref(frame);

// 패킷의 경우
av_packet_unref(packet);
```

## 에러 처리

### 에러 코드

```cpp
int ret = some_ffmpeg_function();
if (ret < 0) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, errbuf, AV_ERROR_MAX_STRING_SIZE);
    std::cerr << "Error: " << errbuf << "\n";
}
```

### 일반적인 에러 코드

- `AVERROR(EAGAIN)` - 출력을 사용할 수 없음, 재시도
- `AVERROR_EOF` - 파일 끝
- `AVERROR(ENOMEM)` - 메모리 부족
- `AVERROR(EINVAL)` - 잘못된 인수
- `AVERROR_DECODER_NOT_FOUND` - 디코더를 찾을 수 없음
- `AVERROR_ENCODER_NOT_FOUND` - 인코더를 찾을 수 없음

## 타이밍 및 타임스탬프

### 타임 베이스

```cpp
// 타임 베이스는 유리수 (num/den)
AVRational time_base = stream->time_base;

// 타임스탬프를 초로 변환
double seconds = timestamp * av_q2d(time_base);

// 초를 타임스탬프로 변환
int64_t timestamp = seconds / av_q2d(time_base);
```

### 프레젠테이션 타임스탬프 (PTS)

```cpp
// 각 프레임/패킷에는 PTS가 있음
frame->pts = frame_number;

// 다른 타임 베이스 간 타임스탬프 재조정
av_packet_rescale_ts(packet, src_time_base, dst_time_base);
```

## 픽셀 포맷

### 일반적인 픽셀 포맷

- `AV_PIX_FMT_YUV420P` - 가장 일반적, 평면 YUV 4:2:0
- `AV_PIX_FMT_RGB24` - RGB 24비트
- `AV_PIX_FMT_BGR24` - BGR 24비트
- `AV_PIX_FMT_RGBA` - RGBA 32비트
- `AV_PIX_FMT_NV12` - 반평면 YUV 4:2:0

## 코덱 옵션

### 옵션 설정

```cpp
// 코덱 전용 옵션 설정
av_opt_set(codec_ctx->priv_data, "preset", "slow", 0);
av_opt_set(codec_ctx->priv_data, "crf", "23", 0);

// 정수 옵션 설정
av_opt_set_int(codec_ctx->priv_data, "option_name", value, 0);
```

### H.264 프리셋

- `ultrafast` - 가장 빠른 인코딩, 최저 품질
- `superfast` - 매우 빠름, 낮은 품질
- `veryfast` - 빠름, 허용 가능한 품질
- `faster` - medium보다 빠름
- `fast` - 빠른 인코딩
- `medium` - 기본, 균형잡힌
- `slow` - 느림, 더 나은 품질
- `slower` - 매우 느림, 매우 좋은 품질
- `veryslow` - 가장 느림, 최고 품질

## 스레딩

### 멀티스레딩 활성화

```cpp
// 스레드 수 설정 (0 = 자동)
codec_ctx->thread_count = 0;

// 스레드 타입 설정
codec_ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
```

## 모범 사례

1. **항상 반환 값 확인** - FFmpeg 함수는 에러 코드를 반환
2. **할당된 리소스 해제** - 메모리 누수 방지
3. **참조 카운팅 사용** - 프레임 및 패킷용
4. **EAGAIN 적절히 처리** - 필요시 작업 재시도
5. **디코더/인코더 플러시** - 남은 데이터를 플러시하기 위해 nullptr 전송
6. **타임스탬프 재조정** - 타임 베이스 변경 시
7. **코덱 지원 확인** - 픽셀 포맷 및 기능 검증
8. **적절한 타임 베이스 사용** - 스트림 타임 베이스와 일치
9. **스레드 수 설정** - 더 나은 성능을 위해 멀티스레딩 활성화
10. **에러 적절히 로깅** - 에러 메시지에 av_strerror 사용

## 추가 리소스

- [공식 FFmpeg 문서](https://ffmpeg.org/documentation.html)
- [FFmpeg API Doxygen](https://ffmpeg.org/doxygen/trunk/index.html)
- [FFmpeg Wiki](https://trac.ffmpeg.org/wiki)
- [FFmpeg 예제](https://github.com/FFmpeg/FFmpeg/tree/master/doc/examples)

## 참조

- [비디오 정보 문서](video_info.md)
- [비디오 디코더 문서](video_decoder.md)
- [비디오 인코더 문서](video_encoder.md)
- [비디오 트랜스코더 문서](video_transcoder.md)
- [비디오 필터 문서](video_filter.md)
- [메인 README](../../README.md)
