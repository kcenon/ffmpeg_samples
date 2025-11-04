cmake_minimum_required(VERSION 3.15)

# Find Google Test
find_package(GTest QUIET)

if(NOT GTest_FOUND)
    message(STATUS "Google Test not found. Fetching from GitHub...")
    include(FetchContent)

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )

    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(googletest)
endif()

# Enable testing
enable_testing()

# Test executable
add_executable(ffmpeg_wrappers_test
    test_ffmpeg_wrappers.cpp
)

target_link_libraries(ffmpeg_wrappers_test
    GTest::gtest_main
    ${FFMPEG_LIBRARIES}
)

target_include_directories(ffmpeg_wrappers_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${FFMPEG_INCLUDE_DIRS}
)

# Discover tests
include(GoogleTest)
gtest_discover_tests(ffmpeg_wrappers_test)

message(STATUS "Tests configured successfully")
