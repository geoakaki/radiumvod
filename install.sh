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

# Installation mode
INSTALL_MODE="daemon"  # Default mode
SKIP_DEPS=false
SKIP_BUILD=false

# Function to display usage
show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --binary-only     Install only the binary (no service, no user creation)"
    echo "  --daemon          Full installation with service (default)"
    echo "  --skip-deps       Skip dependency installation"
    echo "  --skip-build      Skip building (use existing binary in build/)"
    echo "  --help            Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                    # Full installation with daemon (default)"
    echo "  $0 --binary-only      # Install only the radiumvod binary"
    echo "  $0 --skip-deps        # Skip dependency installation"
    echo ""
    exit 0
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --binary-only)
            INSTALL_MODE="binary"
            shift
            ;;
        --daemon)
            INSTALL_MODE="daemon"
            shift
            ;;
        --skip-deps)
            SKIP_DEPS=true
            shift
            ;;
        --skip-build)
            SKIP_BUILD=true
            shift
            ;;
        --help|-h)
            show_usage
            ;;
        *)
            print_error "Unknown option: $1"
            show_usage
            ;;
    esac
done

# Check if running as root
if [[ $EUID -ne 0 ]]; then
   print_error "This script must be run as root (use sudo)"
   exit 1
fi

echo "================================================"
echo "         RadiumVOD Installation Script          "
echo "================================================"
echo ""
echo "Installation mode: $INSTALL_MODE"
echo ""

# Step 1: Install dependencies (if not skipped)
if [ "$SKIP_DEPS" = false ]; then
    print_step "Installing dependencies..."
else
    print_info "Skipping dependency installation (--skip-deps)"
fi

# Detect operating system
OS_TYPE=""
ARCH_TYPE=""

if [ "$SKIP_DEPS" = false ]; then
    if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS detected
    OS_TYPE="macos"
    ARCH_TYPE=$(uname -m)
    print_info "Detected macOS system (Architecture: $ARCH_TYPE)"
    
    # Check for Homebrew
    if ! command -v brew &> /dev/null; then
        print_error "Homebrew is not installed. Please install it first:"
        echo "  /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
        exit 1
    fi
    
    print_info "Installing dependencies via Homebrew..."
    
    # Update Homebrew
    brew update
    
    # Install dependencies
    brew install cmake pkg-config ffmpeg x264 || true
    
    # For M-series Macs, ensure we're using the correct paths
    if [[ "$ARCH_TYPE" == "arm64" ]]; then
        export PATH="/opt/homebrew/bin:$PATH"
        export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:$PKG_CONFIG_PATH"
        print_info "Configured for Apple Silicon (M4/M-series)"
    fi
    
    # sshpass is not available via Homebrew due to security concerns
    # Check if sshpass is installed via other means
    if ! command -v sshpass &> /dev/null; then
        print_warning "sshpass is not available on macOS via Homebrew"
        print_info "To install sshpass (optional for SFTP), you can:"
        echo "  1. Use expect scripts instead of sshpass"
        echo "  2. Build sshpass from source:"
        echo "     curl -L https://sourceforge.net/projects/sshpass/files/sshpass/1.09/sshpass-1.09.tar.gz/download -o sshpass.tar.gz"
        echo "     tar xzf sshpass.tar.gz && cd sshpass-1.09"
        echo "     ./configure && make && sudo make install"
        echo "  3. Use SSH keys instead of password authentication (recommended)"
    fi
    
elif [ -f /etc/debian_version ]; then
    # Debian/Ubuntu
    OS_TYPE="linux"
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
        ffmpeg \
        sshpass
        
elif [ -f /etc/redhat-release ]; then
    # RHEL/CentOS/Fedora
    OS_TYPE="linux"
    print_info "Detected RHEL/CentOS/Fedora system"
    dnf install -y \
        gcc-c++ \
        cmake \
        pkgconfig \
        ffmpeg-devel \
        x264-devel \
        ffmpeg \
        sshpass
        
elif [ -f /etc/arch-release ]; then
    # Arch Linux
    OS_TYPE="linux"
    print_info "Detected Arch Linux system"
    pacman -S --noconfirm \
        base-devel \
        cmake \
        ffmpeg \
        x264 \
        sshpass
else
    print_warning "Unknown distribution. Please install dependencies manually:"
    echo "  - cmake"
    echo "  - g++ or clang++"
    echo "  - ffmpeg development libraries"
    echo "  - x264 development libraries"
fi
fi  # End of SKIP_DEPS check

# Detect OS type if not already detected (for build configuration)
if [ -z "$OS_TYPE" ]; then
    if [[ "$OSTYPE" == "darwin"* ]]; then
        OS_TYPE="macos"
        ARCH_TYPE=$(uname -m)
    else
        OS_TYPE="linux"
    fi
fi

# Step 2: Build RadiumVOD (if not skipped)
if [ "$SKIP_BUILD" = false ]; then
    print_step "Building RadiumVOD..."
    
    # Create build directory
    BUILD_DIR="build"
    mkdir -p $BUILD_DIR
    cd $BUILD_DIR

# Configure with CMake based on OS
if [[ "$OS_TYPE" == "macos" ]]; then
    # macOS specific configuration
    if [[ "$ARCH_TYPE" == "arm64" ]]; then
        # M-series Mac configuration
        cmake .. -DCMAKE_BUILD_TYPE=Release \
                 -DCMAKE_PREFIX_PATH="/opt/homebrew" \
                 -DCMAKE_OSX_ARCHITECTURES="arm64"
    else
        # Intel Mac configuration
        cmake .. -DCMAKE_BUILD_TYPE=Release \
                 -DCMAKE_PREFIX_PATH="/usr/local"
    fi
else
    # Linux configuration
    cmake .. -DCMAKE_BUILD_TYPE=Release
fi

# Build
if [[ "$OS_TYPE" == "macos" ]]; then
    CORES=$(sysctl -n hw.ncpu)
else
    CORES=$(nproc)
fi
print_info "Building with $CORES parallel jobs..."
make -j$CORES

# Check if build succeeded
    if [ ! -f "radiumvod" ]; then
        print_error "Build failed! Binary not found."
        exit 1
    fi
    
    print_info "Build successful!"
else
    print_info "Skipping build (--skip-build)"
    # Check if binary exists
    if [ ! -f "build/radiumvod" ]; then
        print_error "Binary not found in build/radiumvod. Please build first or remove --skip-build"
        exit 1
    fi
    cd build
fi

# Step 3: Create system user and directories (only for daemon mode)
if [ "$INSTALL_MODE" = "daemon" ]; then
    print_step "Creating system user and directories..."

if [[ "$OS_TYPE" == "macos" ]]; then
    # macOS user creation
    RADIUMVOD_USER="_radiumvod"
    RADIUMVOD_GROUP="_radiumvod"
    
    # Check if user exists
    if ! dscl . -read /Users/$RADIUMVOD_USER &>/dev/null; then
        print_info "Creating macOS service user '$RADIUMVOD_USER'"
        
        # Find next available UID/GID under 500 (system accounts)
        LAST_UID=$(dscl . -list /Users UniqueID | awk '$2 < 500 {print $2}' | sort -n | tail -1)
        NEXT_UID=$((LAST_UID + 1))
        
        # Create group
        sudo dscl . -create /Groups/$RADIUMVOD_GROUP
        sudo dscl . -create /Groups/$RADIUMVOD_GROUP PrimaryGroupID $NEXT_UID
        
        # Create user
        sudo dscl . -create /Users/$RADIUMVOD_USER
        sudo dscl . -create /Users/$RADIUMVOD_USER UniqueID $NEXT_UID
        sudo dscl . -create /Users/$RADIUMVOD_USER PrimaryGroupID $NEXT_UID
        sudo dscl . -create /Users/$RADIUMVOD_USER UserShell /usr/bin/false
        sudo dscl . -create /Users/$RADIUMVOD_USER RealName "RadiumVOD Service"
        sudo dscl . -create /Users/$RADIUMVOD_USER NFSHomeDirectory /var/lib/radiumvod
        
        print_info "Created macOS service user"
    else
        print_info "User '$RADIUMVOD_USER' already exists"
    fi
    
    # Create directories with macOS paths
    sudo mkdir -p /usr/local/etc/radiumvod
    sudo mkdir -p /var/media/source
    sudo mkdir -p /var/media/hls
    sudo mkdir -p /var/lib/radiumvod
    sudo mkdir -p /var/lib/radiumvod/.ssh
    
    # Set permissions for macOS
    sudo chown -R $RADIUMVOD_USER:$RADIUMVOD_GROUP /var/media/source
    sudo chown -R $RADIUMVOD_USER:$RADIUMVOD_GROUP /var/media/hls
    sudo chown -R $RADIUMVOD_USER:$RADIUMVOD_GROUP /var/lib/radiumvod
    sudo chmod 700 /var/lib/radiumvod/.ssh
    
    # Log file
    sudo touch /var/log/radiumvod.log
    sudo chown $RADIUMVOD_USER:$RADIUMVOD_GROUP /var/log/radiumvod.log
    
    CONFIG_DIR="/usr/local/etc/radiumvod"
    
else
    # Linux user creation
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
    mkdir -p /var/lib/radiumvod/.ssh
    
    # Set permissions
    chown -R radiumvod:radiumvod /var/media/source
    chown -R radiumvod:radiumvod /var/media/hls
    chown -R radiumvod:radiumvod /var/lib/radiumvod
    chmod 700 /var/lib/radiumvod/.ssh
    touch /var/log/radiumvod.log
    chown radiumvod:radiumvod /var/log/radiumvod.log
    
    CONFIG_DIR="/etc/radiumvod"
fi

    print_info "Directories created and permissions set"
else
    print_info "Skipping user and directory creation (--binary-only mode)"
    # Set config directory for binary-only mode
    if [[ "$OS_TYPE" == "macos" ]]; then
        CONFIG_DIR="/usr/local/etc/radiumvod"
    else
        CONFIG_DIR="/etc/radiumvod"
    fi
fi

# Step 4: Install binary
print_step "Installing RadiumVOD binary..."

# Determine binary installation path
if [[ "$OS_TYPE" == "macos" ]]; then
    BINARY_PATH="/usr/local/bin/radiumvod"
    sudo cp radiumvod /usr/local/bin/
    sudo chmod 755 /usr/local/bin/radiumvod
else
    BINARY_PATH="/usr/bin/radiumvod"
    cp radiumvod /usr/bin/
    chmod 755 /usr/bin/radiumvod
fi

print_info "Binary installed to $BINARY_PATH"

# Step 5: Install configuration (only for daemon mode)
if [ "$INSTALL_MODE" = "daemon" ]; then
    print_step "Installing configuration..."
    
    # Go back to source directory
    cd ..
    
    if [[ "$OS_TYPE" == "macos" ]]; then
    # macOS configuration
    if [ ! -f $CONFIG_DIR/radiumvod.conf ]; then
        sudo cp radiumvod.conf $CONFIG_DIR/
        sudo chown $RADIUMVOD_USER:$RADIUMVOD_GROUP $CONFIG_DIR/radiumvod.conf
        sudo chmod 644 $CONFIG_DIR/radiumvod.conf
        print_info "Configuration file installed to $CONFIG_DIR/radiumvod.conf"
    else
        print_warning "Configuration file already exists, not overwriting"
        sudo cp radiumvod.conf $CONFIG_DIR/radiumvod.conf.example
        print_info "Example configuration saved to $CONFIG_DIR/radiumvod.conf.example"
    fi
else
    # Linux configuration
    if [ ! -f $CONFIG_DIR/radiumvod.conf ]; then
        cp radiumvod.conf $CONFIG_DIR/
        chown radiumvod:radiumvod $CONFIG_DIR/radiumvod.conf
        chmod 644 $CONFIG_DIR/radiumvod.conf
        print_info "Configuration file installed to $CONFIG_DIR/radiumvod.conf"
    else
        print_warning "Configuration file already exists, not overwriting"
        cp radiumvod.conf $CONFIG_DIR/radiumvod.conf.example
        print_info "Example configuration saved to $CONFIG_DIR/radiumvod.conf.example"
    fi
fi

# Step 6: Install service
if [[ "$OS_TYPE" == "macos" ]]; then
    print_step "Installing launchd service for macOS..."
    
    # Create launchd plist file
    cat > com.radiumvod.daemon.plist << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.radiumvod.daemon</string>
    <key>ProgramArguments</key>
    <array>
        <string>/usr/local/bin/radiumvod</string>
        <string>daemon</string>
        <string>-c</string>
        <string>/usr/local/etc/radiumvod/radiumvod.conf</string>
    </array>
    <key>UserName</key>
    <string>$RADIUMVOD_USER</string>
    <key>GroupName</key>
    <string>$RADIUMVOD_GROUP</string>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>StandardOutPath</key>
    <string>/var/log/radiumvod.log</string>
    <key>StandardErrorPath</key>
    <string>/var/log/radiumvod.log</string>
</dict>
</plist>
EOF
    
    sudo cp com.radiumvod.daemon.plist /Library/LaunchDaemons/
    sudo chmod 644 /Library/LaunchDaemons/com.radiumvod.daemon.plist
    
    print_info "launchd service installed"
    
else
    print_step "Installing systemd service..."
    
    # Copy service file
    cp radiumvod.service /lib/systemd/system/
    chmod 644 /lib/systemd/system/radiumvod.service
    
    # Reload systemd
    systemctl daemon-reload
    
    print_info "Systemd service installed"
fi
else
    print_info "Skipping configuration and service installation (--binary-only mode)"
fi  # End of daemon mode check

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
echo "  Binary:        $BINARY_PATH"

if [ "$INSTALL_MODE" = "daemon" ]; then
    echo "  Config:        $CONFIG_DIR/radiumvod.conf"
    if [[ "$OS_TYPE" == "macos" ]]; then
        echo "  Service:       com.radiumvod.daemon"
    else
        echo "  Service:       radiumvod.service"
    fi
    echo "  Source Dir:    /var/media/source"
    echo "  Output Dir:    /var/media/hls"
    echo "  Log File:      /var/log/radiumvod.log"
fi
echo ""
echo "Quick Start Commands:"
echo ""

if [ "$INSTALL_MODE" = "daemon" ]; then
    if [[ "$OS_TYPE" == "macos" ]]; then
        echo "  # Start the daemon service:"
        echo "  sudo launchctl load /Library/LaunchDaemons/com.radiumvod.daemon.plist"
        echo ""
        echo "  # Stop the daemon service:"
        echo "  sudo launchctl unload /Library/LaunchDaemons/com.radiumvod.daemon.plist"
        echo ""
        echo "  # Check service status:"
        echo "  sudo launchctl list | grep radiumvod"
        echo ""
        echo "  # View logs:"
        echo "  tail -f /var/log/radiumvod.log"
        echo ""
        echo "  # Edit configuration:"
        echo "  sudo nano /usr/local/etc/radiumvod/radiumvod.conf"
    else
        echo "  # Start the daemon service:"
        echo "  sudo systemctl start radiumvod"
        echo ""
        echo "  # Enable auto-start on boot:"
        echo "  sudo systemctl enable radiumvod"
        echo ""
        echo "  # Check service status:"
        echo "  sudo systemctl status radiumvod"
        echo ""
        echo "  # View logs:"
        echo "  sudo journalctl -u radiumvod -f"
        echo ""
        echo "  # Edit configuration:"
        echo "  sudo nano /etc/radiumvod/radiumvod.conf"
    fi
    echo ""
fi

echo "  # Convert a single file:"
echo "  radiumvod convert -i input.mp4 -o output.mp4 -f h264 -p high"
echo ""
echo "  # Convert to HLS streaming format:"
echo "  radiumvod convert -i input.mp4 -o output_dir -f hls -p all"
echo ""
echo "  # Uninstall RadiumVOD:"
echo "  sudo radiumvod-uninstall"
echo ""
echo "================================================"

# Ask if user wants to start the service now (only for daemon mode)
if [ "$INSTALL_MODE" = "daemon" ]; then
    read -p "Would you like to start the RadiumVOD service now? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        if [[ "$OS_TYPE" == "macos" ]]; then
            sudo launchctl load /Library/LaunchDaemons/com.radiumvod.daemon.plist
            sleep 2
            if sudo launchctl list | grep -q com.radiumvod.daemon; then
                print_info "RadiumVOD service is now running"
            else
                print_error "Failed to start RadiumVOD service"
            fi
        else
            systemctl start radiumvod
            systemctl status radiumvod --no-pager
            print_info "RadiumVOD service is now running"
        fi
    fi
else
    print_info "Binary-only installation complete. You can now use radiumvod command."
fi