# 비디오 필터

## 개요

`video_filter` 애플리케이션은 FFmpeg의 강력한 필터 그래프 API를 사용하여 비디오 스트림에 다양한 필터와 효과를 적용하는 방법을 보여줍니다. 이 애플리케이션은 비디오 처리를 위한 실용적인 도구이자 FFmpeg의 필터링 시스템을 이해하기 위한 교육 리소스 역할을 합니다.

필터 그래프 API는 FFmpeg의 가장 다재다능한 기능 중 하나로, 간단하고 조합 가능한 필터 요소로부터 복잡한 비디오 처리 파이프라인을 구성할 수 있습니다. 이 샘플은 기본 색상 조정부터 고급 엣지 감지까지 여러 사전 구성된 필터 프리셋을 포함하며, 사용자 정의 필터 조합을 지원하도록 쉽게 확장할 수 있습니다.

### 주요 기능

- 여러 효과 유형을 사용한 실시간 비디오 필터링
- 필터 그래프 구성 및 관리
- 프레임별 처리
- 필터 체이닝 및 복잡한 파이프라인 지원
- 구성 가능한 필터 매개변수
- 처리 중 진행 상황 모니터링

## 사용법

```bash
./video_filter <입력_파일> <출력_파일> <필터_타입>
```

### 매개변수

- `입력_파일` - 입력 비디오 파일 경로 (필수)
- `출력_파일` - 출력 비디오 파일 경로 (필수)
- `필터_타입` - 적용할 필터 프리셋 (필수)

## 사용 가능한 필터

| 필터 | 설명 | 효과 |
|------|------|------|
| `grayscale` | 그레이스케일로 변환 | 색상 제거 |
| `blur` | 가우시안 블러 | 이미지 부드럽게 |
| `sharpen` | 샤픈 필터 | 엣지 강화 |
| `rotate` | 시계방향 90° 회전 | 비디오 회전 |
| `flip_h` | 수평 뒤집기 | 수평으로 미러 |
| `flip_v` | 수직 뒤집기 | 수직으로 미러 |
| `brightness` | 밝기 증가 | 이미지 밝게 |
| `contrast` | 대비 증가 | 대비 강화 |
| `edge` | 엣지 감지 | 엣지 감지 |
| `negative` | 네거티브 이미지 | 색상 반전 |
| `custom` | 사용자 정의 결합 필터 | 여러 효과 |

## 예제

```bash
# 그레이스케일로 변환
./video_filter input.mp4 output_gray.mp4 grayscale

# 블러 효과 적용
./video_filter input.mp4 output_blur.mp4 blur

# 엣지 감지
./video_filter input.mp4 output_edge.mp4 edge

# 비디오 샤픈
./video_filter input.mp4 output_sharp.mp4 sharpen

# 90도 회전
./video_filter input.mp4 output_rotated.mp4 rotate
```

## 구현 세부사항

### 필터 그래프 API

애플리케이션은 FFmpeg의 필터 그래프 시스템을 사용합니다:

1. 필터 그래프 생성
2. 버퍼 소스(입력) 생성
3. 버퍼 싱크(출력) 생성
4. 필터 설명 파싱
5. 그래프 구성
6. 소스에 프레임 푸시
7. 싱크에서 필터링된 프레임 풀

### 필터 설명 구문

필터는 FFmpeg 필터 표현식을 사용합니다:
```
# 단일 필터
hue=s=0

# 체인된 필터 (쉼표로 구분)
eq=brightness=0.1:contrast=1.2,hue=s=1.2

# 매개변수가 있는 복잡한 필터
gblur=sigma=5
```

### 사용자 정의 필터 추가

`get_filter_description()` 함수 수정:

```cpp
else if (strcmp(filter_type, "myfilter") == 0) {
    return "your_filter_expression";
}
```

## 필터 예제

### 색상 조정

```cpp
// 밝기
"eq=brightness=0.2"

// 대비
"eq=contrast=1.5"

// 채도
"eq=saturation=1.5"

// 색조 변경
"hue=h=90"
```

### 공간 필터

```cpp
// 가우시안 블러
"gblur=sigma=5"

// 박스 블러
"boxblur=2:1"

// 샤픈
"unsharp=5:5:1.0:5:5:0.0"
```

### 기하학적 변환

```cpp
// 시계방향 90° 회전
"transpose=1"

// 수평 뒤집기
"hflip"

// 수직 뒤집기
"vflip"

// 스케일
"scale=1280:720"
```

### 효과

```cpp
// 엣지 감지
"edgedetect=low=0.1:high=0.4"

// 네거티브
"negate"

// 그레이스케일
"hue=s=0"

// 비네트
"vignette=PI/4"
```

## 사용 사례

1. **비디오 향상**
   - 색상 보정
   - 샤프닝
   - 노이즈 감소

2. **창의적 효과**
   - 예술적 필터
   - 색상 그레이딩
   - 특수 효과

3. **분석**
   - 엣지 감지
   - 모션 감지
   - 품질 평가

4. **전처리**
   - 정규화
   - 포맷 변환
   - 디인터레이싱

## 성능 고려사항

- 일부 필터는 GPU 가속됨
- 복잡한 필터 체인은 느릴 수 있음
- 필터 적용 시 해상도 고려
- 먼저 짧은 클립으로 테스트

## 일반적인 문제

### 필터를 찾을 수 없음
사용 가능한 필터 확인:
```bash
ffmpeg -filters
```

### 구문 에러
필터 구문 확인:
```bash
ffmpeg -h filter=<filter_name>
```

### 성능 문제
- 필터 체인 단순화
- 해상도 감소
- 더 빠른 알고리즘 사용

## 고급 사용법

### 여러 필터

쉼표로 필터 결합:
```cpp
"hue=s=0,eq=brightness=0.2,unsharp=5:5:1.0"
```

### 조건부 필터링

조건에 따라 필터 적용:
```cpp
"select='gt(scene,0.4)',hue=s=0"
```

### 시간 기반 효과

```cpp
"fade=in:0:30"  // 30프레임에 걸쳐 페이드 인
```

## 필터 문서

완전한 필터 참조:
https://ffmpeg.org/ffmpeg-filters.html

## 참조

- [비디오 트랜스코더 문서](video_transcoder.md)
- [FFmpeg API 가이드](ffmpeg_api.md)
- [메인 README](../../README.md)
