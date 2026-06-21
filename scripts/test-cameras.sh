#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# Proper on-headset camera test.
#
# The PSVR2's cameras (and its SLAM tracker) only run while the headset is in VR
# mode — i.e. an active 4K signal is driving the panel AND the headset is worn.
# With a dark panel the cameras deliver nothing, so a bare `v4l2-ctl` capture
# returns 0 frames. This test establishes that precondition, verifies it, then
# captures and checks real frames.
#
# It will:
#   1. drive the 4K panel (psvr2-kms-modeset) so the headset enters VR mode,
#   2. confirm VR mode by waiting for the SLAM pose stream to come alive,
#   3. capture camera frames and verify them (count, size, and that the sensor
#      is actually imaging — not a black/zero frame),
#   4. report PASS/FAIL and release the display.
#
# REQUIREMENTS
#   * run as root from a TEXT CONSOLE (Ctrl+Alt+F3) — driving the panel needs
#     DRM master, which the desktop compositor otherwise holds;
#   * WEAR THE HEADSET — the proximity sensor must read "worn" or it sleeps and
#     never enters VR mode;
#   * the psvr2 module loaded (sudo scripts/dev-load.sh).
#
#   sudo scripts/test-cameras.sh [frames]
set -u

FRAMES="${1:-30}"
W=1280; H=640; FRAME_BYTES=$((W * H))      # mode 1: 1280x640 GREY
HOLD=120                                    # seconds to hold the panel up
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLS="${SCRIPT_DIR}/../userspace/tools"
HELPER="${TOOLS}/psvr2-kms-modeset"
POSE=/dev/psvr2-pose
RAW="/tmp/psvr2-camera-test.raw"

pass=0; fail=0
ok()   { echo "  PASS: $*"; pass=$((pass + 1)); }
bad()  { echo "  FAIL: $*"; fail=$((fail + 1)); }
note() { echo ">> $*"; }

[ "$(id -u)" -eq 0 ] || { echo "run as root from a TTY (Ctrl+Alt+F3): sudo $0"; exit 1; }
lsmod | grep -q '^psvr2' || { echo "psvr2 module not loaded — run: sudo scripts/dev-load.sh"; exit 1; }

# --- locate the PSVR2 camera node by name (don't hardcode /dev/videoN) --------
CAM=""
for v in /sys/class/video4linux/video*; do
	if grep -qiE 'psvr2|PlayStation VR2' "$v/name" 2>/dev/null; then
		CAM="/dev/$(basename "$v")"; break
	fi
done
[ -n "$CAM" ] || { echo "no PSVR2 camera V4L2 node found"; exit 1; }
note "camera node: $CAM"

# --- cleanup on exit ----------------------------------------------------------
MT=""
cleanup() { [ -n "$MT" ] && kill "$MT" 2>/dev/null; wait "$MT" 2>/dev/null; }
trap cleanup EXIT INT TERM

# --- 1. drive the panel so the headset enters VR mode -------------------------
if [ ! -x "$HELPER" ]; then
	note "building psvr2-kms-modeset"
	make -C "$TOOLS" psvr2-kms-modeset >/dev/null || { echo "build failed (need libdrm)"; exit 1; }
fi
note "driving 4000x2040 for up to ${HOLD}s (headset must be worn)"
"$HELPER" 4000x2040 "$HOLD" >/tmp/psvr2-kms.log 2>&1 &
MT=$!
sleep 9
kill -0 "$MT" 2>/dev/null || { echo "modeset helper exited early:"; cat /tmp/psvr2-kms.log; exit 1; }

# --- 2. confirm VR mode via the SLAM pose stream ------------------------------
note "waiting for VR mode (SLAM pose to come alive) — look around slowly"
vr=0
for i in $(seq 1 10); do
	if [ "$(timeout 2 cat "$POSE" 2>/dev/null | wc -c)" -gt 0 ]; then vr=1; break; fi
	sleep 1
done
if [ "$vr" -eq 1 ]; then ok "headset is in VR mode (pose streaming)"
else
	bad "headset never entered VR mode — is it worn? is the DP cable connected?"
	echo; echo "RESULT: FAIL (precondition not met) — cannot test cameras with a dark panel"
	exit 1
fi

# --- 3a. pose rate (SLAM sanity) ----------------------------------------------
pb=$(timeout 3 cat "$POSE" 2>/dev/null | wc -c)
note "pose: ${pb} bytes in 3s (~$((pb / 48 / 3)) Hz at 48 B/sample)"

# --- 3b. capture camera frames (mode 1). NB: enabling V4L2 switches the cameras
#         out of the SLAM tracking mode, so the pose stream pauses during this —
#         that's expected (shared cameras, exclusive modes). ---------------------
note "capturing ${FRAMES} frames from $CAM (this pauses SLAM)"
rm -f "$RAW"
timeout 20 v4l2-ctl -d "$CAM" \
	--set-fmt-video=width=${W},height=${H},pixelformat=GREY \
	--stream-mmap --stream-count="${FRAMES}" --stream-to="$RAW" >/dev/null 2>&1
chmod 0644 "$RAW" 2>/dev/null || true

# --- 4. verify the captured frames --------------------------------------------
sz=$(stat -c%s "$RAW" 2>/dev/null || echo 0)
got=$((sz / FRAME_BYTES))
note "captured ${sz} bytes = ${got} frames of ${W}x${H}"
[ "$got" -ge 1 ] && ok "received at least one full frame" || bad "no full frames captured"
[ $((sz % FRAME_BYTES)) -eq 0 ] && ok "byte count is an exact multiple of the frame size" \
	|| bad "byte count is not frame-aligned (got $((sz % FRAME_BYTES)) extra)"

if [ "$got" -ge 1 ] && command -v python3 >/dev/null; then
	# Content sanity on the first frame: a real image is not all-black/zero and
	# has dynamic range; a stuck/black frame is.
	python3 - "$RAW" "$FRAME_BYTES" <<'PY'
import sys
raw, fs = sys.argv[1], int(sys.argv[2])
f = open(raw, 'rb').read(fs)
mean = sum(f) / len(f); mn = min(f); mx = max(f)
print(f"  frame0: mean={mean:.1f} min={mn} max={mx}")
imaging = mean > 2 and mx > 20 and (mx - mn) > 10
print("  PASS: sensor is imaging (non-black, has dynamic range)" if imaging
      else "  FAIL: frame looks black/flat — sensor may not be imaging")
sys.exit(0 if imaging else 1)
PY
	[ $? -eq 0 ] && pass=$((pass + 1)) || fail=$((fail + 1))
fi

echo
echo "RESULT: ${pass} passed, ${fail} failed"
echo "(raw frames saved to $RAW — view e.g. with: "
echo "  ffplay -f rawvideo -pixel_format gray -video_size ${W}x${H} $RAW )"
echo ">> releasing display. Switch back to your desktop with Ctrl+Alt+F1."
[ "$fail" -eq 0 ]
