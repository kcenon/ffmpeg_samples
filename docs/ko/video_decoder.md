# 비디오 디코더

## 개요

`video_decoder` 애플리케이션은 다양한 비디오 포맷에서 비디오 프레임을 디코딩하여 개별 PPM(Portable Pixmap) 이미지 파일로 저장하는 방법을 보여줍니다. 이 도구는 프레임별 비디오 분석, 썸네일 생성 및 비디오 디코딩 워크플로우 이해에 필수적입니다.

## 기능

- FFmpeg가 지원하는 모든 포맷에서 비디오 프레임 디코딩
- 프레임을 RGB 포맷으로 변환
- 프레임을 PPM 이미지로 저장
- 구성 가능한 프레임 추출 제한
- 디코딩 중 진행 상황 보고
- 자동 픽셀 포맷 변환
- 다양한 비디오 코덱 지원

## 사용법

```bash
./video_decoder <입력_파일> <출력_디렉토리> [최대_프레임수]
```

### 매개변수

- `입력_파일` - 입력 비디오 파일 경로 (필수)
- `출력_디렉토리` - 디코딩된 프레임이 저장될 디렉토리 (필수)
- `최대_프레임수` - 디코딩할 최대 프레임 수 (선택사항, 기본값: 10)

### 전제조건

실행 전 출력 디렉토리 생성:
```bash
mkdir -p output_frames
```

## 예제

### 기본 사용법

```bash
# 첫 10개 프레임 디코딩
./video_decoder video.mp4 frames/

# 첫 100개 프레임 디코딩
./video_decoder video.mp4 frames/ 100

# 다른 포맷에서 디코딩
./video_decoder video.avi frames/ 50
./video_decoder video.mkv frames/ 25
```

### 일괄 처리

```bash
#!/bin/bash
# 여러 비디오에서 프레임 디코딩
for video in *.mp4; do
    dirname="${video%.mp4}_frames"
    mkdir -p "$dirname"
    ./video_decoder "$video" "$dirname" 30
done
```

## 출력 포맷

프레임은 순차 번호로 PPM 포맷으로 저장됩니다:
- `frame_0.ppm`
- `frame_1.ppm`
- `frame_2.ppm`
- ...

### PPM 포맷을 사용하는 이유

PPM(Portable Pixmap)을 선택한 이유:
- 간단한 비압축 포맷
- 쓰기에 외부 라이브러리 불필요
- 읽고 처리하기 쉬움
- 이미지 뷰어에서 널리 지원
- 사람이 읽을 수 있는 헤더

### PPM 파일 변환

ImageMagick을 사용하여 다른 포맷으로 변환:
```bash
# PNG로 변환
convert frame_0.ppm frame_0.png

# 모든 프레임을 JPEG로 변환
mogrify -format jpg *.ppm

# 애니메이션 GIF 생성
convert -delay 10 frame_*.ppm animation.gif
```

## 구현 세부사항

### 디코딩 파이프라인

1. **입력 파일 열기**
   ```cpp
   avformat_open_input(&format_ctx, filename, nullptr, nullptr);
   ```

2. **스트림 정보 찾기**
   ```cpp
   avformat_find_stream_info(format_ctx, nullptr);
   ```

3. **비디오 스트림 찾기**
   ```cpp
   for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
       if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
           video_stream_index = i;
           break;
       }
   }
   ```

4. **디코더 초기화**
   ```cpp
   codec = avcodec_find_decoder(codecpar->codec_id);
   codec_ctx = avcodec_alloc_context3(codec);
   avcodec_parameters_to_context(codec_ctx, codecpar);
   avcodec_open2(codec_ctx, codec, nullptr);
   ```

5. **색상 변환 초기화**
   ```cpp
   sws_ctx = sws_getContext(
       codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
       codec_ctx->width, codec_ctx->height, AV_PIX_FMT_RGB24,
       SWS_BILINEAR, nullptr, nullptr, nullptr
   );
   ```

6. **패킷 읽기 및 디코딩**
   ```cpp
   while (av_read_frame(format_ctx, packet) >= 0) {
       if (packet->stream_index == video_stream_index) {
           avcodec_send_packet(codec_ctx, packet);
           while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
               // 프레임 처리
           }
       }
       av_packet_unref(packet);
   }
   ```

7. **색 공간 변환**
   ```cpp
   sws_scale(sws_ctx, frame->data, frame->linesize, 0,
             codec_ctx->height, frame_rgb->data, frame_rgb->linesize);
   ```

8. **프레임 저장**
   ```cpp
   save_frame_as_ppm(frame_rgb, width, height, frame_number, output_dir);
   ```

### 메모리 관리

디코더는 FFmpeg 리소스를 적절히 관리합니다:

```cpp
// 할당
packet = av_packet_alloc();
frame = av_frame_alloc();
frame_rgb = av_frame_alloc();
buffer = av_malloc(num_bytes);

// 사용...

// 해제
av_packet_free(&packet);
av_frame_free(&frame);
av_frame_free(&frame_rgb);
av_free(buffer);
sws_freeContext(sws_ctx);
avcodec_free_context(&codec_ctx);
avformat_close_input(&format_ctx);
```

## 픽셀 포맷 변환

### 지원되는 입력 포맷

디코더는 다양한 픽셀 포맷을 처리합니다:
- YUV420P (가장 일반적)
- YUV422P
- YUV444P
- RGB24, BGR24
- NV12, NV21
- 그 외 다수...

### 스케일링 알고리즘

`sws_getContext()`에서 사용 가능:
- `SWS_FAST_BILINEAR` - 빠르지만 낮은 품질
- `SWS_BILINEAR` - 좋은 균형 (샘플에서 사용)
- `SWS_BICUBIC` - 높은 품질, 느림
- `SWS_LANCZOS` - 최고 품질, 가장 느림

## 성능 고려사항

### 속도 최적화

1. **프레임 수 제한**
   - 필요한 프레임만 디코딩
   - `av_seek_frame()`으로 불필요한 프레임 건너뛰기

2. **하드웨어 가속**
   - 가능한 경우 하드웨어 디코더 사용
   - VideoToolbox (macOS), NVDEC (NVIDIA) 등

3. **스레딩**
   - FFmpeg 자동 멀티스레딩
   - `codec_ctx->thread_count` 설정

### 메모리 사용량

프레임당 일반적인 메모리:
- 1920x1080 RGB24: ~6 MB
- 3840x2160 RGB24: ~24 MB

메모리 사용량 = frame_size × buffered_frames

## 일반적인 문제

### 문제: "No video stream found"

**원인:**
- 오디오 전용 파일
- 손상된 비디오 스트림

**해결책:**
```bash
# ffprobe로 스트림 확인
ffprobe input.mp4
```

### 문제: "Codec not found"

**원인:**
- 지원되지 않는 코덱
- 코덱 라이브러리 누락

**해결책:**
```bash
# 사용 가능한 디코더 확인
ffmpeg -decoders | grep <codec_name>
```

### 문제: 출력 디렉토리 에러

**원인:**
- 디렉토리가 존재하지 않음
- 쓰기 권한 없음

**해결책:**
```bash
mkdir -p output_dir
chmod 755 output_dir
```

### 문제: 메모리 부족

**원인:**
- 한 번에 너무 많은 프레임
- 고해상도 비디오

**해결책:**
- `max_frames` 매개변수 줄이기
- 배치로 처리
- 압축된 출력 포맷 사용

## 고급 사용법

### 특정 프레임 추출

특정 프레임 번호를 추출하도록 코드 수정:

```cpp
std::vector<int> target_frames = {0, 10, 25, 50, 100};
if (std::find(target_frames.begin(), target_frames.end(),
              frame_count) != target_frames.end()) {
    save_frame_as_ppm(frame_rgb, width, height, frame_count, output_dir);
}
```

### 특정 시간으로 이동

```cpp
// 30초로 이동
int64_t timestamp = 30 * AV_TIME_BASE;
av_seek_frame(format_ctx, -1, timestamp, AVSEEK_FLAG_BACKWARD);
```

### 프레임 레이트 제어

N번째 프레임마다 추출:

```cpp
if (frame_count % N == 0) {
    save_frame_as_ppm(frame_rgb, width, height,
                      frame_count / N, output_dir);
}
```

## 사용 사례

1. **썸네일 생성**
   - 비디오 미리보기용 키 프레임 추출
   - 썸네일 갤러리 생성

2. **비디오 분석**
   - 프레임별 품질 검사
   - 모션 분석
   - 장면 감지

3. **컴퓨터 비전**
   - 학습 데이터 준비
   - 객체 감지 데이터셋
   - 이미지 전처리

4. **애니메이션 생성**
   - 편집용 프레임 추출
   - 스프라이트 시트 생성
   - 프레임 시퀀스 생성

5. **품질 보증**
   - 인코딩 아티팩트 시각적 검사
   - 준수 검사
   - 포맷 검증

## 통합 예제

### Python 통합

```python
import subprocess
import os

def extract_frames(video_path, output_dir, max_frames=10):
    os.makedirs(output_dir, exist_ok=True)
    subprocess.run([
        './video_decoder',
        video_path,
        output_dir,
        str(max_frames)
    ])
```

### C++ 통합

```cpp
#include <cstdlib>

void process_video(const std::string& video_path) {
    std::string command = "./video_decoder " +
                         video_path + " frames/ 50";
    system(command.c_str());
}
```

## API 참조

### 주요 FFmpeg 함수

- `avformat_open_input()` - 미디어 파일 열기
- `avformat_find_stream_info()` - 스트림 정보 읽기
- `avcodec_find_decoder()` - 코덱용 디코더 찾기
- `avcodec_alloc_context3()` - 코덱 컨텍스트 할당
- `avcodec_parameters_to_context()` - 매개변수 복사
- `avcodec_open2()` - 코덱 초기화
- `av_read_frame()` - 다음 패킷 읽기
- `avcodec_send_packet()` - 디코더에 패킷 보내기
- `avcodec_receive_frame()` - 디코딩된 프레임 받기
- `sws_getContext()` - 스케일러 초기화
- `sws_scale()` - 프레임 스케일 및 변환
- `av_frame_alloc()` - 프레임 할당
- `av_packet_alloc()` - 패킷 할당
- `av_image_get_buffer_size()` - 버퍼 크기 계산
- `av_image_fill_arrays()` - 이미지 평면 포인터 채우기

## 문제 해결

### 디버깅 팁

1. **FFmpeg 로깅 활성화**
   ```cpp
   av_log_set_level(AV_LOG_DEBUG);
   ```

2. **디코더 상태 확인**
   ```cpp
   if (ret < 0) {
       char errbuf[AV_ERROR_MAX_STRING_SIZE];
       av_strerror(ret, errbuf, sizeof(errbuf));
       std::cerr << "Error: " << errbuf << std::endl;
   }
   ```

3. **출력 파일 검증**
   ```bash
   ls -lh frames/
   file frames/frame_0.ppm
   ```

## 참조

- [비디오 인코더 문서](video_encoder.md)
- [비디오 필터 문서](video_filter.md)
- [FFmpeg API 가이드](ffmpeg_api.md)
- [메인 README](../../README.md)
