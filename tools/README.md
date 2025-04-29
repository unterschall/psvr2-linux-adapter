# PSVR2 Adapter Development Tools

This directory contains tools and utilities to help with developing the PSVR2 Linux adapter driver.

## Available Tools

### capture-usb.sh
Captures USB traffic between the PC and the PSVR2 adapter for protocol analysis.

Usage:
```bash
sudo ./capture-usb.sh [output_file]
```

This script:
- Loads the usbmon kernel module if needed
- Initiates a USB traffic capture using tshark or tcpdump
- Saves the capture to a file for analysis in Wireshark

### find-device.sh
Helps identify the VID:PID of the PSVR2 adapter.

Usage:
```bash
./find-device.sh [search_term]
```

This script:
- Searches for USB devices matching the given search term (default: "Sony")
- Displays all matching devices with their VID:PID
- Works on systems with or without lsusb

## Installation

The setup script in the main directory will make these scripts executable and install any necessary dependencies.

## Wireshark Analysis Tips

When analyzing USB traffic with Wireshark:

1. Open the captured file
2. Apply a filter for specific USB endpoints: `usb.endpoint_address == 0x81`
3. Look for control transfers: `usb.transfer_type == 0x02`
4. Examine the data fields for patterns

For detailed USB protocol analysis, consider using the Wireshark USB dissector:
1. Right-click on a packet
2. Select "Decode As..."
3. Choose the appropriate protocol

## Additional Resources

For more in-depth USB protocol analysis:
- USBPcap: https://desowin.org/usbpcap/
- USBlyzer documentation
- USB specification documents
