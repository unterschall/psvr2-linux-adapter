# Runtime Debugging Techniques

This document covers techniques for debugging the PSVR2 Linux adapter during operation, including tracing, profiling, and real-time monitoring.

## Table of Contents

1. [Dynamic Debug Facilities](#dynamic-debug-facilities)
2. [Tracing and Profiling](#tracing-and-profiling)
3. [Event Monitoring](#event-monitoring)
4. [Performance Analysis](#performance-analysis)
5. [Interactive Debugging](#interactive-debugging)
6. [Testing Tools](#testing-tools)

## Dynamic Debug Facilities

Linux provides several mechanisms for dynamic debugging without requiring module recompilation.

### Kernel Dynamic Debug

Control debug output at runtime:

```bash
# Show current dynamic debug settings
sudo cat /sys/kernel/debug/dynamic_debug/control

# Enable all debug messages for the module
sudo bash -c "echo 'module psvr2_adapter +p' > /sys/kernel/debug/dynamic_debug/control"

# Enable debugging for a specific file
sudo bash -c "echo 'file psvr2_adapter_main.c +p' > /sys/kernel/debug/dynamic_debug/control"

# Enable debugging for a specific function with line numbers
sudo bash -c "echo 'func psvr2_adapter_probe +pl' > /sys/kernel/debug/dynamic_debug/control"

# Enable all flags (p=printk, f=function, l=line, m=module, t=thread, v=verbose)
sudo bash -c "echo 'module psvr2_adapter +pflmt' > /sys/kernel/debug/dynamic_debug/control"

# Disable all debugging
sudo bash -c "echo 'module psvr2_adapter -p' > /sys/kernel/debug/dynamic_debug/control"
```

### Controlling Debug Parameters at Runtime

Modify module parameters without reloading:

```bash
# Change debug level (if supported by the module)
sudo bash -c "echo 4 > /sys/module/psvr2_adapter/parameters/debug_level"

# Enable specific feature debugging
sudo bash -c "echo 0x00FF > /sys/module/psvr2_adapter/parameters/features_enabled"
```

## Tracing and Profiling

### Using Ftrace

Ftrace is a powerful kernel tracing utility:

```bash
# Mount debugfs if not already mounted
sudo mount -t debugfs none /sys/kernel/debug

cd /sys/kernel/debug/tracing

# List available tracers
cat available_tracers

# Set function tracer
sudo bash -c "echo function > current_tracer"

# Filter for our module's functions
sudo bash -c "echo '*psvr2*' > set_ftrace_filter"

# Enable tracing
sudo bash -c "echo 1 > tracing_on"

# Perform operations with the device

# View trace
cat trace

# Disable tracing when done
sudo bash -c "echo 0 > tracing_on"
```

### Function Graph Tracing

For call graph with timing information:

```bash
cd /sys/kernel/debug/tracing

# Set function graph tracer
sudo bash -c "echo function_graph > current_tracer"

# Filter for our module's functions
sudo bash -c "echo '*psvr2*' > set_graph_function"

# Enable tracing
sudo bash -c "echo 1 > tracing_on"

# Perform operations

# View trace
cat trace
```

### Using Perf for Profiling

Identify performance bottlenecks:

```bash
# Install perf if needed
sudo apt install linux-tools-common linux-tools-$(uname -r)

# Basic profiling (samples CPU stacks)
sudo perf record -g -a -e cycles -- sleep 10

# Target specific process
sudo perf record -g -p $(pidof your_userspace_app) -- sleep 10

# Analyze results
sudo perf report
```

## Event Monitoring

### Monitoring USB Events

Track USB device events in real-time:

```bash
# Monitor USB device connects/disconnects
sudo udevadm monitor --environment --udev --subsystem-match=usb

# Analyze hardware events
sudo udevadm monitor --kernel --property --subsystem-match=usb
```

### Monitoring Device File Access

Track interactions with the device file:

```bash
# Using inotify tools
sudo apt install inotify-tools
inotifywait -m /dev/psvr2

# Using audit system
sudo apt install auditd
sudo auditctl -w /dev/psvr2 -p rwxa
sudo ausearch -f /dev/psvr2
```

### Tracking IRQ Activity

Monitor interrupt handling:

```bash
# View interrupt statistics
watch -n 1 "cat /proc/interrupts | grep -E 'CPU|usb'"

# Monitor USB device interrupts specifically
cd /sys/kernel/debug/usb/devices
grep -A 5 -B 5 "054c 0cde" *
```

## Performance Analysis

### Latency Tracking

Measure timing between operations:

```bash
# Enable event tracing for USB
cd /sys/kernel/debug/tracing
sudo bash -c "echo 1 > events/usb/enable"

# Add custom trace points in your code using trace_printk()
trace_printk("PSVR2: Starting operation X\n");
/* ... operation ... */
trace_printk("PSVR2: Completed operation X, duration: %lu us\n", duration);

# Monitor in real-time
cat trace_pipe
```

### Memory Usage Analysis

Track memory consumption:

```bash
# Monitor slab allocation
watch -n 1 "cat /proc/slabinfo | grep psvr2"

# Check for memory leaks using kmemleak
echo scan > /sys/kernel/debug/kmemleak
cat /sys/kernel/debug/kmemleak
```

### Bandwidth Monitoring

Measure USB bandwidth usage:

```bash
# Enable USB statistics
sudo bash -c "echo 1 > /sys/module/usbcore/parameters/log_urbs"

# Monitor log
dmesg -w | grep usb
```

## Interactive Debugging

### Using SystemTap

For advanced probing and data collection:

```bash
# Install SystemTap
sudo apt install systemtap systemtap-runtime

# Create a simple probe script (psvr2_probe.stp)
cat > psvr2_probe.stp << 'EOF'
probe module("psvr2_adapter").function("psvr2_adapter_probe") {
  printf("Probe function called for interface %p\n", $interface);
}

probe module("psvr2_adapter").function("psvr2_adapter_disconnect") {
  printf("Disconnect function called for interface %p\n", $interface);
}
EOF

# Run the probe
sudo stap psvr2_probe.stp
```

### Using BPF/bpftrace

For modern kernel tracing:

```bash
# Install bpftrace
sudo apt install bpftrace

# Simple tracing example
sudo bpftrace -e 'kprobe:psvr2_adapter_probe { printf("Probe called\n"); }'

# More detailed function trace with arguments
sudo bpftrace -e 'kprobe:psvr2_adapter_probe { printf("Probe called with interface %p\n", arg0); }'
```

### Using KGDB

For source-level kernel debugging (advanced):

1. **Configure kernel with KGDB support:**
   
   Add to kernel command line in GRUB: `kgdboc=ttyS0,115200`

2. **Trigger kgdb breakpoint:**
   ```bash
   # Trigger a breakpoint
   sudo bash -c "echo g > /proc/sysrq-trigger"
   ```

3. **Connect with GDB:**
   ```bash
   gdb ./vmlinux
   (gdb) target remote /dev/ttyS0
   (gdb) break psvr2_adapter_probe
   (gdb) continue
   ```

## Testing Tools

The project includes several testing utilities for runtime debugging.

### Using find-device.sh

Verify the adapter is properly detected:

```bash
cd /home/user/Projects/psvr2-linux-adapter/tools
./find-device.sh
```

### Module Test Mode

Enable test mode to run predefined test sequences:

```bash
# Load module with test mode
sudo insmod ./psvr2_adapter.ko test_mode=1

# The module will run through test sequences automatically
# Monitor outputs with:
dmesg -w
```

### Userspace Test Applications

Use test applications in the userspace directory:

```bash
cd /home/user/Projects/psvr2-linux-adapter/userspace
make test
./psvr2_test_app
```

---

## Additional Resources

- [Linux Kernel Documentation - Dynamic Debug](https://www.kernel.org/doc/html/latest/admin-guide/dynamic-debug-howto.html)
- [Linux Tracing Handbook](https://lttng.org/docs/v2.13/)
- [SystemTap Beginner's Guide](https://sourceware.org/systemtap/SystemTap_Beginners_Guide/)
- [BPF Performance Tools](http://www.brendangregg.com/bpf-performance-tools-book.html)
