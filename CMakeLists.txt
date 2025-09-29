cmake_minimum_required(VERSION 3.10)
project(VideoConverter VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

# Find required packages
find_package(PkgConfig REQUIRED)

# Find FFmpeg libraries
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswscale
    libavfilter
)

# Find x264
pkg_check_modules(X264 IMPORTED_TARGET libx264)

# Add executable
add_executable(video_converter video_converter.cpp)

# Include directories
target_include_directories(video_converter PRIVATE ${LIBAV_INCLUDE_DIRS})
if(X264_FOUND)
    target_include_directories(video_converter PRIVATE ${X264_INCLUDE_DIRS})
endif()

# Link libraries
target_link_libraries(video_converter
    PkgConfig::LIBAV
    stdc++fs
    pthread
)

if(X264_FOUND)
    target_link_libraries(video_converter PkgConfig::X264)
endif()

# Compiler flags for optimization
target_compile_options(video_converter PRIVATE 
    -O3 
    -march=native 
    -mtune=native
    -Wall
    -Wextra
    -Wpedantic
)

# Installation
install(TARGETS video_converter
    RUNTIME DESTINATION bin
)

# Print configuration summary
message(STATUS "================================================")
message(STATUS "Video Converter Build Configuration:")
message(STATUS "================================================")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "FFmpeg Found: YES")
message(STATUS "x264 Found: ${X264_FOUND}")
message(STATUS "================================================")