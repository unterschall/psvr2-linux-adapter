#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# DECISIVE TEST B (see docs/roadmap.md "Still open" #1): run the DMJC Monado
# fork while the headset is in VR mode (active 4K signal / dprx=1) and capture
# its USB traffic. Our prior fork capture (capture-fork-usb.sh) ran headless, so
# the headset never entered VR mode and the fork never sent whatever post-VR
# sequence (if any) triggers SLAM. This script closes that gap:
#
#   1. unload our psvr2 module (libusb needs the interfaces),
#   2. hold a 4K modeset in the background via psvr2-kms-modeset (KMS only,
#      needs no psvr2 module) so the headset enters VR mode,
#   3. run the fork under usbmon inside that window.
#
# VR mode is self-verified from the fork's own log: each IMU record carries a
# dp_frame_cnt. Headless it is FROZEN (e.g. 44291); with a live signal it must
# ADVANCE. The script reports the distinct dp_frame_cnt values it saw.
#
# Outcomes:
#   - fork emits SLA / 6dof pose records, or LED/RP/VD xfers become non-zero
#     => SLAM works on this adapter; diff the in-VR-mode init against ours.
#   - fork still gets only IMU (no pose, aux xfers still 0) with dp_frame_cnt
#     advancing => conclusively the adapter: no tracking passthrough.
#
# MUST be run from a text console (Ctrl+Alt+F3), NOT the Wayland/X session, so
# the modeset helper can acquire DRM master. Run as root.
#
#   sudo scripts/capture-fork-display.sh [mode] [monado_seconds]
#   e.g. sudo scripts/capture-fork-display.sh 4000x2040 30
set -u

MODE="${1:-4000x2040}"
RUN="${2:-30}"			# how long to run the fork inside the VR-mode window
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HELPER="${SCRIPT_DIR}/../userspace/tools/psvr2-kms-modeset"
CLI="${SCRIPT_DIR}/../references/monado-psvr2/build/src/xrt/targets/cli/monado-cli"
HOLD=$((RUN + 25))		# modeset must outlast settle + fork run
OUT_USB=/tmp/fork-display-usbmon.txt
OUT_LOG=/tmp/fork-display-log.txt
KMS_LOG=/tmp/psvr2-kms-fork.log

die() { echo "error: $*" >&2; exit 1; }
[ "$(id -u)" -eq 0 ] || die "run as root, from a TTY (sudo $0)"
[ -x "$HELPER" ] || die "modeset helper not built: $HELPER (make -C userspace/tools)"
[ -x "$CLI" ]    || die "monado-cli not found: $CLI"

busstr=$(lsusb | awk '/054c:0cde/{print $2; exit}')
[ -n "$busstr" ] || die "PSVR2 (054c:0cde) not found on USB"
BUS=$((10#$busstr))
echo ">> headset on bus $BUS"

# Free the interfaces for libusb.
if rmmod psvr2 2>/dev/null; then echo ">> unloaded our psvr2 module"; fi

cleanup() { kill "${MT:-}" 2>/dev/null; kill "${CAP:-}" 2>/dev/null; }
trap cleanup EXIT

# 1. Bring up the 4K signal and hold it; the headset enters VR mode while held.
echo ">> driving mode $MODE for ${HOLD}s (background)"
"$HELPER" "$MODE" "$HOLD" >"$KMS_LOG" 2>&1 &
MT=$!
sleep 9
kill -0 "$MT" 2>/dev/null || { echo "!! modeset helper exited early:"; cat "$KMS_LOG"; exit 1; }
grep -iE "commit|mode|connector|crtc" "$KMS_LOG"
echo ">> mode held (pid $MT) — headset should now be in VR mode"

# 2. Arm usbmon, then run the fork inside the VR-mode window.
modprobe usbmon || die "modprobe usbmon failed"
NODE="/sys/kernel/debug/usb/usbmon/${BUS}u"
[ -e "$NODE" ] || die "usbmon node $NODE missing"
echo ">> capturing $NODE -> $OUT_USB"
cat "$NODE" > "$OUT_USB" &
CAP=$!
sleep 1

echo ">> running fork: monado-cli test (PSVR2_LOG=trace, ${RUN}s) — MOVE THE HEADSET"
timeout "$RUN" env PSVR2_LOG=trace XRT_LOG=debug "$CLI" test >"$OUT_LOG" 2>&1
echo ">> fork exit: $?"
sleep 1
kill "$CAP" 2>/dev/null; CAP=""
kill "$MT"  2>/dev/null; MT=""
chmod 0644 "$OUT_USB" "$OUT_LOG" "$KMS_LOG" 2>/dev/null

# 3. Report the three things that decide it.
echo
echo "===== VR mode? distinct dp_frame_cnt (FROZEN=no video; ADVANCING=VR mode) ====="
grep -oE "dp_frame_cnt [0-9]+" "$OUT_LOG" | sort -un | awk '{print} END{print "  ("NR" distinct values)"}'
echo
echo "===== SLAM? pose / SLA / 6dof records (any line => tracking is alive) ====="
grep -icE "\"SLA\"|6dof pose|pose record|slam" "$OUT_LOG" | sed 's/^/  match count: /'
grep -iE "\"SLA\"|6dof pose|pose record|slam" "$OUT_LOG" | head -10
echo
echo "===== aux tracking xfer sizes (0 everywhere => no tracking passthrough) ====="
grep -iE "LED Detector xfer|RP xfer|VD xfer" "$OUT_LOG" | sort | uniq -c | head
echo
echo "===== in-VR-mode control init the fork sent (diff vs ours next) ====="
echo "  raw capture: $OUT_USB  ($(wc -l < "$OUT_USB") lines)"
echo "  decode + diff against our module's init with:"
echo "    scripts/diff-usb-init.sh $OUT_USB /tmp/our-usbmon.txt"
echo
echo ">> done — mode released. Switch back with Ctrl+Alt+F1 (or F2)."
