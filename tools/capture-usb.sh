#!/bin/bash
# USB capture script for PSVR2 adapter
# Usage: ./capture-usb.sh [output_file]

output=${1:-"psvr2-usb-capture.pcapng"}

# Function to check if a command exists
command_exists() {
    command -v "$1" &> /dev/null
}

# Check if we're running as root
if [ "$EUID" -ne 0 ]; then
    echo "This script requires root privileges to capture USB traffic."
    echo "Running with sudo..."
    exec sudo "$0" "$@"
    exit $?
fi

# Load usbmon module if needed
if ! lsmod | grep -q "usbmon"; then
    echo "Loading usbmon kernel module..."
    modprobe usbmon
fi

# List available USB bus interfaces
echo "Available USB monitoring interfaces:"
ls -1 /dev/usbmon* 2>/dev/null || echo "No usbmon interfaces found. Make sure usbmon is enabled in your kernel."

# Pick tshark or tcpdump depending on what's available
if command_exists tshark; then
    echo "Using tshark for capture..."
    sudo tshark -i usbmon4 -w "$output"
    echo "Capture saved to $output"
elif command_exists tcpdump; then
    echo "Using tcpdump for capture..."
    sudo tcpdump -i usbmon4 -w "$output"
    echo "Capture saved to $output"
else
    echo "Error: Neither tshark nor tcpdump is installed. Please install one of them to capture USB traffic."
    exit 1
fi
