# 비디오 인코더

## 개요

`video_encoder` 애플리케이션은 FFmpeg 라이브러리를 사용하여 프레임을 생성하고 인코딩하여 비디오 파일을 처음부터 생성하는 방법을 보여줍니다. 애니메이션 테스트 패턴을 생성하고 H.264/MP4 포맷으로 인코딩합니다.

## 사용법

```bash
./video_encoder <출력_파일> [프레임수] [너비] [높이] [fps]
```

### 매개변수

- `출력_파일` - 출력 비디오 파일 경로 (필수)
- `프레임수` - 생성할 프레임 수 (선택사항, 기본값: 100)
- `너비` - 비디오 너비(픽셀) (선택사항, 기본값: 1280)
- `높이` - 비디오 높이(픽셀) (선택사항, 기본값: 720)
- `fps` - 프레임 레이트 (선택사항, 기본값: 30)

## 예제

```bash
# 기본 720p 비디오 생성
./video_encoder output.mp4

# 300프레임의 1080p 60fps 비디오 생성
./video_encoder output.mp4 300 1920 1080 60

# 4K 비디오 생성
./video_encoder output_4k.mp4 200 3840 2160 30
```

## 기능

- 구성 가능한 프리셋을 사용한 H.264 인코딩
- 사용자 정의 가능한 해상도 및 프레임 레이트
- 애니메이션 테스트 패턴 생성
- YUV420P 픽셀 포맷 (가장 호환성 높음)
- 진행 상황 보고
- 적절한 타임스탬프 관리

## 구현 세부사항

### 인코딩 파이프라인

1. 출력 포맷 컨텍스트 생성
2. H.264 인코더 찾기
3. 코덱 매개변수 구성 (해상도, 비트레이트, GOP 크기)
4. 코덱 열고 헤더 쓰기
5. YUV420P 포맷으로 프레임 생성
6. 프레임을 패킷으로 인코딩
7. 파일에 패킷 쓰기
8. 인코더 플러시 및 트레일러 쓰기

### 주요 설정

```cpp
codec_ctx->width = width;
codec_ctx->height = height;
codec_ctx->time_base = AVRational{1, fps};
codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
codec_ctx->bit_rate = 2000000;  // 2 Mbps
codec_ctx->gop_size = 10;        // 10프레임마다 I-프레임
av_opt_set(codec_ctx->priv_data, "preset", "medium", 0);
```

## 사용 사례

- 비디오 처리 파이프라인 테스트
- 검증용 테스트 콘텐츠 생성
- 비디오 인코딩 워크플로우 학습
- 합성 비디오 데이터셋 생성
- 성능 벤치마킹

## 참조

- [비디오 디코더 문서](video_decoder.md)
- [비디오 트랜스코더 문서](video_transcoder.md)
- [FFmpeg API 가이드](ffmpeg_api.md)
