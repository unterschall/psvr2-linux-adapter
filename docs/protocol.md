
# PSVR2 Adapter Protocol Documentation

This document outlines what we currently know about the Sony PSVR2 PC adapter's protocol and what still needs to be investigated.

## Protocol Overview

The Sony PSVR2 PC adapter interfaces between a PC and the PSVR2 headset using:
- DisplayPort 1.4 for video
- USB for control signals and tracking data
- External power for the headset

## Investigation Priorities

### 1. Adapter USB Identification

**Status:** 🔍 To Be Investigated

We need to identify:
- Vendor ID (likely Sony's 0x054C)
- Product ID (needs to be determined)
- USB Interface descriptors
- Endpoint configuration

**Investigation Method:**
- Use `lsusb -v` to capture detailed USB descriptor information
- Check dmesg output when connecting the adapter

### 2. USB Control Channel

**Status:** 🔍 To Be Investigated

The adapter likely uses USB control transfers for:
- Initialization sequence
- Mode switching
- Status queries

**Investigation Method:**
- Capture USB traffic during adapter initialization
- Identify control messages and their parameters
- Document request types, values, and indexes

### 3. Tracking Data Format

**Status:** 🔍 To Be Investigated

The PSVR2 sends tracking data including:
- Accelerometer readings
- Gyroscope readings
- Timestamps
- Status information

**Investigation Method:**
- Capture USB interrupt transfers during active use
- Analyze packet structure
- Correlate with known IMU data formats from `sieimu` module

### 4. DisplayPort Handling

**Status:** 🔍 To Be Investigated

The adapter needs to:
- Accept DisplayPort 1.4 input
- Possibly modify EDID information
- Handle display mode switching
- Support headset display refresh rates (90Hz/120Hz)

**Investigation Method:**
- Capture EDID information presented by the adapter
- Examine supported display modes
- Test various refresh rates and resolutions

### 5. Initialization Sequence

**Status:** 🔍 To Be Investigated

The full startup sequence needs to be documented:
- Power-on sequence
- USB device enumeration
- DisplayPort link training
- Headset initialization commands

**Investigation Method:**
- Capture complete USB traffic from power-on
- Document order of operations
- Identify critical initialization commands

## Reference: Known USB Commands

Based on the existing PSVR2 kernel modules, we can infer these potential commands:

| Command ID | Name               | Description                   | Parameters        |
|------------|--------------------|------------------------------ |-------------------|
| 0x01       | GET_STATUS         | Query device status           | None              |
| 0x02       | SET_DISPLAY_MODE   | Set resolution/refresh rate   | Width, Height, Hz |
| 0x03       | GET_VERSION        | Get firmware version          | None              |
| TBD        | START_TRACKING     | Enable motion tracking        | TBD               |
| TBD        | STOP_TRACKING      | Disable motion tracking       | TBD               |

**Note:** These are speculative based on similar devices and need to be verified.

## Reference: Endpoint Usage

Expected USB endpoint configuration:

| Endpoint | Direction | Type      | Usage                        |
|----------|-----------|-----------|------------------------------|
| 0x00     | Out       | Control   | Control messages             |
| 0x81     | In        | Interrupt | Tracking data                |
| 0x02     | Out       | Bulk      | Display configuration        |
| TBD      | TBD       | TBD       | Additional functions         |

## Protocol Analysis Tools

Useful tools for protocol analysis:

1. **USB Capture:**
   ```bash
   sudo modprobe usbmon
   sudo wireshark -i usbmon1
   ```

2. **Display Analysis:**
   ```bash
   xrandr --verbose
   edid-decode < /sys/class/drm/card0-DP-1/edid
   ```

3. **Custom Tools:**
   - See `tools/capture/` directory for specialized tools

## Next Steps

1. Connect the adapter and capture initial USB enumeration
2. Document all discovered endpoints and descriptors
3. Trace initialization sequence and document commands
4. Test display connection and document EDID information
5. Capture tracking data and analyze format
