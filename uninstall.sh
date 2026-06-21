#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# Remove the psvr2 DKMS module and udev rules.
# Run as root:  sudo ./uninstall.sh
set -euo pipefail

NAME=psvr2
VERSION=0.1
SRC="/usr/src/${NAME}-${VERSION}"

die() { echo "error: $*" >&2; exit 1; }
[ "$(id -u)" -eq 0 ] || die "must be run as root (try: sudo $0)"

echo ">> Unloading module (if loaded)"
modprobe -r "${NAME}" 2>/dev/null || true

if command -v dkms >/dev/null 2>&1 && \
   dkms status -m "${NAME}" -v "${VERSION}" | grep -q "${NAME}"; then
	echo ">> dkms remove ${NAME}/${VERSION}"
	dkms remove -m "${NAME}" -v "${VERSION}" --all || true
fi

rm -rf "${SRC}"
rm -f /etc/udev/rules.d/99-psvr2.rules
udevadm control --reload-rules 2>/dev/null || true

echo ">> Done."
