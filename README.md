# RadiumVOD - Video On Demand Converter and Streaming Service

A high-performance video processing system that converts videos to H.264/H.265 formats and HLS streaming with automatic directory monitoring and SFTP upload capabilities.

## Features

- **Multiple Output Formats**
  - H.264 encoding with ABR (Adaptive Bitrate) profiles
  - HLS (HTTP Live Streaming) with multiple quality levels
  - H.265 encoding (coming soon)

- **Quality Profiles**
  - High: 1920x1080 @ 4Mbps
  - Medium: 1280x720 @ 2.5Mbps  
  - Low: 854x480 @ 1.2Mbps
  - All: Generate all profiles simultaneously

- **Daemon Mode**
  - Automatic directory monitoring
  - File stability checking
  - Batch processing
  - SFTP upload with retry mechanism
  - Systemd integration

- **Professional CLI**
  - Modern command-line interface
  - Verbose logging options
  - Configuration file support

## Quick Start

### Installation

```bash
# Clone the repository
git clone https://github.com/geoakaki/radiumvod.git
cd radiumvod

# Full installation (daemon mode) - Linux/macOS
sudo ./install.sh

# Install only the binary
sudo ./install.sh --binary-only

# Skip dependency installation
sudo ./install.sh --skip-deps

# Use existing binary (skip build)
sudo ./install.sh --skip-build
```

#### Installation Options

- `--daemon` (default) - Full installation with system service
- `--binary-only` - Install only the radiumvod binary
- `--skip-deps` - Skip dependency installation
- `--skip-build` - Use existing binary from build directory

The installation script will:
- Install required dependencies (unless --skip-deps)
- Build the binary (unless --skip-build)
- Create system user and directories (daemon mode only)
- Install systemd/launchd service (daemon mode only)
- Configure the system

### Platform Support

- **Linux**: Ubuntu, Debian, Fedora, RHEL, CentOS, Arch Linux
- **macOS**: Version 10.15+ (Intel and Apple Silicon M1/M2/M3/M4)

### Update

```bash
# Update to latest version
sudo ./update.sh
```

### Basic Usage

#### Convert a single video
```bash
# Convert to H.264
radiumvod convert -i input.mp4 -o output.mp4 -f h264 -p high

# Convert with all ABR profiles
radiumvod convert -i input.mp4 -o output -f h264 -p all

# Convert to HLS streaming format
radiumvod convert -i input.mp4 -o output_hls -f hls
```

#### Run as system service
```bash
# Start the daemon
sudo systemctl start radiumvod

# Enable auto-start on boot
sudo systemctl enable radiumvod

# Check status
sudo systemctl status radiumvod

# View logs
sudo journalctl -u radiumvod -f
```

## Command Reference

### Convert Command

```bash
radiumvod convert [options]
```

**Options:**
- `-i, --input <file>` - Input video file (required)
- `-o, --output <file>` - Output file/directory (required)
- `-f, --format <format>` - Output format: `h264`, `h265`, `hls` (default: h264)
- `-p, --profile <profile>` - Quality profile: `high`, `medium`, `low`, `all` (default: high)
- `-v, --verbose` - Enable verbose output

**Examples:**
```bash
# H.264 high quality
radiumvod convert -i movie.mp4 -o movie_hd.mp4 -f h264 -p high

# All ABR profiles
radiumvod convert -i movie.mp4 -o movie -f h264 -p all
# Creates: movie_high.mp4, movie_medium.mp4, movie_low.mp4

# HLS streaming
radiumvod convert -i movie.mp4 -o movie_hls -f hls
# Creates HLS directory with playlist.m3u8 and segments
```

### Daemon Command

```bash
radiumvod daemon [options]
```

**Options:**
- `-c, --config <file>` - Configuration file (default: `/etc/radiumvod/radiumvod.conf`)

**Example:**
```bash
radiumvod daemon -c /etc/radiumvod/radiumvod.conf
```

## Configuration

The daemon mode uses a JSON configuration file located at `/etc/radiumvod/radiumvod.conf`:

```json
{
  "watcher": {
    "source_directory": "/var/media/source",
    "destination_directory": "/var/media/hls",
    "watch_interval_seconds": 5,
    "file_extensions": [".mp4", ".avi", ".mkv", ".mov"],
    "delete_source_after_conversion": false,
    "log_file": "/var/log/radiumvod.log"
  },
  
  "hls": {
    "segment_duration": 10,
    "profiles": [
      {
        "name": "720p",
        "width": 1280,
        "height": 720,
        "video_bitrate": 3200000,
        "audio_bitrate": 128000,
        "bandwidth": 3500000,
        "folder_name": "stream_3500"
      }
    ]
  },
  
  "sftp": {
    "enabled": false,
    "host": "your_server.com",
    "port": 22,
    "username": "your_username",
    "password": "your_password",
    "remote_path": "/path/to/remote/VOD",
    "delete_source_after_upload": false,
    "retry_attempts": 3
  }
}
```

## Output Specifications

### H.264 ABR Profiles

| Profile | Resolution | Video Bitrate | Audio Bitrate | H.264 Profile | H.264 Level |
|---------|------------|---------------|---------------|---------------|-------------|
| High    | 1920x1080  | 4.0 Mbps      | 128 kbps      | High          | 4.1         |
| Medium  | 1280x720   | 2.5 Mbps      | 96 kbps       | Main          | 3.1         |
| Low     | 854x480    | 1.2 Mbps      | 64 kbps       | Baseline      | 3.0         |

### HLS Streaming Profiles

| Profile | Resolution | Video Bitrate | Audio Bitrate | Bandwidth | Folder |
|---------|------------|---------------|---------------|-----------|--------|
| 720p    | 1280x720   | 3.2 Mbps      | 128 kbps      | 3.5 Mbps  | stream_3500 |
| 432p    | 768x432    | 1.3 Mbps      | 96 kbps       | 1.5 Mbps  | stream_1500 |
| 288p    | 512x288    | 400 kbps      | 64 kbps       | 500 kbps  | stream_500 |

## System Integration

### Directory Structure

```
/usr/bin/radiumvod              # Main executable
/etc/radiumvod/radiumvod.conf   # Configuration file
/var/media/source/               # Watch directory for new videos
/var/media/hls/                  # Output directory for converted files
/var/log/radiumvod.log          # Log file
```

### System Service

#### Linux (systemd)

The service runs as a system daemon with automatic restart on failure:

```bash
# Service management
sudo systemctl start radiumvod     # Start service
sudo systemctl stop radiumvod      # Stop service
sudo systemctl restart radiumvod   # Restart service
sudo systemctl status radiumvod    # Check status
sudo systemctl enable radiumvod    # Enable on boot
sudo systemctl disable radiumvod   # Disable on boot

# View logs
sudo journalctl -u radiumvod -f    # Follow logs
sudo journalctl -u radiumvod -n 50 # Last 50 lines
```

#### macOS (launchd)

```bash
# Service management
sudo launchctl load /Library/LaunchDaemons/com.radiumvod.plist     # Start service
sudo launchctl unload /Library/LaunchDaemons/com.radiumvod.plist   # Stop service

# Check status
sudo launchctl list | grep radiumvod

# View logs
tail -f /var/log/radiumvod.log
```

## Workflow Example

### Automatic Processing with SFTP Upload

1. Configure SFTP settings in `/etc/radiumvod/radiumvod.conf`
2. Start the daemon service
3. Copy video files to `/var/media/source/`
4. RadiumVOD automatically:
   - Detects new files
   - Converts to HLS format
   - Uploads to SFTP server
   - Optionally deletes source files

### Manual Batch Processing

```bash
#!/bin/bash
# Convert all videos in a directory
for video in /path/to/videos/*.mp4; do
    filename=$(basename "$video" .mp4)
    radiumvod convert -i "$video" -o "/output/$filename" -f hls
done
```

## Building from Source

### Prerequisites

#### Ubuntu/Debian
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
    libswresample-dev \
    libavfilter-dev \
    libx264-dev \
    ffmpeg \
    sshpass
```

#### Fedora/RHEL/CentOS
```bash
sudo dnf install -y \
    gcc-c++ \
    cmake \
    pkgconfig \
    ffmpeg-devel \
    x264-devel \
    ffmpeg \
    sshpass
```

#### Arch Linux
```bash
sudo pacman -S \
    base-devel \
    cmake \
    ffmpeg \
    x264 \
    sshpass
```

#### macOS
```bash
# Install Homebrew if not present
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install dependencies
brew install cmake pkg-config ffmpeg x264

# For SFTP support (optional)
brew install hudochenkov/sshpass/sshpass
```

### Manual Build

```bash
# Create build directory
mkdir build && cd build

# Configure
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
make -j$(nproc)  # Linux
make -j$(sysctl -n hw.ncpu)  # macOS

# Install (optional)
sudo make install
```

## Uninstallation

To completely remove RadiumVOD from your system:

```bash
sudo radiumvod-uninstall
```

This will remove:
- Binary from `/usr/bin/`
- Configuration from `/etc/radiumvod/`
- Systemd service file
- System user (optional)

Note: Media directories in `/var/media/` are preserved.

## Performance Optimization

RadiumVOD is optimized for performance:
- Multi-threaded encoding (uses all CPU cores)
- Hardware acceleration support (when available)
- Native CPU instruction optimization on Linux (`-march=native`)
- Platform-optimized builds for Apple Silicon (M1/M2/M3/M4)
- Efficient memory usage with streaming processing

## Troubleshooting

### Service won't start
```bash
# Check service status
sudo systemctl status radiumvod

# Check configuration file
sudo radiumvod daemon -c /etc/radiumvod/radiumvod.conf

# Check permissions
ls -la /var/media/source /var/media/hls
```

### Conversion fails
```bash
# Test with verbose mode
radiumvod convert -i input.mp4 -o output.mp4 -v

# Check FFmpeg installation
ffmpeg -version

# Check available codecs
ffmpeg -codecs | grep h264
```

### SFTP upload issues
```bash
# Test SFTP connection manually
sftp -P 22 user@server

# Check sshpass installation
which sshpass || sudo apt-get install sshpass

# Review logs
sudo journalctl -u radiumvod | grep SFTP
```

## Security Considerations

- The service runs as a dedicated system user `radiumvod`
- Limited file system access through systemd security settings
- SFTP passwords should be properly secured in configuration
- Consider using SSH keys instead of passwords for SFTP

## License

This software uses FFmpeg libraries and x264 encoder. Please ensure compliance with their respective licenses when distributing.

## Support

For issues or questions:
- Check the logs: `sudo journalctl -u radiumvod -f`
- Review configuration: `sudo nano /etc/radiumvod/radiumvod.conf`
- GitHub Issues: https://github.com/geoakaki/radiumvod/issues

## Version

Current version: 1.0.0

Created with assistance