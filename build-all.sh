#!/bin/bash
# Complete build script for PSVR2 Linux adapter
# This script applies fixes and builds the project in one step

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

# Make this script executable
chmod +x "$0"

# Default settings
RECONFIGURE=1
INSTALL=0
CLEAN=1
USE_DKMS=0
DEBUG=0
JOBS=$(nproc)

# Parse command line arguments
while (( "$#" )); do
    case "$1" in
        --install)
            INSTALL=1
            shift
            ;;
        --dkms)
            USE_DKMS=1
            shift
            ;;
        --debug)
            DEBUG=1
            shift
            ;;
        --no-reconfigure)
            RECONFIGURE=0
            shift
            ;;
        --no-clean)
            CLEAN=0
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
            echo "  --dkms              Enable DKMS support"
            echo "  --debug             Build with debug information"
            echo "  --no-reconfigure    Skip reconfiguration step"
            echo "  --no-clean          Skip cleaning step"
            echo "  --jobs, -j NUMBER   Number of parallel jobs (default: $(nproc))"
            exit 1
            ;;
    esac
done

print_msg "Starting full build process for PSVR2 Linux adapter..."

# Make sure all scripts are executable
print_msg "Making scripts executable..."
chmod +x configure.sh
chmod +x build.sh
if [ -f fix-build.sh ]; then
    chmod +x fix-build.sh
fi

# Step 1: Clean if requested
if [ "$CLEAN" -eq 1 ]; then
    print_msg "Cleaning build directory..."
    rm -rf build
fi

# Step 2: Configure the build
if [ "$RECONFIGURE" -eq 1 ] || [ ! -d "build" ]; then
    print_msg "Configuring build system..."
    
    CONFIG_ARGS=""
    if [ "$DEBUG" -eq 1 ]; then
        CONFIG_ARGS="$CONFIG_ARGS --debug"
    fi
    if [ "$USE_DKMS" -eq 1 ]; then
        CONFIG_ARGS="$CONFIG_ARGS --enable-dkms"
    fi
    
    ./configure.sh $CONFIG_ARGS
fi

# Step 3: Apply build fixes
print_msg "Applying build fixes..."

# Fix include paths in all source files
for SRC_FILE in kernel/module/*.c; do
    sed -i 's|#include "../include/psvr2_adapter.h"|#include <psvr2/psvr2_adapter.h>|g' "$SRC_FILE"
done

# Create proper include directory in build
mkdir -p "build/kernel/include/psvr2"
cp -f "kernel/include/psvr2_adapter.h" "build/kernel/include/psvr2/"

# Fix Kbuild file
cat > "build/kernel/Kbuild" << EOF
# Fixed Kbuild file
obj-m := psvr2_adapter.o
psvr2_adapter-objs := psvr2_adapter_main.o psvr2_display.o psvr2_input.o psvr2_hid.o
ccflags-y := -I\$(src)/include
EOF

print_success "Build fixes applied."

# Step 4: Build the project
print_msg "Building with $JOBS parallel jobs..."
cd build
make -j "$JOBS"
BUILD_RESULT=$?

if [ $BUILD_RESULT -ne 0 ]; then
    print_error "Build failed with error code $BUILD_RESULT"
    exit $BUILD_RESULT
fi

print_success "Build completed successfully!"

# Step 5: Install if requested
if [ "$INSTALL" -eq 1 ]; then
    print_msg "Installing..."
    if [ "$(id -u)" -ne 0 ]; then
        print_warning "Installation requires root privileges."
        sudo make install
    else
        make install
    fi
    
    # Reload udev rules if installed
    if [ "$(id -u)" -ne 0 ]; then
        sudo udevadm control --reload-rules
        sudo udevadm trigger
    else
        udevadm control --reload-rules
        udevadm trigger
    fi
    
    print_success "Installation completed."
fi

print_success "All steps completed successfully!"
echo ""
echo "Next steps:"
if [ "$INSTALL" -eq 0 ]; then
    echo "- To install the module: $0 --install"
    echo "- To load the module manually: sudo modprobe psvr2_adapter"
else
    echo "- The module can now be loaded: sudo modprobe psvr2_adapter"
    echo "- To verify the module is loaded: lsmod | grep psvr2"
fi
