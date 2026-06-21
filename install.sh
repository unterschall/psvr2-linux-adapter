#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# Install the psvr2 kernel module via DKMS, plus the udev rules.
# Run as root:  sudo ./install.sh
set -euo pipefail

NAME=psvr2
VERSION=0.1
SRC="/usr/src/${NAME}-${VERSION}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UDEV_RULE="${SCRIPT_DIR}/kernel/99-psvr2.rules"

die() { echo "error: $*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || die "must be run as root (try: sudo $0)"
command -v dkms >/dev/null 2>&1 || die "dkms not found — install the 'dkms' package first"
[ -d "/lib/modules/$(uname -r)/build" ] || \
	die "kernel headers for $(uname -r) not found — install your distro's linux-headers package"

echo ">> Installing source to ${SRC}"
rm -rf "${SRC}"
mkdir -p "${SRC}"
cp -a "${SCRIPT_DIR}/kernel/." "${SRC}/"

# Re-add cleanly in case a previous version is registered.
if dkms status -m "${NAME}" -v "${VERSION}" | grep -q "${NAME}"; then
	echo ">> Removing previously registered ${NAME}/${VERSION}"
	dkms remove -m "${NAME}" -v "${VERSION}" --all || true
fi

echo ">> dkms add / build / install"
dkms add -m "${NAME}" -v "${VERSION}"
dkms build -m "${NAME}" -v "${VERSION}"
dkms install -m "${NAME}" -v "${VERSION}"

echo ">> Installing udev rules"
install -m 0644 "${UDEV_RULE}" /etc/udev/rules.d/99-psvr2.rules
udevadm control --reload-rules || true
udevadm trigger || true

echo ">> Done. Load now with:  sudo modprobe ${NAME}"
echo "   (it also auto-loads when the headset is connected)"
