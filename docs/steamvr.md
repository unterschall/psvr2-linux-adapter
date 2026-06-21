# SteamVR / OpenVR driver (`driver_psvr2`)

A SteamVR driver that makes the PSVR2 appear as a native HMD. SteamVR (`vrserver`)
is Valve's runtime — this is a driver plugin it loads, not a replacement. The
driver is pure userspace and consumes the kernel module's device nodes through
**libpsvr2**; it needs no kernel changes.

## Status

- **Display** — direct mode via DRM leasing: the runtime leases the headset's
  display connector and lights the 4000×2040 panel while the desktop keeps
  running on your other monitors.
- **Head tracking** — 6DoF pose from the headset's onboard tracker, delivered in
  the correct OpenVR frame.
- **In progress** — accurate per-eye FOV and a lens **distortion mesh** are
  placeholders today (the image is visible and tracks, but without lens
  correction); in-headset spatial calibration and eye-tracking/controller
  exposure are not done yet. See [Known limitations](#known-limitations).

## Build & install

```sh
# Builds libpsvr2 + the driver and registers it with SteamVR (via a symlink).
steamvr/install-steamvr-driver.sh
# Remove it again:
steamvr/install-steamvr-driver.sh --uninstall
```

The script auto-detects the SteamVR `drivers/` directory; override with
`STEAMVR_DRIVERS=/path/to/SteamVR/drivers`. Because it registers the build output
by symlink, rebuilding (`cmake --build steamvr/driver_psvr2/build`) is enough to
pick up changes — no reinstall needed.

The driver is built against the OpenVR SDK's `openvr_driver.h` and links
`libpsvr2` statically, so the resulting `.so` is self-contained.

## How direct mode works

For a standard DisplayPort-connected HMD, the driver does **not** do the leasing
or the rendering itself — SteamVR's compositor does. In particular it does **not**
implement `IVRDriverDirectModeComponent` (that interface makes a driver own GPU
swapchain presentation, and is meant for wireless or proprietary display
pipelines). Instead, direct mode is enabled by **describing the display
correctly** so `vrcompositor` finds and DRM-leases the connector itself:

- `IVRDisplayComponent::IsDisplayRealDisplay()` → `true`
- `IVRDisplayComponent::IsDisplayOnDesktop()` → `false` (the trigger for direct
  acquisition; `true` would mean an extended-desktop monitor)
- the panel's **EDID** vendor/product identity, so the compositor can match and
  lease the right connector

This makes the headset's "non-desktop" display connector — the one a desktop
compositor normally hides — available to the runtime, which leases it while your
desktop session keeps running. It is controlled by the `direct_mode` setting
(default `true`) in `default.vrsettings`; setting it `false` selects an
extended-desktop fallback instead.

## Components

```
driver_psvr2/
  src/
    hmd_driver_factory.cpp   HmdDriverFactory — the entry point vrserver loads
    device_provider.{h,cpp}  IServerTrackedDeviceProvider (lifecycle, events)
    hmd_device_driver.{h,cpp}ITrackedDeviceServerDriver + IVRDisplayComponent
    pose_source.{h,cpp}      libpsvr2 wrapper + device→OpenVR pose remap
  resources/
    driver.vrdrivermanifest
    settings/default.vrsettings
```

At runtime the device provider opens `libpsvr2` and registers the HMD; a pose
thread reads 6DoF samples, remaps them into the OpenVR frame, and submits them via
`TrackedDevicePoseUpdated()`. The display component reports the panel geometry
(4000×2040 split into two eyes) and its EDID identity.

## Known limitations

- **Multi-GPU systems.** If the headset is wired to a different GPU than the one
  SteamVR renders on, the compositor can fail to acquire the display. Point
  SteamVR's Vulkan at the GPU that drives the headset — e.g. for an AMD/RADV
  headset GPU, launch Steam with
  `VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json` (or set it in the
  SteamVR launch options).
- **Session type.** Direct-mode display acquisition has been validated under
  **X11**. **Wayland** is not yet verified: DRM leasing on Wayland (via
  `wp_drm_lease_device_v1`, supported by KWin and Mutter) is the intended path,
  but it has not been tested together with the multi-GPU workaround above.
- **FOV / distortion.** These are placeholders, so the image is geometrically
  approximate and has no lens correction yet.
- **Spatial calibration.** Floor height and forward direction come from SteamVR's
  usual calibration (Reset Seated Position / Room Setup). Driver-side
  auto-recenter and a default standing height are planned so this works out of the
  box.
