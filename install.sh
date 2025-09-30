#!/bin/bash

# RadiumVOD Installation Script
# This script builds and installs RadiumVOD system-wide

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
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

print_step() {
    echo -e "${BLUE}[STEP]${NC} $1"
}

# Check if running as root
if [[ $EUID -ne 0 ]]; then
   print_error "This script must be run as root (use sudo)"
   exit 1
fi

echo "================================================"
echo "         RadiumVOD Installation Script          "
echo "================================================"
echo ""

# Step 1: Install dependencies
print_step "Installing dependencies..."

# Detect distribution
if [ -f /etc/debian_version ]; then
    # Debian/Ubuntu
    print_info "Detected Debian/Ubuntu system"
    apt-get update
    apt-get install -y \
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
        ffmpeg
        
elif [ -f /etc/redhat-release ]; then
    # RHEL/CentOS/Fedora
    print_info "Detected RHEL/CentOS/Fedora system"
    dnf install -y \
        gcc-c++ \
        cmake \
        pkgconfig \
        ffmpeg-devel \
        x264-devel \
        ffmpeg
        
elif [ -f /etc/arch-release ]; then
    # Arch Linux
    print_info "Detected Arch Linux system"
    pacman -S --noconfirm \
        base-devel \
        cmake \
        ffmpeg \
        x264
else
    print_warning "Unknown distribution. Please install dependencies manually:"
    echo "  - cmake"
    echo "  - g++ or clang++"
    echo "  - ffmpeg development libraries"
    echo "  - x264 development libraries"
fi

# Step 2: Build RadiumVOD
print_step "Building RadiumVOD..."

# Create build directory
BUILD_DIR="build"
mkdir -p $BUILD_DIR
cd $BUILD_DIR

# Configure with CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
CORES=$(nproc)
print_info "Building with $CORES parallel jobs..."
make -j$CORES

# Check if build succeeded
if [ ! -f "radiumvod" ]; then
    print_error "Build failed! Binary not found."
    exit 1
fi

print_info "Build successful!"

# Step 3: Create system user and directories
print_step "Creating system user and directories..."

# Create radiumvod user if it doesn't exist
if ! id "radiumvod" &>/dev/null; then
    useradd -r -s /bin/false -d /var/lib/radiumvod radiumvod
    print_info "Created system user 'radiumvod'"
else
    print_info "User 'radiumvod' already exists"
fi

# Create necessary directories
mkdir -p /etc/radiumvod
mkdir -p /var/media/source
mkdir -p /var/media/hls
mkdir -p /var/log
mkdir -p /var/lib/radiumvod

# Set permissions
chown -R radiumvod:radiumvod /var/media/source
chown -R radiumvod:radiumvod /var/media/hls
chown radiumvod:radiumvod /var/lib/radiumvod
touch /var/log/radiumvod.log
chown radiumvod:radiumvod /var/log/radiumvod.log

print_info "Directories created and permissions set"

# Step 4: Install binary
print_step "Installing RadiumVOD binary..."

# Copy binary to /usr/bin
cp radiumvod /usr/bin/
chmod 755 /usr/bin/radiumvod

print_info "Binary installed to /usr/bin/radiumvod"

# Step 5: Install configuration
print_step "Installing configuration..."

# Go back to source directory
cd ..

# Copy config file if it doesn't exist
if [ ! -f /etc/radiumvod/radiumvod.conf ]; then
    cp radiumvod.conf /etc/radiumvod/
    chown radiumvod:radiumvod /etc/radiumvod/radiumvod.conf
    chmod 644 /etc/radiumvod/radiumvod.conf
    print_info "Configuration file installed to /etc/radiumvod/radiumvod.conf"
else
    print_warning "Configuration file already exists, not overwriting"
    cp radiumvod.conf /etc/radiumvod/radiumvod.conf.example
    print_info "Example configuration saved to /etc/radiumvod/radiumvod.conf.example"
fi

# Step 6: Install systemd service
print_step "Installing systemd service..."

# Copy service file
cp radiumvod.service /lib/systemd/system/
chmod 644 /lib/systemd/system/radiumvod.service

# Reload systemd
systemctl daemon-reload

print_info "Systemd service installed"

# Step 7: Create uninstall script
print_step "Creating uninstall script..."

cat > /usr/bin/radiumvod-uninstall << 'EOF'
#!/bin/bash

if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root"
   exit 1
fi

echo "Uninstalling RadiumVOD..."

# Stop and disable service
systemctl stop radiumvod 2>/dev/null || true
systemctl disable radiumvod 2>/dev/null || true

# Remove files
rm -f /usr/bin/radiumvod
rm -f /usr/bin/radiumvod-uninstall
rm -f /lib/systemd/system/radiumvod.service
rm -rf /etc/radiumvod

# Remove user (optional, commented out for safety)
# userdel radiumvod 2>/dev/null || true

# Reload systemd
systemctl daemon-reload

echo "RadiumVOD has been uninstalled"
echo "Note: Media directories in /var/media have been preserved"
EOF

chmod 755 /usr/bin/radiumvod-uninstall
print_info "Uninstall script created at /usr/bin/radiumvod-uninstall"

# Step 8: Display completion message
echo ""
echo "================================================"
echo "       RadiumVOD Installation Complete!         "
echo "================================================"
echo ""
echo "Installation Summary:"
echo "  Binary:        /usr/bin/radiumvod"
echo "  Config:        /etc/radiumvod/radiumvod.conf"
echo "  Service:       radiumvod.service"
echo "  Source Dir:    /var/media/source"
echo "  Output Dir:    /var/media/hls"
echo "  Log File:      /var/log/radiumvod.log"
echo ""
echo "Quick Start Commands:"
echo ""
echo "  # Start the daemon service:"
echo "  sudo systemctl start radiumvod"
echo ""
echo "  # Enable auto-start on boot:"
echo "  sudo systemctl enable radiumvod"
echo ""
echo "  # Check service status:"
echo "  sudo systemctl status radiumvod"
echo ""
echo "  # Convert a single file:"
echo "  radiumvod convert -i input.mp4 -o output.mp4 -f h264 -p high"
echo ""
echo "  # Convert to HLS streaming format:"
echo "  radiumvod convert -i input.mp4 -o output_dir -f hls -p all"
echo ""
echo "  # View logs:"
echo "  sudo journalctl -u radiumvod -f"
echo ""
echo "  # Edit configuration:"
echo "  sudo nano /etc/radiumvod/radiumvod.conf"
echo ""
echo "  # Uninstall RadiumVOD:"
echo "  sudo radiumvod-uninstall"
echo ""
echo "================================================"

# Ask if user wants to start the service now
read -p "Would you like to start the RadiumVOD service now? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    systemctl start radiumvod
    systemctl status radiumvod --no-pager
    print_info "RadiumVOD service is now running"
fi