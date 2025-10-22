# 비디오 트랜스코더

## 개요

`video_transcoder` 애플리케이션은 비디오를 다른 포맷, 코덱, 해상도 및 비트레이트로 변환합니다. 디코딩, 스케일링 및 인코딩을 단일 효율적인 파이프라인으로 결합합니다.

## 사용법

```bash
./video_transcoder <입력_파일> <출력_파일> [너비] [높이] [비트레이트] [fps]
```

### 매개변수

- `입력_파일` - 입력 비디오 파일 경로 (필수)
- `출력_파일` - 출력 비디오 파일 경로 (필수)
- `너비` - 출력 너비(픽셀) (선택사항, 기본값: 1280)
- `높이` - 출력 높이(픽셀) (선택사항, 기본값: 720)
- `비트레이트` - 출력 비트레이트(bps) (선택사항, 기본값: 2000000)
- `fps` - 출력 프레임 레이트 (선택사항, 기본값: 30)

## 예제

```bash
# 720p로 변환
./video_transcoder input.avi output.mp4 1280 720

# 높은 비트레이트로 1080p로 변환
./video_transcoder input.mkv output.mp4 1920 1080 5000000 30

# 포맷만 변환 (해상도 유지)
./video_transcoder input.avi output.mp4
```

## 기능

- 포맷 변환 (AVI, MP4, MKV, MOV 등)
- 해상도 스케일링 (업스케일링/다운스케일링)
- 비트레이트 조정
- 프레임 레이트 변환
- 코덱 변환 (모든 코덱을 H.264로)
- 품질 프리셋 (fast, medium, slow)
- 진행 상황 보고

## 구현 세부사항

### 트랜스코딩 파이프라인

1. 입력 파일 열고 스트림 찾기
2. 입력 디코더 초기화
3. 출력 파일 및 인코더 생성
4. 해상도 변경을 위한 swscale 컨텍스트 초기화
5. 입력에서 패킷 읽기
6. 프레임 디코딩
7. 대상 해상도로 프레임 스케일
8. 스케일된 프레임 인코딩
9. 인코딩된 패킷을 출력에 쓰기

### 주요 구성 요소

```cpp
// 디코더
input_codec_ctx = avcodec_alloc_context3(decoder);
avcodec_open2(input_codec_ctx, decoder, nullptr);

// 스케일러
sws_ctx = sws_getContext(
    input_width, input_height, input_pix_fmt,
    output_width, output_height, AV_PIX_FMT_YUV420P,
    SWS_BICUBIC, nullptr, nullptr, nullptr
);

// 인코더
output_codec_ctx = avcodec_alloc_context3(encoder);
output_codec_ctx->bit_rate = bitrate;
avcodec_open2(output_codec_ctx, encoder, nullptr);
```

## 사용 사례

- 포맷 표준화
- 해상도 정규화
- 스트리밍을 위한 비트레이트 최적화
- 코덱 마이그레이션
- 품질 조정
- 프레임 레이트 변환

## 성능 팁

- 가능한 경우 하드웨어 가속 사용
- 적절한 스케일링 알고리즘 선택
- 속도/품질 요구에 따라 인코더 프리셋 조정
- 최상의 품질을 위해 다중 패스 인코딩 고려

## 일반적인 문제

### 품질 손실
- 비트레이트 증가
- 느린 인코딩 프리셋 사용
- 저품질 소스 업스케일링 방지

### 느린 성능
- 더 빠른 프리셋 사용 (fast, veryfast)
- 출력 해상도 감소
- 하드웨어 가속 활성화

## 참조

- [비디오 디코더 문서](video_decoder.md)
- [비디오 인코더 문서](video_encoder.md)
- [FFmpeg API 가이드](ffmpeg_api.md)
