#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# Capture the 6DoF pose stream while performing a known motion sequence, to
# verify the position/quaternion axis convention. Holds the 4K display (so the
# headset is in VR mode and the tracker runs) and logs /dev/psvr2-pose.
#
# MUST run from a text console (Ctrl+Alt+F3) as root, like drive-display.sh.
#
#   sudo scripts/pose-capture.sh [log_seconds]
set -u

HOLD="${1:-45}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLS="${SCRIPT_DIR}/../userspace/tools"
KMS="${TOOLS}/psvr2-kms-modeset"
LOGTOOL="${TOOLS}/psvr2-pose-log"
OUT=/tmp/pose-log.txt

[ "$(id -u)" -eq 0 ] || { echo "run as root from a TTY"; exit 1; }
[ -x "$KMS" ] || make -C "$TOOLS" psvr2-kms-modeset >/dev/null || exit 1
[ -x "$LOGTOOL" ] || make -C "$TOOLS" psvr2-pose-log >/dev/null || exit 1

cat <<EOF

  MOTION SEQUENCE (do each from a centred 'looking straight ahead' rest, hold
  the extreme ~2s, return to centre, then pause ~3s before the next):
    1. hold still (baseline)
    2. NOD DOWN (chin to chest)        -> centre
    3. NOD UP   (look at ceiling)      -> centre
    4. TURN LEFT                       -> centre
    5. TURN RIGHT                      -> centre
    6. LEAN FORWARD (whole head fwd)   -> back
    7. RAISE the headset straight UP   -> back
  You won't see this while wearing it, so memorise the order. You have ~9s after
  starting to don the headset, then ${HOLD}s of logging.

EOF
read -r -p "  press Enter to start..." _

echo ">> driving 4K display"
"$KMS" 4000x2040 $((HOLD + 14)) >/tmp/kms.log 2>&1 &
MT=$!
sleep 9
if ! kill -0 "$MT" 2>/dev/null; then
	echo "!! display didn't come up:"; cat /tmp/kms.log; exit 1
fi

echo ">> LOGGING POSE for ${HOLD}s — do the motion sequence now."
timeout "$HOLD" "$LOGTOOL" > "$OUT" 2>/dev/null
chmod 0644 "$OUT" 2>/dev/null
kill "$MT" 2>/dev/null
wait "$MT" 2>/dev/null

echo ">> captured $(grep -c . "$OUT") lines -> $OUT"
echo ">> first / last sample:"; sed -n '2p;$p' "$OUT"
echo ">> done. Ctrl+Alt+F1 back to the desktop."
