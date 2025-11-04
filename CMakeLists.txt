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
# Note: Warning flags are set via CMAKE_CXX_FLAGS from command line or defaults
if(NOT CMAKE_CXX_FLAGS)
    set(CMAKE_CXX_FLAGS "-Wall -Wextra")
endif()
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# Function to add sample executables
function(add_sample_executable sample_file)
    get_filename_component(sample_name ${sample_file} NAME_WE)
    add_executable(${sample_name} ${sample_file})
    target_link_libraries(${sample_name} ${FFMPEG_LIBRARIES})
    list(APPEND SAMPLE_TARGETS ${sample_name})
    set(SAMPLE_TARGETS ${SAMPLE_TARGETS} PARENT_SCOPE)
endfunction()

# Automatically find and add all sample executables
set(SAMPLE_TARGETS "")

# Video samples
file(GLOB VIDEO_SAMPLES src/video/*.cpp)
list(LENGTH VIDEO_SAMPLES VIDEO_COUNT)
foreach(sample ${VIDEO_SAMPLES})
    add_sample_executable(${sample})
endforeach()

# Audio samples
file(GLOB AUDIO_SAMPLES src/audio/*.cpp)
list(LENGTH AUDIO_SAMPLES AUDIO_COUNT)
foreach(sample ${AUDIO_SAMPLES})
    add_sample_executable(${sample})
endforeach()

# Streaming samples
file(GLOB STREAMING_SAMPLES src/streaming/*.cpp)
list(LENGTH STREAMING_SAMPLES STREAMING_COUNT)
foreach(sample ${STREAMING_SAMPLES})
    add_sample_executable(${sample})
endforeach()

# Calculate total
math(EXPR TOTAL_SAMPLES "${VIDEO_COUNT} + ${AUDIO_COUNT} + ${STREAMING_COUNT}")

# Print summary
message(STATUS "Found ${VIDEO_COUNT} video samples")
message(STATUS "Found ${AUDIO_COUNT} audio samples")
message(STATUS "Found ${STREAMING_COUNT} streaming samples")
message(STATUS "Total: ${TOTAL_SAMPLES} samples")

# Install all targets
install(TARGETS ${SAMPLE_TARGETS}
    RUNTIME DESTINATION bin
)

# Optional: Enable testing
option(BUILD_TESTING "Build tests" OFF)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# Optional: Enable benchmarks
option(BUILD_BENCHMARKS "Build performance benchmarks" OFF)
if(BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# Print build configuration
message(STATUS "========================================")
message(STATUS "FFmpeg Samples Build Configuration")
message(STATUS "========================================")
message(STATUS "C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Testing: ${BUILD_TESTING}")
message(STATUS "Benchmarks: ${BUILD_BENCHMARKS}")
message(STATUS "========================================")
