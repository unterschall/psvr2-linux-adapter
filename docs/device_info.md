# PSVR2 Adapter Device Information

## Device Identification

The Sony PSVR2 PC adapter has been identified with the following parameters:

- **Vendor ID (VID)**: `0x054C` (Sony Corp.)
- **Product ID (PID)**: `0x0CDE` (PlayStation VR2)
- **USB String**: `Bus 004 Device 002: ID 054c:0cde Sony Corp. PlayStation®VR2`

## USB Interface

The adapter presents itself as a USB device with multiple interfaces. The exact interface configuration will be determined through USB protocol analysis. Known interfaces include:

- Control interface for device configuration
- Data interface for tracking information
- Interface for Display/Video data

## Connection Information

The PSVR2 adapter has three external connections:

1. **DisplayPort** - Input from PC graphics card
2. **USB-A** - Control and data connection to PC
3. **Power Input** - External power source
4. **USB-C** - Connection to the PSVR2 headset

## Protocol Analysis

Detailed protocol analysis is needed to understand how to:

1. Initialize the adapter
2. Configure display modes
3. Receive tracking data
4. Handle controller inputs

## Udev Rules

To allow non-root access to the PSVR2 adapter, the following udev rule can be used:

```
SUBSYSTEM=="usb", ATTRS{idVendor}=="054c", ATTRS{idProduct}=="0cde", MODE="0666", GROUP="plugdev"
```

## Detection

You can check if your PSVR2 adapter is detected properly with the following command:

```bash
lsusb -d 054c:0cde
```

This should show the adapter if it's connected and recognized by your system.

## Additional Information

The adapter likely uses the VirtualLink protocol internally, which combines:
- DisplayPort
- USB data
- Power delivery

over a single connection to the headset.
