#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# Bring the PSVR2 panel up as a normal display under X11.
#
# The headset enumerates as a DRM "non-desktop" / lease output, so a normal
# desktop won't draw to it. This flips the connector's non-desktop property off
# and enables the 4000x2040 mode. (Under Wayland this is compositor-specific and
# not handled here — see docs/display.md.)
#
# Usage:
#   psvr2-display-up.sh            # detect the PSVR2 output and enable it
#   psvr2-display-up.sh --off      # hand it back as a non-desktop/lease output
#   psvr2-display-up.sh -o DP-N    # force a specific output name
set -euo pipefail

PSVR2_MODE="4000x2040"
OUTPUT=""
ACTION=up

usage() { sed -n '4,16p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

while [ $# -gt 0 ]; do
	case "$1" in
	--off)      ACTION=off ;;
	-o|--output) OUTPUT="${2:-}"; shift ;;
	-h|--help)  usage 0 ;;
	*) echo "unknown argument: $1" >&2; usage 1 ;;
	esac
	shift
done

command -v xrandr >/dev/null 2>&1 || {
	echo "error: xrandr not found. This helper is X11-only; under Wayland see docs/display.md." >&2
	exit 1
}
[ -n "${DISPLAY:-}" ] || { echo "error: no X DISPLAY set." >&2; exit 1; }

# Auto-detect the output that advertises the PSVR2 panel mode.
if [ -z "${OUTPUT}" ]; then
	OUTPUT="$(xrandr 2>/dev/null | awk -v m="${PSVR2_MODE}" \
		'/^[^ ].*(connected|disconnected)/ {out=$1} index($0, m) {print out; exit}')"
fi
[ -n "${OUTPUT}" ] || {
	echo "error: could not find a PSVR2 output (no ${PSVR2_MODE} mode). Is the headset on and connected?" >&2
	echo "       Pass -o <OUTPUT> to force one (see 'xrandr')." >&2
	exit 1
}

if [ "${ACTION}" = off ]; then
	echo ">> Releasing ${OUTPUT} back to non-desktop (lease) mode"
	xrandr --output "${OUTPUT}" --off || true
	xrandr --output "${OUTPUT}" --set non-desktop 1
else
	echo ">> Enabling ${OUTPUT} as a normal ${PSVR2_MODE} display"
	xrandr --output "${OUTPUT}" --set non-desktop 0
	xrandr --output "${OUTPUT}" --auto
fi
