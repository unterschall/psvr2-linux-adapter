#!/bin/bash
# PSVR2 Linux Adapter - Configuration Script
#
# This script simplifies the build system setup and configuration,
# handling dependencies and preparing the build environment.

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print messages
print_msg() {
    echo -e "${BLUE}[PSVR2 Config]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[PSVR2 Config]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[PSVR2 Config]${NC} $1"
}

print_error() {
    echo -e "${RED}[PSVR2 Config]${NC} $1"
}

# Default configuration
BUILD_TYPE="Release"
BUILD_DIR="build"
ENABLE_KERNEL_MODULE=ON
ENABLE_USERSPACE=OFF
ENABLE_DKMS=OFF
ENABLE_TESTS=OFF
CMAKE_GENERATOR="Unix Makefiles"
INSTALL_PREFIX="/usr/local"
DEBUG=OFF

# Parse command line arguments
while (( "$#" )); do
    case "$1" in
        --debug)
            BUILD_TYPE="Debug"
            DEBUG=ON
            shift
            ;;
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --prefix)
            INSTALL_PREFIX="$2"
            shift 2
            ;;
        --disable-kernel-module)
            ENABLE_KERNEL_MODULE=OFF
            shift
            ;;
        --enable-userspace)
            ENABLE_USERSPACE=ON
            shift
            ;;
        --enable-dkms)
            ENABLE_DKMS=ON
            shift
            ;;
        --enable-tests)
            ENABLE_TESTS=ON
            shift
            ;;
        --ninja)
            CMAKE_GENERATOR="Ninja"
            shift
            ;;
        --clean)
            # Clean build directory
            if [ -d "$BUILD_DIR" ]; then
                print_msg "Cleaning build directory..."
                rm -rf "$BUILD_DIR"
            fi
            print_success "Build directory cleaned."
            exit 0
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --debug                     Enable debug build"
            echo "  --build-dir DIR             Set build directory (default: build)"
            echo "  --prefix DIR                Set installation prefix (default: /usr/local)"
            echo "  --disable-kernel-module     Don't build kernel module"
            echo "  --enable-userspace          Build userspace tools and libraries"
            echo "  --enable-dkms               Enable DKMS support"
            echo "  --enable-tests              Build tests"
            echo "  --ninja                     Use Ninja build system instead of Make"
            echo "  --clean                     Clean build directory"
            echo "  -h, --help                  Show this help message"
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Check for required tools
print_msg "Checking for required tools..."

# Check for CMake
if ! command -v cmake &> /dev/null; then
    print_error "CMake is required but not installed. Please install CMake first."
    exit 1
fi

# Check for compiler
if ! command -v gcc &> /dev/null; then
    print_error "GCC is required but not installed. Please install GCC first."
    exit 1
fi

# Check for Ninja if selected
if [ "$CMAKE_GENERATOR" = "Ninja" ] && ! command -v ninja &> /dev/null; then
    print_warning "Ninja build system requested but not found. Falling back to Make."
    CMAKE_GENERATOR="Unix Makefiles"
fi

# Check for pkg-config
if ! command -v pkg-config &> /dev/null; then
    print_warning "pkg-config is not installed. Some dependency checks may fail."
fi

# Create build directory
if [ ! -d "$BUILD_DIR" ]; then
    print_msg "Creating build directory..."
    mkdir -p "$BUILD_DIR"
fi

# Configure the build
print_msg "Configuring build system..."
cd "$BUILD_DIR"

CMAKE_ARGS=(
    "-G" "${CMAKE_GENERATOR}"
    "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
    "-DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX}"
    "-DENABLE_KERNEL_MODULE=${ENABLE_KERNEL_MODULE}"
    "-DENABLE_USERSPACE=${ENABLE_USERSPACE}"
    "-DENABLE_DKMS=${ENABLE_DKMS}"
    "-DENABLE_TESTS=${ENABLE_TESTS}"
    "-DDEBUG=${DEBUG}"
)

print_msg "Running CMake with the following options:"
for arg in "${CMAKE_ARGS[@]}"; do
    if [[ "$arg" != "-G" ]]; then
        echo "  $arg"
    fi
done

cmake "${CMAKE_ARGS[@]}" ..

# Display next steps
print_success "Configuration completed successfully!"
echo ""
echo -e "${GREEN}Next steps:${NC}"
echo "1. Change to the build directory: cd $BUILD_DIR"
echo "2. Build the project: make"
echo "3. Install (as root): make install"
echo ""
echo -e "${YELLOW}Note:${NC} You may need to run 'make' with parallel jobs for faster build:"
echo "make -j\$(nproc)"
echo ""
