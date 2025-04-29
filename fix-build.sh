#!/bin/bash
# Script to fix the build issues with the PSVR2 Linux adapter

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print messages
print_msg() {
    echo -e "${BLUE}[PSVR2 Fix]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[PSVR2 Fix]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[PSVR2 Fix]${NC} $1"
}

print_error() {
    echo -e "${RED}[PSVR2 Fix]${NC} $1"
}

# Make this script executable
chmod +x "$0"

print_msg "Fixing build issues for PSVR2 Linux adapter project..."

# 1. Install the fixed KernelModule.cmake
print_msg "Installing fixed KernelModule.cmake..."
if [ -f "build/cmake/KernelModule.cmake.fixed" ]; then
    cp -f build/cmake/KernelModule.cmake.fixed build/cmake/KernelModule.cmake
    print_success "KernelModule.cmake updated."
else
    print_error "Fixed KernelModule.cmake not found. Skipping."
fi

# 2. Fix include paths in source files
print_msg "Fixing include paths in source files..."

# Directory to store fixed source files
FIXED_DIR="kernel/module/fixed"
mkdir -p "$FIXED_DIR"

# Function to fix include path in a file
fix_include_path() {
    local file="$1"
    local filename=$(basename "$file")
    
    # Create fixed version of the file
    sed 's|#include "../include/psvr2_adapter.h"|#include <psvr2/psvr2_adapter.h>|g' "$file" > "$FIXED_DIR/$filename"
    
    print_msg "Fixed include path in $filename"
}

# Fix all relevant source files
fix_include_path "kernel/module/psvr2_adapter_main.c"
fix_include_path "kernel/module/psvr2_display.c"
fix_include_path "kernel/module/psvr2_input.c"
fix_include_path "kernel/module/psvr2_hid.c"

# 3. Copy fixed files back to their original locations
print_msg "Installing fixed source files..."
cp -f "$FIXED_DIR"/* kernel/module/

# 4. Create a proper include directory structure
print_msg "Creating proper include directory structure..."
mkdir -p "build/kernel/include/psvr2"
cp -f "kernel/include/psvr2_adapter.h" "build/kernel/include/psvr2/"

# 5. Fix the Kbuild file
print_msg "Fixing Kbuild file..."
cat > "build/kernel/Kbuild" << EOF
# Fixed Kbuild file
obj-m := psvr2_adapter.o
psvr2_adapter-objs := psvr2_adapter_main.o psvr2_display.o psvr2_input.o psvr2_hid.o
ccflags-y := -I\$(src)/include
EOF

print_success "Kbuild file updated."

# 6. Clean any leftover build artifacts
print_msg "Cleaning build artifacts..."
cd build
make clean
cd ..

print_success "All fixes applied! Try building again with:"
echo ""
echo "cd build"
echo "make"
echo ""
echo "If you still encounter issues, you may need to reconfigure with:"
echo "./configure.sh --clean"
