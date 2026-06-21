# PSVR2 Hardware & USB Topology

The Sony PSVR2 PC adapter presents the headset as a single USB device that
fans out into a DisplayPort sink (handled by the GPU's DRM driver) and a
multi-interface USB function (handled by this project's kernel module).

## USB identity

| Field      | Value            |
|------------|------------------|
| Vendor ID  | `0x054c` (Sony)  |
| Product ID | `0x0cde`         |
| Interfaces | 13               |

## Interface map

| IF  | Class            | Purpose                          | Owner                |
|-----|------------------|----------------------------------|----------------------|
| 0   | HID              | Control                          | usbhid (ep0 control used directly) |
| 1   | Audio Control    | Speaker/mic control              | snd-usb-audio        |
| 2   | Audio Streaming  | 48 kHz PCM speaker + mic         | snd-usb-audio        |
| 3   | Vendor (subcl 1) | **SLAM 6DoF pose ("SLA" packets)** | **psvr2 (this module)** |
| 4   | Vendor (subcl 2) | Data                             | future               |
| 5   | Vendor (subcl 3) | **Eye / gaze tracking**          | **psvr2 (this module)** |
| 6   | Vendor (subcl 4) | **Camera frames**                | **psvr2 (this module)** |
| 7   | Vendor (subcl 5) | **Status header + IMU (2 kHz)**  | **psvr2 (this module)** |
| 8   | Vendor (subcl 6) | LED detector                     | future               |
| 9   | Vendor (subcl 7) | Relocalizer                      | future               |
| 10  | Vendor (subcl 8) | Vendor data                      | future               |
| 11  | Vendor (subcl 9) | Data                             | future               |
| 12  | Vendor (subcl …) | Data                             | future               |

Audio works today with the in-tree `snd-usb-audio` driver and is intentionally
left untouched. The module matches **only the specific interfaces it implements**
(currently IF7 and IF3) so it never contends with audio or HID. The interfaces
share one reference-counted per-headset context.

## IF7 (status / IMU) endpoint

| Property        | Value                  |
|-----------------|------------------------|
| Alt setting     | 1                      |
| Endpoint        | `0x88` (interrupt IN)  |
| Transfer size   | 1024 bytes             |
| Contents        | 1× status header (32 B) + N× IMU record (24 B), ~41 records/transfer |
| IMU rate        | ~2000 Hz               |

## IF3 (SLAM) endpoint

| Property        | Value                  |
|-----------------|------------------------|
| Alt setting     | 0                      |
| Endpoint        | `0x83` (bulk IN)       |
| Transfer size   | 512-byte "SLA" records |
| Output          | `/dev/psvr2-pose` (pose sample stream) |

## IF6 (cameras) endpoint

| Property        | Value                  |
|-----------------|------------------------|
| Alt setting     | 0                      |
| Endpoint        | `0x87` (bulk IN)       |
| Transfer        | one frame per bulk transfer; length selects the mode |
| Output          | `/dev/videoN` (V4L2) — mode 1: 1280x640 `GREY` |

## IF5 (eye/gaze) endpoint

| Property        | Value                  |
|-----------------|------------------------|
| Alt setting     | 0                      |
| Endpoint        | `0x85` (bulk IN)       |
| Transfer        | "GS" gaze packets (~324 B used) |
| Keepalive       | re-send enable (report 0x0c) ~1/s |
| Output          | `/dev/psvr2-gaze` (gaze sample stream) |

See [protocol.md](protocol.md) for the byte-level record layouts.

## Display

The headset is a DisplayPort 1.4 sink (`4000x2040 @ 119.88`, needs DSC + FEC).
This is driven by the GPU DRM stack, not by this module. See
[display.md](display.md).

## Controllers

The PSVR2 Sense controllers are separate Bluetooth HID devices and enumerate on
Linux independently (as `/dev/input/js*`); they are out of scope for this module.
