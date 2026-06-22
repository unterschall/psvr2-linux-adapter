# SPDX-License-Identifier: GPL-2.0
CFLAGS ?= -O2 -Wall -Wextra
BINS := psvr2-imu-test psvr2-pose-test psvr2-gaze-test psvr2-pose-log

all: $(BINS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

# Atomic-KMS display helper — needs libdrm, so it is a separate explicit target
# (not in `all`) to avoid requiring libdrm everywhere.
psvr2-kms-modeset: psvr2-kms-modeset.c
	$(CC) $(CFLAGS) $(shell pkg-config --cflags libdrm) -o $@ $< $(shell pkg-config --libs libdrm)

# Guided on-headset pose capture — needs libdrm; renders text via font8x8.h.
psvr2-pose-guide: psvr2-pose-guide.c font8x8.h
	$(CC) $(CFLAGS) $(shell pkg-config --cflags libdrm) -o $@ $< $(shell pkg-config --libs libdrm)

# Wayland DRM-lease + Vulkan acquire probe (layer 2/3 of the leasing pipeline).
# Needs wayland-client + vulkan; the wp_drm_lease_device_v1 client bindings are
# generated from the system's wayland-protocols XML by wayland-scanner.
WL_SCANNER  := $(shell pkg-config --variable=wayland_scanner wayland-scanner)
WL_PROTODIR := $(shell pkg-config --variable=pkgdatadir wayland-protocols)
DRM_LEASE_XML := $(WL_PROTODIR)/staging/drm-lease/drm-lease-v1.xml

drm-lease-v1-client-protocol.h:
	$(WL_SCANNER) client-header $(DRM_LEASE_XML) $@
drm-lease-v1-protocol.c:
	$(WL_SCANNER) private-code $(DRM_LEASE_XML) $@

vk-acquire-drm-display: vk-acquire-drm-display.c drm-lease-v1-client-protocol.h drm-lease-v1-protocol.c
	$(CC) $(CFLAGS) -o $@ vk-acquire-drm-display.c drm-lease-v1-protocol.c \
		$(shell pkg-config --cflags wayland-client libdrm) \
		$(shell pkg-config --libs wayland-client libdrm) -lvulkan

clean:
	$(RM) $(BINS) psvr2-kms-modeset psvr2-pose-guide vk-acquire-drm-display \
		drm-lease-v1-client-protocol.h drm-lease-v1-protocol.c

.PHONY: all clean
