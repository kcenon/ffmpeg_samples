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

# Install targets
install(TARGETS
    video_info video_decoder video_encoder video_transcoder video_filter
    audio_info audio_decoder audio_encoder audio_resampler audio_mixer
    RUNTIME DESTINATION bin
)
