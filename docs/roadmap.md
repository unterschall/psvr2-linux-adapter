# Status & roadmap

The goal is to expose as much PSVR2 functionality on Linux as possible, with a
hybrid design: a thin kernel module integrates device data into standard Linux
subsystems (IIO, input, V4L2), and userspace handles the heavier work (`libpsvr2`
and the SteamVR driver).

## What works today

### Kernel module (`psvr2.ko`)

- **IMU** — accelerometer + gyroscope at ~2 kHz via **IIO**, with correct
  `*_scale` (m/s² and rad/s).
- **Controls** — function button, proximity (worn/removed) and the IPD dial via
  the **input** subsystem.
- **6DoF pose** — the headset's onboard tracker as a `struct psvr2_pose_sample`
  stream on **`/dev/psvr2-pose`** (`read()` + `poll()`; ABI in
  `kernel/psvr2_uapi.h`).
- **Eye/gaze** — per-eye and combined gaze point/direction, pupil diameter and
  blink on **`/dev/psvr2-gaze`** (stream kept alive automatically).
- **Cameras** — the two bottom tracking cameras as a **V4L2** capture device
  (`/dev/videoN`, 1280×640 grayscale stereo).
- **Brightness** via **sysfs**, and **debugfs** raw-frame dumps
  (`raw_status`, `raw_slam`, `raw_gaze`) for protocol work.

The module binds only the vendor interfaces it implements, so `snd-usb-audio`
and `usbhid` keep their interfaces.

### Userspace

- **`libpsvr2`** — a small C API over the device nodes: device discovery,
  float-converted 6DoF pose and gaze streams (with `poll()` fds), the latest
  scaled IMU sample, the camera-node path, and brightness control.
- **SteamVR / OpenVR driver** (`steamvr/driver_psvr2`) — the PSVR2 as a native
  SteamVR HMD: **direct-mode display** (the runtime DRM-leases the headset
  connector and lights the panel while the desktop keeps running) and **6DoF head
  tracking** in the correct OpenVR frame. Per-eye FOV, lens distortion, and
  in-headset spatial calibration are still being refined — see
  [steamvr.md](steamvr.md).

## Roadmap

Contributions in any of these areas are welcome.

### Kernel / protocol

- **More camera modes** — the interleaved multi-view (controller-tracking /
  fisheye) modes and the SLAM tracking-camera frames, with a V4L2 control to
  select between them.
- **Haptics** — the headset rumble report is not yet reverse-engineered.
- **Multi-headset support** — per-device node naming (the `/dev/psvr2-*` nodes
  currently assume a single headset).
- **Wider gaze ABI** — several gaze packet fields are still undecoded.

### SteamVR driver

- **Lens distortion mesh** and accurate **per-eye FOV** (currently placeholders).
- **Driver-side recenter / default standing height** so a usable pose comes up
  without running SteamVR Room Setup.
- **Confirm Wayland** direct-mode display acquisition (DRM leasing via
  `wp_drm_lease_device_v1`; not yet validated, see steamvr.md).
- **Eye-tracking** exposure and **controller** support.

### Other runtimes

- **OpenXR / Monado** and **OpenHMD** integrations layered on `libpsvr2`.

## Display

The 4000×2040 panel is a standard DisplayPort (DSC) sink driven by the GPU's DRM
stack — not by this module. A VR runtime acquires it directly via **DRM leasing**
(see [steamvr.md](steamvr.md)); the panel can also be brought up as an ordinary
output for development (see [display.md](display.md)). An amdgpu DSC/FEC EDID
quirk is carried in `patches/` for setups that need it.

## Audio

Handled by the in-tree `snd-usb-audio` driver; this module deliberately leaves
the audio interfaces alone.
