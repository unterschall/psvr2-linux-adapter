# PSVR2 Adapter Debugging

This directory contains tools for debugging the PSVR2 adapter kernel module.

## Files

- `psvr2_debug.sh`: Main debugging script that loads the module with debug flags
- `psvr2_incremental_test.sh`: Test the module with incremental feature enabling
- `simple_test.c`: Minimal test module to verify your kernel module environment
- `psvr2_debug.h`: Debug infrastructure to include in your main module
- `psvr2_adapter_debug_template.c`: Template with examples of using debug macros

## Getting Started

1. First, test your environment with the simple test module:
   ```
   make -f Makefile.test
   sudo insmod simple_test.ko
   dmesg | tail
   sudo rmmod simple_test
   ```

2. Add the debug infrastructure to your module:
   - Copy `psvr2_debug.h` to your module directory
   - Add the module parameters and include the header in your main .c file
   - Add debug prints to key functions using the provided macros
   - Wrap feature-specific code in `FEATURE_ENABLED(...)` checks

3. Run the debugging scripts:
   - For general debugging: `./psvr2_debug.sh`
   - For step-by-step testing: `./psvr2_incremental_test.sh`

4. Check the generated logs in the `debug_logs` directory

## Debug Levels

- 0: None - No debug output
- 1: Error - Only errors
- 2: Warning - Errors and warnings
- 3: Info - General information (default)
- 4: Debug - Verbose debugging output

## Feature Flags

The `features_enabled` parameter is a bitmask that controls which parts of the driver are enabled:

- 0x0001: USB initialization
- 0x0002: Device initialization
- 0x0004: Input handling
- 0x0008: Output handling
- 0x0010: HID functionality
- 0x0020: Sensor handling
- 0x0040: Display handling
- 0x0080: Tracking
- 0x0100: Audio

Default is 0xFFFF (all features enabled).

## Example Usage

```bash
# Load with minimal functionality and verbose debug
sudo insmod psvr2_adapter.ko features_enabled=1 debug_level=4

# Load with only USB and device init
sudo insmod psvr2_adapter.ko features_enabled=3 debug_level=3
```

## Debugging the Kernel Panic

If you're experiencing a kernel panic when loading the module, follow these steps:

1. Start with the minimal test module to verify your build environment:
   ```bash
   cd debug
   make -f Makefile.test
   sudo insmod simple_test.ko
   dmesg | tail
   sudo rmmod simple_test
   ```

2. If the simple test works, make the following changes to your main module:
   - Copy the contents of `psvr2_debug.h` to a header file
   - Add the debug parameters to your main module file
   - Add feature flags to conditionally enable/disable parts of the code
   - Add debug prints at key points in your code

3. Use the incremental testing script to identify which component causes the crash:
   ```bash
   ./psvr2_incremental_test.sh
   ```

4. Check the logs in the `debug_logs` directory for:
   - NULL pointer dereferences
   - Stack traces
   - Error messages
   - Failed assertions

5. Common issues to check:
   - Memory allocation without proper error checking
   - USB endpoint access before proper initialization
   - Race conditions in device initialization
   - Incorrect ordering of operations
   - Missing mutex locks

## Creating a Fixed Version

Once you've identified the issue, make incremental changes:

1. Fix one issue at a time
2. Test each change thoroughly
3. Keep detailed notes about what was changed and why
4. Maintain a backup of the original code
5. Submit the fixes back to the project when complete