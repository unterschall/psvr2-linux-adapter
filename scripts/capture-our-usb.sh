#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# Capture OUR psvr2 module's full USB init under usbmon: arm usbmon first, then
# (re)load the module so the probe-time SET_INTERFACE sweep + vendor reads are
# captured, then briefly stream the camera so the STREAMON camera-mode SET is
# captured too. The result is directly comparable to the fork capture from
# scripts/capture-fork-usb.sh — diff them with scripts/diff-usb-init.sh.
#
# Run as root (does NOT need the display up; we only want the control transfers):
#   sudo scripts/capture-our-usb.sh [camera_mode]
#   e.g. sudo scripts/capture-our-usb.sh 0x10
set -u

CAM_MODE="${1:-0x10}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT=/tmp/our-usbmon.txt
FORK=/tmp/fork-usbmon.txt

die() { echo "error: $*" >&2; exit 1; }
[ "$(id -u)" -eq 0 ] || die "run as root (sudo $0)"

# Headset bus (strip leading zeros), same detection as capture-fork-usb.sh.
busstr=$(lsusb | awk '/054c:0cde/{print $2; exit}')
[ -n "$busstr" ] || die "PSVR2 (054c:0cde) not found on USB"
BUS=$((10#$busstr))
echo ">> headset on bus $BUS"

# usbmon must be armed BEFORE insmod so the probe-time init is captured.
modprobe usbmon || die "modprobe usbmon failed"
NODE="/sys/kernel/debug/usb/usbmon/${BUS}u"
[ -e "$NODE" ] || die "usbmon node $NODE missing (debugfs not mounted?)"

# Start from a clean slate so probe runs (and is captured) fresh.
if rmmod psvr2 2>/dev/null; then echo ">> unloaded existing psvr2"; fi

echo ">> capturing $NODE -> $OUT"
cat "$NODE" > "$OUT" &
CAP=$!
sleep 1

echo ">> loading module (probe-time init)"
"${SCRIPT_DIR}/dev-load.sh" >/dev/null 2>&1 || die "dev-load.sh failed"
sleep 2

# Trigger the STREAMON camera-mode SET. No display needed — the control transfer
# goes out regardless of whether the headset then delivers frames; timeout keeps
# us from blocking if it only sends ZLPs.
echo "$CAM_MODE" > /sys/module/psvr2/parameters/camera_mode 2>/dev/null
VID=""
for v in /sys/class/video4linux/video*; do
	[ -e "$v" ] || continue
	grep -q "PlayStation VR2" "$v/name" 2>/dev/null && { VID="/dev/$(basename "$v")"; break; }
done
if [ -n "$VID" ]; then
	echo ">> STREAMON on $VID (camera_mode=$CAM_MODE)"
	timeout 4 v4l2-ctl -d "$VID" --stream-mmap --stream-count=1 >/dev/null 2>&1
else
	echo "!! PSVR2 V4L2 node not found — skipping STREAMON (no camera-mode SET captured)"
fi
sleep 1

kill "$CAP" 2>/dev/null
chmod 0644 "$OUT" 2>/dev/null
echo ">> $(wc -l < "$OUT") usbmon lines -> $OUT"
echo

if [ -f "$FORK" ]; then
	echo ">> diffing against fork capture ($FORK):"
	"${SCRIPT_DIR}/diff-usb-init.sh" "$FORK" "$OUT"
else
	echo ">> fork capture $FORK not present; run scripts/capture-fork-usb.sh first,"
	echo "   then: scripts/diff-usb-init.sh $FORK $OUT"
fi
