#!/bin/bash
#
# PSVR2 Linux Adapter - Build Testing Script
#
# This script tests if the kernel module can be built on the current system
# and provides detailed feedback on any issues encountered.

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print messages
print_msg() {
    echo -e "${BLUE}[Build Test]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[Build Test]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[Build Test]${NC} $1"
}

print_error() {
    echo -e "${RED}[Build Test]${NC} $1"
}

# Detect Linux distribution
detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        DISTRO=$ID
        DISTRO_VERSION=$VERSION_ID
        DISTRO_FAMILY=$ID_LIKE
    elif [ -f /etc/lsb-release ]; then
        . /etc/lsb-release
        DISTRO=$DISTRIB_ID
        DISTRO_VERSION=$DISTRIB_RELEASE
        DISTRO_FAMILY="debian"
    elif [ -f /etc/arch-release ]; then
        DISTRO="arch"
        DISTRO_FAMILY="arch"
    elif [ -f /etc/fedora-release ]; then
        DISTRO="fedora"
        DISTRO_FAMILY="fedora"
    elif [ -f /etc/gentoo-release ]; then
        DISTRO="gentoo"
        DISTRO_FAMILY="gentoo"
    elif [ -f /etc/SuSE-release ]; then
        DISTRO="opensuse"
        DISTRO_FAMILY="suse"
    else
        DISTRO="unknown"
        DISTRO_FAMILY="unknown"
    fi

    print_msg "Detected distribution: $DISTRO"
}

# Check build dependencies
check_dependencies() {
    print_msg "Checking build dependencies..."
    
    local missing_deps=()
    
    # Check for essential build tools
    if ! command -v gcc &>/dev/null; then
        missing_deps+=("gcc")
    fi
    
    if ! command -v make &>/dev/null; then
        missing_deps+=("make")
    fi
    
    # Check for kernel headers
    local kernel_ver=$(uname -r)
    if [ ! -d "/lib/modules/$kernel_ver/build" ]; then
        # Try some distro-specific paths
        if [ ! -d "/usr/src/linux" ] && [ ! -d "/usr/src/kernels/$kernel_ver" ]; then
            missing_deps+=("linux-headers")
        fi
    fi
    
    # Check for libraries
    case $DISTRO_FAMILY in
        "debian")
            if ! dpkg -l | grep -q "libdrm-dev"; then
                missing_deps+=("libdrm-dev")
            fi
            if ! dpkg -l | grep -q "libusb-1.0-0-dev"; then
                missing_deps+=("libusb-1.0-0-dev")
            fi
            if ! dpkg -l | grep -q "libhidapi-dev"; then
                missing_deps+=("libhidapi-dev")
            fi
            ;;
        "arch")
            if ! pacman -Q libdrm &>/dev/null; then
                missing_deps+=("libdrm")
            fi
            if ! pacman -Q libusb &>/dev/null; then
                missing_deps+=("libusb")
            fi
            if ! pacman -Q hidapi &>/dev/null; then
                missing_deps+=("hidapi")
            fi
            ;;
        *)
            # For other distributions, we'll just rely on the basic build tools check
            ;;
    esac
    
    if [ ${#missing_deps[@]} -eq 0 ]; then
        print_success "All required dependencies are installed."
        return 0
    else
        print_warning "Missing dependencies: ${missing_deps[*]}"
        
        # Suggest installation command
        case $DISTRO_FAMILY in
            "debian")
                print_msg "Run: sudo apt-get install ${missing_deps[*]}"
                ;;
            "fedora"|"rhel")
                print_msg "Run: sudo dnf install ${missing_deps[*]}"
                ;;
            "arch")
                print_msg "Run: sudo pacman -S ${missing_deps[*]}"
                ;;
            "suse")
                print_msg "Run: sudo zypper install ${missing_deps[*]}"
                ;;
            "gentoo")
                print_msg "Run: sudo emerge -av ${missing_deps[*]}"
                ;;
            *)
                print_msg "Please install the missing dependencies with your package manager."
                ;;
        esac
        
        return 1
    fi
}

# Test module build
test_build() {
    print_msg "Testing kernel module build..."
    
    local module_dir="../kernel/module"
    
    if [ ! -d "$module_dir" ]; then
        print_error "Module directory not found at $module_dir"
        print_msg "Please run this script from the tools directory."
        return 1
    fi
    
    # Save current directory
    local curr_dir=$(pwd)
    
    # Change to module directory
    cd "$module_dir"
    
    # Perform a clean build
    if make clean &>/dev/null; then
        print_msg "Clean build directory..."
    else
        print_warning "Clean failed, but continuing..."
    fi
    
    # Capture build output
    local build_log=$(mktemp)
    local build_status=0
    if ! make 2>&1 | tee "$build_log"; then
        build_status=1
    fi
    
    # Analyze build results
    if [ $build_status -eq 0 ]; then
        if [ -f "psvr2_adapter.ko" ]; then
            print_success "Build succeeded! Module created: psvr2_adapter.ko"
            print_msg "Module details:"
            modinfo psvr2_adapter.ko | sed 's/^/  /'
        else
            print_warning "Make command succeeded but module file not found."
            build_status=1
        fi
    else
        print_error "Build failed! Check the errors below:"
        grep -A 5 -B 2 "error:" "$build_log" | sed 's/^/  /'
    fi
    
    # Cleanup
    rm -f "$build_log"
    
    # Return to original directory
    cd "$curr_dir"
    
    return $build_status
}

# Main function
main() {
    echo -e "${BLUE}==================================${NC}"
    echo -e "${BLUE} PSVR2 Linux Adapter Build Test ${NC}"
    echo -e "${BLUE}==================================${NC}"
    
    # Detect distribution
    detect_distro
    
    # Check dependencies
    check_dependencies
    local deps_status=$?
    
    # Only try to build if dependencies are satisfied or --force is used
    if [ $deps_status -eq 0 ] || [ "$1" = "--force" ]; then
        # Test build
        test_build
        local build_status=$?
        
        if [ $build_status -eq 0 ]; then
            print_success "Build test completed successfully!"
            echo ""
            print_msg "You can now install the module with:"
            echo "  cd ../kernel/module"
            echo "  sudo insmod psvr2_adapter.ko"
            echo ""
            print_msg "To load the module automatically at boot:"
            echo "  sudo make modules_install"
            echo "  sudo depmod -a"
            echo "  echo 'psvr2_adapter' | sudo tee -a /etc/modules"
        else
            print_error "Build test failed. Please fix the errors before continuing."
            return 1
        fi
    else
        print_warning "Skipping build test due to missing dependencies."
        print_msg "Install the required dependencies and try again, or use --force to attempt building anyway."
        return 1
    fi
    
    return 0
}

# Run main function with all args
main "$@"
