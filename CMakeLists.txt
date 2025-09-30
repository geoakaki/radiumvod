cmake_minimum_required(VERSION 3.10)
project(RadiumVOD VERSION 1.0.0)

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
    libswresample
    libavfilter
)

# Find x264
pkg_check_modules(X264 IMPORTED_TARGET libx264)

# Main executable with all converters
add_executable(radiumvod 
    radiumvod.cpp
    converter_standard.cpp
    converter_abr.cpp
    converter_hls.cpp
    watcher_sftp.cpp
)

# Include directories
target_include_directories(radiumvod PRIVATE ${LIBAV_INCLUDE_DIRS})
if(X264_FOUND)
    target_include_directories(radiumvod PRIVATE ${X264_INCLUDE_DIRS})
endif()

# Link libraries
target_link_libraries(radiumvod
    PkgConfig::LIBAV
    stdc++fs
    pthread
)

if(X264_FOUND)
    target_link_libraries(radiumvod PkgConfig::X264)
endif()

# Compiler flags for optimization
if(APPLE)
    # macOS specific flags
    target_compile_options(radiumvod PRIVATE 
        -O3 
        -Wall
        -Wextra
        -Wpedantic
    )
    # Link macOS frameworks if needed
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    if(COREFOUNDATION_LIBRARY)
        target_link_libraries(radiumvod ${COREFOUNDATION_LIBRARY})
    endif()
else()
    # Linux specific flags
    target_compile_options(radiumvod PRIVATE 
        -O3 
        -march=native 
        -mtune=native
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

# Installation
install(TARGETS radiumvod
    RUNTIME DESTINATION bin
)

# Install config file
install(FILES radiumvod.conf
    DESTINATION /etc/radiumvod
    RENAME radiumvod.conf
)

# Install systemd service
install(FILES radiumvod.service
    DESTINATION /lib/systemd/system
)

# Print configuration summary
message(STATUS "================================================")
message(STATUS "RadiumVOD Build Configuration:")
message(STATUS "================================================")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "FFmpeg Found: YES")
message(STATUS "x264 Found: ${X264_FOUND}")
message(STATUS "================================================")