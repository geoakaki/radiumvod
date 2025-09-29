# Video Converter - x264 Full HD

A high-performance C++ video converter that converts any video format to x264-encoded Full HD (1920x1080) MP4 files.

## Features

- Converts any video format supported by FFmpeg to x264 Full HD
- Automatic resolution scaling to 1920x1080
- Preserves audio tracks (AAC encoding)
- Optimized for quality and performance
- Multi-threaded processing
- Cross-format compatibility

## Prerequisites

### Ubuntu/Debian

Install required dependencies:

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libavformat-dev \
    libavcodec-dev \
    libavutil-dev \
    libswscale-dev \
    libavfilter-dev \
    libx264-dev
```

### Fedora/RHEL/CentOS

```bash
sudo dnf install -y \
    gcc-c++ \
    cmake \
    pkgconfig \
    ffmpeg-devel \
    x264-devel
```

### Arch Linux

```bash
sudo pacman -S \
    base-devel \
    cmake \
    ffmpeg \
    x264
```

## Building

1. Clone or download the source code:
```bash
git clone <repository>
cd ott-transcode
```

2. Run the build script:
```bash
./build.sh
```

Or build manually:
```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## Usage

```bash
./video_converter <input_file> <output_file>
```

### Examples

Convert AVI to MP4:
```bash
./video_converter movie.avi movie.mp4
```

Convert MKV to MP4:
```bash
./video_converter video.mkv output.mp4
```

Convert MOV to MP4:
```bash
./video_converter recording.mov converted.mp4
```

## Output Specifications

- **Video Codec**: H.264/x264
- **Resolution**: 1920x1080 (Full HD)
- **Frame Rate**: Preserved from source (default 25 fps)
- **Bit Rate**: 4 Mbps
- **Pixel Format**: YUV420P
- **Audio Codec**: AAC
- **Audio Bit Rate**: 128 kbps

## Performance Optimization

The converter uses:
- Hardware-accelerated scaling (when available)
- Multi-threaded encoding
- Optimized x264 presets
- Native CPU instructions (-march=native)

## Advanced Settings

The converter uses the following x264 settings:
- **Preset**: medium (balanced speed/quality)
- **Tune**: film (optimized for movie content)
- **CRF**: 23 (visually lossless quality)

## Troubleshooting

### Missing Libraries

If you encounter "library not found" errors, ensure all dependencies are installed:
```bash
pkg-config --list-all | grep -E "libav|x264"
```

### Permission Denied

Make the build script executable:
```bash
chmod +x build.sh
```

### Build Errors

Clean the build directory and try again:
```bash
rm -rf build
./build.sh
```

## System Requirements

- **OS**: Linux x64 (Ubuntu 18.04+, Debian 10+, Fedora 30+, etc.)
- **RAM**: Minimum 2GB (4GB+ recommended)
- **CPU**: x64 processor with SSE2 support
- **Disk**: Sufficient space for input and output files

## License

This software uses FFmpeg libraries and x264 encoder. Please ensure compliance with their respective licenses when distributing.

## Support

For issues or questions, please check the build output for specific error messages and ensure all dependencies are correctly installed.

Created with assistance