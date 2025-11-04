cmake_minimum_required(VERSION 3.15)

# Find Google Benchmark
find_package(benchmark QUIET)

if(NOT benchmark_FOUND)
    message(STATUS "Google Benchmark not found. Fetching from GitHub...")
    include(FetchContent)

    FetchContent_Declare(
        googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.8.3
    )

    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googlebenchmark)
endif()

# Benchmark executable
add_executable(ffmpeg_benchmarks
    bench_ffmpeg_wrappers.cpp
)

target_link_libraries(ffmpeg_benchmarks
    benchmark::benchmark
    ${FFMPEG_LIBRARIES}
)

target_include_directories(ffmpeg_benchmarks PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${FFMPEG_INCLUDE_DIRS}
)

message(STATUS "Benchmarks configured successfully")
