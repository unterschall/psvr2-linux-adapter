#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# Development unloader: remove psvr2.ko (leaves the shared dep modules loaded).
# Run as root:  sudo scripts/dev-unload.sh
set -u

[ "$(id -u)" -eq 0 ] || { echo "error: run as root (sudo $0)" >&2; exit 1; }

if rmmod psvr2 2>/dev/null; then
	echo ">> psvr2 unloaded"
else
	echo ">> psvr2 was not loaded"
fi
