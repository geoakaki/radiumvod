# Video Converter - x264 with ABR Support

A high-performance C++ video converter that converts any video format to x264-encoded MP4 files with support for Adaptive Bitrate (ABR) streaming profiles.

## Features

- **Standard Converter**: Single Full HD output (1920x1080)
- **ABR Converter**: Multiple quality profiles for adaptive streaming
- Converts any video format supported by FFmpeg to x264
- Automatic resolution scaling
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

### Standard Converter (Single Full HD Output)

```bash
./video_converter <input_file> <output_file>
```

Example:
```bash
./video_converter movie.avi movie.mp4
```

### ABR Converter (Multiple Quality Profiles)

```bash
./video_converter_abr <input_file> <output_base> <profile>
```

Available profiles:
- `high` - 1920x1080 @ 4Mbps video, 128kbps audio (H.264 High Profile)
- `medium` - 1280x720 @ 2.5Mbps video, 96kbps audio (H.264 Main Profile)  
- `low` - 854x480 @ 1.2Mbps video, 64kbps audio (H.264 Baseline Profile)
- `all` - Generate all three profiles

Examples:
```bash
# Generate all ABR profiles
./video_converter_abr input.mp4 output all
# Creates: output_high.mp4, output_medium.mp4, output_low.mp4

# Generate only high quality profile
./video_converter_abr input.mp4 output high
# Creates: output_high.mp4
```

## Output Specifications

### Standard Converter
- **Video Codec**: H.264/x264
- **Resolution**: 1920x1080 (Full HD)
- **Frame Rate**: Preserved from source
- **Bit Rate**: 4 Mbps
- **Pixel Format**: YUV420P
- **Audio Codec**: AAC
- **Audio Bit Rate**: 128 kbps

### ABR Profiles

| Profile | Resolution | Video Bitrate | Audio Bitrate | H.264 Profile | H.264 Level |
|---------|------------|---------------|---------------|---------------|-------------|
| High    | 1920x1080  | 4.0 Mbps      | 128 kbps      | High          | 4.1         |
| Medium  | 1280x720   | 2.5 Mbps      | 96 kbps       | Main          | 3.1         |
| Low     | 854x480    | 1.2 Mbps      | 64 kbps       | Baseline      | 3.0         |

All profiles use:
- **Keyframe Interval**: 120 frames (2 seconds at 60fps)
- **Audio Codec**: AAC (MPEG-4 AAC ADTS)
- **Container**: MP4 with fragmented output for streaming

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