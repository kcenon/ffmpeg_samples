/**
 * Unit tests for FFmpeg RAII wrappers
 */

#include "ffmpeg_wrappers.hpp"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

class FFmpegWrappersTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary directory for test files
        test_dir_ = fs::temp_directory_path() / "ffmpeg_test";
        fs::create_directories(test_dir_);
    }

    void TearDown() override {
        // Clean up test directory
        if (fs::exists(test_dir_)) {
            fs::remove_all(test_dir_);
        }
    }

    fs::path test_dir_;
};

// Test FFmpegError exception
TEST_F(FFmpegWrappersTest, FFmpegErrorConstruction) {
    try {
        throw ffmpeg::FFmpegError("Test error");
        FAIL() << "Exception should have been thrown";
    } catch (const ffmpeg::FFmpegError& e) {
        EXPECT_STREQ(e.what(), "Test error");
        EXPECT_EQ(e.error_code(), 0);
    }
}

TEST_F(FFmpegWrappersTest, FFmpegErrorWithErrorCode) {
    try {
        throw ffmpeg::FFmpegError(AVERROR(ENOENT));
        FAIL() << "Exception should have been thrown";
    } catch (const ffmpeg::FFmpegError& e) {
        EXPECT_NE(e.error_code(), 0);
        EXPECT_NE(std::string(e.what()), "");
    }
}

// Test frame creation
TEST_F(FFmpegWrappersTest, CreateFrame) {
    auto frame = ffmpeg::create_frame();
    ASSERT_NE(frame, nullptr);
    EXPECT_NE(frame.get(), nullptr);
}

// Test packet creation
TEST_F(FFmpegWrappersTest, CreatePacket) {
    auto packet = ffmpeg::create_packet();
    ASSERT_NE(packet, nullptr);
    EXPECT_NE(packet.get(), nullptr);
}

// Test codec context creation
TEST_F(FFmpegWrappersTest, CreateCodecContext) {
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (codec) {
        auto ctx = ffmpeg::create_codec_context(codec);
        ASSERT_NE(ctx, nullptr);
        EXPECT_EQ(ctx->codec_id, AV_CODEC_ID_H264);
    } else {
        GTEST_SKIP() << "H264 codec not available";
    }
}

// Test opening non-existent file
TEST_F(FFmpegWrappersTest, OpenNonExistentFile) {
    fs::path non_existent = test_dir_ / "does_not_exist.mp4";

    EXPECT_THROW({
        auto ctx = ffmpeg::open_input_format(non_existent.c_str());
    }, ffmpeg::FFmpegError);
}

// Test find_stream_index with null context
TEST_F(FFmpegWrappersTest, FindStreamIndexReturnsNullopt) {
    AVFormatContext dummy_ctx = {};
    dummy_ctx.nb_streams = 0;
    dummy_ctx.streams = nullptr;

    auto result = ffmpeg::find_stream_index(&dummy_ctx, AVMEDIA_TYPE_VIDEO);
    EXPECT_FALSE(result.has_value());
}

// Test ScopedFrameUnref RAII
TEST_F(FFmpegWrappersTest, ScopedFrameUnref) {
    auto frame = ffmpeg::create_frame();
    AVFrame* raw_frame = frame.get();

    // Allocate some data to the frame
    raw_frame->width = 1920;
    raw_frame->height = 1080;
    raw_frame->format = AV_PIX_FMT_YUV420P;

    ASSERT_EQ(av_frame_get_buffer(raw_frame, 0), 0);
    EXPECT_NE(raw_frame->data[0], nullptr);

    {
        ffmpeg::ScopedFrameUnref unref(raw_frame);
        // Frame should still have data
        EXPECT_NE(raw_frame->data[0], nullptr);
    }

    // After scope exit, frame should be unreferenced
    EXPECT_EQ(raw_frame->data[0], nullptr);
}

// Test ScopedPacketUnref RAII
TEST_F(FFmpegWrappersTest, ScopedPacketUnref) {
    auto packet = ffmpeg::create_packet();
    AVPacket* raw_packet = packet.get();

    // Allocate some data to the packet
    ASSERT_EQ(av_new_packet(raw_packet, 1024), 0);
    EXPECT_NE(raw_packet->data, nullptr);
    EXPECT_EQ(raw_packet->size, 1024);

    {
        ffmpeg::ScopedPacketUnref unref(raw_packet);
        // Packet should still have data
        EXPECT_NE(raw_packet->data, nullptr);
    }

    // After scope exit, packet should be unreferenced
    EXPECT_EQ(raw_packet->data, nullptr);
    EXPECT_EQ(raw_packet->size, 0);
}

// Test smart pointer cleanup
TEST_F(FFmpegWrappersTest, SmartPointerCleanup) {
    // Create pointers in nested scope
    {
        auto frame = ffmpeg::create_frame();
        auto packet = ffmpeg::create_packet();

        EXPECT_NE(frame.get(), nullptr);
        EXPECT_NE(packet.get(), nullptr);

        // Pointers should automatically clean up when going out of scope
    }

    // If we reach here without crashes, cleanup worked
    SUCCEED();
}

// Test codec finding
TEST_F(FFmpegWrappersTest, FindCommonCodecs) {
    // These codecs should be available in most FFmpeg builds
    EXPECT_NE(avcodec_find_decoder(AV_CODEC_ID_H264), nullptr);
    EXPECT_NE(avcodec_find_decoder(AV_CODEC_ID_AAC), nullptr);
    EXPECT_NE(avcodec_find_encoder(AV_CODEC_ID_H264), nullptr);
    EXPECT_NE(avcodec_find_encoder(AV_CODEC_ID_AAC), nullptr);
}

// Test error checking helper
TEST_F(FFmpegWrappersTest, CheckErrorHelper) {
    // Should not throw on success (ret >= 0)
    EXPECT_NO_THROW(ffmpeg::check_error(0, "test operation"));
    EXPECT_NO_THROW(ffmpeg::check_error(1, "test operation"));

    // Should throw on error (ret < 0)
    EXPECT_THROW(
        ffmpeg::check_error(AVERROR(EINVAL), "test operation"),
        ffmpeg::FFmpegError
    );
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
