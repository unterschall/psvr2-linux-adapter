#!/bin/bash
# psvr2_incremental_test.sh - Step-by-step testing

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

# Testing stages
STAGES=(
    "1"       # Minimal - Just initialize module
    "3"       # Basic - Add USB initialization
    "7"       # Input - Add input handling
    "15"      # Output - Add output handling
    "31"      # Display - Add display handling
    "63"      # Sensors - Add sensor handling
    "127"     # Audio - Add audio handling
    "255"     # Full - All features enabled
)

STAGE_NAMES=(
    "Minimal (Module Init Only)"
    "Basic (USB Init)"
    "Input Handling"
    "Output Handling"
    "Display Handling"
    "Sensor Handling"
    "Audio Handling"
    "Full Functionality"
)

# Test each stage
for i in "${!STAGES[@]}"; do
    STAGE=${STAGES[$i]}
    STAGE_NAME=${STAGE_NAMES[$i]}
    
    print_status "Testing stage $((i+1))/${#STAGES[@]}: $STAGE_NAME (features_enabled=$STAGE)"
    
    # Clear kernel log
    sudo dmesg -c > /dev/null
    
    # Try to load module with current stage features
    print_status "Loading module with features_enabled=$STAGE..."
    if sudo insmod "./${MODULE_FILE}" features_enabled=$STAGE debug_level=4 2> "${LOG_DIR}/insmod_error_stage${i}_${TIMESTAMP}.txt"; then
        print_success "Stage $((i+1)) loaded successfully!"
        
        # Save logs
        sudo dmesg > "${LOG_DIR}/dmesg_stage${i}_${TIMESTAMP}.txt"
        
        # Unload module
        print_status "Unloading module..."
        if sudo rmmod "$MODULE_NAME" 2> "${LOG_DIR}/rmmod_error_stage${i}_${TIMESTAMP}.txt"; then
            print_success "Module unloaded successfully"
        else
            print_error "Failed to unload module for stage $((i+1))"
            cat "${LOG_DIR}/rmmod_error_stage${i}_${TIMESTAMP}.txt"
            # Exit on unload failure
            exit 1
        fi
    else
        print_error "Failed to load module for stage $((i+1))"
        sudo dmesg > "${LOG_DIR}/dmesg_stage${i}_fail_${TIMESTAMP}.txt"
        
        # Check for panic or oops
        if grep -iE "panic|oops|null pointer|call trace" "${LOG_DIR}/dmesg_stage${i}_fail_${TIMESTAMP}.txt"; then
            print_error "Kernel error detected! See ${LOG_DIR}/dmesg_stage${i}_fail_${TIMESTAMP}.txt"
            grep -iE "panic|oops|null pointer|call trace" -A 20 "${LOG_DIR}/dmesg_stage${i}_fail_${TIMESTAMP}.txt" > "${LOG_DIR}/error_stage${i}_${TIMESTAMP}.txt"
        fi
        
        # Exit on first failure
        print_warning "Stopping tests at failed stage $((i+1)): $STAGE_NAME"
        exit 1
    fi
    
    # Small delay between tests
    sleep 2
done

print_success "All stages completed successfully!"