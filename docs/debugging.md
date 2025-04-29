# PSVR2 Linux Adapter Debugging

This document provides an overview of the debugging resources available for the PSVR2 Linux adapter project.

## Getting Started with Debugging

The PSVR2 Linux adapter project includes comprehensive debugging tools and documentation to help developers diagnose and resolve issues. Whether you're working on kernel modules, analyzing USB protocol, or troubleshooting runtime problems, these resources will help you efficiently debug the system.

## Available Debugging Documentation

Our debugging documentation is organized into several specialized guides:

### [General Debugging Guide](debugging/README.md)

The main debugging guide provides a comprehensive overview of debugging techniques for the PSVR2 Linux adapter project. It covers:

- Setting up the development environment with debugging tools
- Building the project with debug options
- Basic debugging workflows
- Common debugging scenarios

This is the best place to start for new contributors.

### [Kernel Module Debugging](debugging/kernel_module_debugging.md)

This specialized guide focuses on debugging the kernel modules that form the core of the project. It covers:

- Module loading and unloading issues
- Using printk and debug macros
- Memory management debugging
- Crash analysis techniques
- Using Linux kernel debugging tools

Essential reading for kernel module development.

### [USB Protocol Debugging](debugging/usb_protocol_debugging.md)

This guide focuses on capturing, analyzing, and debugging the USB communication between the Linux system and the PSVR2 adapter. It covers:

- USB traffic capture techniques
- Protocol analysis with Wireshark
- Manual USB communication testing
- Protocol reverse engineering methods
- Common USB communication issues

Crucial for understanding and implementing the adapter's protocol.

### [Runtime Debugging](debugging/runtime_debugging.md)

This guide covers techniques for debugging the system during operation. It includes:

- Dynamic debug facilities
- Tracing and profiling methods
- Event monitoring
- Performance analysis
- Interactive debugging techniques

Useful for diagnosing issues that occur during active usage.

### [Troubleshooting Guide](debugging/troubleshooting_guide.md)

This practical guide provides solutions to common issues encountered when working with the PSVR2 Linux adapter. It includes:

- Build and installation issues
- Module loading problems
- Device detection troubleshooting
- USB communication failures
- Kernel crashes and panics
- Permission problems
- Performance issues

A helpful reference when you encounter specific problems.

## Debugging Tools

The project includes several debugging tools located in different directories:

### Kernel Module Debug Tools

Located in `/kernel/module/debug/`:

- `psvr2_debug.sh`: Main debugging script for kernel modules
- `psvr2_incremental_test.sh`: Incremental testing script
- `simple_test.c`: Simple test module for basic functionality testing

### USB Analysis Tools

Located in `/tools/`:

- `capture-usb.sh`: USB traffic capture script
- `find-device.sh`: Device detection tool
- Sample USB captures for reference

### Integration Test Tools

Located in `/integration/tests/`:

- Test applications for different VR frameworks
- Integration validation tools

## Debug Build Configuration

To build the project with debugging enabled:

```bash
# Configure with debug options
./configure.sh --debug --enable-tests

# Build with debugging enabled
./build.sh --debug
```

For kernel module debugging:

```bash
# Load module with debug parameters
sudo insmod ./psvr2_adapter.ko debug_level=4 features_enabled=0x00FF
```

## Contributing to Debugging

If you develop new debugging techniques or tools for the project, please consider:

1. Documenting your approach in the appropriate guide
2. Adding your tools to the repository
3. Updating this overview document

Clear debugging documentation is essential for project sustainability.

## Getting Help

If you're stuck with a debugging issue:

1. Check if the issue is addressed in the [Troubleshooting Guide](debugging/troubleshooting_guide.md)
2. Look through GitHub issues for similar problems
3. Ask for help in the project's communication channels
4. When reporting issues, include all relevant debug logs and information

## Future Improvements

We plan to enhance our debugging capabilities with:

- Automated testing frameworks
- Enhanced USB protocol analyzers
- GUI-based debugging tools
- Diagnostic userspace applications

Contributions in these areas are welcome!
