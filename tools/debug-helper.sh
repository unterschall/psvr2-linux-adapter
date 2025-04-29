#!/bin/bash
# PSVR2 Debugging Helper Script
# A comprehensive tool for debugging the PSVR2 Linux adapter

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# Function to print messages
print_header() {
    echo -e "\n${BOLD}${BLUE}==== $1 ====${NC}\n"
}

print_status() {
    echo -e "${BLUE}[STATUS]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Function to create a timestamp for logs
get_timestamp() {
    date +%Y%m%d_%H%M%S
}

# Function to check if we have necessary privileges
check_privileges() {
    if [ "$(id -u)" -ne 0 ]; then
        print_error "This script requires root privileges for certain operations."
        print_status "Re-running with sudo..."
        exec sudo "$0" "$@"
        exit $?
    fi
}

# Function to set up log directory
setup_logs() {
    TIMESTAMP=$(get_timestamp)
    LOG_DIR="./debug_logs/${TIMESTAMP}"
    print_status "Creating log directory at ${LOG_DIR}"
    mkdir -p "${LOG_DIR}"
}

# Function to save system information
save_system_info() {
    print_header "Collecting System Information"
    
    print_status "Saving kernel information..."
    uname -a > "${LOG_DIR}/kernel_info.txt"
    
    print_status "Saving USB devices..."
    lsusb -v > "${LOG_DIR}/usb_devices.txt" 2>&1
    
    print_status "Checking for PSVR2 adapter..."
    lsusb | grep -i "054c.*0cde" > "${LOG_DIR}/psvr2_device.txt" 2>&1
    
    print_status "Saving loaded modules..."
    lsmod > "${LOG_DIR}/loaded_modules.txt"
    
    print_status "Saving PCI devices..."
    lspci -v > "${LOG_DIR}/pci_devices.txt" 2>&1
}

# Function to test module loading
test_module_loading() {
    print_header "Testing Module Loading"
    
    # Check if the module is already loaded
    if lsmod | grep -q "psvr2_adapter"; then
        print_status "PSVR2 adapter module is already loaded. Unloading first..."
        rmmod psvr2_adapter 2> "${LOG_DIR}/rmmod_error.txt"
        if [ $? -ne 0 ]; then
            print_error "Failed to unload module. See ${LOG_DIR}/rmmod_error.txt for details."
            return 1
        fi
    fi
    
    print_status "Clearing kernel log buffer..."
    dmesg -c > /dev/null
    
    print_status "Loading PSVR2 adapter module with debug options..."
    insmod ./psvr2_adapter.ko debug_level=4 features_enabled=0x00FF 2> "${LOG_DIR}/insmod_error.txt"
    RESULT=$?
    
    # Save kernel messages regardless of outcome
    dmesg > "${LOG_DIR}/dmesg_after_load.txt"
    
    if [ $RESULT -eq 0 ]; then
        print_success "Module loaded successfully."
        lsmod | grep psvr2 > "${LOG_DIR}/module_info.txt"
        return 0
    else
        print_error "Failed to load module. See ${LOG_DIR}/insmod_error.txt for details."
        return 1
    fi
}

# Function to capture USB traffic
capture_usb_traffic() {
    print_header "Capturing USB Traffic"
    
    print_status "Loading usbmon module if needed..."
    modprobe usbmon
    
    print_status "Detecting USB buses..."
    ls -1 /dev/usbmon* > "${LOG_DIR}/usbmon_interfaces.txt"
    
    print_status "Checking for PSVR2 adapter..."
    local DEVICE_BUS=$(lsusb | grep -i "054c.*0cde" | awk '{print $2}')
    
    if [ -z "$DEVICE_BUS" ]; then
        print_warning "PSVR2 adapter not detected. Will capture on all buses."
        print_status "Starting USB capture for 10 seconds..."
        tshark -a duration:10 -i usbmon1 -w "${LOG_DIR}/usb_capture.pcapng" 2> "${LOG_DIR}/tshark_error.txt"
    else
        print_status "PSVR2 adapter found on bus ${DEVICE_BUS}."
        print_status "Starting USB capture for 10 seconds..."
        tshark -a duration:10 -i "usbmon${DEVICE_BUS}" -w "${LOG_DIR}/usb_capture.pcapng" 2> "${LOG_DIR}/tshark_error.txt"
    fi
    
    print_status "USB capture saved to ${LOG_DIR}/usb_capture.pcapng"
}

# Function to test device files
test_device_files() {
    print_header "Testing Device Files"
    
    print_status "Checking for device file..."
    if [ -c "/dev/psvr2" ]; then
        print_success "Device file /dev/psvr2 exists."
        
        print_status "Checking permissions..."
        ls -la /dev/psvr2 > "${LOG_DIR}/device_permissions.txt"
        
        print_status "Testing device read..."
        dd if=/dev/psvr2 of="${LOG_DIR}/device_read.bin" bs=64 count=1 2> "${LOG_DIR}/device_read_error.txt"
        if [ $? -eq 0 ]; then
            print_success "Successfully read from device."
            hexdump -C "${LOG_DIR}/device_read.bin" > "${LOG_DIR}/device_read_hex.txt"
        else
            print_error "Failed to read from device. See ${LOG_DIR}/device_read_error.txt"
        fi
    else
        print_error "Device file /dev/psvr2 does not exist."
        
        print_status "Checking for any psvr2 related devices..."
        find /dev -name "*psvr*" > "${LOG_DIR}/psvr2_devices.txt" 2>&1
    fi
}

# Function to run diagnostics
run_diagnostics() {
    print_header "Running Diagnostics"
    
    print_status "Checking for kernel errors..."
    dmesg | grep -i "error\|warn\|fail" > "${LOG_DIR}/kernel_errors.txt"
    
    print_status "Checking USB errors..."
    dmesg | grep -i "usb\|hid" | grep -i "error\|warn\|fail" > "${LOG_DIR}/usb_errors.txt"
    
    print_status "Checking for segfaults or kernel oops..."
    dmesg | grep -i "segfault\|oops\|panic" > "${LOG_DIR}/crash_indicators.txt"
    
    print_status "Checking loaded module parameters..."
    if lsmod | grep -q "psvr2_adapter"; then
        cat /sys/module/psvr2_adapter/parameters/* > "${LOG_DIR}/module_parameters.txt" 2>&1
    else
        echo "Module not loaded" > "${LOG_DIR}/module_parameters.txt"
    fi
}

# Function to unload module
unload_module() {
    print_header "Unloading Module"
    
    if lsmod | grep -q "psvr2_adapter"; then
        print_status "Unloading PSVR2 adapter module..."
        rmmod psvr2_adapter 2> "${LOG_DIR}/rmmod_unload_error.txt"
        if [ $? -eq 0 ]; then
            print_success "Module unloaded successfully."
        else
            print_error "Failed to unload module. See ${LOG_DIR}/rmmod_unload_error.txt"
        fi
    else
        print_status "PSVR2 adapter module is not loaded."
    fi
    
    dmesg > "${LOG_DIR}/dmesg_after_unload.txt"
}

# Function to generate report
generate_report() {
    print_header "Generating Report"
    
    REPORT_FILE="${LOG_DIR}/debug_report.txt"
    
    print_status "Creating debug report at ${REPORT_FILE}"
    
    {
        echo "PSVR2 Linux Adapter Debug Report"
        echo "================================="
        echo "Generated at: $(date)"
        echo ""
        
        echo "System Information:"
        echo "------------------"
        echo "Kernel version: $(uname -r)"
        echo "Architecture: $(uname -m)"
        echo ""
        
        echo "USB Devices:"
        echo "------------"
        lsusb | grep -i "sony\|054c"
        echo ""
        
        echo "PSVR2 Module Status:"
        echo "-------------------"
        if lsmod | grep -q "psvr2_adapter"; then
            echo "Module is loaded."
            echo "Module details:"
            lsmod | grep psvr2
        else
            echo "Module is not loaded."
        fi
        echo ""
        
        echo "Device File Status:"
        echo "------------------"
        if [ -c "/dev/psvr2" ]; then
            echo "Device file exists:"
            ls -la /dev/psvr2
        else
            echo "Device file does not exist."
        fi
        echo ""
        
        echo "Kernel Log Summary:"
        echo "------------------"
        echo "Last 20 kernel messages:"
        dmesg | tail -20
        echo ""
        
        echo "Errors and Warnings:"
        echo "-------------------"
        dmesg | grep -i "error\|warn\|fail\|psvr2" | tail -30
        echo ""
        
        echo "Debug Log Location:"
        echo "------------------"
        echo "All debug logs are saved in: ${LOG_DIR}"
        echo ""
        
    } > "${REPORT_FILE}"
    
    print_success "Debug report generated: ${REPORT_FILE}"
}

# Main function to run all tests
run_all_tests() {
    check_privileges
    setup_logs
    save_system_info
    test_module_loading
    capture_usb_traffic
    test_device_files
    run_diagnostics
    unload_module
    generate_report
    
    print_header "Debug Session Complete"
    print_success "All debug information saved to ${LOG_DIR}"
    print_success "Debug report available at ${LOG_DIR}/debug_report.txt"
}

# Function to display help
show_help() {
    echo "PSVR2 Linux Adapter Debugging Helper"
    echo "Usage: $0 [OPTION]"
    echo ""
    echo "Options:"
    echo "  -a, --all        Run all debug tests (default)"
    echo "  -s, --system     Collect system information only"
    echo "  -m, --module     Test module loading only"
    echo "  -u, --usb        Capture USB traffic only"
    echo "  -d, --device     Test device files only"
    echo "  -g, --diagnose   Run diagnostics only"
    echo "  -r, --report     Generate report only"
    echo "  -h, --help       Display this help"
    echo ""
    echo "Examples:"
    echo "  $0                 # Run all debug tests"
    echo "  $0 --usb           # Only capture USB traffic"
    echo "  $0 --module --usb  # Test module loading and capture USB traffic"
}

# Parse command line arguments
if [ $# -eq 0 ]; then
    # No arguments, run all tests
    run_all_tests
else
    # Parse arguments
    while [ $# -gt 0 ]; do
        case "$1" in
            -a|--all)
                run_all_tests
                exit 0
                ;;
            -s|--system)
                check_privileges
                setup_logs
                save_system_info
                shift
                ;;
            -m|--module)
                check_privileges
                setup_logs
                save_system_info
                test_module_loading
                unload_module
                shift
                ;;
            -u|--usb)
                check_privileges
                setup_logs
                save_system_info
                capture_usb_traffic
                shift
                ;;
            -d|--device)
                check_privileges
                setup_logs
                save_system_info
                test_device_files
                shift
                ;;
            -g|--diagnose)
                check_privileges
                setup_logs
                save_system_info
                run_diagnostics
                shift
                ;;
            -r|--report)
                check_privileges
                setup_logs
                save_system_info
                generate_report
                shift
                ;;
            -h|--help)
                show_help
                exit 0
                ;;
            *)
                print_error "Unknown option: $1"
                show_help
                exit 1
                ;;
        esac
    done
fi

print_header "Debug Session Complete"
print_success "All requested debug information saved to ${LOG_DIR}"
