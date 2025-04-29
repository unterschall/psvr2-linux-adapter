# USB Protocol Debugging

This document provides detailed guidance on capturing, analyzing, and debugging the USB protocol for the PSVR2 adapter.

## Table of Contents

1. [Understanding the PSVR2 USB Protocol](#understanding-the-psvr2-usb-protocol)
2. [USB Traffic Capture Tools](#usb-traffic-capture-tools)
3. [Protocol Analysis Techniques](#protocol-analysis-techniques)
4. [Manual USB Communication](#manual-usb-communication)
5. [Common USB Issues](#common-usb-issues)
6. [Advanced Protocol Reverse Engineering](#advanced-protocol-reverse-engineering)

## Understanding the PSVR2 USB Protocol

The Sony PSVR2 PC adapter uses USB for command, control, and data transfer. Understanding this protocol is essential for developing a functional Linux driver.

### USB Device Identification

The adapter uses the following identifiers:
- Vendor ID (VID): `0x054C` (Sony Corporation)
- Product ID (PID): `0x0CDE` (PlayStation VR2 PC Adapter)

### Interface Overview

The adapter exposes multiple USB interfaces:
- Interface 0: Primary control interface
- Interface 3: Data transfer interface

Each interface has specific endpoints for different communication purposes.

## USB Traffic Capture Tools

### Using the Project's Capture Script

The simplest way to capture USB traffic is using the provided script:

```bash
cd /home/user/Projects/psvr2-linux-adapter/tools
sudo ./capture-usb.sh my_capture.pcapng
```

This script automatically:
1. Loads the usbmon kernel module
2. Identifies appropriate USB interfaces
3. Captures traffic using tshark or tcpdump

### Manual USB Traffic Capture

For more control over the capture process:

```bash
# Load usbmon if not already loaded
sudo modprobe usbmon

# List USB buses to find the PSVR2 adapter
lsusb
# Look for "Sony Corp." with ID 054c:0cde

# Determine which USB bus the adapter is on (let's say bus 3)
ls -l /dev/bus/usb/003/

# Capture on that specific bus
sudo tshark -i usbmon3 -w psvr2_capture.pcapng

# Alternatively, use tcpdump
sudo tcpdump -i usbmon3 -w psvr2_capture.pcapng
```

### Continuous Monitoring

For longer debugging sessions:

```bash
# Stream USB traffic to stdout
sudo cat /sys/kernel/debug/usb/usbmon/3u | grep -A 5 -B 5 "054c 0cde"

# With timing information
sudo cat /sys/kernel/debug/usb/usbmon/3t | grep -A 5 -B 5 "054c 0cde"
```

## Protocol Analysis Techniques

### Analyzing Captures with Wireshark

1. **Open the capture file:**
   ```bash
   wireshark psvr2_capture.pcapng
   ```

2. **Apply useful filters:**
   ```
   # Filter by Sony VID/PID
   usb.idVendor == 0x054c && usb.idProduct == 0x0cde
   
   # Filter by specific endpoint
   usb.endpoint_address.number == 1
   
   # Filter by transfer type
   usb.transfer_type == 2  # Bulk
   usb.transfer_type == 3  # Interrupt
   
   # Filter by direction
   usb.endpoint_address.direction == IN
   usb.endpoint_address.direction == OUT
   
   # Filter by setup request
   usb.setup.bRequest == 0x09  # SET_REPORT
   usb.setup.bRequest == 0x01  # GET_REPORT
   ```

3. **Identify initialization sequence:**
   - Look for a series of control transfers when the device is first connected
   - Note the sequence of requests and responses
   - Identify recurring patterns that may indicate status polling

4. **Decode data payloads:**
   - In Wireshark, right-click the data field and select "Show Packet Bytes"
   - Analyze byte patterns to identify headers, commands, and data structures
   - Look for repeated patterns or fields that change predictably

### Creating a Protocol Map

As you identify different message types, document them in a structured format:

```
Command: PSVR2_GET_STATUS (0x01)
Direction: Host -> Device
Data Format:
  Byte 0: Report ID (0x01)
  Byte 1: Command code (0x01)
  Bytes 2-63: Reserved (0x00)
  
Response:
  Byte 0: Report ID (0x01)
  Byte 1: Command code (0x01)
  Byte 2: Status code
     0x00 = OK
     0x01 = Initializing
     0x02 = Error
  Byte 3: Error code (if status = 0x02)
  Bytes 4-63: Additional data
```

## Manual USB Communication

For testing isolated protocol components, use direct USB communication tools.

### Using libusb Tools

1. **Sending Control Transfers:**

   ```bash
   # Install libusb utilities if needed
   sudo apt install libusb-1.0-0-dev
   
   # Basic control transfer (example - adapt based on protocol analysis)
   sudo usb-ctrl -d 3:14 -s 0x054c:0x0cde -r 0x01 -v 0x0100 -i 0 -a 0x21 -l 64
   ```

2. **Using Python for rapid testing:**

   ```python
   #!/usr/bin/env python3
   import usb.core
   import usb.util
   
   # Find PSVR2 adapter
   dev = usb.core.find(idVendor=0x054c, idProduct=0x0cde)
   if dev is None:
       raise ValueError("Device not found")
   
   # Set configuration
   dev.set_configuration()
   
   # Get active configuration
   cfg = dev.get_active_configuration()
   
   # Get interface
   interface = cfg[(0,0)]
   
   # Example: Send HID SET_REPORT
   report = [0x01, 0x02, 0x00, 0x00] + [0x00] * 60  # 64 byte report
   dev.ctrl_transfer(
       bmRequestType=0x21,  # Host to device, class, interface
       bRequest=0x09,      # SET_REPORT
       wValue=0x0200,      # Report type and ID
       wIndex=0,           # Interface
       data_or_wLength=report
   )
   
   # Example: Read HID GET_REPORT
   result = dev.ctrl_transfer(
       bmRequestType=0xA1,  # Device to host, class, interface
       bRequest=0x01,      # GET_REPORT
       wValue=0x0100,      # Report type and ID
       wIndex=0,           # Interface
       data_or_wLength=64  # Length to read
   )
   
   print("Response:", [hex(b) for b in result])
   ```

### Kernel Module Test Mode

The project includes facilities for testing USB communication from the kernel module:

```bash
# Load module with test mode enabled
sudo insmod ./psvr2_adapter.ko debug_level=4 test_mode=1

# Interact with the device file to trigger test operations
sudo cat /dev/psvr2_test

# Monitor kernel messages for results
dmesg -w
```

## Common USB Issues

### Permission Problems

If you encounter permission issues:

```bash
# Check current permissions
ls -la /dev/bus/usb/003/014  # Adjust bus/device numbers accordingly

# Create a udev rule for persistent permissions
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="054c", ATTRS{idProduct}=="0cde", MODE="0666"' | sudo tee /etc/udev/rules.d/51-psvr2-adapter.rules

# Reload rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

### USB Power Management Issues

If the device disconnects unexpectedly:

```bash
# Disable autosuspend for the device
echo -1 | sudo tee /sys/bus/usb/devices/3-1/power/autosuspend_delay_ms

# Disable selective suspend for all USB devices
sudo bash -c 'echo on > /sys/bus/usb/devices/usb3/power/control'
```

### Bus Bandwidth Issues

If you encounter bandwidth problems:

```bash
# Check USB controller information
lsusb -t

# Move device to a dedicated controller if available
# (physically move to another USB port)

# Reduce competing traffic by disconnecting other USB devices
```

## Advanced Protocol Reverse Engineering

### Pattern Recognition

Look for repeated sequences that indicate:
- Periodic status reports
- Sensor data updates
- Command/response patterns
- Handshake sequences

### State Machine Analysis

Map out the device's state transitions:
1. Initialization sequence
2. Idle/ready state
3. Active operation states
4. Error handling
5. Shutdown sequence

Create a state diagram to visualize these transitions.

### Incremental Protocol Testing

Test protocol elements systematically:

1. **Start with basic identification:**
   - Device descriptor queries
   - Configuration setting

2. **Move to status commands:**
   - Device status
   - Feature report requests

3. **Progress to operational commands:**
   - Display settings
   - Tracking initialization
   - Sensor calibration

Document successful commands and responses thoroughly.

### Binary Analysis Tools

For complex payload structures:

```bash
# Convert binary data to hex dump
hexdump -C captured_data.bin

# Basic structure analysis
binwalk captured_data.bin

# Compare similar packets to find differences
cmp -l packet1.bin packet2.bin | gawk '{printf "%08X %02X %02X\n", $1, strtonum(0$2), strtonum(0$3)}'
```

---

This document provides a comprehensive framework for USB protocol debugging. As you discover more about the PSVR2 adapter's protocol, update this documentation to create a complete protocol reference for other developers.
