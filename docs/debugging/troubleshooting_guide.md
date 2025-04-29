# PSVR2 Linux Adapter Troubleshooting Guide

This document provides solutions for common issues encountered when working with the PSVR2 Linux adapter project.

## Table of Contents

1. [Build and Installation Issues](#build-and-installation-issues)
2. [Module Loading Issues](#module-loading-issues)
3. [Device Detection Problems](#device-detection-problems)
4. [USB Communication Failures](#usb-communication-failures)
5. [Kernel Crashes and Panics](#kernel-crashes-and-panics)
6. [Permission Problems](#permission-problems)
7. [Performance Issues](#performance-issues)
8. [Debugging Flowchart](#debugging-flowchart)

## Build and Installation Issues

### Error: Cannot Find Kernel Headers

**Symptoms:**
- Build fails with "kernel headers not found" error
- Error message about missing `linux/module.h`

**Solutions:**
1. Install kernel headers:
   ```bash
   # Debian/Ubuntu
   sudo apt install linux-headers-$(uname -r)
   
   # Fedora
   sudo dnf install kernel-devel
   
   # Arch Linux
   sudo pacman -S linux-headers
   ```

2. Verify headers match running kernel:
   ```bash
   uname -r                   # Shows running kernel version
   dpkg -l | grep linux-headers  # Should include matching version
   ```

### Error: Compilation Fails with Undefined References

**Symptoms:**
- Build fails with "undefined reference" errors
- Linker errors about missing symbols

**Solutions:**
1. Check for recursive dependencies:
   ```bash
   # Clean the build
   ./cleanup.sh
   
   # Reconfigure with debug enabled
   ./configure.sh --debug
   
   # Build with verbose output
   cd build
   make VERBOSE=1
   ```

2. Fix circular includes:
   - Check for header file circular dependencies
   - Ensure proper forward declarations in header files

## Module Loading Issues

### Error: Module Not Found

**Symptoms:**
- `modprobe psvr2_adapter` returns "module not found"

**Solutions:**
1. Ensure module is in the correct location:
   ```bash
   # Copy module to system modules directory
   sudo cp build/kernel/psvr2_adapter.ko /lib/modules/$(uname -r)/extra/
   sudo depmod -a
   ```

2. Verify module installation:
   ```bash
   modinfo psvr2_adapter
   ```

### Error: Unknown Symbol in Module

**Symptoms:**
- Module load fails with "unknown symbol" error
- dmesg shows symbol resolution errors

**Solutions:**
1. Check for kernel version mismatch:
   ```bash
   # Rebuild module against current kernel
   sudo apt install linux-headers-$(uname -r)
   ./cleanup.sh
   ./configure.sh
   cd build && make
   ```

2. Check for missing dependencies:
   ```bash
   # Identify module dependencies
   modinfo psvr2_adapter.ko | grep depends
   
   # Load any missing dependencies
   sudo modprobe <dependency_module>
   ```

3. Use the debug script to capture detailed error information:
   ```bash
   ./kernel/module/debug/psvr2_debug.sh
   ```

## Device Detection Problems

### Error: Device Not Detected

**Symptoms:**
- PSVR2 adapter not showing in `lsusb` output
- No device file created at `/dev/psvr2`

**Solutions:**
1. Verify hardware connection:
   ```bash
   # Check USB devices
   lsusb | grep -i sony
   ```

2. Try different USB ports:
   - Move to another USB port, preferably a USB 3.0 port
   - Try ports directly connected to the motherboard (not through a hub)

3. Check USB power delivery:
   - The adapter may require more power than available
   - Use a powered USB hub if necessary

### Error: Device File Missing

**Symptoms:**
- Module loads successfully but `/dev/psvr2` doesn't appear

**Solutions:**
1. Check kernel log for device creation errors:
   ```bash
   dmesg | grep psvr2
   ```

2. Manually create device node for testing:
   ```bash
   # Find the major number
   grep psvr2 /proc/devices
   
   # Create device node (replace X with major number)
   sudo mknod /dev/psvr2 c X 0
   sudo chmod 666 /dev/psvr2
   ```

## USB Communication Failures

### Error: USB Transfer Timeout

**Symptoms:**
- "Transfer timed out" errors in dmesg
- Functions stalling during USB operations

**Solutions:**
1. Check USB controller settings:
   ```bash
   # Disable USB autosuspend
   sudo bash -c 'echo -1 > /sys/module/usbcore/parameters/autosuspend'
   ```

2. Increase USB timeout values in the module:
   ```bash
   # Load module with longer timeouts
   sudo insmod ./psvr2_adapter.ko usb_timeout=5000
   ```

3. Monitor USB errors:
   ```bash
   # Look for specific USB error messages
   dmesg | grep -i "usb.*error"
   ```

### Error: I/O Error During USB Communication

**Symptoms:**
- "I/O error" or "pipe error" in dmesg
- Error codes -32 (EPIPE) or -71 (EPROTO)

**Solutions:**
1. Reset USB device:
   ```bash
   # Find the USB path (e.g., 3-1)
   ls /sys/bus/usb/devices/
   
   # Reset the device
   echo 0 > /sys/bus/usb/devices/3-1/authorized
   echo 1 > /sys/bus/usb/devices/3-1/authorized
   ```

2. Check for USB protocol errors in captures:
   ```bash
   # Capture USB traffic during failure
   sudo ./tools/capture-usb.sh failure_capture.pcapng
   
   # Analyze with Wireshark
   wireshark failure_capture.pcapng
   ```

## Kernel Crashes and Panics

### Error: Kernel Oops or Panic on Module Load

**Symptoms:**
- System crashes when loading the module
- Kernel log shows "Oops" or "BUG" messages

**Solutions:**
1. Capture detailed crash information:
   ```bash
   # Run the debug script, which automatically saves crash logs
   ./kernel/module/debug/psvr2_debug.sh
   ```

2. Analyze the call trace in the logs:
   ```bash
   # Look for the crash point in the call trace
   grep -A 30 "Call Trace" kernel/module/debug/debug_logs/dmesg_*.txt
   ```

3. Test with a minimal module first:
   ```bash
   # Build and test a simple module
   cd kernel/module/debug
   make -f Makefile.test
   sudo insmod simple_test.ko
   ```

### Error: Null Pointer Dereference

**Symptoms:**
- Kernel logs show "null pointer dereference"
- Stack trace points to specific function

**Solutions:**
1. Add additional NULL checks:
   - Identify the function in the stack trace
   - Add `DBG_ASSERT(pointer != NULL, "pointer is NULL")` before using pointers
   - Ensure pointer initialization is properly sequenced

2. Use memory debugging tools:
   ```bash
   # Load module with memory debugging
   sudo insmod ./psvr2_adapter.ko memcheck=1
   ```

## Permission Problems

### Error: Permission Denied

**Symptoms:**
- "Permission denied" when accessing `/dev/psvr2`
- Cannot connect to device from userspace

**Solutions:**
1. Check current permissions:
   ```bash
   ls -la /dev/psvr2
   ```

2. Add udev rule for persistent permissions:
   ```bash
   # Copy the provided udev rule
   sudo cp 99-psvr2.rules /etc/udev/rules.d/
   
   # Reload udev rules
   sudo udevadm control --reload-rules
   sudo udevadm trigger
   ```

3. Add your user to the appropriate group:
   ```bash
   # Create a psvr2 group if it doesn't exist
   sudo groupadd -f psvr2
   
   # Add your user to the group
   sudo usermod -aG psvr2 $USER
   
   # Log out and back in for changes to take effect
   ```

## Performance Issues

### Problem: High Latency

**Symptoms:**
- Display lag or high tracking latency
- Jerky movement in VR applications

**Solutions:**
1. Check for USB bandwidth issues:
   ```bash
   # Verify USB controller mode
   lsusb -t | grep "psvr2\|Sony"
   ```
   - Ensure the device is on a USB 3.0 controller
   - Move other high-bandwidth devices to different controllers

2. Check for system load:
   ```bash
   # Monitor system load
   top -b -n 1 | head -20
   ```
   - Reduce background processes
   - Consider adjusting process priorities

3. Check for thermal throttling:
   ```bash
   # Monitor CPU frequencies
   watch -n 1 "cat /proc/cpuinfo | grep MHz"
   ```
   - Improve system cooling if frequencies are dropping

### Problem: Unstable Connection

**Symptoms:**
- Device disconnects randomly
- Intermittent connection drops

**Solutions:**
1. Check for power management issues:
   ```bash
   # Disable USB autosuspend for the device
   echo on > /sys/bus/usb/devices/3-1/power/control
   ```

2. Monitor USB errors during disconnection:
   ```bash
   # Keep a dmesg watch running
   dmesg -w | grep -i "usb\|psvr2"
   ```

3. Try a different USB cable or port:
   - Use high-quality USB cables
   - Connect directly to the motherboard USB port

## Debugging Flowchart

Use this flowchart to systematically debug issues:

1. **Verify hardware detection:**
   - Run `lsusb | grep -i sony`
   - If not detected, check hardware connections

2. **Check kernel module status:**
   - Run `lsmod | grep psvr2`
   - If not loaded, attempt to load with `sudo insmod ./psvr2_adapter.ko debug_level=4`
   - Check dmesg for errors

3. **Verify device file creation:**
   - Check for `/dev/psvr2`
   - If missing but module is loaded, check dmesg for device creation errors

4. **Test basic communication:**
   - Try `sudo cat /dev/psvr2` or use test tools
   - Check dmesg for communication errors

5. **Analyze specific errors:**
   - Use the appropriate section in this guide based on error messages
   - Collect detailed logs with `./kernel/module/debug/psvr2_debug.sh`

6. **Report issues with complete information:**
   - Include kernel version: `uname -a`
   - Include module version
   - Attach debug logs
   - Describe exact steps to reproduce

---

If you encounter an issue not covered in this guide, please submit a detailed bug report on the project repository including all steps to reproduce the problem and relevant logs.
