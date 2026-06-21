# PSVR2 for Linux

An open project to bring the Sony **PlayStation VR2** to Linux: a kernel module
that exposes the headset's sensors and cameras through standard Linux subsystems,
a small userspace library over those device nodes, and a **SteamVR driver** that
presents the headset as a native VR HMD. It targets the official Sony PSVR2 PC
adapter.

Contributions are very welcome — see [Contributing](#contributing).

> Not affiliated with or endorsed by Sony Interactive Entertainment.

## What it does

The `psvr2` kernel module binds the headset's vendor interfaces and exposes:

- **IMU** (accelerometer + gyroscope, ~2 kHz) via the **IIO** subsystem, with
  correct `*_scale` attributes (m/s² and rad/s).
- **Function button**, **proximity sensor** (worn/removed) and **IPD dial**
  (59–72 mm) via the **input** subsystem.
- **6DoF pose** from the headset's onboard tracker as a sample stream on the
  **`/dev/psvr2-pose`** character device (`read()` + `poll()`; ABI in
  `kernel/psvr2_uapi.h`).
- **Eye/gaze tracking** (per-eye + combined gaze point/direction, pupil
  diameter, blink) on the **`/dev/psvr2-gaze`** character device.
- **Tracking cameras** as a **V4L2** capture device (`/dev/videoN`).
- **Panel brightness** via **sysfs**, and **debugfs** raw-frame dumps for
  protocol work.

On top of that:

- **`libpsvr2`** — a small C library over the device nodes (6DoF pose and gaze
  streams, scaled IMU, camera-node path, brightness) for building VR-runtime
  integrations.
- **SteamVR driver** (`steamvr/driver_psvr2`) — presents the PSVR2 as a native
  SteamVR HMD with direct-mode display (via DRM leasing) and 6DoF head tracking.
  See [docs/steamvr.md](docs/steamvr.md).

Audio is handled by the in-tree `snd-usb-audio` driver and left untouched. The
display is a standard DisplayPort sink driven by the GPU's DRM stack (see
[docs/display.md](docs/display.md)).

See [docs/roadmap.md](docs/roadmap.md) for current status and planned work.

## Architecture

A thin kernel module integrates device data into standard Linux subsystems;
heavier parsing (cameras, tracking, eye tracking) and runtime integration live in
userspace. The module binds **only** the vendor interfaces it implements, so it
never contends with `snd-usb-audio` or `usbhid`.

| Path                | Owner                                          |
|---------------------|------------------------------------------------|
| Display (DP 1.4)    | GPU DRM driver (+ DSC/FEC quirk in `patches/`)  |
| Audio               | `snd-usb-audio` (unchanged)                     |
| IMU / buttons       | **this module** → IIO + input                   |
| 6DoF pose           | **this module** → `/dev/psvr2-pose`             |
| Cameras             | **this module** → V4L2 `/dev/videoN`            |
| Eye / gaze tracking | **this module** → `/dev/psvr2-gaze`             |
| VR runtime          | **`libpsvr2`** → SteamVR driver / OpenXR shims  |

See [docs/hardware.md](docs/hardware.md) and [docs/protocol.md](docs/protocol.md).

## Build & load

```bash
# Kernel module (needs linux-headers for your running kernel)
make -C kernel
sudo insmod kernel/psvr2.ko          # or install via DKMS, see below

# Userspace smoke tests
make -C userspace/tools
```

Connect the headset via the Sony PC adapter, then:

```bash
dmesg | grep psvr2                   # probe messages
lsusb -t                             # confirm audio still on snd-usb-audio
ls /sys/bus/iio/devices/             # an iio:deviceN named "psvr2_imu"
sudo evtest                          # pick "PlayStation VR2 Headset Controls"
userspace/tools/psvr2-imu-test       # live scaled IMU + control events
userspace/tools/psvr2-pose-test      # live 6DoF pose from /dev/psvr2-pose
userspace/tools/psvr2-gaze-test      # live eye-tracking from /dev/psvr2-gaze

v4l2-ctl --list-devices              # find the "PlayStation VR2 Cameras" node
v4l2-ctl -d /dev/videoN --stream-mmap --stream-count=30 --stream-to=frames.grey
# or view live: gst-launch-1.0 v4l2src device=/dev/videoN ! videoconvert ! autovideosink
```

High-rate buffered IMU capture works with `libiio` (`iio_readdev`) against the
`psvr2_imu` device.

### Install (persistent)

```bash
sudo ./install.sh                    # DKMS build/install + udev rules
# Arch: cd packaging && makepkg -si  # builds the psvr2-dkms package
```

`sudo ./uninstall.sh` reverses it. See [docs/install.md](docs/install.md) for all
install paths and [docs/display.md](docs/display.md) for the display details.

## Use as a SteamVR headset

The SteamVR driver builds against the OpenVR SDK and links `libpsvr2`:

```bash
steamvr/install-steamvr-driver.sh    # build + register with SteamVR
```

Then start SteamVR. See [docs/steamvr.md](docs/steamvr.md) for how direct-mode
display works, build details, and current limitations.

## Project layout

```
kernel/          psvr2.ko sources, Makefile, dkms.conf, udev rule
userspace/lib/   libpsvr2 (C API over the device nodes) + psvr2-monitor example
userspace/tools/ smoke tests + display bring-up helpers
steamvr/         SteamVR / OpenVR driver (driver_psvr2) + installer
docs/            install, hardware, protocol, display, steamvr, references, roadmap
patches/         amdgpu DSC/FEC EDID quirk (+ apply script) for the display path
packaging/       Arch PKGBUILD (psvr2-dkms)
install.sh       DKMS install / uninstall.sh to reverse
```

## Contributing

This is an early-stage, community-oriented project — issues, fixes, and new
capabilities are all welcome. Good areas to help with are listed in
[docs/roadmap.md](docs/roadmap.md) (additional camera modes, haptics, lens
distortion/FOV for the SteamVR driver, Wayland display acquisition, OpenXR/Monado
integration, and testing across GPUs and distros). The protocol notes in
[docs/protocol.md](docs/protocol.md) and the acknowledgments in
[docs/references.md](docs/references.md) are good starting points.

## Acknowledgments

Built on substantial public prior art (Monado, Sony's PSVR2 OSS release, the
OpenVR SDK, and community reverse-engineering work) and developed with the
assistance of **Claude** (Anthropic). Details and credits in
[docs/references.md](docs/references.md).

## License

GPL-2.0. See [COPYING](COPYING), and [docs/references.md](docs/references.md) for
acknowledgments and the provenance of reused protocol definitions.
