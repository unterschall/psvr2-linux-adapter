# PSVR2 Linux Adapter Project

## Overview

This project aims to develop Linux kernel modules and userspace tools for using the Sony PSVR2 headset with Linux via the official Sony PSVR2 PC adapter. The goal is to enable PSVR2 functionality on Linux by interfacing with Sony's adapter hardware, leveraging the existing PSVR2 kernel modules codebase as a reference.

## Project Status

**Current Status:** Early Development

This project is in the early stages of development and is not yet functional. The current focus is on:

1. Reverse engineering the adapter's USB protocol
2. Developing basic kernel modules for adapter integration
3. Creating test tools to validate communication

## Prerequisites

### Hardware Requirements

- Sony PSVR2 headset
- Official Sony PSVR2 PC adapter
- DisplayPort 1.4 compatible graphics card
- USB 3.0 port
- Bluetooth 4.0 or newer for controller support

### Software Requirements

- Linux kernel 5.10 or newer
- CMake 3.10 or newer
- GCC or Clang compiler
- Linux kernel headers
- Development packages for:
  - libusb-1.0
  - libdrm
  - hidapi
  - libevdev
  - libinput

## Project Structure

```
psvr2-linux-adapter/
├── build/              # CMake modules and build support
├── docs/               # Documentation
├── kernel/             # Kernel modules
│   ├── module/         # Module source code
│   └── include/        # Public headers
├── tools/              # Protocol analysis and testing tools
├── userspace/          # Userspace libraries and applications
└── integration/        # Framework integration (OpenHMD, SteamVR, etc.)
```

## Building and Installation

### Quick Start

The project uses a modern CMake-based build system with distribution detection and automatic dependency resolution:

```bash
# Clone the repository
git clone https://github.com/your-username/psvr2-linux-adapter.git
cd psvr2-linux-adapter

# Make the configuration script executable
chmod +x configure.sh

# Configure the build system
./configure.sh

# Build the project
cd build
make

# Install (as root)
sudo make install
```

### Advanced Configuration

The configuration script offers several options to customize the build:

```bash
./configure.sh --help
```

Options include:

- `--debug`: Enable debug build with additional logging
- `--build-dir DIR`: Set custom build directory
- `--prefix DIR`: Set installation prefix (default: /usr/local)
- `--disable-kernel-module`: Skip building the kernel module
- `--enable-userspace`: Build userspace tools and libraries
- `--enable-dkms`: Enable DKMS support for automatic module rebuilding
- `--enable-tests`: Build test programs
- `--ninja`: Use Ninja build system instead of Make

### DKMS Support

For automatic module rebuilding when updating the kernel:

```bash
./configure.sh --enable-dkms
cd build
make
sudo make install
```

### Distribution-Specific Notes

The build system will detect your Linux distribution and provide specific installation instructions for dependencies. If you need to install dependencies manually, here are the common packages for major distributions:

#### Debian/Ubuntu and Derivatives
```bash
sudo apt-get update
sudo apt-get install build-essential cmake linux-headers-$(uname -r) \
    libdrm-dev libusb-1.0-0-dev libhidapi-dev libudev-dev \
    libevdev-dev libinput-dev pkg-config
```

#### Fedora/RHEL and Derivatives
```bash
sudo dnf install kernel-devel kernel-headers gcc gcc-c++ make cmake \
    libdrm-devel libusbx-devel hidapi-devel systemd-devel \
    libevdev-devel libinput-devel pkgconfig
```

#### Arch Linux and Derivatives
```bash
sudo pacman -S linux-headers base-devel cmake \
    libdrm libusb hidapi libevdev libinput pkgconf
```

#### openSUSE
```bash
sudo zypper install kernel-devel gcc gcc-c++ make cmake \
    libdrm-devel libusb-1_0-devel hidapi-devel libudev-devel \
    libevdev-devel libinput-devel pkg-config
```

#### Gentoo
```bash
sudo emerge --ask sys-kernel/linux-headers dev-util/cmake \
    x11-libs/libdrm dev-libs/libusb dev-libs/hidapi \
    dev-libs/libevdev dev-libs/libinput dev-util/pkgconfig
```

## Usage

Since this project is still in development, there is no stable usage pattern yet. For developers, we provide:

1. **Testing Tools**:
   - USB protocol analyzer in `tools/capture-usb.sh`
   - Device detection in `tools/find-device.sh`

2. **Kernel Module**:
   After installation, the module can be loaded manually:
   ```bash
   sudo modprobe psvr2_adapter
   ```

   To verify the module is loaded:
   ```bash
   lsmod | grep psvr2
   ```

   The device interface will be available at `/dev/psvr2`

## Development Roadmap

### Phase 1: Research and Protocol Analysis
- Set up hardware test environment
- Capture and analyze USB and DisplayPort traffic
- Document protocol details
- Reverse engineer necessary communication patterns

### Phase 2: Core Kernel Module Development
- Implement adapter detection and initialization
- Develop DisplayPort passthrough functionality
- Implement USB communication for tracking data
- Create basic device file interface

### Phase 3: Input and Controller Support
- Implement Bluetooth interfacing for controllers
- Develop input event mapping
- Create configuration system

### Phase 4: Framework Integration
- Develop OpenHMD driver plugin
- Create SteamVR compatibility layer
- Test with existing VR applications

### Phase 5: User Tools and Documentation
- Create configuration tools
- Develop calibration utilities
- Complete documentation

## Contributing

Contributions are welcome! Here's how you can help:

1. **Protocol Analysis**: Help reverse engineer the adapter's communication protocol
2. **Code Development**: Implement kernel modules and userspace tools
3. **Documentation**: Improve documentation and create tutorials
4. **Testing**: Test the implementation with different hardware configurations

Please see [CONTRIBUTING.md](CONTRIBUTING.md) for more details on our development workflow.

## License

This project is licensed under the GNU General Public License v2.0.

## Acknowledgements

- Sony Interactive Entertainment for releasing the PSVR2 PC adapter
- Contributors to the Linux kernel VR initiatives
- The OpenHMD project for their work on open VR drivers
