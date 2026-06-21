#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# Apply the amdgpu DSC/FEC EDID quirk to a kernel source tree, so the PSVR2
# panel (4000x2040, needs DSC+FEC) lights up on AMD GPUs.
#
# Usage:  ./apply-amdgpu-dsc.sh /path/to/linux
#
# This patches drivers/gpu/drm/amd/display/dc/link/link_detection.c; you then
# need to rebuild amdgpu (or the kernel) and reboot/reload. See docs/display.md.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PATCH="${SCRIPT_DIR}/amdgpu-psvr2-dsc-fec.patch"
TREE="${1:-}"

die() { echo "error: $*" >&2; exit 1; }

[ -n "${TREE}" ] || die "usage: $0 /path/to/linux"
[ -f "${PATCH}" ] || die "patch not found: ${PATCH}"
[ -d "${TREE}/drivers/gpu/drm/amd/display" ] || \
	die "'${TREE}' does not look like a kernel source tree with amdgpu"

cd "${TREE}"
if [ -d .git ]; then
	echo ">> git apply (checking first)"
	git apply --check "${PATCH}" || die "patch does not apply cleanly to this tree"
	git apply "${PATCH}"
else
	echo ">> patch -p1 (dry run first)"
	patch -p1 --dry-run < "${PATCH}" || die "patch does not apply cleanly to this tree"
	patch -p1 < "${PATCH}"
fi

echo ">> Applied. Now rebuild amdgpu / the kernel and reboot. See docs/display.md."
