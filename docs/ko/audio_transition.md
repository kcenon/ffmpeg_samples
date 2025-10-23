# 오디오 전환 효과 가이드

## 개요

`audio_transition` 샘플은 FFmpeg의 오디오 처리 기능을 사용하여 두 오디오 클립 간에 부드러운 크로스페이드 전환을 적용하는 방법을 보여줍니다. 크로스페이딩은 매끄러운 오디오 구성, DJ 믹스, 팟캐스트 및 전문 오디오 제작에 필수적입니다.

## 주요 기능

- **16가지 크로스페이드 곡선** - 다양한 페이드 특성을 위한 수학적 곡선
- **사용자 정의 지속 시간** - 크로스페이드 지속 시간 제어
- **오버랩 모드** - 겹침 또는 순차 재생 선택
- **Modern C++20** - RAII 래퍼 및 구조화된 바인딩
- **프로덕션 준비** - 적절한 오류 처리 및 리소스 관리
- **WAV 출력** - 표준 비압축 오디오 형식

## 사용법

```bash
./audio_transition <audio1> <audio2> <output> <curve> [duration] [overlap]
```

### 매개변수

- `audio1` - 첫 번째 오디오 클립 (먼저 재생)
- `audio2` - 두 번째 오디오 클립 (전환 후 재생)
- `output` - 출력 오디오 파일 경로 (WAV 형식)
- `curve` - 크로스페이드 곡선 유형 (아래 목록 참조)
- `duration` - (선택) 크로스페이드 지속 시간(초) (기본값: 2.0)
- `overlap` - (선택) 오버랩 모드: 0=없음, 1=오버랩 (기본값: 1)

### 예제

**기본 선형 크로스페이드:**
```bash
./audio_transition music1.mp3 music2.mp3 output.wav tri
```

**3초 쿼터 사인 웨이브:**
```bash
./audio_transition audio1.wav audio2.wav result.wav qsin 3.0
```

**오버랩이 있는 지수 페이드:**
```bash
./audio_transition clip1.flac clip2.flac final.wav exp 1.5 1
```

**오버랩 없이 순차 재생:**
```bash
./audio_transition intro.mp3 main.mp3 podcast.wav tri 2.0 0
```

## 사용 가능한 크로스페이드 곡선

### 선형 곡선

| 곡선 | 이름 | 설명 |
|------|------|------|
| `tri` | 삼각형 | 단순 선형 크로스페이드 (일정 전력) |

### 사인 기반 곡선

| 곡선 | 이름 | 설명 |
|------|------|------|
| `qsin` | 쿼터 사인 | 부드러운 쿼터 사인 웨이브 페이드 |
| `esin` | 지수 사인 | 지수 사인 곡선 |
| `hsin` | 하프 사인 | 하프 사인 웨이브 (S-곡선) |
| `iqsin` | 역 쿼터 사인 | 역 쿼터 사인 |
| `ihsin` | 역 하프 사인 | 역 하프 사인 |

### 다항식 곡선

| 곡선 | 이름 | 설명 |
|------|------|------|
| `qua` | 이차 | 이차 곡선 (x²) |
| `cub` | 삼차 | 삼차 곡선 (x³) |
| `par` | 포물선 | 포물선 곡선 |
| `ipar` | 역 포물선 | 역 포물선 곡선 |

### 루트 곡선

| 곡선 | 이름 | 설명 |
|------|------|------|
| `squ` | 제곱근 | 제곱근 곡선 (√x) |
| `cbr` | 세제곱근 | 세제곱근 곡선 (∛x) |

### 고급 곡선

| 곡선 | 이름 | 설명 |
|------|------|------|
| `log` | 로그 | 로그 페이드 |
| `exp` | 지수 | 지수 페이드 |
| `dese` | 이중 지수 스무더스텝 | 매우 부드러운 이중 지수 |
| `desi` | 이중 지수 시그모이드 | 시그모이드 형태의 이중 지수 |

## 작동 원리

### 1. 오디오 디코딩

샘플은 두 입력 오디오 파일을 열고 디코딩합니다:

```cpp
decoder1_ = std::make_unique<AudioDecoder>(audio1, sample_rate, channels);
decoder2_ = std::make_unique<AudioDecoder>(audio2, sample_rate, channels);
```

### 2. 리샘플링

두 오디오 스트림은 공통 형식(44.1kHz, 스테레오, 16비트)으로 리샘플링됩니다:

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

### 3. 크로스페이드 처리

선택한 곡선을 사용하여 전환을 적용합니다:

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

### 4. WAV 출력

최종 오디오는 적절한 헤더와 함께 WAV 파일로 작성됩니다:

```cpp
write_wav_header(output, sample_rate_, channels_, total_bytes);
```

## 기술 세부사항

### 오디오 요구사항

- **형식**: FFmpeg이 지원하는 모든 오디오 형식 (MP3, WAV, FLAC, AAC, OGG 등)
- **샘플 레이트**: 자동으로 44.1kHz로 리샘플링
- **채널**: 자동으로 스테레오로 변환
- **비트 깊이**: 16비트 PCM 출력

### 출력 사양

- **형식**: WAV (Waveform Audio File Format)
- **샘플 레이트**: 44.1 kHz
- **채널**: 2 (스테레오)
- **비트 깊이**: 16비트 부호 있는 정수
- **인코딩**: PCM (비압축)

### 오버랩 모드

#### 오버랩 모드 (overlap=1, 기본값)

크로스페이드는 첫 번째 오디오의 끝과 두 번째 오디오의 시작 부분에서 발생합니다:

```
Audio 1: [==========FADE]
Audio 2:           [FADE==========]
Result:  [==========XXXX==========]
         여기서 X = 크로스페이드 영역
```

총 지속 시간 = duration1 + duration2 - crossfade_duration

#### 오버랩 없음 모드 (overlap=0)

크로스페이드가 두 오디오 파일 사이에 삽입됩니다:

```
Audio 1: [==========]FADE
Audio 2:            FADE[==========]
Result:  [==========XXXX==========]
```

총 지속 시간 = duration1 + duration2 + crossfade_duration

## 일반적인 사용 사례

### 1. DJ 믹스

노래 간의 부드러운 전환 만들기:

```bash
./audio_transition track1.mp3 track2.mp3 mix.wav qsin 4.0
```

### 2. 팟캐스트 제작

인트로 음악과 메인 콘텐츠 블렌딩:

```bash
./audio_transition intro_music.mp3 podcast_content.wav output.wav hsin 2.0
```

### 3. 오디오북

챕터 간의 부드러운 전환:

```bash
./audio_transition chapter1.mp3 chapter2.mp3 book.wav tri 1.0
```

### 4. 음악 제작

앨범의 매끄러운 트랙 전환:

```bash
./audio_transition song1.flac song2.flac album.wav esin 3.0
```

### 5. 라디오 제작

전문적인 스테이션 ID 및 징글:

```bash
./audio_transition jingle.wav program.mp3 broadcast.wav exp 1.5
```

## 곡선 선택 가이드

### 각 곡선을 사용하는 경우

**선형 (tri)**
- 범용 크로스페이딩
- 일정 전력 페이드
- 음악 믹싱
- 빠르고 간단함

**쿼터 사인 (qsin)**
- 부드럽고 자연스러운 전환
- 대부분의 음악에 권장
- 전문 DJ 믹스
- 미묘한 페이드 효과

**하프 사인 (hsin)**
- 매우 부드러운 S-곡선
- 영화 오디오 전환
- 고품질 제작
- 최소 아티팩트

**지수 (exp)**
- 빠른 초기 페이드
- 극적인 전환
- 특수 효과
- 동적 강조 변경

**로그 (log)**
- 느린 시작, 빠른 끝
- 지각된 음량 매칭
- 음성 전환
- 볼륨 매칭 페이드

**삼차/이차**
- 사용자 정의 페이드 특성
- 예술적 제어
- 실험적 효과
- 비표준 전환

**이중 지수 (dese/desi)**
- 초부드러운 전환
- 오디오파일 품질 페이드
- 마스터링 애플리케이션
- 프리미엄 제작

## 성능 고려사항

### 처리 시간

- 크로스페이드 처리는 비교적 빠릅니다
- 지속 시간은 오디오 길이와 크로스페이드 지속 시간에 따라 다릅니다
- 일반적인 처리 시간: 실시간 또는 더 빠름

### 메모리 사용량

- 두 개의 오디오 버퍼가 동시에 메모리에 있음
- 크로스페이드 버퍼 크기는 지속 시간 기반
- 일반적인 사용 사례의 경우 최소 메모리 공간

### 품질 고려사항

1. **샘플 레이트**: 44.1kHz는 대부분의 용도에 좋은 품질 제공
2. **비트 깊이**: 16비트는 대부분의 애플리케이션에 충분
3. **곡선 선택**: 지각된 품질에 크게 영향
4. **지속 시간**: 더 긴 크로스페이드가 더 부드럽지만 더 눈에 띔

## 문제 해결

### 다른 샘플 레이트

다른 샘플 레이트의 입력 파일은 자동으로 44.1kHz로 리샘플링됩니다. 이는 투명하며 사용자 개입이 필요하지 않습니다.

### 다른 채널 수

모노 파일은 스테레오로 변환됩니다. 스테레오 파일은 스테레오로 유지됩니다. 다채널 파일은 스테레오로 다운믹스됩니다.

### 잘못된 곡선 이름

잘못된 곡선 이름이 제공되면 도구는 선형(tri) 페이드로 기본 설정됩니다:

```cpp
return t;  // 선형으로 기본 설정
```

### 지속 시간 검증

크로스페이드 지속 시간은 0에서 10초 사이여야 합니다:

```
Duration must be between 0 and 10 seconds
```

### 파일 형식 문제

입력 파일을 디코딩할 수 없는 경우:

```
Error: No audio stream found
Error: Decoder not found
Error: Failed to open decoder
```

파일이 FFmpeg에서 지원하는 유효한 오디오 파일인지 확인하세요.

## 코드 구조

### 주요 클래스: AudioTransition

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

### 헬퍼 클래스: AudioDecoder

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

### 주요 메서드

- `process()` - 메인 처리 루프
- `process_first_audio()` - 크로스페이드 전 첫 번째 오디오 작성
- `process_crossfade()` - 크로스페이드 전환 적용
- `process_second_audio()` - 크로스페이드 후 두 번째 오디오 작성
- `apply_curve()` - 수학적 곡선 함수

## 수학적 곡선

곡선은 수학 함수로 구현됩니다:

```cpp
// 선형
t

// 쿼터 사인
sin(t * π/2)

// 하프 사인
(1 - cos(t * π)) / 2

// 지수
exp(t * 4 - 4)

// 로그
log₁₀(t * 9 + 1)

// 이차
t²

// 삼차
t³

// 제곱근
√t

// 세제곱근
∛t
```

## 고급 사용법

### 일괄 처리

스크립트에서 여러 전환 처리:

```bash
#!/bin/bash
tracks=("track1.mp3" "track2.mp3" "track3.mp3" "track4.mp3")
for i in {0..2}; do
    ./audio_transition "${tracks[$i]}" "${tracks[$i+1]}" \
        "transition_$i.wav" qsin 3.0
done
```

### 체인 전환

여러 트랙에서 완전한 믹스 만들기:

```bash
# 첫 번째 전환
./audio_transition track1.mp3 track2.mp3 temp1.wav qsin 3.0

# 두 번째 전환
./audio_transition temp1.wav track3.mp3 temp2.wav qsin 3.0

# 최종 전환
./audio_transition temp2.wav track4.mp3 final_mix.wav qsin 3.0
```

### 다양한 분위기를 위한 다양한 곡선

```bash
# 활기찬 전환
./audio_transition upbeat1.mp3 upbeat2.mp3 energetic.wav exp 1.0

# 부드러운 전환
./audio_transition ambient1.mp3 ambient2.mp3 smooth.wav hsin 4.0

# 빠른 컷
./audio_transition rock1.mp3 rock2.mp3 quick.wav tri 0.5
```

## 비디오 전환과의 비교

`video_transition`은 시각적 크로스페이드를 처리하는 반면, `audio_transition`은 특히 오디오에 중점을 둡니다:

| 기능 | 비디오 전환 | 오디오 전환 |
|------|-----------|-----------|
| 입력 | 비디오 파일 | 오디오 파일 |
| 출력 | 비디오 파일 | WAV 파일 |
| 효과 | 33개 이상의 시각적 전환 | 16가지 크로스페이드 곡선 |
| 처리 | 프레임 단위 | 샘플 단위 |
| 지속 시간 | 일반적으로 1-2초 | 일반적으로 2-4초 |

## 관련 샘플

- **audio_mixer** - 볼륨 제어로 두 오디오 파일 믹싱
- **audio_format_converter** - 오디오 형식 간 변환
- **video_transition** - 비디오 간 시각적 전환 적용

## 추가 개선 사항

프로덕션 사용을 위한 잠재적 개선 사항:

1. **파라메트릭 EQ** - 주파수 기반 크로스페이딩
2. **다중 형식** - 다양한 출력 형식 지원 (MP3, FLAC 등)
3. **BPM 감지** - 음악을 위한 비트 매칭 크로스페이딩
4. **자동 게인** - 자동 볼륨 정규화
5. **미리보기 모드** - 처리 전 크로스페이드 듣기
6. **메타데이터 보존** - 소스 파일에서 태그 복사
7. **다중 트랙** - 두 개 이상의 입력 지원
8. **실시간 처리** - 라이브 크로스페이딩 기능

## 모범 사례

1. **음량 매칭**: 크로스페이딩 전에 오디오 레벨 정규화
2. **적절한 지속 시간 선택**:
   - 음악: 2-4초
   - 음성: 1-2초
   - 음향 효과: 0.5-1초
3. **다양한 곡선 테스트**: 각 곡선에는 고유한 특성이 있음
4. **오버랩 모드 사용**: 일반적으로 순차보다 더 자연스럽게 들림
5. **컨텍스트 고려**: 극적 vs. 미묘한 전환

## 결론

`audio_transition` 샘플은 페이드 특성에 대한 광범위한 제어를 통해 오디오 클립 간의 전문적인 품질의 크로스페이딩을 제공합니다. 16가지 다른 곡선과 사용자 정의 가능한 매개변수로 광범위한 오디오 제작 작업에 적합합니다.

프로덕션 애플리케이션의 경우 향상된 기능을 위해 형식 변환, 메타데이터 처리 및 자동 게인 제어 추가를 고려하세요.
