#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# Development loader: modprobe the dependency modules (raw insmod doesn't
# resolve them) and insert the freshly-built psvr2.ko. For a persistent,
# dependency-resolving install use ../install.sh (DKMS) + `modprobe psvr2`.
#
# Run as root:  sudo scripts/dev-load.sh [module params...]
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KO="${SCRIPT_DIR}/../kernel/psvr2.ko"
DEPS=(industrialio kfifo_buf videodev videobuf2-common videobuf2-v4l2 videobuf2-vmalloc)

[ "$(id -u)" -eq 0 ] || { echo "error: run as root (sudo $0)" >&2; exit 1; }
[ -f "${KO}" ] || { echo "error: ${KO} not found — run 'make -C kernel' first" >&2; exit 1; }

echo ">> loading dependencies"
for m in "${DEPS[@]}"; do
	if modprobe "${m}" 2>/dev/null; then
		echo "   ok:   ${m}"
	else
		echo "   FAIL: ${m}"
	fi
done

echo ">> insmod psvr2.ko"
if rmmod psvr2 2>/dev/null; then echo "   (unloaded previous psvr2)"; fi
if ! insmod "${KO}" "$@"; then
	echo "error: insmod failed; recent dmesg:" >&2
	dmesg | tail -8 >&2
	exit 1
fi

echo ">> loaded. status:"
lsmod | grep -E '^psvr2'
for d in /sys/bus/iio/devices/iio:device*; do
	[ -e "$d" ] && echo "   iio: $d -> $(cat "$d/name")"
done
for v in /sys/class/video4linux/video*; do
	[ -e "$v" ] && grep -q "PlayStation VR2" "$v/name" 2>/dev/null && \
		echo "   v4l2: /dev/$(basename "$v") -> $(cat "$v/name")"
done
ls -l /dev/psvr2-* 2>/dev/null
echo ">> recent dmesg:"
dmesg | grep -i psvr2 | tail -8
