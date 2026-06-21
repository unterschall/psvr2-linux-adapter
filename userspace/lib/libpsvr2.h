/* SPDX-License-Identifier: GPL-2.0 */
/*
 * libpsvr2 — userspace access to the PSVR2 kernel module's device nodes.
 *
 * Wraps the raw interfaces exposed by the psvr2 module (the /dev/psvr2-pose and
 * /dev/psvr2-gaze character devices, the psvr2_imu IIO device, the V4L2 camera
 * node, and the brightness sysfs attribute) behind a small C API that returns
 * host-native floats. Wire data is little-endian IEEE-754; this library does the
 * reinterpretation and IIO scaling for you.
 *
 * Copyright (C) 2026 PSVR2 Linux project
 */
#ifndef LIBPSVR2_H
#define LIBPSVR2_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct psvr2 psvr2_t;

/* 6DoF pose from the headset's onboard tracker, in the device's native frame. */
struct psvr2_pose {
	uint64_t timestamp_ns;		/* host CLOCK_MONOTONIC */
	uint32_t device_vts_us;
	int	 valid;
	float	 position[3];		/* metres */
	float	 orientation[4];	/* quaternion: w, x, y, z */
};

struct psvr2_eye {
	int	gaze_point_valid;
	float	gaze_point_mm[3];
	int	gaze_direction_valid;
	float	gaze_direction[3];
	int	pupil_diameter_valid;
	float	pupil_diameter_mm;
	int	blink_valid;
	int	blink;			/* 0 = open, 1 = closed */
};

struct psvr2_gaze {
	uint64_t timestamp_ns;
	uint32_t device_timestamp_us;
	int	 valid;
	struct psvr2_eye left;
	struct psvr2_eye right;
	struct {
		int	gaze_point_valid;
		float	gaze_point_mm[3];
		int	gaze_direction_valid;
		float	gaze_direction[3];	/* normalised */
	} combined;
};

/* Latest scaled IMU sample (m/s^2 and rad/s), device-native axes. */
struct psvr2_imu {
	float accel_m_s2[3];
	float gyro_rad_s[3];
};

/*
 * Open the headset. Discovers whichever interfaces the module has exposed;
 * missing ones simply make the corresponding calls return -1 / no data.
 * Returns NULL only on allocation failure (a headset with no nodes still opens).
 */
psvr2_t *psvr2_open(void);
void psvr2_close(psvr2_t *p);

/* True (1) if the named interface was found. */
int psvr2_has_pose(const psvr2_t *p);
int psvr2_has_gaze(const psvr2_t *p);
int psvr2_has_imu(const psvr2_t *p);

/* Path of the V4L2 camera node (e.g. "/dev/video3"), or NULL if not found. */
const char *psvr2_camera_path(const psvr2_t *p);

/* File descriptors for poll()/select(), or -1 if unavailable. */
int psvr2_pose_fd(const psvr2_t *p);
int psvr2_gaze_fd(const psvr2_t *p);

/*
 * Read one sample. With block != 0 the call waits for data; otherwise it returns
 * 0 immediately if none is pending. Returns 1 on a sample, 0 if none (nonblock),
 * -1 on error.
 */
int psvr2_read_pose(psvr2_t *p, struct psvr2_pose *out, int block);
int psvr2_read_gaze(psvr2_t *p, struct psvr2_gaze *out, int block);

/* Read the latest IMU sample (scaled). Returns 0 on success, -1 on error. */
int psvr2_read_imu(psvr2_t *p, struct psvr2_imu *out);

/* Set panel brightness, 0..31. Returns 0 on success, -1 on error. */
int psvr2_set_brightness(psvr2_t *p, int level);

#ifdef __cplusplus
}
#endif

#endif /* LIBPSVR2_H */
