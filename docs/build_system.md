# PSVR2 Linux Adapter Build System Documentation

This document explains the architecture and design of the build system used for the PSVR2 Linux Adapter project.

## Overview

The build system is designed with the following goals:

1. **Cross-Distribution Compatibility**: Work seamlessly across major Linux distributions
2. **Flexible Configuration**: Support various build options and configurations
3. **Modern Approach**: Use CMake as the build system generator
4. **Automatic Dependency Handling**: Detect and install required dependencies
5. **DKMS Integration**: Support for automatic kernel module rebuilding
6. **User-Friendly Interface**: Simple scripts for common operations

## Architecture

The build system is structured as follows:

```
project/
├── build/                  # Build system files
│   ├── cmake/              # CMake modules
│   │   ├── DistroDetect.cmake      # Distribution detection
│   │   ├── KernelModule.cmake      # Kernel module build support
│   │   ├── PackageRequirements.cmake  # Dependency handling
│   │   └── InstallRequirements.cmake  # Installation configuration
│   ├── scripts/            # Helper scripts
│   │   ├── dkms-postinst.sh.in     # DKMS installation script
│   │   └── dkms-prerm.sh.in        # DKMS removal script
│   └── templates/          # Template files
│       └── PKGBUILD.in            # Arch Linux package template
├── CMakeLists.txt          # Top-level CMake configuration
├── configure.sh            # Configuration script
└── build.sh                # Build automation script
```

## Key Components

### 1. CMake Modules

#### DistroDetect.cmake
- Detects the Linux distribution and family
- Sets variables `DETECTED_DISTRO`, `DETECTED_DISTRO_FAMILY`, and `DETECTED_DISTRO_VER`
- Handles special cases for derivatives (Ubuntu → Debian, Manjaro → Arch, etc.)

#### KernelModule.cmake
- Locates kernel headers and build system
- Provides the `add_kernel_module()` function for creating kernel module targets
- Sets up DKMS configuration if enabled
- Handles cross-distribution kernel header paths

#### PackageRequirements.cmake
- Checks for required and optional dependencies
- Generates distribution-specific installation instructions
- Provides detailed error messages for missing dependencies

#### InstallRequirements.cmake
- Sets up installation targets
- Configures DKMS if enabled
- Sets up packaging (RPM, DEB, etc.) based on distribution

### 2. Helper Scripts

#### configure.sh
- Entry point for configuring the build system
- Parses command-line options
- Sets up the CMake build directory
- Handles special cases and compatibility issues

#### build.sh
- Automates the build process
- Provides options for cleaning, rebuilding, and installing
- Handles parallel builds and sudo requirements

#### DKMS Scripts
- `dkms-postinst.sh.in`: Template for post-installation DKMS registration
- `dkms-prerm.sh.in`: Template for pre-removal DKMS cleanup

### 3. Integration with Kernel Build System

The build system integrates with the Linux kernel's Kbuild system:

1. It generates a `Kbuild` file for the kernel module
2. It correctly links against kernel headers
3. It handles distribution-specific kernel building quirks
4. It supports proper DKMS integration for kernel updates

## Configuration Options

The build system supports the following configuration options:

| Option | Description | Default |
|--------|-------------|---------|
| `--debug` | Enable debug build | OFF |
| `--build-dir DIR` | Set build directory | `build` |
| `--prefix DIR` | Set installation prefix | `/usr/local` |
| `--disable-kernel-module` | Don't build kernel module | Enabled |
| `--enable-userspace` | Build userspace components | Disabled |
| `--enable-dkms` | Enable DKMS support | Disabled |
| `--enable-tests` | Build tests | Disabled |
| `--ninja` | Use Ninja build system | Make |
| `--clean` | Clean build directory | No |

## Distribution-Specific Handling

The build system includes special handling for different distribution families:

### Debian/Ubuntu
- Handles specific kernel header package naming
- Sets up DEB packaging
- Configures udev rules in the appropriate location

### Fedora/RHEL
- Handles specific kernel source paths
- Sets up RPM packaging
- Adjusts for specific library naming (libusbx vs libusb)

### Arch Linux
- Creates PKGBUILD template for AUR packaging
- Handles rolling release versioning
- Adjusts for specific package names

### openSUSE
- Handles specific package naming and paths
- Configures for openSUSE build service

### Gentoo
- Handles Portage-specific paths and requirements
- Configures for specific USE flags

## Build Process

The typical build process consists of the following steps:

1. **Configuration**:
   ```bash
   ./configure.sh [options]
   ```
   This creates the build directory and generates the build system.

2. **Building**:
   ```bash
   cd build
   make
   ```
   This compiles the kernel module and other components.

3. **Installation**:
   ```bash
   sudo make install
   ```
   This installs the kernel module, udev rules, and other components.

## DKMS Integration

When DKMS is enabled:

1. The source files are installed to `/usr/src/psvr2-linux-adapter-<version>/`
2. A DKMS configuration file is generated
3. The module is registered with DKMS
4. The module is automatically rebuilt when the kernel is updated

## Packaging

The build system supports creating distribution-specific packages:

- For Debian/Ubuntu: DEB packages
- For Fedora/RHEL: RPM packages
- For Arch Linux: PKGBUILD template for AUR

## Advanced Usage

### Building with Custom Kernel Headers

```bash
./configure.sh
cd build
cmake -DKERNEL_HEADERS_PATH=/path/to/kernel/headers .
make
```

### Creating a Distribution Package

```bash
./configure.sh --enable-dkms
cd build
make package
```

### Running Tests

```bash
./configure.sh --enable-tests
cd build
make
make test
```

## Troubleshooting

### Common Issues

1. **Missing kernel headers**:
   - Verify your distribution's kernel header package is installed
   - Check that the headers match your running kernel

2. **Build failures**:
   - Check for missing dependencies
   - Verify compiler and toolchain compatibility
   - Look for distribution-specific issues

3. **Installation problems**:
   - Check permissions
   - Verify DKMS is installed if using DKMS mode
   - Check for conflicting modules

### Debugging the Build System

For detailed build system debugging:

```bash
CMAKE_VERBOSE_MAKEFILE=ON ./configure.sh --debug
cd build
make VERBOSE=1
```

## Contributing to the Build System

When making changes to the build system:

1. Ensure backward compatibility
2. Test on multiple distributions
3. Document new configuration options
4. Update this documentation
5. Consider distribution-specific implications
