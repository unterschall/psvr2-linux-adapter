#!/bin/bash
# Script to help find the VID:PID of the PSVR2 adapter
# Usage: ./find-device.sh [search_term]

search_term=${1:-"Sony"}

echo "Searching for USB devices matching: $search_term"
echo "--------------------------------------------"

if command -v lsusb &> /dev/null; then
    lsusb | grep -i "$search_term"
    echo ""
    echo "For more detailed information, run:"
    echo "  lsusb -v -d [VID:PID]"
    echo "For example: lsusb -v -d 054c:0000"
else
    echo "lsusb not found. Please install usbutils package."
    
    # Try alternative methods
    if [ -d "/sys/bus/usb/devices" ]; then
        echo "Trying to use sysfs..."
        for device in /sys/bus/usb/devices/*; do
            if [ -f "$device/idVendor" ] && [ -f "$device/idProduct" ]; then
                vendor=$(cat "$device/idVendor" 2>/dev/null)
                product=$(cat "$device/idProduct" 2>/dev/null)
                manufacturer=$(cat "$device/manufacturer" 2>/dev/null || echo "Unknown")
                
                if echo "$manufacturer" | grep -qi "$search_term"; then
                    echo "Found: $manufacturer - $vendor:$product"
                fi
            fi
        done
    fi
fi
