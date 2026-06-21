// SPDX-License-Identifier: GPL-2.0
/*
 * psvr2-pose-guide — guided, self-labelling pose capture.
 *
 * Drives the headset's 4K panel (atomic KMS, like psvr2-kms-modeset) and renders
 * a sequence of text prompts INTO the headset (centred in each eye), telling the
 * wearer which motion to perform. Each pose sample read from /dev/psvr2-pose is
 * tagged with the prompt that was on-screen, so the capture is perfectly
 * labelled with no timing/memory guesswork. Output: /tmp/pose-guide-log.txt.
 *
 * Must run as root from a text console (so it can take DRM master).
 *
 * Build: cc -O2 $(pkg-config --cflags libdrm) -o psvr2-pose-guide \
 *           psvr2-pose-guide.c $(pkg-config --libs libdrm)
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
#include <time.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <drm_mode.h>

#include "font8x8.h"

/* ---- pose stream (mirrors kernel/psvr2_uapi.h) ---- */
struct psvr2_pose_sample {
	uint64_t timestamp_ns;
	uint32_t device_vts_us;
	uint32_t flags;
	uint32_t position[3];
	uint32_t orientation[4];
};

/* ---- the guided motion sequence ---- */
struct step { const char *label, *text; int secs; };
static const struct step SEQ[] = {
	{ "baseline",    "HOLD STILL",  5 },
	{ "pitch_down",  "LOOK DOWN",   3 }, { "center", "CENTER", 2 },
	{ "pitch_up",    "LOOK UP",     3 }, { "center", "CENTER", 2 },
	{ "yaw_left",    "TURN LEFT",   3 }, { "center", "CENTER", 2 },
	{ "yaw_right",   "TURN RIGHT",  3 }, { "center", "CENTER", 2 },
	{ "roll_left",   "TILT LEFT",   3 }, { "center", "CENTER", 2 },
	{ "roll_right",  "TILT RIGHT",  3 }, { "center", "CENTER", 2 },
	{ "strafe_left", "STEP LEFT",   3 }, { "center", "CENTER", 2 },
	{ "strafe_right","STEP RIGHT",  3 }, { "center", "CENTER", 2 },
	{ "up",          "RISE UP",     3 }, { "center", "CENTER", 2 },
	{ "forward",     "LEAN FWD",    3 }, { "center", "CENTER", 2 },
	{ "done",        "DONE",        3 },
};

/* ---- framebuffer text rendering ---- */
static uint8_t *g_fb;
static uint32_t g_pitch, g_w, g_h;

static void put(uint32_t x, uint32_t y, uint32_t argb)
{
	if (x < g_w && y < g_h)
		*(uint32_t *)(g_fb + y * g_pitch + x * 4) = argb;
}

/* Draw one 8x8 glyph scaled by s at (x,y). */
static void glyph(uint32_t x, uint32_t y, unsigned char c, int s, uint32_t col)
{
	for (int row = 0; row < 8; row++) {
		unsigned char bits = font8x8[c * 8 + row];

		for (int b = 0; b < 8; b++)
			if (bits & (0x80 >> b))
				for (int dy = 0; dy < s; dy++)
					for (int dx = 0; dx < s; dx++)
						put(x + b * s + dx, y + row * s + dy, col);
	}
}

/* Draw text centred horizontally on cx, vertically on cy. */
static void text_centred(uint32_t cx, uint32_t cy, const char *str, int s,
			 uint32_t col)
{
	uint32_t tw = (uint32_t)strlen(str) * 8 * s;
	uint32_t x = cx - tw / 2, y = cy - 4 * s;

	for (const char *p = str; *p; p++, x += 8 * s)
		glyph(x, y, (unsigned char)*p, s, col);
}

/* Clear, then draw the prompt centred in each eye half (left/right). */
static void render_prompt(const char *str)
{
	const int s = 10;		/* 80px-tall glyphs */
	uint32_t qx = g_w / 4;		/* centre of each half */

	memset(g_fb, 0, (size_t)g_pitch * g_h);
	text_centred(qx, g_h / 2, str, s, 0x00ffffff);
	text_centred(g_w / 2 + qx, g_h / 2, str, s, 0x00ffffff);
}

/* ---- DRM helpers (same approach as psvr2-kms-modeset) ---- */
static uint32_t prop_id(int fd, uint32_t obj, uint32_t type, const char *name)
{
	drmModeObjectProperties *p = drmModeObjectGetProperties(fd, obj, type);
	uint32_t id = 0;

	for (uint32_t i = 0; p && i < p->count_props && !id; i++) {
		drmModePropertyRes *pr = drmModeGetProperty(fd, p->props[i]);

		if (pr) {
			if (!strcmp(pr->name, name))
				id = pr->prop_id;
			drmModeFreeProperty(pr);
		}
	}
	if (p)
		drmModeFreeObjectProperties(p);
	return id;
}

static int open_amdgpu(void)
{
	for (int i = 0; i < 16; i++) {
		char path[32];
		int fd;
		drmVersion *v;
		int ok;

		snprintf(path, sizeof(path), "/dev/dri/card%d", i);
		fd = open(path, O_RDWR | O_CLOEXEC);
		if (fd < 0)
			continue;
		v = drmGetVersion(fd);
		ok = v && v->name && !strcmp(v->name, "amdgpu");
		if (v)
			drmFreeVersion(v);
		if (ok)
			return fd;
		close(fd);
	}
	return -1;
}

static uint64_t now_ns(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec;
}

int main(void)
{
	int fd = open_amdgpu();
	drmModeRes *res;
	drmModeConnector *conn = NULL;
	drmModeModeInfo mode;
	uint32_t crtc_id = 0, plane_id = 0, blob = 0, fb_id = 0;
	int crtc_idx = -1, posefd, i;
	FILE *log;

	if (fd < 0) { fprintf(stderr, "no amdgpu DRM device\n"); return 1; }
	if (drmSetMaster(fd)) {
		fprintf(stderr, "drmSetMaster failed: %s (run from a TTY)\n",
			strerror(errno));
		return 1;
	}
	drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);

	res = drmModeGetResources(fd);
	for (i = 0; res && i < res->count_connectors; i++) {
		drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);

		if (c && c->connection == DRM_MODE_CONNECTED &&
		    c->connector_type == DRM_MODE_CONNECTOR_DisplayPort &&
		    c->count_modes > 0) { conn = c; break; }
		if (c) drmModeFreeConnector(c);
	}
	if (!conn) { fprintf(stderr, "no connected DP connector\n"); return 1; }
	mode = conn->modes[0];			/* preferred (4000x2040) */

	/* free CRTC the connector's encoder can use */
	{
		uint32_t possible = 0;

		for (int e = 0; e < conn->count_encoders; e++) {
			drmModeEncoder *en = drmModeGetEncoder(fd, conn->encoders[e]);

			if (en) { possible |= en->possible_crtcs; drmModeFreeEncoder(en); }
		}
		for (i = 0; i < res->count_crtcs; i++) {
			drmModeCrtc *cc;

			if (!(possible & (1 << i))) continue;
			cc = drmModeGetCrtc(fd, res->crtcs[i]);
			if (cc && !cc->mode_valid && !crtc_id) {
				crtc_id = res->crtcs[i]; crtc_idx = i;
			}
			if (cc) drmModeFreeCrtc(cc);
		}
	}
	if (!crtc_id) { fprintf(stderr, "no free CRTC\n"); return 1; }

	{
		drmModePlaneRes *pr = drmModeGetPlaneResources(fd);

		for (uint32_t k = 0; pr && k < pr->count_planes && !plane_id; k++) {
			drmModePlane *pl = drmModeGetPlane(fd, pr->planes[k]);

			if (pl && (pl->possible_crtcs & (1 << crtc_idx))) {
				uint32_t tp = prop_id(fd, pl->plane_id,
						      DRM_MODE_OBJECT_PLANE, "type");
				drmModeObjectProperties *pp = drmModeObjectGetProperties(
					fd, pl->plane_id, DRM_MODE_OBJECT_PLANE);

				for (uint32_t j = 0; pp && j < pp->count_props; j++)
					if (pp->props[j] == tp &&
					    pp->prop_values[j] == DRM_PLANE_TYPE_PRIMARY)
						plane_id = pl->plane_id;
				if (pp) drmModeFreeObjectProperties(pp);
			}
			if (pl) drmModeFreePlane(pl);
		}
	}
	if (!plane_id) { fprintf(stderr, "no primary plane\n"); return 1; }

	/* dumb fb */
	struct drm_mode_create_dumb creq = {
		.width = mode.hdisplay, .height = mode.vdisplay, .bpp = 32 };
	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq)) {
		fprintf(stderr, "create dumb: %s\n", strerror(errno)); return 1;
	}
	uint32_t h[4] = { creq.handle }, p[4] = { creq.pitch }, o[4] = { 0 };

	drmModeAddFB2(fd, mode.hdisplay, mode.vdisplay, DRM_FORMAT_XRGB8888,
		      h, p, o, &fb_id, 0);
	struct drm_mode_map_dumb mreq = { .handle = creq.handle };

	drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
	g_fb = mmap(NULL, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
		    mreq.offset);
	if (g_fb == MAP_FAILED) { fprintf(stderr, "mmap fb failed\n"); return 1; }
	g_pitch = creq.pitch; g_w = mode.hdisplay; g_h = mode.vdisplay;
	render_prompt("GET READY");

	/* atomic modeset */
	drmModeCreatePropertyBlob(fd, &mode, sizeof(mode), &blob);
	drmModeAtomicReq *req = drmModeAtomicAlloc();
	uint32_t w16 = mode.hdisplay << 16, h16 = mode.vdisplay << 16;
#define A(ob, ty, nm, v) drmModeAtomicAddProperty(req, ob, prop_id(fd, ob, ty, nm), v)
	A(conn->connector_id, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID", crtc_id);
	A(crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID", blob);
	A(crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE", 1);
	A(plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID", fb_id);
	A(plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID", crtc_id);
	A(plane_id, DRM_MODE_OBJECT_PLANE, "SRC_X", 0);
	A(plane_id, DRM_MODE_OBJECT_PLANE, "SRC_Y", 0);
	A(plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W", w16);
	A(plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H", h16);
	A(plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_X", 0);
	A(plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_Y", 0);
	A(plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_W", mode.hdisplay);
	A(plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_H", mode.vdisplay);
#undef A
	if (drmModeAtomicCommit(fd, req, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL)) {
		fprintf(stderr, "atomic commit: %s\n", strerror(errno)); return 1;
	}
	drmModeAtomicFree(req);

	/* pose stream + labelled log */
	posefd = open("/dev/psvr2-pose", O_RDONLY | O_NONBLOCK);
	if (posefd < 0) fprintf(stderr, "warning: /dev/psvr2-pose: %s\n", strerror(errno));
	log = fopen("/tmp/pose-guide-log.txt", "w");
	fprintf(log ? log : stderr,
		"# label  px py pz  qw qx qy qz  rel_ms\n");

	fprintf(stderr, ">> guiding capture; follow the prompts in the headset.\n");
	uint64_t t0 = now_ns();

	for (size_t st = 0; st < sizeof(SEQ) / sizeof(SEQ[0]); st++) {
		uint64_t end = now_ns() + (uint64_t)SEQ[st].secs * 1000000000ull;

		render_prompt(SEQ[st].text);
		fprintf(stderr, "   [%s] %s (%ds)\n", SEQ[st].label, SEQ[st].text, SEQ[st].secs);

		while (now_ns() < end) {
			struct psvr2_pose_sample s;

			if (posefd >= 0 && read(posefd, &s, sizeof(s)) == (ssize_t)sizeof(s) && log) {
				float f[7];

				for (int k = 0; k < 3; k++) memcpy(&f[k], &s.position[k], 4);
				for (int k = 0; k < 4; k++) memcpy(&f[3 + k], &s.orientation[k], 4);
				fprintf(log, "%-12s % .4f % .4f % .4f  % .4f % .4f % .4f % .4f  %.1f\n",
					SEQ[st].label, f[0], f[1], f[2], f[3], f[4], f[5], f[6],
					(now_ns() - t0) / 1e6);
			} else {
				struct timespec ts = { 0, 2000000 };  /* 2ms */

				nanosleep(&ts, NULL);
			}
		}
	}

	if (log) fclose(log);
	fprintf(stderr, ">> done -> /tmp/pose-guide-log.txt. Take off the headset.\n");
	return 0;
}
