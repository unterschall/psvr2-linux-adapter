# Installing the PSVR2 driver

Three ways to install, from quickest-to-try to most integrated. All need your
distro's **kernel headers** for the running kernel and a toolchain
(`gcc`/`make`).

## 1. Build and load manually (no install)

Good for development and a quick try:

```bash
make -C kernel
sudo insmod kernel/psvr2.ko
make -C userspace/tools          # build the smoke-test tools
```

Unload with `sudo rmmod psvr2`. Device-node permissions (IIO, input,
`/dev/psvr2-pose`, `/dev/psvr2-gaze`, `/dev/videoN`) need the udev rules from
the install paths below, or run the tools as root.

## 2. DKMS install (any distro)

Installs the module so it persists across reboots and rebuilds on kernel
upgrades, and installs the udev rules:

```bash
sudo ./install.sh        # dkms add/build/install + udev rules
sudo modprobe psvr2
```

Remove with `sudo ./uninstall.sh`. Requires the `dkms` package.

## 3. Arch package

```bash
cd packaging
makepkg -si
```

This builds `psvr2-dkms`; the `dkms` pacman hooks compile the module on install
and rebuild it on every kernel upgrade. Udev rules are installed to
`/usr/lib/udev/rules.d/`.

## Display (separate from the module)

The headset's panel is driven by your GPU's DRM stack, not this module. On AMD
you likely need the DSC/FEC EDID quirk:

```bash
patches/apply-amdgpu-dsc.sh /path/to/linux   # then rebuild amdgpu/kernel
```

Then bring the panel up (X11):

```bash
userspace/tools/psvr2-display-up.sh
```

See [display.md](display.md) for AMD/NVIDIA/Wayland details.

## Verifying

```bash
dmesg | grep psvr2                   # probe messages per interface
lsusb -t                             # audio still on snd-usb-audio
ls /sys/bus/iio/devices/             # iio:deviceN named "psvr2_imu"
ls /dev/psvr2-* /dev/video*          # pose/gaze char devs + camera node
```

Then run the tools in `userspace/tools/` (`psvr2-imu-test`, `psvr2-pose-test`,
`psvr2-gaze-test`) or standard utilities (`evtest`, `v4l2-ctl`, `iio_readdev`).

## libpsvr2 (optional)

A C library wrapping the device nodes for application/runtime use:

```bash
make -C userspace/lib                 # builds libpsvr2.a/.so + psvr2-monitor
sudo make -C userspace/lib install    # installs header, libs, and pkg-config
```

Then build against it with `pkg-config --cflags --libs libpsvr2`. See
`userspace/lib/psvr2-monitor.c` for a worked example.
