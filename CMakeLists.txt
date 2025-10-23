cmake_minimum_required(VERSION 3.15)
project(ffmpeg_samples VERSION 1.0.0 LANGUAGES CXX)

# Modern C++ settings (C++20 for std::span and other features)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find FFmpeg libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavcodec
    libavformat
    libavutil
    libavfilter
    libswscale
    libswresample
)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${FFMPEG_INCLUDE_DIRS}
)

# Link directories
link_directories(${FFMPEG_LIBRARY_DIRS})

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# Video sample executables
add_executable(video_info src/video/video_info.cpp)
target_link_libraries(video_info ${FFMPEG_LIBRARIES})

add_executable(video_decoder src/video/video_decoder.cpp)
target_link_libraries(video_decoder ${FFMPEG_LIBRARIES})

add_executable(video_encoder src/video/video_encoder.cpp)
target_link_libraries(video_encoder ${FFMPEG_LIBRARIES})

add_executable(video_transcoder src/video/video_transcoder.cpp)
target_link_libraries(video_transcoder ${FFMPEG_LIBRARIES})

add_executable(video_filter src/video/video_filter.cpp)
target_link_libraries(video_filter ${FFMPEG_LIBRARIES})

add_executable(video_thumbnail src/video/video_thumbnail.cpp)
target_link_libraries(video_thumbnail ${FFMPEG_LIBRARIES})

add_executable(video_metadata src/video/video_metadata.cpp)
target_link_libraries(video_metadata ${FFMPEG_LIBRARIES})

add_executable(video_subtitles src/video/video_subtitles.cpp)
target_link_libraries(video_subtitles ${FFMPEG_LIBRARIES})

add_executable(video_watermark src/video/video_watermark.cpp)
target_link_libraries(video_watermark ${FFMPEG_LIBRARIES})

add_executable(video_splitter src/video/video_splitter.cpp)
target_link_libraries(video_splitter ${FFMPEG_LIBRARIES})

add_executable(video_slideshow src/video/video_slideshow.cpp)
target_link_libraries(video_slideshow ${FFMPEG_LIBRARIES})

add_executable(video_stabilization src/video/video_stabilization.cpp)
target_link_libraries(video_stabilization ${FFMPEG_LIBRARIES})

add_executable(video_transition src/video/video_transition.cpp)
target_link_libraries(video_transition ${FFMPEG_LIBRARIES})

add_executable(video_concatenate src/video/video_concatenate.cpp)
target_link_libraries(video_concatenate ${FFMPEG_LIBRARIES})

# Streaming sample executables
add_executable(streaming_server src/streaming/streaming_server.cpp)
target_link_libraries(streaming_server ${FFMPEG_LIBRARIES})

# Audio sample executables
add_executable(audio_info src/audio/audio_info.cpp)
target_link_libraries(audio_info ${FFMPEG_LIBRARIES})

add_executable(audio_decoder src/audio/audio_decoder.cpp)
target_link_libraries(audio_decoder ${FFMPEG_LIBRARIES})

add_executable(audio_encoder src/audio/audio_encoder.cpp)
target_link_libraries(audio_encoder ${FFMPEG_LIBRARIES})

add_executable(audio_resampler src/audio/audio_resampler.cpp)
target_link_libraries(audio_resampler ${FFMPEG_LIBRARIES})

add_executable(audio_mixer src/audio/audio_mixer.cpp)
target_link_libraries(audio_mixer ${FFMPEG_LIBRARIES})

add_executable(audio_noise_reduction src/audio/audio_noise_reduction.cpp)
target_link_libraries(audio_noise_reduction ${FFMPEG_LIBRARIES})

add_executable(audio_format_converter src/audio/audio_format_converter.cpp)
target_link_libraries(audio_format_converter ${FFMPEG_LIBRARIES})

add_executable(audio_spectrum src/audio/audio_spectrum.cpp)
target_link_libraries(audio_spectrum ${FFMPEG_LIBRARIES})

add_executable(audio_equalizer src/audio/audio_equalizer.cpp)
target_link_libraries(audio_equalizer ${FFMPEG_LIBRARIES})

add_executable(audio_transition src/audio/audio_transition.cpp)
target_link_libraries(audio_transition ${FFMPEG_LIBRARIES})

# Install targets
install(TARGETS
    video_info video_decoder video_encoder video_transcoder video_filter
    video_thumbnail video_metadata video_subtitles video_watermark
    video_splitter video_slideshow video_stabilization video_transition
    video_concatenate streaming_server
    audio_info audio_decoder audio_encoder audio_resampler audio_mixer
    audio_noise_reduction audio_format_converter audio_spectrum audio_equalizer audio_transition
    RUNTIME DESTINATION bin
)
