#!/bin/bash

# RadiumVOD Update Script
# This script updates RadiumVOD to the latest version from GitHub

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
echo "         RadiumVOD Update Script               "
echo "================================================"
echo ""

# Check if radiumvod is installed
if [ ! -f /usr/bin/radiumvod ]; then
    print_error "RadiumVOD is not installed. Please run install.sh first."
    exit 1
fi

# Get current version if possible
CURRENT_VERSION="unknown"
if /usr/bin/radiumvod version 2>/dev/null | grep -q "version"; then
    CURRENT_VERSION=$(/usr/bin/radiumvod version | head -1 | awk '{print $3}')
fi
print_info "Current version: $CURRENT_VERSION"

# Step 1: Stop the service if running
print_step "Checking RadiumVOD service..."
if systemctl is-active --quiet radiumvod; then
    print_info "Stopping RadiumVOD service..."
    systemctl stop radiumvod
    SERVICE_WAS_RUNNING=true
else
    print_info "RadiumVOD service is not running"
    SERVICE_WAS_RUNNING=false
fi

# Step 2: Create temporary directory for update
print_step "Creating temporary update directory..."
UPDATE_DIR=$(mktemp -d /tmp/radiumvod-update.XXXXXX)
cd $UPDATE_DIR

print_info "Working directory: $UPDATE_DIR"

# Step 3: Clone or download the latest version
print_step "Downloading latest version from GitHub..."

# Check if git is available
if command -v git &> /dev/null; then
    git clone --depth 1 https://github.com/geoakaki/radiumvod.git
    cd radiumvod
else
    # Use wget or curl as fallback
    if command -v wget &> /dev/null; then
        wget -O radiumvod.tar.gz https://github.com/geoakaki/radiumvod/archive/refs/heads/main.tar.gz
    elif command -v curl &> /dev/null; then
        curl -L -o radiumvod.tar.gz https://github.com/geoakaki/radiumvod/archive/refs/heads/main.tar.gz
    else
        print_error "Neither git, wget, nor curl is available. Cannot download update."
        rm -rf $UPDATE_DIR
        exit 1
    fi
    
    tar -xzf radiumvod.tar.gz
    cd radiumvod-main
fi

# Step 4: Check dependencies
print_step "Checking build dependencies..."

check_dependency() {
    if ! command -v $1 &> /dev/null; then
        print_error "$1 is not installed!"
        return 1
    fi
    return 0
}

check_library() {
    if ! pkg-config --exists $1 2>/dev/null; then
        print_error "$1 library is not installed!"
        return 1
    fi
    return 0
}

# Check build tools
DEPS_OK=true
check_dependency "cmake" || DEPS_OK=false
check_dependency "g++" || DEPS_OK=false
check_dependency "pkg-config" || DEPS_OK=false

# Check libraries
check_library "libavformat" || DEPS_OK=false
check_library "libavcodec" || DEPS_OK=false
check_library "libavutil" || DEPS_OK=false
check_library "libswscale" || DEPS_OK=false

if [ "$DEPS_OK" = false ]; then
    print_error "Missing dependencies. Please install them first or run install.sh"
    rm -rf $UPDATE_DIR
    exit 1
fi

# Step 5: Build the new version
print_step "Building RadiumVOD..."

# Create build directory
mkdir -p build
cd build

# Configure with CMake
cmake .. -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1 || {
    print_error "CMake configuration failed"
    rm -rf $UPDATE_DIR
    exit 1
}

# Build
CORES=$(nproc)
print_info "Building with $CORES parallel jobs..."
make -j$CORES > /dev/null 2>&1 || {
    print_error "Build failed"
    rm -rf $UPDATE_DIR
    exit 1
}

# Check if build succeeded
if [ ! -f "radiumvod" ]; then
    print_error "Build failed! Binary not found."
    rm -rf $UPDATE_DIR
    exit 1
fi

print_info "Build successful!"

# Get new version
NEW_VERSION="unknown"
if ./radiumvod version 2>/dev/null | grep -q "version"; then
    NEW_VERSION=$(./radiumvod version | head -1 | awk '{print $3}')
fi
print_info "New version: $NEW_VERSION"

# Step 6: Backup current binary
print_step "Backing up current binary..."
BACKUP_FILE="/usr/bin/radiumvod.backup.$(date +%Y%m%d_%H%M%S)"
cp /usr/bin/radiumvod $BACKUP_FILE
print_info "Backup saved to: $BACKUP_FILE"

# Step 7: Install new binary
print_step "Installing new binary..."
cp radiumvod /usr/bin/radiumvod
chmod 755 /usr/bin/radiumvod
print_info "New binary installed successfully"

# Step 8: Update configuration file if needed
print_step "Checking configuration..."
cd ..
if [ -f "radiumvod.conf" ]; then
    if [ ! -f /etc/radiumvod/radiumvod.conf ]; then
        print_info "Installing default configuration..."
        mkdir -p /etc/radiumvod
        cp radiumvod.conf /etc/radiumvod/
        chown radiumvod:radiumvod /etc/radiumvod/radiumvod.conf 2>/dev/null || true
    else
        # Save new example config
        cp radiumvod.conf /etc/radiumvod/radiumvod.conf.new
        print_info "New example configuration saved to /etc/radiumvod/radiumvod.conf.new"
        print_info "Your existing configuration has been preserved"
    fi
fi

# Ensure SSH directory exists for SFTP
if [ -d /var/lib/radiumvod ]; then
    if [ ! -d /var/lib/radiumvod/.ssh ]; then
        print_info "Creating SSH directory for SFTP support..."
        mkdir -p /var/lib/radiumvod/.ssh
        chown -R radiumvod:radiumvod /var/lib/radiumvod
        chmod 700 /var/lib/radiumvod/.ssh
    fi
fi

# Step 9: Update systemd service if changed
if [ -f "radiumvod.service" ]; then
    if ! diff -q radiumvod.service /lib/systemd/system/radiumvod.service > /dev/null 2>&1; then
        print_info "Updating systemd service file..."
        cp radiumvod.service /lib/systemd/system/
        systemctl daemon-reload
        print_info "Systemd service updated"
    else
        # Force update if service has old permissions
        if ! grep -q "ReadWritePaths=/var/media /var/log/radiumvod.log /var/lib/radiumvod" /lib/systemd/system/radiumvod.service; then
            print_info "Updating systemd service permissions..."
            cp radiumvod.service /lib/systemd/system/
            systemctl daemon-reload
            print_info "Systemd service permissions updated"
        fi
    fi
fi

# Fix directory permissions (in case they were changed)
print_step "Verifying directory permissions..."
if [ -d /var/media/source ]; then
    chown -R radiumvod:radiumvod /var/media/source 2>/dev/null || true
fi
if [ -d /var/media/hls ]; then
    chown -R radiumvod:radiumvod /var/media/hls 2>/dev/null || true
fi
if [ -d /var/lib/radiumvod ]; then
    chown -R radiumvod:radiumvod /var/lib/radiumvod 2>/dev/null || true
    chmod 700 /var/lib/radiumvod/.ssh 2>/dev/null || true
fi
if [ -f /var/log/radiumvod.log ]; then
    chown radiumvod:radiumvod /var/log/radiumvod.log 2>/dev/null || true
fi

# Step 10: Cleanup
print_step "Cleaning up temporary files..."
cd /
rm -rf $UPDATE_DIR
print_info "Cleanup complete"

# Step 11: Restart service if it was running
if [ "$SERVICE_WAS_RUNNING" = true ]; then
    print_step "Restarting RadiumVOD service..."
    systemctl start radiumvod
    sleep 2
    if systemctl is-active --quiet radiumvod; then
        print_info "RadiumVOD service restarted successfully"
    else
        print_error "Failed to restart RadiumVOD service"
        print_info "Check the logs: journalctl -u radiumvod -n 50"
    fi
fi

# Step 12: Verify update
print_step "Verifying update..."
if /usr/bin/radiumvod version > /dev/null 2>&1; then
    print_info "RadiumVOD is working correctly"
else
    print_error "RadiumVOD binary test failed"
    print_info "Restoring backup..."
    cp $BACKUP_FILE /usr/bin/radiumvod
    print_info "Backup restored. Update failed."
    exit 1
fi

# Display completion message
echo ""
echo "================================================"
echo "       RadiumVOD Update Complete!              "
echo "================================================"
echo ""
echo "Update Summary:"
echo "  Previous version: $CURRENT_VERSION"
echo "  New version:      $NEW_VERSION"
echo "  Backup binary:    $BACKUP_FILE"
echo ""

if [ "$CURRENT_VERSION" = "$NEW_VERSION" ]; then
    print_warning "Version numbers are the same. You may already be on the latest version."
fi

echo "To check the status:"
echo "  sudo systemctl status radiumvod"
echo ""
echo "To view recent logs:"
echo "  sudo journalctl -u radiumvod -n 50"
echo ""
echo "To restore previous version if needed:"
echo "  sudo cp $BACKUP_FILE /usr/bin/radiumvod"
echo "  sudo systemctl restart radiumvod"
echo ""

# Keep last 5 backups, remove older ones
print_info "Cleaning old backups (keeping last 5)..."
ls -t /usr/bin/radiumvod.backup.* 2>/dev/null | tail -n +6 | xargs rm -f 2>/dev/null || true

echo "================================================"