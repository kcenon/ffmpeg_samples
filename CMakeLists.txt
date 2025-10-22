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
add_executable(video_info src/video_info.cpp)
target_link_libraries(video_info ${FFMPEG_LIBRARIES})

add_executable(video_decoder src/video_decoder.cpp)
target_link_libraries(video_decoder ${FFMPEG_LIBRARIES})

add_executable(video_encoder src/video_encoder.cpp)
target_link_libraries(video_encoder ${FFMPEG_LIBRARIES})

add_executable(video_transcoder src/video_transcoder.cpp)
target_link_libraries(video_transcoder ${FFMPEG_LIBRARIES})

add_executable(video_filter src/video_filter.cpp)
target_link_libraries(video_filter ${FFMPEG_LIBRARIES})

add_executable(video_thumbnail src/video_thumbnail.cpp)
target_link_libraries(video_thumbnail ${FFMPEG_LIBRARIES})

add_executable(video_metadata src/video_metadata.cpp)
target_link_libraries(video_metadata ${FFMPEG_LIBRARIES})

add_executable(video_subtitles src/video_subtitles.cpp)
target_link_libraries(video_subtitles ${FFMPEG_LIBRARIES})

add_executable(video_watermark src/video_watermark.cpp)
target_link_libraries(video_watermark ${FFMPEG_LIBRARIES})

add_executable(video_splitter src/video_splitter.cpp)
target_link_libraries(video_splitter ${FFMPEG_LIBRARIES})

add_executable(video_slideshow src/video_slideshow.cpp)
target_link_libraries(video_slideshow ${FFMPEG_LIBRARIES})

add_executable(video_stabilization src/video_stabilization.cpp)
target_link_libraries(video_stabilization ${FFMPEG_LIBRARIES})

add_executable(streaming_server src/streaming_server.cpp)
target_link_libraries(streaming_server ${FFMPEG_LIBRARIES})

# Audio sample executables
add_executable(audio_info src/audio_info.cpp)
target_link_libraries(audio_info ${FFMPEG_LIBRARIES})

add_executable(audio_decoder src/audio_decoder.cpp)
target_link_libraries(audio_decoder ${FFMPEG_LIBRARIES})

add_executable(audio_encoder src/audio_encoder.cpp)
target_link_libraries(audio_encoder ${FFMPEG_LIBRARIES})

add_executable(audio_resampler src/audio_resampler.cpp)
target_link_libraries(audio_resampler ${FFMPEG_LIBRARIES})

add_executable(audio_mixer src/audio_mixer.cpp)
target_link_libraries(audio_mixer ${FFMPEG_LIBRARIES})

add_executable(audio_noise_reduction src/audio_noise_reduction.cpp)
target_link_libraries(audio_noise_reduction ${FFMPEG_LIBRARIES})

add_executable(audio_format_converter src/audio_format_converter.cpp)
target_link_libraries(audio_format_converter ${FFMPEG_LIBRARIES})

add_executable(audio_spectrum src/audio_spectrum.cpp)
target_link_libraries(audio_spectrum ${FFMPEG_LIBRARIES})

add_executable(audio_equalizer src/audio_equalizer.cpp)
target_link_libraries(audio_equalizer ${FFMPEG_LIBRARIES})

# Install targets
install(TARGETS
    video_info video_decoder video_encoder video_transcoder video_filter
    video_thumbnail video_metadata video_subtitles video_watermark
    video_splitter video_slideshow video_stabilization streaming_server
    audio_info audio_decoder audio_encoder audio_resampler audio_mixer
    audio_noise_reduction audio_format_converter audio_spectrum audio_equalizer
    RUNTIME DESTINATION bin
)
