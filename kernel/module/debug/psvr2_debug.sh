#!/bin/bash
# psvr2_debug.sh - Enhanced debugging script for PSVR2 adapter

# Text formatting
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
RESET='\033[0m'

# Print functions
print_status() { echo -e "${BLUE}[STATUS]${RESET} $1"; }
print_success() { echo -e "${GREEN}[SUCCESS]${RESET} $1"; }
print_error() { echo -e "${RED}[ERROR]${RESET} $1"; }
print_warning() { echo -e "${YELLOW}[WARNING]${RESET} $1"; }

MODULE_NAME="psvr2_adapter"
MODULE_FILE="${MODULE_NAME}.ko"
LOG_DIR="./debug_logs"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

# Create log directory if it doesn't exist
mkdir -p "$LOG_DIR"

# Clear kernel log buffer
print_status "Clearing kernel log buffer..."
sudo dmesg -c > /dev/null

# Save current loaded modules for comparison
print_status "Saving current module state..."
lsmod > "${LOG_DIR}/lsmod_before_${TIMESTAMP}.txt"

# Try loading with various debug options
print_status "Attempting to load module with debug flags..."

# First try with minimal functionality if parameter exists
if grep -q "features_enabled" "$(find . -name "*.c" | xargs grep -l "module_param" | head -1 2>/dev/null)"; then
    print_status "Detected features_enabled parameter, trying minimal functionality..."
    sudo insmod "./${MODULE_FILE}" features_enabled=1 debug_level=4 dyndbg=+pmfl 2> "${LOG_DIR}/insmod_error_${TIMESTAMP}.txt"
else
    # Otherwise just try with dynamic debug
    print_status "Loading with dynamic debug enabled..."
    sudo insmod "./${MODULE_FILE}" dyndbg=+pmfl 2> "${LOG_DIR}/insmod_error_${TIMESTAMP}.txt"
fi

# Capture result
RESULT=$?

# Save kernel logs regardless of outcome
sudo dmesg > "${LOG_DIR}/dmesg_${TIMESTAMP}.txt"

# Check result
if [ $RESULT -eq 0 ]; then
    print_success "Module loaded successfully with debug flags."
    lsmod | grep "$MODULE_NAME" > "${LOG_DIR}/module_info_${TIMESTAMP}.txt"
    print_status "Kernel module information saved to ${LOG_DIR}/module_info_${TIMESTAMP}.txt"
    
    # Capture some time for logs to accumulate
    print_status "Waiting 5 seconds to collect logs..."
    sleep 5
    
    # Save additional logs
    sudo dmesg > "${LOG_DIR}/dmesg_after_5s_${TIMESTAMP}.txt"
    
    # Unload module
    print_status "Unloading module..."
    if sudo rmmod "$MODULE_NAME" 2> "${LOG_DIR}/rmmod_error_${TIMESTAMP}.txt"; then
        print_success "Module unloaded successfully"
    else
        print_error "Failed to unload module"
        cat "${LOG_DIR}/rmmod_error_${TIMESTAMP}.txt"
    fi
else
    print_error "Module load failed with debug flags."
    cat "${LOG_DIR}/insmod_error_${TIMESTAMP}.txt"
    
    # Check for panic in logs
    if grep -i "panic" "${LOG_DIR}/dmesg_${TIMESTAMP}.txt"; then
        print_error "KERNEL PANIC DETECTED!"
        print_status "Extracting panic information..."
        grep -i -A 20 "panic" "${LOG_DIR}/dmesg_${TIMESTAMP}.txt" > "${LOG_DIR}/panic_info_${TIMESTAMP}.txt"
        print_status "Panic details saved to ${LOG_DIR}/panic_info_${TIMESTAMP}.txt"
    fi
    
    # Check for oops
    if grep -i "oops" "${LOG_DIR}/dmesg_${TIMESTAMP}.txt"; then
        print_error "KERNEL OOPS DETECTED!"
        print_status "Extracting oops information..."
        grep -i -A 20 "oops" "${LOG_DIR}/dmesg_${TIMESTAMP}.txt" > "${LOG_DIR}/oops_info_${TIMESTAMP}.txt"
        print_status "Oops details saved to ${LOG_DIR}/oops_info_${TIMESTAMP}.txt"
    fi
    
    # Look for NULL pointer dereference
    if grep -i "null pointer dereference" "${LOG_DIR}/dmesg_${TIMESTAMP}.txt"; then
        print_error "NULL POINTER DEREFERENCE DETECTED!"
    fi
    
    # Check for call traces
    if grep -i "call trace" "${LOG_DIR}/dmesg_${TIMESTAMP}.txt"; then
        print_status "Extracting call trace..."
        grep -i -A 50 "call trace" "${LOG_DIR}/dmesg_${TIMESTAMP}.txt" > "${LOG_DIR}/call_trace_${TIMESTAMP}.txt"
        print_status "Call trace saved to ${LOG_DIR}/call_trace_${TIMESTAMP}.txt"
    fi
fi

print_status "All logs saved to $LOG_DIR directory"
print_status "To analyze: less ${LOG_DIR}/dmesg_${TIMESTAMP}.txt"