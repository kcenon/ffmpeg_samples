# 비디오 전환 효과 가이드

## 개요

`video_transition` 샘플은 FFmpeg의 강력한 xfade 필터를 사용하여 두 비디오 클립 사이에 부드러운 전환 효과를 적용하는 방법을 보여줍니다. 전문적인 비디오 구성, 몽타주, 프레젠테이션 제작에 필수적인 기능입니다.

## 주요 기능

- **33개 이상의 전환 효과** - 다양한 전문 전환 효과 지원
- **사용자 정의 지속 시간** - 전환 효과 지속 시간 제어
- **유연한 타이밍** - 전환이 발생하는 시점 선택 가능
- **Modern C++20** - RAII 래퍼 및 구조화된 바인딩 사용
- **프로덕션 준비** - 적절한 오류 처리 및 리소스 관리

## 사용법

```bash
./video_transition <video1> <video2> <output> <transition> [duration] [offset]
```

### 매개변수

- `video1` - 첫 번째 비디오 클립 (먼저 재생)
- `video2` - 두 번째 비디오 클립 (전환 후 재생)
- `output` - 출력 비디오 파일 경로
- `transition` - 전환 효과 유형 (아래 목록 참조)
- `duration` - (선택) 전환 지속 시간(초) (기본값: 1.0)
- `offset` - (선택) 전환 시작 시점(초) (기본값: 0 = video1 끝)

### 예제

**기본 페이드 전환:**
```bash
./video_transition clip1.mp4 clip2.mp4 output.mp4 fade
```

**2초 디졸브:**
```bash
./video_transition intro.mp4 main.mp4 result.mp4 dissolve 2.0
```

**사용자 정의 타이밍으로 오른쪽 슬라이드:**
```bash
./video_transition part1.mp4 part2.mp4 final.mp4 slideright 1.5 5.0
```

## 사용 가능한 전환 효과

### 기본 전환

| 전환 효과 | 설명 |
|----------|------|
| `fade` | 클래식 페이드 전환 |
| `dissolve` | 부드러운 디졸브 효과 |
| `fadeblack` | 검은색을 통한 페이드 |
| `fadewhite` | 흰색을 통한 페이드 |
| `fadegrays` | 회색조를 통한 페이드 |

### 와이프 전환

| 전환 효과 | 설명 |
|----------|------|
| `wipeleft` | 오른쪽에서 왼쪽으로 와이프 |
| `wiperight` | 왼쪽에서 오른쪽으로 와이프 |
| `wipeup` | 아래에서 위로 와이프 |
| `wipedown` | 위에서 아래로 와이프 |

### 슬라이드 전환

| 전환 효과 | 설명 |
|----------|------|
| `slideleft` | 오른쪽에서 왼쪽으로 슬라이드 |
| `slideright` | 왼쪽에서 오른쪽으로 슬라이드 |
| `slideup` | 아래에서 위로 슬라이드 |
| `slidedown` | 위에서 아래로 슬라이드 |
| `smoothleft` | 부드러운 왼쪽 슬라이드 |
| `smoothright` | 부드러운 오른쪽 슬라이드 |
| `smoothup` | 부드러운 위쪽 슬라이드 |
| `smoothdown` | 부드러운 아래쪽 슬라이드 |

### 원형 전환

| 전환 효과 | 설명 |
|----------|------|
| `circlecrop` | 원형 크롭 전환 |
| `circleclose` | 원으로 닫기 |
| `circleopen` | 원에서 열기 |
| `radial` | 방사형 스윕 전환 |

### 대각선 전환

| 전환 효과 | 설명 |
|----------|------|
| `diagtl` | 왼쪽 위에서 대각선 |
| `diagtr` | 오른쪽 위에서 대각선 |
| `diagbl` | 왼쪽 아래에서 대각선 |
| `diagbr` | 오른쪽 아래에서 대각선 |

### 슬라이스 전환

| 전환 효과 | 설명 |
|----------|------|
| `hlslice` | 수평 슬라이스 왼쪽 |
| `hrslice` | 수평 슬라이스 오른쪽 |
| `vuslice` | 수직 슬라이스 위 |
| `vdslice` | 수직 슬라이스 아래 |

### 특수 효과

| 전환 효과 | 설명 |
|----------|------|
| `pixelize` | 픽셀화 효과 |
| `squeezeh` | 수평 스퀴즈 |
| `squeezev` | 수직 스퀴즈 |
| `distance` | 거리 변환 |

## 작동 원리

### 1. 입력 처리

샘플은 두 입력 비디오를 열고 분석합니다:

```cpp
input1_format_ctx_ = ffmpeg::open_input_format(video1.data());
input2_format_ctx_ = ffmpeg::open_input_format(video2.data());
```

### 2. 비디오 스트림 감지

두 파일에서 비디오 스트림을 찾습니다:

```cpp
input1_stream_idx_ = find_video_stream(input1_format_ctx_.get());
input2_stream_idx_ = find_video_stream(input2_format_ctx_.get());
```

### 3. 디코더 설정

두 입력 비디오에 대한 디코더를 설정합니다:

```cpp
setup_decoder(input1_format_ctx_.get(), input1_stream_idx_, input1_codec_ctx_);
setup_decoder(input2_format_ctx_.get(), input2_stream_idx_, input2_codec_ctx_);
```

### 4. 전환 처리

클립 간에 선택한 전환 효과를 적용합니다:

```cpp
// video1 처리
while (process_input(input1_format_ctx_.get(), ...)) {
    // 프레임 디코드 및 인코드
}

// 전환과 함께 video2 처리
while (process_input(input2_format_ctx_.get(), ...)) {
    // 전환 적용 및 인코드
}
```

### 5. 출력 인코딩

전환이 포함된 최종 비디오를 인코딩합니다:

```cpp
encode_frame(frame.get());
flush_encoder();
av_write_trailer(output_format_ctx_.get());
```

## 기술 세부사항

### 비디오 요구사항

- **형식**: FFmpeg이 지원하는 모든 비디오 형식 (MP4, AVI, MKV 등)
- **해상도**: 비디오가 다른 해상도를 가질 수 있음 (첫 번째 비디오의 해상도 사용)
- **코덱**: FFmpeg이 지원하는 모든 코덱
- **프레임 레이트**: 다른 프레임 레이트 자동 처리

### 출력 사양

- **코덱**: H.264 (libx264)
- **컨테이너**: 출력 파일 확장자로 결정
- **픽셀 형식**: YUV420P
- **비트레이트**: 2 Mbps (코드에서 설정 가능)
- **프레임 레이트**: 30 fps (코드에서 설정 가능)

### 전환 타이밍

전환 타이밍은 다음과 같이 계산됩니다:

- **기본 오프셋 (0)**: 전환이 `duration1 - transition_duration`에서 시작
- **사용자 정의 오프셋**: 첫 번째 비디오의 지정된 시간에 전환 시작

```cpp
const auto duration1 = static_cast<double>(input1_format_ctx_->duration) / AV_TIME_BASE;
transition_start_ = offset_ > 0 ? offset_ : duration1 - duration_;
```

## 일반적인 사용 사례

### 1. 비디오 몽타주

다른 장면 간의 부드러운 전환 만들기:

```bash
./video_transition scene1.mp4 scene2.mp4 montage.mp4 dissolve 1.5
```

### 2. 프레젠테이션 비디오

슬라이드 또는 섹션 간 전환:

```bash
./video_transition intro.mp4 content.mp4 presentation.mp4 slideright 1.0
```

### 3. 결혼식 비디오

순간 간의 우아한 전환 만들기:

```bash
./video_transition ceremony.mp4 reception.mp4 wedding.mp4 fade 2.0
```

### 4. 마케팅 비디오

홍보 콘텐츠의 전문적인 전환:

```bash
./video_transition product_intro.mp4 features.mp4 promo.mp4 circleopen 1.0
```

## 성능 고려사항

### 처리 시간

- 전환 처리는 CPU 집약적입니다
- 지속 시간은 비디오 길이, 해상도, 전환 복잡성에 따라 다릅니다
- 더 빠른 처리를 위해 하드웨어 가속 사용을 고려하세요

### 메모리 사용량

- 두 개의 입력 비디오가 동시에 처리됩니다
- 메모리 사용량은 비디오 해상도에 따라 확장됩니다
- HD 비디오의 경우 최소 4GB RAM 권장

### 최적화 팁

1. **해상도 줄이기**: 최종 출력이 허용되는 경우 낮은 해상도로 처리
2. **짧은 클립**: 전환 전에 필요한 부분으로 비디오 트리밍
3. **하드웨어 가속**: 가능한 경우 하드웨어 인코더 (VideoToolbox, NVENC) 사용
4. **비트레이트 조정**: 품질이 허용되는 경우 더 빠른 인코딩을 위해 비트레이트 낮추기

## 문제 해결

### 다른 해상도

입력 비디오의 해상도가 다른 경우 경고가 표시되고 첫 번째 비디오의 해상도가 사용됩니다:

```
Warning: Input videos have different resolutions. Using first video's resolution.
```

최상의 결과를 위해 비디오를 동일한 해상도로 사전 스케일링하는 것을 고려하세요.

### 잘못된 전환

잘못된 전환 이름이 제공된 경우:

```
Error: Invalid transition: xyz
```

사용 가능한 전환 목록을 확인하고 올바른 철자를 사용하세요.

### 코덱을 찾을 수 없음

H.264 인코더를 사용할 수 없는 경우:

```
Error: H264 encoder not found
```

FFmpeg이 libx264 지원으로 컴파일되었는지 확인하세요:

```bash
ffmpeg -codecs | grep h264
```

### 지속 시간 검증

전환 지속 시간은 0에서 10초 사이여야 합니다:

```
Duration must be between 0 and 10 seconds
```

## 코드 구조

### 주요 클래스: VideoTransition

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

### 주요 메서드

- `initialize()` - 입력/출력 컨텍스트 및 디코더 설정
- `setup_decoder()` - 비디오 디코더 구성
- `setup_output()` - H.264 인코더 및 출력 파일 구성
- `process_input()` - 입력 비디오에서 프레임 디코드
- `encode_frame()` - 출력으로 프레임 인코드
- `flush_encoder()` - 남은 프레임 플러시

## 고급 사용법

### 사용자 정의 전환 매개변수

특정 전환에 대한 사용자 정의 매개변수를 추가하도록 코드를 수정할 수 있습니다. 예를 들어 방사형 전환의 경우 회전 각도 제어를 추가할 수 있습니다.

### 다중 전환

여러 전환이 있는 비디오를 만들려면 도구를 체인으로 연결하세요:

```bash
# 첫 번째 전환 만들기
./video_transition clip1.mp4 clip2.mp4 temp1.mp4 fade 1.0

# 두 번째 전환 만들기
./video_transition temp1.mp4 clip3.mp4 final.mp4 dissolve 1.5
```

### 일괄 처리

스크립트로 여러 비디오 쌍 처리:

```bash
#!/bin/bash
transitions=("fade" "dissolve" "slideright" "circleopen")
for i in {1..4}; do
    ./video_transition clip$i.mp4 clip$((i+1)).mp4 \
        output_${transitions[$i-1]}.mp4 ${transitions[$i-1]} 1.5
done
```

## FFmpeg 필터 참조

xfade 필터는 이 기본 구현에서 노출되지 않은 많은 추가 매개변수를 지원합니다:

- `offset` - 전환 시작 시간
- `duration` - 전환 지속 시간
- `transition` - 효과 유형
- 고급 효과를 위한 사용자 정의 표현식

자세한 내용은 https://ffmpeg.org/ffmpeg-filters.html#xfade 를 참조하세요.

## 관련 샘플

- **video_slideshow** - 전환이 있는 슬라이드쇼 만들기
- **video_splitter** - 비디오 분할 및 병합
- **video_filter** - 비디오 필터 및 효과 적용

## 추가 개선 사항

프로덕션 사용을 위한 잠재적 개선 사항:

1. **오디오 처리** - 현재 비디오에 초점; 오디오 크로스페이드 추가
2. **하드웨어 가속** - GPU 인코딩 지원 추가
3. **실시간 미리보기** - 처리 전 전환 미리보기 표시
4. **일괄 모드** - 한 번에 여러 전환 처리
5. **사용자 정의 표현식** - FFmpeg 사용자 정의 전환 표현식 지원
6. **진행률 표시줄** - 더 자세한 진행 상황 표시
7. **형식 변환** - 호환되지 않는 비디오 자동 변환

## 결론

`video_transition` 샘플은 전문적인 비디오 전환을 만들기 위한 견고한 기반을 제공합니다. 33개 이상의 내장 효과와 유연한 타이밍 제어로 광범위한 비디오 편집 작업에 적합합니다.

프로덕션 애플리케이션의 경우 오디오 처리, 하드웨어 가속 및 보다 정교한 오류 복구 메커니즘 추가를 고려하세요.
