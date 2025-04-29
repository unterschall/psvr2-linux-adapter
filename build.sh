#!/bin/bash
# PSVR2 Linux Adapter - Build Script
#
# This script automates the build process using the CMake-based system

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print messages
print_msg() {
    echo -e "${BLUE}[PSVR2 Build]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[PSVR2 Build]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[PSVR2 Build]${NC} $1"
}

print_error() {
    echo -e "${RED}[PSVR2 Build]${NC} $1"
}

# Default settings
BUILD_DIR="build"
INSTALL=0
CLEAN=0
REBUILD=0
CONFIG_ARGS=""
JOBS=$(nproc)

# Parse command line arguments
while (( "$#" )); do
    case "$1" in
        --install)
            INSTALL=1
            shift
            ;;
        --clean)
            CLEAN=1
            shift
            ;;
        --rebuild)
            REBUILD=1
            shift
            ;;
        --debug)
            CONFIG_ARGS="$CONFIG_ARGS --debug"
            shift
            ;;
        --dkms)
            CONFIG_ARGS="$CONFIG_ARGS --enable-dkms"
            shift
            ;;
        --userspace)
            CONFIG_ARGS="$CONFIG_ARGS --enable-userspace"
            shift
            ;;
        --tests)
            CONFIG_ARGS="$CONFIG_ARGS --enable-tests"
            shift
            ;;
        --jobs|-j)
            JOBS="$2"
            shift 2
            ;;
        *)
            print_error "Unknown option: $1"
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --install           Install after building"
            echo "  --clean             Clean before building"
            echo "  --rebuild           Force reconfiguration"
            echo "  --debug             Build with debug information"
            echo "  --dkms              Enable DKMS support"
            echo "  --userspace         Build userspace components"
            echo "  --tests             Build tests"
            echo "  --jobs, -j NUMBER   Number of parallel jobs (default: $(nproc))"
            exit 1
            ;;
    esac
done

# Make sure configure.sh is executable
chmod +x configure.sh

# Clean if requested
if [ "$CLEAN" -eq 1 ]; then
    print_msg "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

# Rebuild requires a clean configuration
if [ "$REBUILD" -eq 1 ] && [ -d "$BUILD_DIR" ]; then
    print_msg "Forcing reconfiguration..."
    rm -rf "$BUILD_DIR/CMakeCache.txt" "$BUILD_DIR/CMakeFiles"
fi

# Configure if needed
if [ ! -d "$BUILD_DIR" ] || [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    print_msg "Configuring build system..."
    ./configure.sh $CONFIG_ARGS
fi

# Build
print_msg "Building with $JOBS parallel jobs..."
cd "$BUILD_DIR"
make -j "$JOBS"

# Install if requested
if [ "$INSTALL" -eq 1 ]; then
    print_msg "Installing..."
    if [ "$(id -u)" -ne 0 ]; then
        print_warning "Installation requires root privileges."
        sudo make install
    else
        make install
    fi
fi

print_success "Build completed successfully!"

# Show next steps if not installing
if [ "$INSTALL" -eq 0 ]; then
    echo ""
    echo -e "${GREEN}Next steps:${NC}"
    echo "To install: $0 --install"
    echo "To load the module manually: sudo modprobe psvr2_adapter"
    echo "To verify the module is loaded: lsmod | grep psvr2"
fi
