#!/bin/bash

# PSVR2 Adapter Development Script
# This script handles the compile, unload, and load cycle for the PSVR2 adapter kernel module

set -e  # Exit on any error

MODULE_NAME="psvr2_adapter"
MODULE_FILE="${MODULE_NAME}.ko"

# Text formatting
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
RESET='\033[0m'

# Function to print status messages
print_status() {
    echo -e "${BLUE}[STATUS]${RESET} $1"
}

# Function to print success messages
print_success() {
    echo -e "${GREEN}[SUCCESS]${RESET} $1"
}

# Function to print error messages
print_error() {
    echo -e "${RED}[ERROR]${RESET} $1"
}

# Function to print warning messages
print_warning() {
    echo -e "${YELLOW}[WARNING]${RESET} $1"
}

# Function to safely unload the module
# Function to safely unload the module with additional force options
unload_module() {
    print_status "Attempting to unload ${MODULE_NAME} module..."

    # Check if module is loaded
    if ! lsmod | grep -q "${MODULE_NAME}"; then
        print_warning "Module ${MODULE_NAME} is not currently loaded"
        return 0
    fi

    # Try normal unload first
    if sudo rmmod "${MODULE_NAME}" 2>/dev/null; then
        print_success "Module ${MODULE_NAME} unloaded successfully"
        return 0
    fi

    # Module is in use - try to find processes
    print_warning "Module ${MODULE_NAME} is in use. Attempting to find and stop processes using it..."

    # Find processes using the module via /dev device
    local FOUND_PROCS=0
    if [ -e "/dev/${MODULE_NAME}" ]; then
        PROCS=$(sudo lsof -n -w -t "/dev/${MODULE_NAME}" 2>/dev/null)
        if [ -n "$PROCS" ]; then
            print_warning "Found processes using the module: $PROCS"
            read -p "Kill these processes? (y/n): " KILL_PROCS
            if [[ "$KILL_PROCS" == "y" || "$KILL_PROCS" == "Y" ]]; then
                echo "$PROCS" | xargs sudo kill -9
                sleep 1
                FOUND_PROCS=1
            fi
        fi
    fi

    # Try general device users
    if [ $FOUND_PROCS -eq 0 ]; then
        print_status "Looking for processes using character devices..."
        PROCS=$(sudo fuser -k /dev/* 2>/dev/null | grep -v "^$")
        if [ -n "$PROCS" ]; then
            print_warning "Found some processes using devices, but can't directly link to our module"
        fi
    fi

    # Try again after killing processes
    if sudo rmmod "${MODULE_NAME}" 2>/dev/null; then
        print_success "Module ${MODULE_NAME} unloaded successfully after stopping processes"
        return 0
    fi

    # Try force options
    print_warning "Still having trouble unloading. Here are additional options:"
    echo "1. Try force unload (unsafe, may crash system)"
    echo "2. Use sysrq to sync and remount filesystems read-only"
    echo "3. Show module dependencies and exit"
    echo "4. Attempt to reset USB bus (if this is a USB device)"
    read -p "Choose an option (1-4), or any other key to exit: " FORCE_OPTION

    case $FORCE_OPTION in
        1)
            print_warning "Attempting forced module removal - THIS IS UNSAFE!"
            echo -n 1 | sudo tee /sys/module/${MODULE_NAME}/refcnt >/dev/null 2>&1 || true
            sudo rmmod -f "${MODULE_NAME}" 2>/dev/null
            if ! lsmod | grep -q "${MODULE_NAME}"; then
                print_success "Module forcibly unloaded"
                return 0
            else
                print_error "Force unload failed"
            fi
            ;;
        2)
            print_warning "Syncing filesystem and remounting read-only"
            echo "This may help prepare for a safer module unload"
            sudo bash -c "echo s > /proc/sysrq-trigger" # sync
            sudo bash -c "echo u > /proc/sysrq-trigger" # remount read-only
            echo "Filesystems synced and remounted read-only"
            echo "Try unloading the module again? (y/n): "
            read TRY_AGAIN
            if [[ "$TRY_AGAIN" == "y" || "$TRY_AGAIN" == "Y" ]]; then
                sudo rmmod "${MODULE_NAME}" 2>/dev/null
                if ! lsmod | grep -q "${MODULE_NAME}"; then
                    print_success "Module unloaded after filesystem sync"
                    # Remount read-write
                    sudo mount -o remount,rw /
                    return 0
                else
                    print_error "Unload still failed after filesystem sync"
                    # Remount read-write
                    sudo mount -o remount,rw /
                fi
            fi
            ;;
        3)
            # Display module info and dependencies
            print_status "Module information:"
            sudo modinfo "${MODULE_NAME}"

            print_status "Module dependencies:"
            sudo lsmod | grep "${MODULE_NAME}" -A 5

            print_status "Process information that might be related:"
            ps aux | grep -i "vr\|psvr\|${MODULE_NAME}" | grep -v grep
            ;;
        4)
            # Reset USB device - helpful for USB devices
            print_status "Attempting to reset USB devices..."
            echo "This might disconnect other USB devices temporarily"

            # Find USB buses
            USB_BUSES=$(ls /sys/bus/usb/devices/ | grep -E "usb[0-9]+" || echo "")

            if [ -n "$USB_BUSES" ]; then
                echo "Found USB buses: $USB_BUSES"
                for bus in $USB_BUSES; do
                    echo "Resetting USB bus: $bus"
                    if [ -e "/sys/bus/usb/devices/$bus/authorized_default" ]; then
                        echo 0 | sudo tee /sys/bus/usb/devices/$bus/authorized_default
                        sleep 1
                        echo 1 | sudo tee /sys/bus/usb/devices/$bus/authorized_default
                    else
                        print_warning "Could not reset bus $bus (no authorized_default)"
                    fi
                done

                # Try unloading again
                sleep 2
                if sudo rmmod "${MODULE_NAME}" 2>/dev/null; then
                    print_success "Module unloaded after USB reset"
                    return 0
                else
                    print_error "Unload still failed after USB reset"
                fi
            else
                print_warning "No USB buses found"
            fi
            ;;
        *)
            print_warning "No force option selected"
            ;;
    esac

    # If we get here, all unload attempts failed
    print_error "All unload attempts failed. You may need to reboot your system."
    print_warning "You might try 'sudo modprobe -r ${MODULE_NAME}' manually."

    return 1
}

# Function to compile the module
compile_module() {
    print_status "Compiling ${MODULE_NAME} module..."
    
    # Clean previous build
    if ! make clean; then
        print_error "Failed to clean previous build"
        return 1
    fi
    
    # Build the module
    if ! make; then
        print_error "Failed to compile module"
        return 1
    fi
    
    # Check if the module file exists
    if [ ! -f "${MODULE_FILE}" ]; then
        print_error "Module file ${MODULE_FILE} not found after compilation"
        return 1
    fi
    
    print_success "Module ${MODULE_NAME} compiled successfully"
    return 0
}

# Function to load the module
# Modify the load_module function to capture more information
load_module() {
    print_status "Loading ${MODULE_NAME} module..."

    # Check kernel log before loading
    print_status "Checking kernel log before loading module..."
    sudo dmesg -c > /tmp/dmesg_before.log

    # Try loading with verbose output
    print_status "Attempting to load module with verbose output..."
    sudo insmod "./${MODULE_FILE}" || {
        local ERR=$?
        print_error "Module load failed with error code: $ERR"

        # Check kernel log after failed loading
        print_status "Checking kernel log for error details..."
        sudo dmesg > /tmp/dmesg_after.log
        diff /tmp/dmesg_before.log /tmp/dmesg_after.log > /tmp/dmesg_diff.log

        if [ -s /tmp/dmesg_diff.log ]; then
            print_status "Kernel messages during module load:"
            cat /tmp/dmesg_diff.log
        else
            print_warning "No kernel messages captured during module load attempt."
            print_warning "This might indicate a severe error causing immediate termination."
        fi

        # Check system logs
        print_status "Checking system logs for OOM killer activity..."
        sudo journalctl -k | grep -i "out of memory" | tail -n 10

        return 1
    }

    print_success "Module ${MODULE_NAME} loaded successfully"
    return 0
}

# Function to install the module (now just copies to the modules directory)
install_module() {
    print_status "Installing ${MODULE_NAME} module..."

    # Get the correct module directory for the current kernel
    MODULE_DIR="/lib/modules/$(uname -r)/extra"

    # Create the directory if it doesn't exist
    if [ ! -d "$MODULE_DIR" ]; then
        print_status "Creating module directory: $MODULE_DIR"
        sudo mkdir -p "$MODULE_DIR"
    fi

    # Copy the module to the module directory
    print_status "Copying module to $MODULE_DIR/${MODULE_FILE}"
    if sudo cp "./${MODULE_FILE}" "$MODULE_DIR/${MODULE_FILE}"; then
        # Update module dependencies
        print_status "Updating module dependencies..."
        sudo depmod -a
        print_success "Module ${MODULE_NAME} installed successfully"
        return 0
    else
        print_error "Failed to install module"
        return 1
    fi
}

# Function to check module status
check_module_status() {
    print_status "Checking status of ${MODULE_NAME} module..."

    if lsmod | grep -q "${MODULE_NAME}"; then
        print_success "Module ${MODULE_NAME} is loaded"

        # Show recent dmesg entries related to the module
        echo
        print_status "Recent kernel messages related to ${MODULE_NAME}:"
        sudo dmesg | grep "${MODULE_NAME}" | tail -n 10
    else
        print_warning "Module ${MODULE_NAME} is not loaded"
    fi
}

# Main execution
echo "========================================"
echo "PSVR2 Adapter Module Development Script"
echo "========================================"
echo

# Parse command line arguments
INSTALL=0
JUST_UNLOAD=0
JUST_STATUS=0

for arg in "$@"; do
    case $arg in
        --install)
            INSTALL=1
            shift
            ;;
        --unload)
            JUST_UNLOAD=1
            shift
            ;;
        --status)
            JUST_STATUS=1
            shift
            ;;
        *)
            echo "Unknown option: $arg"
            echo "Usage: $0 [--install] [--unload] [--status]"
            exit 1
            ;;
    esac
done

if [ $JUST_STATUS -eq 1 ]; then
    check_module_status
    exit 0
fi

if [ $JUST_UNLOAD -eq 1 ]; then
    unload_module
    exit $?
fi

# Standard development cycle: unload -> compile -> load
if ! unload_module; then
    print_error "Failed to unload module. Cannot continue."
    exit 1
fi

if ! compile_module; then
    print_error "Failed to compile module. Cannot continue."
    exit 1
fi

if [ $INSTALL -eq 1 ]; then
    if ! install_module; then
        print_error "Failed to install module."
        exit 1
    fi
    print_status "Loading module..."
    # We'll still use insmod for the installed module
    if ! load_module; then
        print_error "Failed to load module"
        exit 1
    fi
    print_success "Installed module loaded successfully"
else
    if ! load_module; then
        print_error "Failed to load module"
        exit 1
    fi
fi

# Final status check
check_module_status

echo
print_success "Development cycle completed successfully"