#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# Wayland DRM-leasing diagnostic for the PSVR2 direct-mode display path.
#
# When a VR runtime can't light the headset on Wayland (e.g. SteamVR logs
# "vkGetDrmDisplay failed" / VRInitError_Compositor_CannotDRMLeaseDisplay), the
# fault is in one of a few layers. This checks the *prerequisites* for each, so
# you can tell WHERE it breaks — or establish that the prerequisites are all met
# and the fault is in the runtime's own Vulkan acquisition.
#
# Pipeline (and which layer each check covers):
#   kernel/DRM  the headset connector is present, connected, and leasable
#   compositor  it advertises wp_drm_lease_device_v1 and offers the connector
#   Vulkan      the GPU that drives the connector supports VK_EXT_acquire_drm_display
#   runtime     requests the lease + acquires the VkDisplay  (NOT tested here —
#               needs the runtime, or the separate vk-acquire-drm-display probe)
#
# Run from your normal WAYLAND session (no root needed):
#   scripts/test-wayland-lease.sh
set -u

pass=0; fail=0; warn=0
ok()   { echo "  [PASS] $*"; pass=$((pass + 1)); }
bad()  { echo "  [FAIL] $*"; fail=$((fail + 1)); }
wrn()  { echo "  [WARN] $*"; warn=$((warn + 1)); }
hdr()  { echo; echo "== $* =="; }

icd_for_driver() {
	case "$1" in
		amdgpu)  echo /usr/share/vulkan/icd.d/radeon_icd.json ;;
		nvidia)  echo /usr/share/vulkan/icd.d/nvidia_icd.json ;;
		i915|xe) echo /usr/share/vulkan/icd.d/intel_icd.x86_64.json ;;
		*)       echo "" ;;
	esac
}

# --- layer 0: session ---------------------------------------------------------
hdr "Session"
if [ "${XDG_SESSION_TYPE:-}" = "wayland" ] && [ -n "${WAYLAND_DISPLAY:-}" ]; then
	ok "Wayland session ($XDG_SESSION_TYPE, ${XDG_CURRENT_DESKTOP:-unknown})"
else
	bad "not a Wayland session (XDG_SESSION_TYPE=${XDG_SESSION_TYPE:-unset})"
	echo "  This test only applies to Wayland. On X11, leasing uses VK_EXT_acquire_xlib_display."
	echo; echo "RESULT: not applicable here"
	exit 1
fi

# --- layer 1: kernel/DRM — headset connector present & leasable ---------------
hdr "Kernel / DRM — headset display connector"
CONN=""; CARD=""; DRV=""
for c in /sys/class/drm/card*-*; do
	[ -e "$c/status" ] || continue
	if [ "$(cat "$c/status" 2>/dev/null)" = "connected" ] && grep -q '^4000x2040' "$c/modes" 2>/dev/null; then
		CONN="$(basename "$c")"
		CARD="${CONN%%-*}"
		DRV="$(basename "$(readlink -f "/sys/class/drm/$CARD/device/driver" 2>/dev/null)" 2>/dev/null)"
		break
	fi
done
if [ -n "$CONN" ]; then
	ok "headset connector up: $CONN (4000x2040), driven by '$DRV' ($CARD)"
else
	# distinguish "asleep/unplugged" from "no PSVR2 at all"
	if ls /sys/class/drm/card*-DP-* >/dev/null 2>&1; then
		bad "no connected 4000x2040 output — the headset isn't presenting its panel"
		echo "  Wear the headset (proximity wakes it) and/or reseat the USB-C->DP cable,"
		echo "  then re-run. Without the panel up there is nothing to lease."
	else
		bad "no DisplayPort connectors found at all"
	fi
fi

# --- layer 2: compositor — advertises leasing & offers the connector ----------
hdr "Compositor — wp_drm_lease_device_v1"
if command -v wayland-info >/dev/null 2>&1; then
	WI="$(wayland-info 2>/dev/null)"
	if echo "$WI" | grep -q 'wp_drm_lease_device'; then
		ok "compositor advertises wp_drm_lease_device_v1 (DRM leasing supported)"
		# Surface any lease-connector detail wayland-info exposes (format varies
		# by version); the runtime's lease request is the definitive offer test.
		offered="$(echo "$WI" | grep -iA3 'drm_lease' | grep -iE 'connector|name|description' | sed 's/^/      /')"
		if [ -n "$offered" ]; then
			echo "      offered lease connectors (per wayland-info):"
			echo "$offered"
		else
			wrn "wayland-info didn't enumerate the offered connectors — confirm the"
			echo "         headset is offered via the runtime's lease request (it leases by EDID/name)."
		fi
	else
		bad "compositor does NOT advertise wp_drm_lease_device_v1"
		echo "  Needs a compositor with DRM-lease support (KWin Plasma 6+, Mutter, wlroots)."
	fi
else
	wrn "wayland-info not installed — can't inspect the compositor (pacman -S wayland-utils)"
fi

# --- layer 3: Vulkan — the headset GPU can acquire a DRM display --------------
hdr "Vulkan — DRM display acquisition on the headset's GPU"
if ! command -v vulkaninfo >/dev/null 2>&1; then
	wrn "vulkaninfo not installed (pacman -S vulkan-tools) — skipping Vulkan checks"
else
	ICD="$(icd_for_driver "$DRV")"
	if [ -n "$ICD" ] && [ -f "$ICD" ]; then
		VI="$(VK_DRIVER_FILES="$ICD" vulkaninfo 2>/dev/null)"
		name="$(echo "$VI" | grep -m1 'deviceName' | sed 's/.*= *//')"
		echo "  headset GPU Vulkan device: ${name:-unknown}  (ICD: $ICD)"
		echo "$VI" | grep -q 'VK_KHR_display'            && ok "VK_KHR_display supported"            || bad "VK_KHR_display MISSING"
		echo "$VI" | grep -q 'VK_EXT_acquire_drm_display' && ok "VK_EXT_acquire_drm_display supported" || bad "VK_EXT_acquire_drm_display MISSING (can't lease-acquire)"
		drv="$(echo "$VI" | grep -m1 'driverInfo' | sed 's/.*= *//')"
		[ -n "$drv" ] && echo "  driver: $drv  (note for upstream bug reports)"
	else
		wrn "no Vulkan ICD mapping for driver '$DRV' — check VK_EXT_acquire_drm_display manually"
	fi

	# multi-GPU caveat: the runtime must use the GPU that owns the connector
	ngpu="$(vulkaninfo --summary 2>/dev/null | grep -c 'deviceName')"
	if [ "${ngpu:-0}" -gt 1 ]; then
		wrn "multiple Vulkan GPUs present ($ngpu) — the runtime may pick the wrong one"
		[ -n "${ICD:-}" ] && echo "         force the headset GPU when launching: VK_DRIVER_FILES=$ICD"
	fi
fi

# --- verdict ------------------------------------------------------------------
hdr "Result"
echo "  $pass passed, $fail failed, $warn warning(s)"
if [ "$fail" -eq 0 ]; then
	cat <<'EOF'
  All prerequisites for Wayland leasing are in place. If a runtime still fails to
  acquire the display, the fault is in the *runtime's* Vulkan acquisition (the
  vkGetDrmDisplay/vkAcquireDrmDisplay step), not the compositor or kernel:
    - retry the runtime with the headset GPU forced (VK_DRIVER_FILES above);
    - cross-check with a second runtime (Monado) — if it works where the first
      doesn't, the first runtime's Wayland path is the bug.
EOF
else
	echo "  Fix the [FAIL] item(s) above first — that layer is the blocker."
fi
[ "$fail" -eq 0 ]
