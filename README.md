# Video Converter - x264 with ABR Support

A high-performance C++ video converter that converts any video format to x264-encoded MP4 files with support for Adaptive Bitrate (ABR) streaming profiles.

## Features

- **Standard Converter**: Single Full HD output (1920x1080)
- **ABR Converter**: Multiple quality profiles for adaptive streaming
- **HLS Converter**: HTTP Live Streaming with m3u8 playlists
- **HLS Watcher**: Automatic directory monitoring and conversion
- Converts any video format supported by FFmpeg to x264
- Automatic resolution scaling
- Preserves audio tracks (AAC encoding)
- Optimized for quality and performance
- Multi-threaded processing
- Cross-format compatibility
- Configurable source/destination directories
- JSON configuration support

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

### HLS Converter (HTTP Live Streaming)

```bash
./video_converter_hls <input_file> <output_directory>
```

Creates HLS streaming structure with master playlist and three quality variants:
- `stream_3500` - 1280x720 @ 3.5Mbps bandwidth
- `stream_1500` - 768x432 @ 1.5Mbps bandwidth
- `stream_500` - 512x288 @ 500kbps bandwidth

Example:
```bash
./video_converter_hls input.mp4 output_hls
```

Creates directory structure:
```
output_hls/
├── playlist.m3u8           # Master playlist
├── stream_3500/
│   ├── index.m3u8         # 720p variant playlist
│   └── segment_*.ts       # Video segments
├── stream_1500/
│   ├── index.m3u8         # 432p variant playlist
│   └── segment_*.ts
└── stream_500/
    ├── index.m3u8         # 288p variant playlist
    └── segment_*.ts
```

### HLS Watcher (Automatic Conversion Service)

```bash
./hls_watcher [config_file]
```

Monitors a source directory for new video files and automatically converts them to HLS format.

#### Configuration File

Create a `config.json` file:

```json
{
  "watcher": {
    "source_directory": "/var/media/source",
    "destination_directory": "/var/media/hls",
    "watch_interval_seconds": 5,
    "file_extensions": [".mp4", ".avi", ".mkv", ".mov"],
    "delete_source_after_conversion": false,
    "log_file": "/var/log/hls_watcher.log"
  },
  "hls": {
    "segment_duration": 10,
    "profiles": [...]
  }
}
```

#### Usage Example

```bash
# Start the watcher service
./hls_watcher config.json

# Or run as a system service
nohup ./hls_watcher /etc/hls_watcher/config.json &

# The watcher will:
# 1. Monitor the source directory
# 2. Detect new video files
# 3. Automatically convert to HLS
# 4. Save to destination directory
```

#### Features

- **Automatic Detection**: Monitors for new files every N seconds
- **File Stability Check**: Ensures files are fully written before processing
- **Configurable Profiles**: Define custom quality profiles in JSON
- **Logging**: Comprehensive logging to file or console
- **Graceful Shutdown**: Handles SIGINT/SIGTERM signals
- **Prevent Reprocessing**: Tracks already converted files

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

### HLS Streaming Profiles

| Profile | Resolution | Video Bitrate | Audio Bitrate | Bandwidth | Folder |
|---------|------------|---------------|---------------|-----------|--------|
| 720p    | 1280x720   | 3.2 Mbps      | 128 kbps      | 3.5 Mbps  | stream_3500 |
| 432p    | 768x432    | 1.3 Mbps      | 96 kbps       | 1.5 Mbps  | stream_1500 |
| 288p    | 512x288    | 400 kbps      | 64 kbps       | 500 kbps  | stream_500 |

HLS features:
- **Segment Duration**: 10 seconds per segment
- **Playlist Format**: HLS v3 compatible
- **Master Playlist**: playlist.m3u8 with bandwidth ladder
- **Codec**: H.264 High Profile + AAC audio

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