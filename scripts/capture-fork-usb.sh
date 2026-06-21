#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# Capture the USB control/bulk traffic of a libusb-based PSVR2 driver (e.g. a
# Monado build), to diff its init sequence against this kernel module's. Unloads
# our module (libusb needs the interfaces), runs the tool briefly under usbmon,
# and dumps the vendor control transfers it sent. A reverse-engineering aid.
#
# Run as root:  sudo scripts/capture-fork-usb.sh /path/to/monado-cli [mode]
set -u

CLI="${1:?usage: sudo $0 /path/to/monado-cli [test|probe]}"
MODE="${2:-test}"		# monado-cli subcommand: test | probe
OUT_USB=/tmp/fork-usbmon.txt
OUT_LOG=/tmp/fork-log.txt

die() { echo "error: $*" >&2; exit 1; }
[ "$(id -u)" -eq 0 ] || die "run as root (sudo $0)"
[ -x "$CLI" ] || die "monado-cli not found at $CLI"

# Headset bus (strip leading zeros).
busstr=$(lsusb | awk '/054c:0cde/{print $2; exit}')
[ -n "$busstr" ] || die "PSVR2 (054c:0cde) not found on USB"
BUS=$((10#$busstr))
echo ">> headset on bus $BUS"

# Free the interfaces for libusb.
if rmmod psvr2 2>/dev/null; then echo ">> unloaded our psvr2 module"; fi

# usbmon.
modprobe usbmon || die "modprobe usbmon failed"
NODE="/sys/kernel/debug/usb/usbmon/${BUS}u"
[ -e "$NODE" ] || die "usbmon node $NODE missing (is debugfs mounted at /sys/kernel/debug?)"

echo ">> capturing $NODE"
cat "$NODE" > "$OUT_USB" &
CAP=$!
sleep 1

echo ">> running fork: monado-cli $MODE (PSVR2_LOG=trace, ~12s)"
timeout 12 env PSVR2_LOG=trace XRT_LOG=debug "$CLI" "$MODE" > "$OUT_LOG" 2>&1
echo ">> fork exit: $?"
sleep 1
kill "$CAP" 2>/dev/null

chmod 0644 "$OUT_USB" "$OUT_LOG" 2>/dev/null
echo ">> $(wc -l < "$OUT_USB") usbmon lines -> $OUT_USB"
echo ">> $(wc -l < "$OUT_LOG") log lines -> $OUT_LOG"
echo
echo "===== vendor CONTROL transfers the fork sent (bRequest/wValue=report) ====="
grep -E "Co:|Ci:" "$OUT_USB" | grep -E " s [4c]0 " | sed -E 's/^[0-9a-f]+ //' | head -40
echo
echo "===== psvr2 driver log (camera mode / slam / errors) ====="
grep -iE "camera|slam|mode|fail|error|firmware|calib" "$OUT_LOG" | head -30
