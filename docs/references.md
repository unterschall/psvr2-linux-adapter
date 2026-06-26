# Acknowledgments & provenance

This project stands on a lot of public prior art. The work below made it
possible — our thanks to everyone involved. Material that was reused or directly
informed by a source is credited here and in per-file SPDX/copyright headers.

These projects were used as references during development; they are not
redistributed here (the local `references/` working copies are not part of this
repository).

## Sources

- **Monado** and its PSVR2 reverse-engineering work
  (<https://gitlab.freedesktop.org/monado/monado>, `SPDX: BSL-1.0`). The
  definitive host-side reverse engineering — the source of the USB
  interface/endpoint map, the control/status/IMU record layouts, the IMU scale
  factors, the ep0 vendor-control semantics, and the 6DoF pose frame convention
  this driver adopts. BSL-1.0 is permissive and compatible with this GPL-2.0
  project.

- **Sony's PSVR2 OSS release** — Sony's published GPL-2.0 source for the
  headset-internal MediaTek SoC (`SPDX: GPL-2.0`). This is **device-side** code
  (it does not run on the host), but it corroborates the IMU record layout, the
  USB control-event enums, the panel EDID, and the display pipeline (dprx/dsc/dsi).

- **Community PSVR2 reverse-engineering notes** — USB descriptor dumps, the
  interface map, controller IDs and FOV parameters, and the amdgpu DSC/FEC EDID
  quirk (carried here as `patches/amdgpu-psvr2-dsc-fec.patch`).

- **OpenVR SDK** — Valve (<https://github.com/ValveSoftware/openvr>,
  `SPDX: BSD-3-Clause`). The SteamVR driver is built against its
  `openvr_driver.h` and modelled on the SDK's sample HMD driver. The needed
  files (`openvr_driver.h` and the sample `driverlog` helper, SDK v2.15.6) are
  **vendored** under `steamvr/driver_psvr2/third_party/openvr/` with the OpenVR
  `LICENSE` retained, so the driver builds from a clean checkout. See that
  directory's README.

- **VR on Linux wiki** — <https://wiki.vronlinux.org/docs/hardware/psvr2/>.

## Embedded font

`userspace/tools/font8x8.h` is the Linux kernel's 8×8 CP437 console font
(`lib/fonts/font_8x8.c`, GPL-2.0), used by the on-headset display helpers.
GPL-2.0, compatible with this project.

## Licensing

This project is **GPL-2.0** (see [COPYING](../COPYING)). Where wire-protocol
structures were ported from Monado (BSL-1.0), the original copyright lines are
preserved in the relevant file headers (e.g. `kernel/psvr2_protocol.h`).

## Development

This project was developed with the assistance of **Claude** (Anthropic), via
Claude Code.

## Not affiliated with Sony

"PlayStation" and "PSVR" are trademarks of Sony Interactive Entertainment Inc.
This is an independent project, not endorsed by or affiliated with Sony.
