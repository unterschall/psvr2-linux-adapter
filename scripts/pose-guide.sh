#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# Guided on-headset pose capture: shows motion prompts inside the headset and
# logs each pose sample tagged with the on-screen instruction (no timing
# guesswork). Builds psvr2-pose-guide if needed and runs it.
#
# MUST run as root from a text console (Ctrl+Alt+F3).
# Output: /tmp/pose-guide-log.txt
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLS="${SCRIPT_DIR}/../userspace/tools"

[ "$(id -u)" -eq 0 ] || { echo "run as root from a TTY"; exit 1; }
[ -x "${TOOLS}/psvr2-pose-guide" ] || \
	make -C "${TOOLS}" psvr2-pose-guide >/dev/null || exit 1

echo ">> Put the headset on. Prompts will appear inside it; follow each one"
echo ">> (return to a comfortable centre between motions). ~60s total."
exec "${TOOLS}/psvr2-pose-guide"
