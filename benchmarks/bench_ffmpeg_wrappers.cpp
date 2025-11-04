/**
 * Performance benchmarks for FFmpeg RAII wrappers
 */

#include "ffmpeg_wrappers.hpp"
#include <benchmark/benchmark.h>

// Benchmark frame allocation/deallocation
static void BM_FrameAllocation(benchmark::State& state) {
    for (auto _ : state) {
        auto frame = ffmpeg::create_frame();
        benchmark::DoNotOptimize(frame.get());
    }
}
BENCHMARK(BM_FrameAllocation);

// Benchmark packet allocation/deallocation
static void BM_PacketAllocation(benchmark::State& state) {
    for (auto _ : state) {
        auto packet = ffmpeg::create_packet();
        benchmark::DoNotOptimize(packet.get());
    }
}
BENCHMARK(BM_PacketAllocation);

// Benchmark codec context creation
static void BM_CodecContextCreation(benchmark::State& state) {
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        state.SkipWithError("H264 codec not found");
        return;
    }

    for (auto _ : state) {
        auto ctx = ffmpeg::create_codec_context(codec);
        benchmark::DoNotOptimize(ctx.get());
    }
}
BENCHMARK(BM_CodecContextCreation);

// Benchmark frame allocation with buffer
static void BM_FrameWithBuffer(benchmark::State& state) {
    const int width = state.range(0);
    const int height = state.range(1);

    for (auto _ : state) {
        auto frame = ffmpeg::create_frame();
        frame->width = width;
        frame->height = height;
        frame->format = AV_PIX_FMT_YUV420P;

        if (av_frame_get_buffer(frame.get(), 0) < 0) {
            state.SkipWithError("Failed to allocate frame buffer");
            break;
        }

        benchmark::DoNotOptimize(frame.get());
    }
}
BENCHMARK(BM_FrameWithBuffer)
    ->Args({1920, 1080})  // Full HD
    ->Args({3840, 2160})  // 4K
    ->Args({7680, 4320}); // 8K

// Benchmark packet data allocation
static void BM_PacketWithData(benchmark::State& state) {
    const int size = state.range(0);

    for (auto _ : state) {
        auto packet = ffmpeg::create_packet();

        if (av_new_packet(packet.get(), size) < 0) {
            state.SkipWithError("Failed to allocate packet data");
            break;
        }

        benchmark::DoNotOptimize(packet.get());
    }
}
BENCHMARK(BM_PacketWithData)
    ->Arg(1024)        // 1 KB
    ->Arg(10240)       // 10 KB
    ->Arg(102400)      // 100 KB
    ->Arg(1024000);    // 1 MB

// Benchmark ScopedFrameUnref overhead
static void BM_ScopedFrameUnref(benchmark::State& state) {
    auto frame = ffmpeg::create_frame();
    frame->width = 1920;
    frame->height = 1080;
    frame->format = AV_PIX_FMT_YUV420P;

    if (av_frame_get_buffer(frame.get(), 0) < 0) {
        state.SkipWithError("Failed to allocate frame buffer");
        return;
    }

    for (auto _ : state) {
        ffmpeg::ScopedFrameUnref unref(frame.get());
        benchmark::DoNotOptimize(frame.get());
    }
}
BENCHMARK(BM_ScopedFrameUnref);

// Benchmark ScopedPacketUnref overhead
static void BM_ScopedPacketUnref(benchmark::State& state) {
    auto packet = ffmpeg::create_packet();

    if (av_new_packet(packet.get(), 1024) < 0) {
        state.SkipWithError("Failed to allocate packet data");
        return;
    }

    for (auto _ : state) {
        ffmpeg::ScopedPacketUnref unref(packet.get());
        benchmark::DoNotOptimize(packet.get());
    }
}
BENCHMARK(BM_ScopedPacketUnref);

// Benchmark codec finding
static void BM_FindCodec(benchmark::State& state) {
    const AVCodecID codec_id = static_cast<AVCodecID>(state.range(0));

    for (auto _ : state) {
        const AVCodec* codec = avcodec_find_decoder(codec_id);
        benchmark::DoNotOptimize(codec);
    }
}
BENCHMARK(BM_FindCodec)
    ->Arg(AV_CODEC_ID_H264)
    ->Arg(AV_CODEC_ID_H265)
    ->Arg(AV_CODEC_ID_VP9)
    ->Arg(AV_CODEC_ID_AAC)
    ->Arg(AV_CODEC_ID_MP3);

// Benchmark stream index finding
static void BM_FindStreamIndex(benchmark::State& state) {
    // Create a dummy format context with multiple streams
    AVFormatContext dummy_ctx = {};
    AVStream stream1 = {}, stream2 = {}, stream3 = {};
    AVCodecParameters params1 = {}, params2 = {}, params3 = {};

    params1.codec_type = AVMEDIA_TYPE_VIDEO;
    params2.codec_type = AVMEDIA_TYPE_AUDIO;
    params3.codec_type = AVMEDIA_TYPE_SUBTITLE;

    stream1.codecpar = &params1;
    stream2.codecpar = &params2;
    stream3.codecpar = &params3;

    AVStream* streams[] = {&stream1, &stream2, &stream3};
    dummy_ctx.streams = streams;
    dummy_ctx.nb_streams = 3;

    for (auto _ : state) {
        auto result = ffmpeg::find_stream_index(&dummy_ctx, AVMEDIA_TYPE_AUDIO);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_FindStreamIndex);

// Benchmark error handling
static void BM_ErrorHandling(benchmark::State& state) {
    for (auto _ : state) {
        try {
            ffmpeg::check_error(AVERROR(EINVAL), "test");
        } catch (const ffmpeg::FFmpegError&) {
            // Expected
        }
    }
}
BENCHMARK(BM_ErrorHandling);

BENCHMARK_MAIN();
