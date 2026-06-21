#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# Drive a video mode onto the PSVR2's DisplayPort connector via a proper atomic
# KMS commit (psvr2-kms-modeset, which lets amdgpu engage DSC for the 4K panel),
# then probe the optical streams (SLAM / gaze / camera) while the mode is held.
#
# MUST be run from a text console (Ctrl+Alt+F3), NOT the Wayland/X session, so
# the helper can acquire DRM master. Run as root.
#
#   sudo scripts/drive-display.sh [mode] [hold_seconds] [camera_mode]
#   e.g. sudo scripts/drive-display.sh 4000x2040
#        sudo scripts/drive-display.sh 1920x1080
#        sudo scripts/drive-display.sh 4000x2040 45 0x10
#
# camera_mode accepts kernel base-0 syntax: use 0x10 (or 16) for BC4, NOT 10
# (which the kernel parses as decimal 10 == 0xa, interleaved T/B).
set -u

MODE="${1:-4000x2040}"
HOLD="${2:-45}"
CAM_MODE="${3:-0x10}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HELPER="${SCRIPT_DIR}/../userspace/tools/psvr2-kms-modeset"

[ "$(id -u)" -eq 0 ] || { echo "run as root, from a TTY (Ctrl+Alt+F3)"; exit 1; }

LOG=/tmp/psvr2-display-test.log
exec > >(tee "$LOG") 2>&1
echo ">> logging to $LOG"

# Build the helper if needed.
if [ ! -x "$HELPER" ]; then
	echo ">> building $HELPER"
	make -C "${SCRIPT_DIR}/../userspace/tools" psvr2-kms-modeset || {
		echo "build failed (need libdrm + headers)"; exit 1; }
fi

echo ">> driving mode $MODE for ${HOLD}s"
"$HELPER" "$MODE" "$HOLD" >/tmp/psvr2-kms.log 2>&1 &
MT=$!
sleep 9

if ! kill -0 "$MT" 2>/dev/null; then
	echo "!! modeset helper exited early — mode not set:"
	cat /tmp/psvr2-kms.log
	exit 1
fi
echo ">> mode held (pid $MT):"; grep -i "commit\|mode\|connector\|crtc" /tmp/psvr2-kms.log

echo "== IF7 header ([0]=dprx [1]=prox [2]=btn [5]=ipd) =="
od -A d -t u1 -N 6 /sys/kernel/debug/psvr2/raw_status
echo "== gaze (expect G S) =="
od -A d -c -N 8 /sys/kernel/debug/psvr2/raw_gaze

# Set the camera mode the kernel will apply at STREAMON (read fresh each time in
# psvr2_cam_start_streaming), then read it back to prove the on-the-wire value.
# The sysfs store parses base-0, so 0x10 and 16 both give 0x10; 10 gives 0xa.
echo "== setting camera mode $CAM_MODE =="
echo "$CAM_MODE" > /sys/module/psvr2/parameters/camera_mode || {
	echo "!! failed to write camera_mode (is psvr2 loaded?)"; }
CAM_MODE_NOW="$(cat /sys/module/psvr2/parameters/camera_mode)"
printf "   camera_mode now = %s (0x%x)\n" "$CAM_MODE_NOW" "$CAM_MODE_NOW"
RAW="/tmp/psvr2-cam-mode-0x$(printf '%x' "$CAM_MODE_NOW").raw"

# Arm usbmon on the headset's bus BEFORE STREAMON so we capture the actual
# vendor control transfer that carries the mode (passive — our module stays
# bound). The camera-mode SET is bRequest 0x09, bmRequestType 0x42, payload
# "0b000100 08000000 01000000 <mode>000000"; mode is payload byte 12.
USBMON=/tmp/psvr2-streamon-usbmon.txt
NODE=""
busstr=$(lsusb | awk '/054c:0cde/{print $2; exit}')
if [ -n "$busstr" ] && modprobe usbmon 2>/dev/null; then
	NODE="/sys/kernel/debug/usb/usbmon/$((10#$busstr))u"
	[ -e "$NODE" ] || { echo "!! usbmon node $NODE missing"; NODE=""; }
fi
if [ -n "$NODE" ]; then
	cat "$NODE" > "$USBMON" 2>/dev/null &
	MON=$!
	sleep 1
else
	echo "!! usbmon unavailable — skipping on-wire mode confirmation"
fi

# Now that the headset is in VR mode (dprx=1), start ONE camera stream and keep
# it — the mode is set exactly once, never toggled (the previous bounded-capture
# + restart sequence is what the headset ignored). dmesg byte size confirms it.
echo "== starting single camera stream (mode set once, kept; saving frames) =="
rm -f "$RAW"
v4l2-ctl -d /dev/video2 --stream-mmap --stream-count=2000 \
	--stream-to="$RAW" >/dev/null 2>&1 &
CAM=$!
sleep 2

# Decode the captured camera-mode SET and confirm the byte on the wire.
if [ -n "$NODE" ]; then
	kill "$MON" 2>/dev/null
	chmod 0644 "$USBMON" 2>/dev/null
	echo "== on-wire camera-mode SET (usbmon: bRequest 09, report 0x0b) =="
	# Match the SET-camera-mode control OUT (data starts 0b000100), strip the
	# 4-byte-word grouping, and read payload byte 12 = the mode.
	awk '/ s 42 09 / && /= ?0b000100/ {
		sub(/^.*= */, ""); gsub(/ /, "");
		printf "   payload = %s\n", $0;
		printf "   mode byte (offset 12) = 0x%s\n", substr($0, 25, 2);
		found = 1; exit
	}
	END { if (!found) print "   (no camera-mode SET seen in capture)" }' "$USBMON"
fi

echo "== active camera mode (1040640 = mode 0xa/0x10, 819456 = mode 1) =="
dmesg | grep -iE "camera URB|streaming with camera mode" | tail -5

echo "== SLAM poll — *** MOVE/ROTATE THE HEADSET SLOWLY NOW *** =="
for i in $(seq 1 10); do
	dprx=$(od -A n -t u1 -N 1 /sys/kernel/debug/psvr2/raw_status | tr -d ' ')
	# raw_slam holds the latest 512-byte pose record; show its first 4 header
	# bytes (expect "SLP\x01" = 534c5001). Non-empty => poses are being parsed.
	hdr=$(od -A n -t x1 -N 4 /sys/kernel/debug/psvr2/raw_slam 2>/dev/null | tr -d ' \n')
	printf "  [%2ds] dprx=%s slam_hdr=%s\n" "$((i * 3))" "$dprx" "${hdr:-<empty>}"
	[ -n "$hdr" ] && { echo "  >> SLAM producing — run psvr2-pose-test for live poses"; break; }
	sleep 3
done

kill "$CAM" 2>/dev/null
echo "== frames captured (0 = endpoint sent only ZLPs) =="
ls -l "$RAW" 2>/dev/null
chmod 0644 "$RAW" 2>/dev/null
kill "$MT" 2>/dev/null
wait "$MT" 2>/dev/null
echo ">> done — mode released. Switch back with Ctrl+Alt+F1 (or F2)."
