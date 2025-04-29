# PSVR2 Linux Adapter Debugging Guide

## Table of Contents

1. [Introduction](#introduction)
2. [Setting Up the Development Environment](#setting-up-the-development-environment)
3. [Building with Debug Options](#building-with-debug-options)
4. [Kernel Module Debugging](#kernel-module-debugging)
5. [USB Protocol Debugging](#usb-protocol-debugging)
6. [Runtime Debug Techniques](#runtime-debug-techniques)
7. [Common Issues and Solutions](#common-issues-and-solutions)
8. [Advanced Debugging](#advanced-debugging)
9. [Continuous Integration Testing](#continuous-integration-testing)
10. [Appendix: Useful Commands](#appendix-useful-commands)

## Introduction

This document provides detailed guidance on debugging the PSVR2 Linux adapter project. The project consists of kernel modules and userspace tools designed to enable the use of Sony PSVR2 headsets on Linux systems via the official Sony PSVR2 PC adapter.

As a work in progress, effective debugging is essential for:
- Understanding the adapter's USB protocol
- Developing and testing kernel modules
- Troubleshooting hardware communication issues
- Verifying the correctness of the implementation

## Setting Up the Development Environment

### Prerequisites

Before beginning debugging, ensure you have:

1. **Hardware Requirements**:
   - Sony PSVR2 headset
   - Official Sony PSVR2 PC adapter
   - DisplayPort 1.4 compatible graphics card
   - USB 3.0 port
   - Bluetooth 4.0 or newer for controller support

2. **Software Requirements**:
   - Linux kernel 5.10 or newer
   - Development packages (as listed in README.md)
   - Debugging tools (detailed below)

### Essential Debugging Tools

Install these tools to assist with debugging:

```bash
# Debian/Ubuntu
sudo apt-get install build-essential linux-headers-$(uname -r) \
    usbutils wireshark tshark libusb-dev \
    evtest input-utils trace-cmd systemtap kernelshark \
    linux-tools-common linux-tools-$(uname -r)

# Fedora/RHEL
sudo dnf install kernel-devel usbutils wireshark-cli \
    libusb-devel evtest trace-cmd systemtap kernelshark \
    perf

# Arch Linux
sudo pacman -S linux-headers base-devel usbutils wireshark-cli \
    libusb evtest trace-cmd systemtap kernelshark \
    perf
```

### Setting Up Kernel Debug Access

To avoid requiring root access for every debug operation:

```bash
# Create a udev rule for the PSVR2 adapter
sudo cp /home/user/Projects/psvr2-linux-adapter/99-psvr2.rules /etc/udev/rules.d/

# Create a debug group and add yourself to it
sudo groupadd -f debug
sudo usermod -aG debug $USER

# Apply rules
sudo udevadm control --reload-rules
```

Log out and log back in for the group changes to take effect.

## Building with Debug Options

The project build system supports several debug options to facilitate development.

### Debug Build Configuration

To configure a debug build:

```bash
# Basic debug build
./configure.sh --debug

# Advanced debug options
./configure.sh --debug --enable-tests --enable-userspace
```

### Available Debug Flags

The following options are particularly useful for debugging:

| Option | Effect |
|--------|--------|
| `--debug` | Enables debug symbols and debug logging |
| `--enable-tests` | Builds test programs |
| `--enable-userspace` | Builds userspace tools for testing |

### Module Parameters for Debugging

When loading the kernel module, debug options can be enabled:

```bash
# Load with debug level 3 (INFO)
sudo insmod ./psvr2_adapter.ko debug_level=3

# Enable specific feature debugging
sudo insmod ./psvr2_adapter.ko debug_level=4 features_enabled=0x007F

# Enable dynamic debug
sudo insmod ./psvr2_adapter.ko dyndbg=+pmfl
```

### Using the Debug Scripts

The project includes several debug scripts:

```bash
# Run the primary debug script
cd /home/user/Projects/psvr2-linux-adapter/kernel/module/debug
./psvr2_debug.sh

# Incremental testing
./psvr2_incremental_test.sh
```

## Kernel Module Debugging

The kernel module is the core component of the project and requires specialized debugging techniques.

### Reading Kernel Logs

The simplest way to debug the kernel module is through kernel logs:

```bash
# Clear kernel ring buffer
sudo dmesg -c

# Load the module
sudo insmod ./psvr2_adapter.ko debug_level=4

# View the logs
dmesg

# Follow logs in real-time
dmesg -wH
```

### Using printk Debug Levels

The module uses different printk levels for debugging:

```c
/* In your code */
pr_emerg("System is unusable\n");     // 0
pr_alert("Action must be taken immediately\n"); // 1
pr_crit("Critical conditions\n");      // 2
pr_err("Error conditions\n");         // 3
pr_warning("Warning conditions\n");    // 4
pr_notice("Normal but significant\n"); // 5
pr_info("Informational\n");           // 6
pr_debug("Debug-level messages\n");    // 7
```

Adjust the console log level to see more messages:

```bash
# Show all messages up to debug level
echo 8 > /proc/sys/kernel/printk
```

### Using the Debug Macros

The project provides custom debug macros in `psvr2_debug.h`:

```c
// Entry/exit tracking
DBG_FUNC_ENTRY();
DBG_FUNC_EXIT();

// Feature-specific debug
if (FEATURE_ENABLED(FEAT_USB_INIT)) {
    DBG_DBG("USB initialization details: %d\n", value);
}

// Assert-like checks
DBG_ASSERT(dev != NULL, "Device structure is NULL");
```

### Debugging Module Loading Issues

If the module fails to load:

1. Check for symbol resolution issues:
   ```bash
   sudo modprobe --resolve-alias psvr2_adapter
   ```

2. Check module dependencies:
   ```bash
   sudo depmod -a
   modinfo psvr2_adapter
   ```

3. Use the project's debug script which captures detailed information:
   ```bash
   cd /home/user/Projects/psvr2-linux-adapter/kernel/module/debug
   ./psvr2_debug.sh
   ```

### Debugging Memory Issues

For memory-related debugging:

1. Enable kernel memory debugging:
   ```bash
   sudo sysctl -w kernel.tainted=1
   sudo sysctl -w vm.panic_on_oom=0
   ```

2. Use kmemleak to detect memory leaks:
   ```bash
   # Enable kmemleak
   sudo mount -t debugfs none /sys/kernel/debug
   sudo bash -c "echo scan > /sys/kernel/debug/kmemleak"
   
   # After running your module
   sudo bash -c "echo scan > /sys/kernel/debug/kmemleak"
   sudo cat /sys/kernel/debug/kmemleak
   ```

## USB Protocol Debugging

Understanding the USB protocol is crucial for this project.

### Capturing USB Traffic

Use the provided script to capture USB traffic:

```bash
cd /home/user/Projects/psvr2-linux-adapter/tools
sudo ./capture-usb.sh my-capture.pcapng
```

Alternatively, use traditional tools:

```bash
# Load the usbmon module
sudo modprobe usbmon

# Find the correct bus
lsusb
# Look for Sony Corp. with VID:PID 054C:0CDE

# Capture on the correct bus (replace X with bus number)
sudo tshark -i usbmonX -w capture.pcapng
```

### Analyzing USB Captures

1. Use Wireshark to analyze captures:
   ```bash
   wireshark my-capture.pcapng
   ```

2. Filter for PSVR2 adapter:
   ```
   usb.idVendor == 0x054C && usb.idProduct == 0x0CDE
   ```

3. Look for control transfers (SET_REPORT/GET_REPORT) to understand the initialization sequence.

4. Observe data transfers to understand tracking data format.

### Testing Direct USB Communication

You can test direct USB communication using the `libusb` tools:

```bash
# List all USB devices
lsusb -v

# Basic control transfer test (replace X with the bus and device numbers)
sudo usb-ctrl -d X:X -s 0x054C:0x0CDE -a 0 -A 1 -o 0 -O 0 -R 1 -L 8
```

## Runtime Debug Techniques

These techniques are useful for debugging the driver during operation.

### Dynamic Debug

The Linux kernel's dynamic debug feature allows enabling specific debug messages at runtime:

```bash
# Enable all debug messages for the module
sudo bash -c "echo 'module psvr2_adapter +p' > /sys/kernel/debug/dynamic_debug/control"

# Enable specific file debugging
sudo bash -c "echo 'file psvr2_adapter_main.c +p' > /sys/kernel/debug/dynamic_debug/control"

# Enable specific function debugging
sudo bash -c "echo 'func psvr2_adapter_probe +p' > /sys/kernel/debug/dynamic_debug/control"
```

### Tracing with ftrace

Use ftrace to trace function calls within the kernel:

```bash
# Mount debugfs if not already mounted
sudo mount -t debugfs none /sys/kernel/debug

# Set up function tracing for the module
cd /sys/kernel/debug/tracing
sudo bash -c "echo function > current_tracer"
sudo bash -c "echo 'psvr2_*' > set_ftrace_filter"
sudo bash -c "echo 1 > tracing_on"

# View the trace
sudo cat trace

# Reset when done
sudo bash -c "echo 0 > tracing_on"
```

### Using SystemTap

For more advanced probing, SystemTap provides powerful capabilities:

```bash
# Create a simple probe script (save as psvr2_probe.stp)
probe module("psvr2_adapter").function("psvr2_adapter_probe") {
  printf("Probe called with interface %p\n", $interface);
}

# Run the probe
sudo stap psvr2_probe.stp
```

### Debugging USB URBs

To track USB Request Blocks (URBs) for detailed USB communication analysis:

```bash
# Enable URB tracing
sudo bash -c "echo 1 > /sys/kernel/debug/usb/usbmon/captureURB"
sudo bash -c "echo 2 > /sys/kernel/debug/usb/usbmon/capture"

# Monitor the specific device (replace X with the bus number)
sudo cat /sys/kernel/debug/usb/usbmonX
```

## Common Issues and Solutions

### Module Loading Failures

Problem: Module fails to load with "Unknown symbol" errors.

Solution:
1. Verify kernel headers match running kernel:
   ```bash
   uname -r
   dpkg -l | grep linux-headers
   ```

2. Check for missing dependencies:
   ```bash
   ldd -r ./psvr2_adapter.ko
   ```

3. Try the incremental test script:
   ```bash
   ./kernel/module/debug/psvr2_incremental_test.sh
   ```

### USB Communication Issues

Problem: Device is detected but communication fails.

Solution:
1. Verify USB permissions:
   ```bash
   ls -l /dev/bus/usb/*/
   ```

2. Check if the adapter is in the correct mode:
   ```bash
   lsusb -d 054C:0CDE -v
   ```

3. Try different power management settings:
   ```bash
   echo 'on' | sudo tee /sys/bus/usb/devices/X-X/power/control
   ```

### Kernel Panics or Oops

Problem: The module causes kernel panics or oops messages.

Solution:
1. Collect the full kernel log:
   ```bash
   sudo dmesg > kernel_panic.log
   ```

2. Use the debug script which automatically captures panic information:
   ```bash
   ./kernel/module/debug/psvr2_debug.sh
   ```

3. Examine the call trace to identify the problematic code path.

4. Test with a simple module first:
   ```bash
   cd ./kernel/module/debug
   make -f Makefile.test
   sudo insmod simple_test.ko
   sudo rmmod simple_test
   ```

## Advanced Debugging

### Using GDB for Kernel Debugging

For very advanced debugging, you can use GDB with KGDB:

1. Configure the kernel with KGDB support:
   ```bash
   # Add to kernel command line in GRUB
   kgdboc=ttyS0,115200
   ```

2. Connect GDB to the kernel:
   ```bash
   sudo echo g > /proc/sysrq-trigger
   gdb ./vmlinux
   (gdb) target remote /dev/ttyS0
   ```

3. Set breakpoints in your module:
   ```
   (gdb) break psvr2_adapter_probe
   ```

### Performance Analysis

For performance bottlenecks, use the Linux perf tool:

```bash
# Record performance data
sudo perf record -g -p $(pidof process_using_psvr2) -o perf.data

# Analyze the data
sudo perf report -i perf.data
```

### Memory Profiling

Track kernel memory usage:

```bash
# Monitor memory usage
watch -n 1 cat /proc/meminfo

# Track slab allocator for kernel objects
watch -n 1 cat /proc/slabinfo | grep psvr2
```

## Continuous Integration Testing

The project supports automated testing through the `tools/build-test.sh` script.

### Setting Up Automated Tests

1. Configure your build environment:
   ```bash
   ./configure.sh --debug --enable-tests
   ```

2. Run automated tests:
   ```bash
   cd build
   make test
   ```

3. For kernel module testing, use the incremental test script:
   ```bash
   ./kernel/module/debug/psvr2_incremental_test.sh
   ```

### Debugging CI Failures

When automated tests fail:

1. Check the build logs:
   ```bash
   cat build/Testing/Temporary/LastTest.log
   ```

2. For kernel module tests, check the debug logs:
   ```bash
   cat kernel/module/debug/debug_logs/dmesg_*.txt
   ```

3. Run specific tests with increased verbosity:
   ```bash
   cd build
   ctest -V -R test_name
   ```

## Appendix: Useful Commands

### Kernel Module Commands

```bash
# List all loaded modules
lsmod | grep psvr2

# Remove module
sudo rmmod psvr2_adapter

# Module information
modinfo psvr2_adapter.ko

# Kernel logs
dmesg -wH | grep psvr2
```

### USB Debugging Commands

```bash
# List USB devices
lsusb -d 054C:0CDE -v

# Monitor USB events
sudo udevadm monitor --environment --udev

# Check USB device permissions
ls -la /dev/bus/usb/*/

# Reset USB device (replace X-X with the USB path)
echo "0" | sudo tee /sys/bus/usb/devices/X-X/authorized
echo "1" | sudo tee /sys/bus/usb/devices/X-X/authorized
```

### System Information

```bash
# Kernel version
uname -a

# List PCI devices
lspci -vnn | grep VGA

# Display kernel modules dependencies
sudo depmod -a
sudo modprobe --show-depends psvr2_adapter
```

### Debugging Tools Quick Reference

```bash
# USB packet capture
sudo ./tools/capture-usb.sh capture.pcapng

# Find the PSVR2 device
./tools/find-device.sh

# Run kernel module debug script
./kernel/module/debug/psvr2_debug.sh

# Build with debug options
./build.sh --debug --enable-tests

# Apply kernel module parameters
sudo insmod ./psvr2_adapter.ko debug_level=4 features_enabled=0x007F
```

---

This document provides a comprehensive framework for debugging the PSVR2 Linux adapter project. As the project evolves, these techniques can be refined and expanded to address new challenges.
