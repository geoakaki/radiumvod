#!/bin/bash

# Video Converter Build Script for Linux x64
# This script compiles the x264 video converter with all dependencies

set -e

echo "================================================"
echo "Video Converter Build Script for Linux x64"
echo "================================================"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print colored messages
print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Check for required dependencies
print_info "Checking dependencies..."

check_dependency() {
    if ! command -v $1 &> /dev/null; then
        print_error "$1 is not installed!"
        echo "Please install it using:"
        echo "  sudo apt-get install $2"
        exit 1
    fi
}

check_library() {
    if ! pkg-config --exists $1 2>/dev/null; then
        print_error "$1 library is not installed!"
        echo "Please install it using:"
        echo "  sudo apt-get install $2"
        exit 1
    fi
}

# Check build tools
check_dependency "cmake" "cmake"
check_dependency "g++" "g++"
check_dependency "pkg-config" "pkg-config"

# Check FFmpeg libraries
print_info "Checking FFmpeg libraries..."
check_library "libavformat" "libavformat-dev"
check_library "libavcodec" "libavcodec-dev"
check_library "libavutil" "libavutil-dev"
check_library "libswscale" "libswscale-dev"
check_library "libavfilter" "libavfilter-dev"

# Check x264 library
print_info "Checking x264 library..."
if ! pkg-config --exists libx264 2>/dev/null; then
    print_warning "libx264 not found. The converter will use FFmpeg's built-in x264 support."
    print_info "For better performance, install x264:"
    echo "  sudo apt-get install libx264-dev"
fi

# Create build directory
BUILD_DIR="build"
print_info "Creating build directory: $BUILD_DIR"
mkdir -p $BUILD_DIR
cd $BUILD_DIR

# Configure with CMake
print_info "Configuring project with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

# Get number of CPU cores for parallel compilation
CORES=$(nproc)
print_info "Building with $CORES parallel jobs..."

# Build the project
make -j$CORES

if [ $? -eq 0 ]; then
    print_info "Build successful!"
    
    # Check if executable was created
    if [ -f "video_converter" ]; then
        print_info "Executable created: $(pwd)/video_converter"
        
        # Make it executable (should already be, but just in case)
        chmod +x video_converter
        
        # Print usage information
        echo ""
        echo "================================================"
        echo "Build Complete!"
        echo "================================================"
        echo "Executable location: $(pwd)/video_converter"
        echo ""
        echo "Usage:"
        echo "  ./video_converter <input_file> <output_file>"
        echo ""
        echo "Example:"
        echo "  ./video_converter input.avi output.mp4"
        echo ""
        echo "To install system-wide (optional):"
        echo "  sudo make install"
        echo "================================================"
    else
        print_error "Executable not found after build!"
        exit 1
    fi
else
    print_error "Build failed!"
    exit 1
fi