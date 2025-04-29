# PSVR2 Linux Adapter Debugging Guide

This comprehensive debugging guide provides developers with the tools, techniques, and knowledge needed to effectively debug the PSVR2 Linux adapter project. Whether you're working on kernel modules, analyzing USB protocols, or troubleshooting runtime issues, this documentation will help you efficiently identify and resolve problems.

## Available Debugging Documentation

- [**General Debugging Guide**](README.md) - Complete overview of debugging techniques
- [**Kernel Module Debugging**](kernel_module_debugging.md) - In-depth guide for kernel module debugging
- [**USB Protocol Debugging**](usb_protocol_debugging.md) - Techniques for USB protocol analysis
- [**Runtime Debugging**](runtime_debugging.md) - Methods for debugging during system operation
- [**Troubleshooting Guide**](troubleshooting_guide.md) - Solutions for common issues

## Debugging Workflow

For most debugging scenarios, we recommend the following workflow:

1. **Initial Assessment**
   - Identify the layer where the issue occurs (kernel, USB, userspace)
   - Check the [Troubleshooting Guide](troubleshooting_guide.md) for known issues

2. **Environment Preparation**
   - Configure for debugging: `./configure.sh --debug --enable-tests`
   - Build with debug symbols: `./build.sh --debug`

3. **Issue Reproduction**
   - Create a minimal test case that reproduces the issue
   - Use appropriate debugging tools to capture relevant information

4. **Analysis**
   - Use the specialized debugging guides to analyze the collected information
   - Identify the root cause of the issue

5. **Resolution**
   - Implement and test a fix
   - Document your findings to help others

## Common Debugging Tools

### Kernel Module Debugging
```bash
# Load module with debugging enabled
sudo insmod ./psvr2_adapter.ko debug_level=4 features_enabled=0x00FF

# Monitor kernel messages
dmesg -wH | grep psvr2

# Use the debug script
./kernel/module/debug/psvr2_debug.sh
```

### USB Protocol Analysis
```bash
# Capture USB traffic
sudo ./tools/capture-usb.sh my_capture.pcapng

# Find PSVR2 device
./tools/find-device.sh
```

### Runtime Debugging
```bash
# Enable dynamic debug
sudo bash -c "echo 'module psvr2_adapter +p' > /sys/kernel/debug/dynamic_debug/control"

# Trace function calls
cd /sys/kernel/debug/tracing
sudo bash -c "echo function > current_tracer"
sudo bash -c "echo '*psvr2*' > set_ftrace_filter"
sudo bash -c "echo 1 > tracing_on"
```

## Tips for Effective Debugging

1. **Be Methodical**
   - Follow a systematic approach to isolating problems
   - Document your findings as you go

2. **Capture Complete Information**
   - Always collect full logs when an issue occurs
   - Save USB captures during failure scenarios

3. **Isolate Variables**
   - Test with minimal configurations
   - Change one thing at a time

4. **Ask for Help**
   - Share complete logs and steps to reproduce
   - Describe what you've already tried

## Contributing to Debugging Documentation

If you discover new debugging techniques or common issues:

1. Document your findings
2. Submit a pull request to improve the debugging guides
3. Share your knowledge with the community

Good debugging documentation is crucial for the long-term success of this project.

## Getting Help

If you're stuck with a debugging issue:

- Review the [Troubleshooting Guide](troubleshooting_guide.md)
- Check existing GitHub issues
- Reach out to the community through the project's communication channels
