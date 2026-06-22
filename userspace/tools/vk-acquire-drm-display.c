// SPDX-License-Identifier: GPL-2.0
//
// vk-acquire-drm-display — layer-2/3 probe for the PSVR2 Wayland direct-mode path.
//
// Reproduces, outside any VR runtime, the exact step a runtime (e.g. SteamVR)
// fails at on Wayland:
//
//   layer 2  lease the headset's DRM connector from the Wayland compositor via
//            wp_drm_lease_device_v1, obtaining a leased DRM master fd;
//   layer 3  hand that fd + connector id to Vulkan and acquire the display via
//            VK_EXT_acquire_drm_display (vkGetDrmDisplayEXT / vkAcquireDrmDisplayEXT).
//
// Interpreting the result:
//   * PASS here, but the runtime fails  -> the runtime's Wayland path is the bug
//     (not the compositor or Mesa). For SteamVR that means waiting on Valve; for
//     an open runtime (Monado) we can fix it.
//   * FAIL at layer 2 (no lease)        -> compositor doesn't offer/grant the lease.
//   * FAIL at layer 3 (vkGetDrmDisplay) -> Mesa/RADV can't map the leased connector
//     -> minimal, runtime-independent repro for a Mesa bug report.
//
// Two modes:
//
//   (default) WAYLAND LEASE — run inside your Wayland session (no root, no TTY).
//     Leases the connector from the compositor and acquires it in Vulkan, exactly
//     like the runtime. Tests layers 2 + 3 together.
//
//   --drm  DIRECT DRM master — run from a TEXT CONSOLE (no compositor holding
//     DRM master). Opens the connector directly and acquires it in Vulkan,
//     bypassing the compositor/lease entirely. Isolates layer 3 alone:
//       * direct mode PASS but Wayland-lease mode FAIL -> the leased-fd path is
//         the problem (compositor or runtime fd handling);
//       * both FAIL at vkGetDrmDisplay -> Mesa/RADV can't acquire this connector
//         at all (a clean lower-level repro).
//
//   ./vk-acquire-drm-display [--connector NAME] [--gpu N]        # wayland lease
//   sudo ./vk-acquire-drm-display --drm [--card /dev/dri/cardN] [--gpu N]  # TTY
//
// --connector NAME : substring of the connector to target (e.g. DP-3; default auto)
// --gpu N          : only try Vulkan physical device N (default: try each)
//
// NOTE: draft — compiles cleanly but the lease/acquire paths are untested
// without PSVR2 hardware (Wayland for lease mode, a TTY for direct mode).
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wayland-client.h>
#include "drm-lease-v1-client-protocol.h"

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <vulkan/vulkan.h>

/* ---- offered-connector tracking ----------------------------------------- */
struct conn {
	struct wp_drm_lease_connector_v1 *obj;
	char *name;
	char *description;
	uint32_t connector_id;
	int has_id;
	struct conn *next;
};

static struct {
	struct wp_drm_lease_device_v1 *device;
	struct conn *connectors;
	int device_done;
	struct wp_drm_lease_v1 *lease;
	int lease_fd;
	int lease_finished;
} S = { .lease_fd = -1 };

/* ---- connector listener -------------------------------------------------- */
static void conn_name(void *data, struct wp_drm_lease_connector_v1 *c, const char *name)
{
	(void)c; struct conn *cn = data; free(cn->name); cn->name = strdup(name);
}
static void conn_desc(void *data, struct wp_drm_lease_connector_v1 *c, const char *d)
{
	(void)c; struct conn *cn = data; free(cn->description); cn->description = strdup(d);
}
static void conn_id(void *data, struct wp_drm_lease_connector_v1 *c, uint32_t id)
{
	(void)c; struct conn *cn = data; cn->connector_id = id; cn->has_id = 1;
}
static void conn_done(void *data, struct wp_drm_lease_connector_v1 *c) { (void)data; (void)c; }
static void conn_withdrawn(void *data, struct wp_drm_lease_connector_v1 *c) { (void)data; (void)c; }

static const struct wp_drm_lease_connector_v1_listener conn_listener = {
	.name = conn_name, .description = conn_desc, .connector_id = conn_id,
	.done = conn_done, .withdrawn = conn_withdrawn,
};

/* ---- lease device listener ----------------------------------------------- */
static void dev_drm_fd(void *data, struct wp_drm_lease_device_v1 *dev, int fd)
{
	(void)data; (void)dev; close(fd); /* enumeration fd; we don't need it */
}
static void dev_connector(void *data, struct wp_drm_lease_device_v1 *dev,
                          struct wp_drm_lease_connector_v1 *id)
{
	(void)data; (void)dev;
	struct conn *cn = calloc(1, sizeof *cn);
	cn->obj = id; cn->next = S.connectors; S.connectors = cn;
	wp_drm_lease_connector_v1_add_listener(id, &conn_listener, cn);
}
static void dev_done(void *data, struct wp_drm_lease_device_v1 *dev)
{
	(void)data; (void)dev; S.device_done = 1;
}
static void dev_released(void *data, struct wp_drm_lease_device_v1 *dev) { (void)data; (void)dev; }

static const struct wp_drm_lease_device_v1_listener dev_listener = {
	.drm_fd = dev_drm_fd, .connector = dev_connector,
	.done = dev_done, .released = dev_released,
};

/* ---- registry ------------------------------------------------------------ */
static void reg_global(void *data, struct wl_registry *r, uint32_t name,
                       const char *iface, uint32_t ver)
{
	(void)data; (void)ver;
	if (!strcmp(iface, wp_drm_lease_device_v1_interface.name)) {
		S.device = wl_registry_bind(r, name, &wp_drm_lease_device_v1_interface, 1);
		wp_drm_lease_device_v1_add_listener(S.device, &dev_listener, NULL);
	}
}
static void reg_global_remove(void *data, struct wl_registry *r, uint32_t name)
{ (void)data; (void)r; (void)name; }
static const struct wl_registry_listener reg_listener = {
	.global = reg_global, .global_remove = reg_global_remove,
};

/* ---- lease listener ------------------------------------------------------ */
static void lease_fd_ev(void *data, struct wp_drm_lease_v1 *l, int fd)
{ (void)data; (void)l; S.lease_fd = fd; }
static void lease_finished(void *data, struct wp_drm_lease_v1 *l)
{ (void)data; (void)l; S.lease_finished = 1; }
static const struct wp_drm_lease_v1_listener lease_listener = {
	.lease_fd = lease_fd_ev, .finished = lease_finished,
};

/* readable VkResult for bug reports (falls back to the number) */
static const char *vkr(VkResult r)
{
	switch (r) {
	case VK_SUCCESS:                     return "VK_SUCCESS";
	case VK_ERROR_OUT_OF_HOST_MEMORY:    return "VK_ERROR_OUT_OF_HOST_MEMORY";
	case VK_ERROR_OUT_OF_DEVICE_MEMORY:  return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
	case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
	case VK_ERROR_DEVICE_LOST:           return "VK_ERROR_DEVICE_LOST";
	case VK_ERROR_LAYER_NOT_PRESENT:     return "VK_ERROR_LAYER_NOT_PRESENT";
	case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
	case VK_ERROR_FEATURE_NOT_PRESENT:   return "VK_ERROR_FEATURE_NOT_PRESENT";
	case VK_ERROR_INCOMPATIBLE_DRIVER:   return "VK_ERROR_INCOMPATIBLE_DRIVER";
	case VK_ERROR_FORMAT_NOT_SUPPORTED:  return "VK_ERROR_FORMAT_NOT_SUPPORTED";
	case VK_ERROR_UNKNOWN:               return "VK_ERROR_UNKNOWN";
	case VK_ERROR_SURFACE_LOST_KHR:      return "VK_ERROR_SURFACE_LOST_KHR";
	case VK_ERROR_OUT_OF_DATE_KHR:       return "VK_ERROR_OUT_OF_DATE_KHR";
	default:                             return "VK_ERROR_(other)";
	}
}

/* ---- layer 3: Vulkan acquire of the leased connector --------------------- */
static int vk_acquire(int drm_fd, uint32_t connector_id, int want_gpu)
{
	const char *exts[] = {
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_DISPLAY_EXTENSION_NAME,
		VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME,
		VK_EXT_ACQUIRE_DRM_DISPLAY_EXTENSION_NAME,
	};
	VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "vk-acquire-drm-display", .apiVersion = VK_API_VERSION_1_1 };
	VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app, .enabledExtensionCount = 4, .ppEnabledExtensionNames = exts };

	VkInstance inst;
	if (vkCreateInstance(&ici, NULL, &inst) != VK_SUCCESS) {
		fprintf(stderr, "FAIL [layer3]: vkCreateInstance — display/acquire extensions unavailable\n");
		return 1;
	}
	PFN_vkGetDrmDisplayEXT GetDrm =
		(PFN_vkGetDrmDisplayEXT)vkGetInstanceProcAddr(inst, "vkGetDrmDisplayEXT");
	PFN_vkAcquireDrmDisplayEXT AcqDrm =
		(PFN_vkAcquireDrmDisplayEXT)vkGetInstanceProcAddr(inst, "vkAcquireDrmDisplayEXT");
	if (!GetDrm || !AcqDrm) {
		fprintf(stderr, "FAIL [layer3]: VK_EXT_acquire_drm_display entry points missing\n");
		return 1;
	}

	uint32_t n = 0;
	vkEnumeratePhysicalDevices(inst, &n, NULL);
	if (n == 0) { fprintf(stderr, "FAIL [layer3]: no Vulkan physical devices\n"); return 1; }
	VkPhysicalDevice *devs = calloc(n, sizeof *devs);
	vkEnumeratePhysicalDevices(inst, &n, devs);

	int acquired = 0;
	for (uint32_t i = 0; i < n; i++) {
		if (want_gpu >= 0 && (int)i != want_gpu) continue;
		VkPhysicalDeviceProperties p;
		vkGetPhysicalDeviceProperties(devs[i], &p);
		printf("GPU %u: %s\n", i, p.deviceName);

		VkDisplayKHR disp = VK_NULL_HANDLE;
		VkResult r = GetDrm(devs[i], drm_fd, connector_id, &disp);
		if (r != VK_SUCCESS || disp == VK_NULL_HANDLE) {
			printf("  vkGetDrmDisplayEXT: FAIL (%s [%d]) — this GPU can't map the connector\n",
			       vkr(r), (int)r);
			continue;
		}
		printf("  vkGetDrmDisplayEXT: PASS (got VkDisplay)\n");

		r = AcqDrm(devs[i], drm_fd, disp);
		if (r != VK_SUCCESS) {
			printf("  vkAcquireDrmDisplayEXT: FAIL (%s [%d])\n", vkr(r), (int)r);
			continue;
		}
		printf("  vkAcquireDrmDisplayEXT: PASS — display acquired on this GPU\n");
		acquired = 1;
		break;
	}

	printf("\nRESULT: %s\n", acquired
		? "PASS — Wayland lease + Vulkan acquire WORKS (a runtime should too; if it"
		  " doesn't, the bug is the runtime's Wayland path)"
		: "FAIL — could not acquire the leased connector in Vulkan (try --gpu N for"
		  " the GPU that drives the headset; otherwise a Mesa/RADV repro)");
	return acquired ? 0 : 1;
}

/* ---- --drm mode: open a connector directly as DRM master (from a TTY) ----- */
static const char *drm_conn_type(uint32_t t)
{
	switch (t) {
	case DRM_MODE_CONNECTOR_DisplayPort: return "DP";
	case DRM_MODE_CONNECTOR_eDP:         return "eDP";
	case DRM_MODE_CONNECTOR_HDMIA:       return "HDMI-A";
	case DRM_MODE_CONNECTOR_HDMIB:       return "HDMI-B";
	default:                             return "conn";
	}
}

/* Find a connected connector advertising the 4000x2040 PSVR2 mode (or whose name
 * matches want_name, e.g. "DP-3") on some /dev/dri/cardN, open it as DRM master,
 * and return the fd + connector id. Returns 0 on success. */
static int drm_find(const char *card, const char *want_name, int *out_fd, uint32_t *out_id)
{
	for (int i = 0; i < 16; i++) {
		char path[64];
		if (card) snprintf(path, sizeof path, "%s", card);
		else      snprintf(path, sizeof path, "/dev/dri/card%d", i);

		int fd = open(path, O_RDWR | O_CLOEXEC);
		if (fd < 0) { if (card) return -1; else continue; }

		drmModeRes *res = drmModeGetResources(fd);
		if (res) {
			for (int c = 0; c < res->count_connectors; c++) {
				drmModeConnector *conn = drmModeGetConnector(fd, res->connectors[c]);
				if (!conn) continue;
				char name[32];
				snprintf(name, sizeof name, "%s-%u",
				         drm_conn_type(conn->connector_type), conn->connector_type_id);
				int match = 0;
				if (conn->connection == DRM_MODE_CONNECTED) {
					if (want_name) {
						match = strstr(name, want_name) != NULL;
					} else {
						for (int m = 0; m < conn->count_modes; m++)
							if (conn->modes[m].hdisplay == 4000 &&
							    conn->modes[m].vdisplay == 2040) { match = 1; break; }
					}
				}
				if (match) {
					printf("DRM: %s -> connector %s (id %u)\n", path, name, conn->connector_id);
					*out_id = conn->connector_id;
					drmModeFreeConnector(conn);
					drmModeFreeResources(res);
					if (drmSetMaster(fd) != 0)
						fprintf(stderr, "  WARN: drmSetMaster failed (%s) — run from a "
						                "TTY with no compositor holding DRM master\n",
						        strerror(errno));
					*out_fd = fd;
					return 0;
				}
				drmModeFreeConnector(conn);
			}
			drmModeFreeResources(res);
		}
		close(fd);
		if (card) return -1;
	}
	return -1;
}

int main(int argc, char **argv)
{
	const char *want_conn = NULL;
	const char *card = NULL;
	int want_gpu = -1;
	int drm_mode = 0;
	static const struct option opts[] = {
		{ "connector", required_argument, 0, 'c' },
		{ "gpu", required_argument, 0, 'g' },
		{ "drm", no_argument, 0, 'd' },
		{ "card", required_argument, 0, 'C' },
		{ 0, 0, 0, 0 },
	};
	int o;
	while ((o = getopt_long(argc, argv, "c:g:dC:", opts, NULL)) != -1) {
		if (o == 'c') want_conn = optarg;
		else if (o == 'g') want_gpu = atoi(optarg);
		else if (o == 'd') drm_mode = 1;
		else if (o == 'C') card = optarg;
	}

	if (drm_mode) {
		int fd; uint32_t cid;
		if (drm_find(card, want_conn, &fd, &cid) != 0) {
			fprintf(stderr, "FAIL [drm]: no connected 4000x2040 connector found "
			                "(headset worn + cable up? run from a TTY as root)\n");
			return 1;
		}
		printf("PASS [drm]: have a DRM master fd for connector id %u\n\n", cid);
		int rc = vk_acquire(fd, cid, want_gpu);
		drmDropMaster(fd);
		close(fd);
		return rc;
	}

	struct wl_display *dpy = wl_display_connect(NULL);
	if (!dpy) {
		fprintf(stderr, "FAIL: no Wayland display — run inside a Wayland session\n");
		return 1;
	}
	struct wl_registry *reg = wl_display_get_registry(dpy);
	wl_registry_add_listener(reg, &reg_listener, NULL);
	wl_display_roundtrip(dpy); /* globals */

	if (!S.device) {
		fprintf(stderr, "FAIL [layer2]: compositor has no wp_drm_lease_device_v1 "
		                "(needs KWin Plasma 6+, Mutter, or wlroots)\n");
		return 1;
	}
	/* drive events until the device finishes advertising its connectors */
	while (!S.device_done) {
		if (wl_display_roundtrip(dpy) < 0) break;
	}
	wl_display_roundtrip(dpy); /* let connector name/id events land */

	printf("Offered lease connectors:\n");
	struct conn *target = NULL, *only = NULL;
	int count = 0;
	for (struct conn *c = S.connectors; c; c = c->next) {
		printf("  - name=%-10s id=%-4u desc=\"%s\"\n",
		       c->name ? c->name : "?", c->connector_id, c->description ? c->description : "");
		if (!c->has_id) continue;
		count++; only = c;
		if (want_conn) {
			if (c->name && strstr(c->name, want_conn)) target = c;
		} else if (c->description &&
		           (strcasestr(c->description, "vr") || strcasestr(c->description, "sie") ||
		            strcasestr(c->description, "psvr"))) {
			target = c; /* auto: a VR/SIE-looking sink */
		}
	}
	if (!target && !want_conn && count == 1) target = only; /* sole offer */

	if (!target) {
		fprintf(stderr, "FAIL [layer2a]: no matching lease connector "
		                "(use --connector NAME from the list above)\n");
		return 1;
	}
	printf("PASS [layer2a]: target connector \"%s\" (id %u) is offered for lease\n",
	       target->name ? target->name : "?", target->connector_id);

	/* request the lease */
	struct wp_drm_lease_request_v1 *req =
		wp_drm_lease_device_v1_create_lease_request(S.device);
	wp_drm_lease_request_v1_request_connector(req, target->obj);
	S.lease = wp_drm_lease_request_v1_submit(req);
	wp_drm_lease_v1_add_listener(S.lease, &lease_listener, NULL);

	while (S.lease_fd < 0 && !S.lease_finished) {
		if (wl_display_roundtrip(dpy) < 0) break;
	}
	if (S.lease_fd < 0) {
		fprintf(stderr, "FAIL [layer2b]: compositor refused the lease (sent 'finished')\n");
		return 1;
	}
	printf("PASS [layer2b]: compositor granted the lease (drm fd %d)\n\n", S.lease_fd);

	return vk_acquire(S.lease_fd, target->connector_id, want_gpu);
}
