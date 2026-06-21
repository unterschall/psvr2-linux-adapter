# Display Enablement

The PSVR2 panel is a standard DisplayPort 1.4 sink — **not** driven by this
project's kernel module. It is brought up by the GPU's DRM/KMS driver. The panel
runs at `4000x2040 @ 119.88 Hz` and requires **DSC** (Display Stream Compression)
and **FEC**, which some drivers do not enable for this sink without a quirk.

This is a separate track from the USB module: the IMU/buttons module in
`kernel/` works regardless of whether the display is lit (though see the open
question about whether the IMU streams before the DP link is up — noted in
[roadmap.md](roadmap.md)).

## AMD (amdgpu)

`patches/amdgpu-psvr2-dsc-fec.patch` forces DSC 1.1 + FEC capabilities for the
PSVR2 EDID (manufacturer `0xD94D`, products `0xA205`/`0xC207`, plus the generic
MTK EDID `0x8B36:0x3612`). Without it, amdgpu may mis-detect the EDID and bring
the headset up at the wrong resolution.

Apply against a kernel source tree and rebuild amdgpu (or the kernel). The
helper script applies it (with a dry-run check first) to a git or tarball tree:

```
patches/apply-amdgpu-dsc.sh /path/to/linux
# then rebuild amdgpu / the kernel and reboot
```

## Bringing the panel up (X11)

The headset enumerates as a DRM non-desktop / lease device, so a normal desktop
won't draw to it. Under X11, promote it to a normal output with the helper
(it auto-detects the PSVR2 connector by its `4000x2040` mode):

```
userspace/tools/psvr2-display-up.sh          # enable as a normal display
userspace/tools/psvr2-display-up.sh --off    # hand it back as a lease output
```

Equivalent manual steps (replace `DP-N` with whatever connector the headset
enumerates as — the one advertising the `4000x2040` mode):

```
xrandr --output DP-N --set non-desktop 0
xrandr --output DP-N --auto
```

Under **Wayland** this is compositor-specific (the connector stays a
non-desktop/lease output for VR runtimes); the helper does not handle it.

## NVIDIA

NVIDIA GPUs are largely untested for this sink. The proprietary driver handles
DSC internally; `nouveau` DSC support is unverified here. This needs hardware
testing — tracked in [roadmap.md](roadmap.md).

## Intel (reference)

Community reports indicate Intel Alder Lake iGPUs bring the 4K VR mode up
automatically over a USB-C→DP path, which has been one of the more reliable
display paths so far.
