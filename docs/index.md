# PSVR2 Linux Adapter Documentation

Welcome to the PSVR2 Linux Adapter project documentation. This project aims to develop Linux kernel modules and userspace tools for using the Sony PSVR2 headset with Linux via the official Sony PSVR2 PC adapter.

## Project Overview

The PSVR2 Linux Adapter project is currently in early development. Our goal is to enable full PSVR2 functionality on Linux systems by:

1. Reverse engineering the adapter's USB protocol
2. Developing kernel modules for hardware integration
3. Creating userspace libraries and tools
4. Providing framework integration (OpenHMD, SteamVR, etc.)

## Documentation Sections

### Getting Started
- [Installation Guide](installation.md)
- [Hardware Requirements](hardware_requirements.md)
- [Quick Start Guide](quickstart.md)

### Development
- [Build System](development/build_system.md)
- [Architecture Overview](development/architecture.md)
- [Contributing Guidelines](../CONTRIBUTING.md)

### Debugging
- [Debugging Overview](debugging/README.md)
- [Kernel Module Debugging](debugging/kernel_module_debugging.md)
- [USB Protocol Debugging](debugging/usb_protocol_debugging.md)
- [Runtime Debugging](debugging/runtime_debugging.md)
- [Troubleshooting Guide](debugging/troubleshooting_guide.md)

### Protocol Documentation
- [USB Protocol Overview](protocol/usb_protocol.md)
- [Initialization Sequence](protocol/initialization.md)
- [Input Data Format](protocol/input_data.md)
- [Display Control](protocol/display_control.md)

### API Reference
- [Kernel Module API](api/kernel_module.md)
- [Userspace Library API](api/userspace_library.md)
- [IOCTL Reference](api/ioctl_reference.md)

### Integration
- [OpenHMD Integration](integration/openhmd.md)
- [SteamVR Integration](integration/steamvr.md)
- [Monado Integration](integration/monado.md)

## Project Status

This project is in **early development** and is not yet ready for end users. Current focus areas:

- Reverse engineering the USB protocol
- Developing basic kernel module support
- Creating debugging and testing tools

Contributions are welcome! See our [Contributing Guidelines](../CONTRIBUTING.md) for information on how to get involved.

## Support

If you encounter issues or have questions:

- Check the [Troubleshooting Guide](debugging/troubleshooting_guide.md)
- Open an issue on our GitHub repository
- Join our development chat (see project README for details)

## License

This project is licensed under the GNU General Public License v2.0. See the [LICENSE](../LICENSE) file for details.
