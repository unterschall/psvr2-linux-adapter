#!/bin/bash
# Cleanup script to remove files obsoleted by the new build system

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print messages
print_msg() {
    echo -e "${BLUE}[PSVR2 Cleanup]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[PSVR2 Cleanup]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[PSVR2 Cleanup]${NC} $1"
}

# Check if run with --force
FORCE=0
if [ "$1" = "--force" ]; then
    FORCE=1
fi

# Make this script executable
chmod +x "$0"

# Make sure the new scripts are executable
chmod +x configure.sh
chmod +x build.sh
chmod +x make-scripts-executable.sh

# Confirm before proceeding
if [ "$FORCE" -ne 1 ]; then
    echo "This script will remove files that are obsolete with the new build system."
    echo "The following files will be removed:"
    echo ""
    echo "From project root:"
    echo "  - setup.sh (replaced by configure.sh)"
    echo "  - make-executable.sh (replaced by make-scripts-executable.sh)"
    echo ""
    echo "From kernel/module:"
    echo "  - Makefile (replaced by CMake and Makefile.in)"
    echo "  - Makefile.orig (backup of original Makefile)"
    echo "  - build.sh, build_test.sh, run_build.sh, simulate_build.sh"
    echo "  - check_dependencies.sh, check_env.sh"
    echo "  - make_scripts_executable.sh"
    echo "  - analyze_code.sh"
    echo "  - build.conf"
    echo ""
    echo "Build artifacts:"
    echo "  - All .cmd, .o, .ko, .mod files"
    echo "  - Module.symvers, modules.order"
    echo "  - build.log"
    echo ""
    read -p "Do you want to proceed? [y/N] " confirm
    if [[ ! $confirm =~ ^[Yy]$ ]]; then
        print_msg "Cleanup cancelled."
        exit 0
    fi
fi

# Remove obsolete files from project root
print_msg "Removing obsolete files from project root..."
rm -f setup.sh make-executable.sh

# Remove obsolete files from kernel/module
print_msg "Removing obsolete files from kernel/module..."
rm -f kernel/module/Makefile kernel/module/Makefile.orig
rm -f kernel/module/build.sh kernel/module/build_test.sh
rm -f kernel/module/run_build.sh kernel/module/simulate_build.sh
rm -f kernel/module/check_dependencies.sh kernel/module/check_env.sh
rm -f kernel/module/make_scripts_executable.sh
rm -f kernel/module/analyze_code.sh kernel/module/build.conf

# Remove build artifacts
print_msg "Removing build artifacts..."
rm -f kernel/module/*.cmd kernel/module/.*.cmd
rm -f kernel/module/*.o
rm -f kernel/module/*.ko
rm -f kernel/module/*.mod kernel/module/*.mod.c kernel/module/*.mod.o
rm -f kernel/module/Module.symvers kernel/module/modules.order
rm -f kernel/module/build.log

# Replace README.md with the new version
print_msg "Updating README.md..."
if [ -f README.md.new ]; then
    mv README.md README.md.old
    mv README.md.new README.md
    print_success "README.md updated (old version saved as README.md.old)"
else
    print_warning "README.md.new not found, skipping README update"
fi

print_success "Cleanup completed successfully!"
echo ""
echo "Your project now uses the new CMake-based build system."
echo "To get started with the new build system:"
echo "1. Run './configure.sh' to configure the build system"
echo "2. Run './build.sh' to build the project"
echo "3. Run './build.sh --install' to install the compiled modules"
