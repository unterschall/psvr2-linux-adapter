# Kernel Module Debugging

This document provides detailed guidance on debugging kernel modules for the PSVR2 Linux adapter project.

## Table of Contents

1. [Module Loading and Unloading](#module-loading-and-unloading)
2. [Logging and Tracing](#logging-and-tracing)
3. [Memory Management](#memory-management)
4. [Debugging USB Communication](#debugging-usb-communication)
5. [Crash Analysis](#crash-analysis)
6. [Using Debug Flags](#using-debug-flags)

## Module Loading and Unloading

### Loading the Module with Debug Options

The `psvr2_adapter` kernel module supports several debug parameters:

```bash
# Load with debug level (0-4)
sudo insmod ./psvr2_adapter.ko debug_level=4

# Enable specific feature debugging
sudo insmod ./psvr2_adapter.ko features_enabled=0x007F

# Combine multiple parameters
sudo insmod ./psvr2_adapter.ko debug_level=4 features_enabled=0x007F
```

Available debug flags in `features_enabled`:

| Flag | Hex Value | Description |
|------|-----------|-------------|
| `FEAT_USB_INIT` | 0x0001 | USB initialization debugging |
| `FEAT_DEVICE_INIT` | 0x0002 | Device initialization debugging |
| `FEAT_INPUT` | 0x0004 | Input subsystem debugging |
| `FEAT_OUTPUT` | 0x0008 | Output subsystem debugging |
| `FEAT_HID` | 0x0010 | HID protocol debugging |
| `FEAT_SENSORS` | 0x0020 | Sensor data debugging |
| `FEAT_DISPLAY` | 0x0040 | Display subsystem debugging |
| `FEAT_TRACKING` | 0x0080 | Tracking subsystem debugging |
| `FEAT_AUDIO` | 0x0100 | Audio subsystem debugging |

### Debugging Module Loading Failures

If the module fails to load:

1. **Check the kernel log:**
   ```bash
   dmesg | tail -30
   ```

2. **Look for missing symbols:**
   ```bash
   sudo modprobe --resolve-alias psvr2_adapter
   ```

3. **Verify module dependencies:**
   ```bash
   sudo depmod -a
   modinfo -F depends psvr2_adapter.ko
   ```

4. **Use the debug script for comprehensive diagnostics:**
   ```bash
   ./kernel/module/debug/psvr2_debug.sh
   ```

5. **Try loading modules in smaller chunks** (if available):
   ```bash
   sudo insmod ./psvr2_adapter_core.ko
   sudo insmod ./psvr2_adapter_display.ko
   sudo insmod ./psvr2_adapter_input.ko
   ```

## Logging and Tracing

### Configuring Debug Logging Levels

The module uses several printk levels. Ensure your console is configured to show the appropriate level:

```bash
# Show current printk configuration
cat /proc/sys/kernel/printk

# Show all messages up to debug level (format: console_loglevel default_loglevel ...)
echo "8 4 1 7" > /proc/sys/kernel/printk
```

### Using Dynamic Debug

Linux kernel's dynamic debug feature allows fine-tuning debug output at runtime:

```bash
# Enable debug for all module functions
sudo bash -c "echo 'module psvr2_adapter +p' > /sys/kernel/debug/dynamic_debug/control"

# Enable specific file debugging
sudo bash -c "echo 'file psvr2_adapter_main.c +p' > /sys/kernel/debug/dynamic_debug/control"

# Enable specific function debugging
sudo bash -c "echo 'func psvr2_adapter_probe +p' > /sys/kernel/debug/dynamic_debug/control"

# Add timing information to debug messages
sudo bash -c "echo 'module psvr2_adapter +pt' > /sys/kernel/debug/dynamic_debug/control"
```

### Using Ftrace for Function Tracing

Trace function calls in the module:

```bash
# Mount debugfs if needed
sudo mount -t debugfs none /sys/kernel/debug

# Set up tracing for the module
cd /sys/kernel/debug/tracing
sudo bash -c "echo function > current_tracer"
sudo bash -c "echo 'psvr2_*' > set_ftrace_filter"
sudo bash -c "echo 1 > tracing_on"

# View the trace
sudo cat trace

# Reset tracing
sudo bash -c "echo 0 > tracing_on"
```

### Function Graph Tracing

For more detailed function call graph with timing:

```bash
cd /sys/kernel/debug/tracing
sudo bash -c "echo function_graph > current_tracer"
sudo bash -c "echo 'psvr2_*' > set_graph_function"
sudo bash -c "echo 1 > tracing_on"

# Perform operations with the device

sudo cat trace
```

## Memory Management

### Detecting Memory Leaks

Use kmemleak to find memory leaks in the kernel module:

```bash
# Enable kmemleak if not already done
sudo mount -t debugfs none /sys/kernel/debug
sudo bash -c "echo scan > /sys/kernel/debug/kmemleak"

# Load module and exercise functionality

# Scan for leaks
sudo bash -c "echo scan > /sys/kernel/debug/kmemleak"
sudo cat /sys/kernel/debug/kmemleak
```

### Debugging Memory Allocation Issues

1. **Enable memory allocation debugging:**
   ```bash
   sudo sysctl -w vm.panic_on_oom=0
   sudo sysctl -w kernel.tainted=1
   sudo sysctl -w vm.overcommit_memory=2
   ```

2. **Monitor memory usage:**
   ```bash
   watch -n 1 "cat /proc/meminfo | grep -E 'Slab|Mapped|KReclaimable'"
   ```

3. **Check slab allocation for the module:**
   ```bash
   watch -n 1 "cat /proc/slabinfo | grep -i psvr2"
   ```

## Debugging USB Communication

### Testing Basic Communication

To verify USB endpoints are working correctly:

```bash
# Load the module with USB debugging enabled
sudo insmod ./psvr2_adapter.ko debug_level=4 features_enabled=0x0001

# Monitor USB communication
sudo cat /sys/kernel/debug/usb/usbmon/Xw | grep -A 5 -B 5 "054c:0cde"
```

### Capturing Detailed USB Traffic

For comprehensive USB debugging:

```bash
# Load module
sudo modprobe usbmon

# Find the bus where PSVR2 adapter is connected (X)
lsusb

# Capture traffic
sudo tshark -i usbmonX -w psvr2_capture.pcapng \
  -f "host 054c:0cde" -a duration:30
```

### Testing Control Transfers

Debug USB control transfers manually:

```bash
# Using usb-ctrl (install usbutils if not available)
sudo usb-ctrl -d X:X -s 0x054c:0x0cde -v 0x0 -i 0x0 -g 1 -r 0

# Use the specific index and request values from the captured USB traffic
```

## Crash Analysis

### Kernel Oops and Panics

When a kernel crash occurs:

1. **Capture the complete kernel log:**
   ```bash
   sudo dmesg > crash_log.txt
   ```

2. **Look for the call trace in the log:**
   ```bash
   grep -A 30 "Call Trace" crash_log.txt
   ```

3. **Use the panic info collection script:**
   ```bash
   ./kernel/module/debug/psvr2_debug.sh
   # The script automatically saves crash logs to debug_logs/
   ```

4. **Analyze memory dumps if available:**
   ```bash
   # If kdump is configured
   sudo kdumpctl save crash_dump
   sudo crash /usr/lib/debug/vmlinux crash_dump
   ```

### Using the Simple Test Module

Before debugging complex issues, verify basic kernel module functionality:

```bash
# Compile simple test module
cd ./kernel/module/debug
make -f Makefile.test

# Load and test
sudo insmod simple_test.ko
dmesg | tail
sudo rmmod simple_test
dmesg | tail
```

## Using Debug Flags

The project includes a powerful debug infrastructure in `psvr2_debug.h`. Make effective use of these macros:

### Entry/Exit Tracing

Mark function entry and exit points:

```c
int my_function(struct psvr2_device *dev)
{
    DBG_FUNC_ENTRY();
    
    // Function logic
    
    if (error_condition) {
        DBG_FUNC_EXIT_ERR(err_code);
        return err_code;
    }
    
    DBG_FUNC_EXIT();
    return 0;
}
```

### Feature-Specific Debugging

Enable detailed logging for specific features:

```c
if (FEATURE_ENABLED(FEAT_USB_INIT)) {
    DBG_INF("USB device information: VID:%04x PID:%04x\n", vid, pid);
    DBG_DBG("Endpoint details: addr=0x%02x, max_packet=%d\n", 
            ep->bEndpointAddress, le16_to_cpu(ep->wMaxPacketSize));
}
```

### Assertion-Like Checks

Validate assumptions during development:

```c
DBG_ASSERT(dev != NULL, "Device structure is NULL in %s", __func__);
DBG_ASSERT(dev->interface != NULL, "USB interface is NULL");
```

### Memory Debugging

Track memory allocations and deallocations:

```c
buffer = kmalloc(size, GFP_KERNEL);
if (buffer) {
    DBG_MEM_ALLOC(buffer, size);
}

// Later
if (buffer) {
    DBG_MEM_FREE(buffer);
    kfree(buffer);
    buffer = NULL;
}
```

---

## Additional Resources

- [Linux Kernel Documentation - Dynamic Debug](https://www.kernel.org/doc/html/latest/admin-guide/dynamic-debug-howto.html)
- [Linux Kernel Documentation - ftrace](https://www.kernel.org/doc/html/latest/trace/ftrace.html)
- [USB Protocol Debugging Guide](usb_protocol_debugging.md)
- [KernelNewbies Debugging Resources](https://kernelnewbies.org/Debugging)
