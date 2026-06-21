// SPDX-License-Identifier: GPL-2.0
/*
 * psvr2-kms-modeset — drive a mode onto the PSVR2's DisplayPort connector via a
 * proper *atomic* KMS commit, so the kernel can engage DSC for the 4000x2040
 * panel (which legacy modesetting / modetest cannot). Used to put the headset
 * into VR mode so the optical streams (camera/SLAM) start.
 *
 * Must run as root from a text console (the desktop compositor must not hold DRM
 * master). It sets the mode, holds it for a while (so you can probe the streams
 * from another VT), then restores and exits.
 *
 *   psvr2-kms-modeset [WxH] [hold_seconds]
 *   e.g. psvr2-kms-modeset 4000x2040 20
 *        psvr2-kms-modeset 1920x1080 20
 *
 * Build: cc -O2 $(pkg-config --cflags libdrm) -o psvr2-kms-modeset \
 *           psvr2-kms-modeset.c $(pkg-config --libs libdrm)
 *
 * Copyright (C) 2026 PSVR2 Linux project
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <drm_mode.h>

static uint32_t prop_id(int fd, uint32_t obj, uint32_t type, const char *name)
{
	drmModeObjectProperties *props =
		drmModeObjectGetProperties(fd, obj, type);
	uint32_t id = 0;

	if (!props)
		return 0;
	for (uint32_t i = 0; i < props->count_props && !id; i++) {
		drmModePropertyRes *p = drmModeGetProperty(fd, props->props[i]);

		if (p) {
			if (!strcmp(p->name, name))
				id = p->prop_id;
			drmModeFreeProperty(p);
		}
	}
	drmModeFreeObjectProperties(props);
	return id;
}

/* Add a property to the atomic request, logging it and flagging if missing. */
static int g_missing;
static void add(int fd, drmModeAtomicReq *req, uint32_t obj, uint32_t type,
		const char *name, uint64_t val)
{
	uint32_t pid = prop_id(fd, obj, type, name);

	if (!pid) {
		fprintf(stderr, "  !! property NOT FOUND: %s (obj %u)\n", name, obj);
		g_missing++;
		return;
	}
	fprintf(stderr, "  %-8s obj %u = %llu (pid %u)\n", name, obj,
		(unsigned long long)val, pid);
	drmModeAtomicAddProperty(req, obj, pid, val);
}

/* Open the amdgpu DRM primary node. */
static int open_amdgpu(void)
{
	for (int i = 0; i < 16; i++) {
		char path[32];
		int fd;
		drmVersion *v;
		int match;

		snprintf(path, sizeof(path), "/dev/dri/card%d", i);
		fd = open(path, O_RDWR | O_CLOEXEC);
		if (fd < 0)
			continue;
		v = drmGetVersion(fd);
		match = v && v->name && !strcmp(v->name, "amdgpu");
		if (v)
			drmFreeVersion(v);
		if (match) {
			fprintf(stderr, "using %s (amdgpu)\n", path);
			return fd;
		}
		close(fd);
	}
	return -1;
}

int main(int argc, char **argv)
{
	const char *want_mode = argc > 1 ? argv[1] : "4000x2040";
	int hold = argc > 2 ? atoi(argv[2]) : 20;
	int fd, ret;
	drmModeRes *res;
	drmModeConnector *conn = NULL;
	drmModeModeInfo mode;
	int have_mode = 0;
	uint32_t crtc_id = 0, plane_id = 0, mode_blob = 0, fb_id = 0;
	int crtc_idx = -1;

	fd = open_amdgpu();
	if (fd < 0) {
		fprintf(stderr, "no amdgpu DRM device\n");
		return 1;
	}

	if (drmSetMaster(fd)) {
		fprintf(stderr, "drmSetMaster failed: %s (run from a TTY, not the desktop)\n",
			strerror(errno));
		return 1;
	}
	drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1)) {
		fprintf(stderr, "atomic not supported: %s\n", strerror(errno));
		return 1;
	}

	res = drmModeGetResources(fd);
	if (!res) {
		fprintf(stderr, "drmModeGetResources failed\n");
		return 1;
	}

	/* Pick the connected DisplayPort connector (not eDP). */
	for (int i = 0; i < res->count_connectors; i++) {
		drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);

		if (c && c->connection == DRM_MODE_CONNECTED &&
		    c->connector_type == DRM_MODE_CONNECTOR_DisplayPort &&
		    c->count_modes > 0) {
			conn = c;
			break;
		}
		if (c)
			drmModeFreeConnector(c);
	}
	if (!conn) {
		fprintf(stderr, "no connected DisplayPort connector found\n");
		return 1;
	}
	fprintf(stderr, "connector %u, %d modes\n", conn->connector_id,
		conn->count_modes);

	/* Match the requested WxH; fall back to the preferred (first) mode. */
	for (int i = 0; i < conn->count_modes; i++) {
		char n[32];

		snprintf(n, sizeof(n), "%dx%d", conn->modes[i].hdisplay,
			 conn->modes[i].vdisplay);
		if (!strcmp(n, want_mode)) {
			mode = conn->modes[i];
			have_mode = 1;
			break;
		}
	}
	if (!have_mode) {
		mode = conn->modes[0];
		fprintf(stderr, "mode %s not found, using preferred %dx%d\n",
			want_mode, mode.hdisplay, mode.vdisplay);
	}
	fprintf(stderr, "mode: %s %.2fHz\n", mode.name,
		mode.clock * 1000.0 / (mode.htotal * mode.vtotal));

	/*
	 * Find a CRTC this connector can drive, PREFERRING one not already in
	 * use — on a TTY the primary display still occupies a CRTC, and reusing
	 * it makes the atomic check reject the state (EINVAL).
	 */
	uint32_t possible = 0;

	for (int e = 0; e < conn->count_encoders; e++) {
		drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoders[e]);

		if (enc) {
			possible |= enc->possible_crtcs;
			drmModeFreeEncoder(enc);
		}
	}
	for (int i = 0; i < res->count_crtcs; i++) {
		if (!(possible & (1 << i)))
			continue;
		drmModeCrtc *cc = drmModeGetCrtc(fd, res->crtcs[i]);
		int inuse = cc && cc->mode_valid;

		fprintf(stderr, "  candidate crtc %u: %s\n", res->crtcs[i],
			inuse ? "in-use" : "free");
		if (cc && !inuse && crtc_id == 0) {
			crtc_id = res->crtcs[i];
			crtc_idx = i;
		}
		if (cc)
			drmModeFreeCrtc(cc);
	}
	if (!crtc_id) {	/* none free — fall back to first possible */
		for (int i = 0; i < res->count_crtcs; i++)
			if (possible & (1 << i)) {
				crtc_id = res->crtcs[i];
				crtc_idx = i;
				break;
			}
	}
	if (!crtc_id) {
		fprintf(stderr, "no usable CRTC\n");
		return 1;
	}

	/* Find the primary plane for that CRTC. */
	drmModePlaneRes *pres = drmModeGetPlaneResources(fd);

	for (uint32_t i = 0; pres && i < pres->count_planes && !plane_id; i++) {
		drmModePlane *pl = drmModeGetPlane(fd, pres->planes[i]);

		if (pl && (pl->possible_crtcs & (1 << crtc_idx))) {
			uint32_t tp = prop_id(fd, pl->plane_id,
					      DRM_MODE_OBJECT_PLANE, "type");
			drmModeObjectProperties *pp = drmModeObjectGetProperties(
				fd, pl->plane_id, DRM_MODE_OBJECT_PLANE);

			for (uint32_t j = 0; pp && j < pp->count_props; j++)
				if (pp->props[j] == tp &&
				    pp->prop_values[j] == DRM_PLANE_TYPE_PRIMARY)
					plane_id = pl->plane_id;
			if (pp)
				drmModeFreeObjectProperties(pp);
		}
		if (pl)
			drmModeFreePlane(pl);
	}
	if (!plane_id) {
		fprintf(stderr, "no primary plane for CRTC\n");
		return 1;
	}
	fprintf(stderr, "crtc %u, primary plane %u\n", crtc_id, plane_id);

	/* Create + map a dumb framebuffer and fill it with a visible pattern. */
	struct drm_mode_create_dumb creq = {
		.width = mode.hdisplay, .height = mode.vdisplay, .bpp = 32 };
	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq)) {
		fprintf(stderr, "create dumb failed: %s\n", strerror(errno));
		return 1;
	}
	uint32_t handles[4] = { creq.handle }, pitches[4] = { creq.pitch };
	uint32_t offsets[4] = { 0 };

	if (drmModeAddFB2(fd, mode.hdisplay, mode.vdisplay, DRM_FORMAT_XRGB8888,
			  handles, pitches, offsets, &fb_id, 0)) {
		fprintf(stderr, "addfb2 failed: %s\n", strerror(errno));
		return 1;
	}
	struct drm_mode_map_dumb mreq = { .handle = creq.handle };

	if (!drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq)) {
		uint8_t *map = mmap(NULL, creq.size, PROT_READ | PROT_WRITE,
				    MAP_SHARED, fd, mreq.offset);
		if (map != MAP_FAILED) {
			for (uint32_t y = 0; y < mode.vdisplay; y++)
				for (uint32_t x = 0; x < mode.hdisplay; x++)
					((uint32_t *)(map + y * creq.pitch))[x] =
						((x * 255 / mode.hdisplay) << 16) |
						((y * 255 / mode.vdisplay) << 8);
			munmap(map, creq.size);
		}
	}

	/* Build and commit the atomic modeset. */
	if (drmModeCreatePropertyBlob(fd, &mode, sizeof(mode), &mode_blob)) {
		fprintf(stderr, "create mode blob failed: %s\n", strerror(errno));
		return 1;
	}
	drmModeAtomicReq *req = drmModeAtomicAlloc();
	uint32_t w16 = mode.hdisplay << 16, h16 = mode.vdisplay << 16;

	fprintf(stderr, "atomic properties:\n");
	add(fd, req, conn->connector_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID", crtc_id);
	add(fd, req, crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID", mode_blob);
	add(fd, req, crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE", 1);
	add(fd, req, plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID", fb_id);
	add(fd, req, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID", crtc_id);
	add(fd, req, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_X", 0);
	add(fd, req, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_Y", 0);
	add(fd, req, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W", w16);
	add(fd, req, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H", h16);
	add(fd, req, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_X", 0);
	add(fd, req, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_Y", 0);
	add(fd, req, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_W", mode.hdisplay);
	add(fd, req, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_H", mode.vdisplay);
	if (g_missing)
		fprintf(stderr, "WARNING: %d properties missing — commit will EINVAL\n",
			g_missing);

	/* Validate first (TEST_ONLY applies nothing), then commit for real. */
	ret = drmModeAtomicCommit(fd, req,
				  DRM_MODE_ATOMIC_TEST_ONLY | DRM_MODE_ATOMIC_ALLOW_MODESET,
				  NULL);
	fprintf(stderr, "TEST_ONLY commit: %s\n", ret ? strerror(errno) : "OK");

	if (!ret)
		ret = drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL);
	drmModeAtomicFree(req);

	if (ret) {
		fprintf(stderr, "ATOMIC COMMIT FAILED: %s (errno %d)\n",
			strerror(errno), errno);
		fprintf(stderr, "(EINVAL with no missing props at 1080p = request/CRTC issue;\n"
			" EINVAL only at 4K = amdgpu won't DSC this sink -> apply the patch)\n");
		return 2;
	}

	printf("ATOMIC COMMIT OK: %dx%d active for %ds\n",
	       mode.hdisplay, mode.vdisplay, hold);
	fflush(stdout);
	sleep(hold);

	fprintf(stderr, "done; releasing\n");
	return 0;
}
