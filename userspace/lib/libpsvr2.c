// SPDX-License-Identifier: GPL-2.0
/*
 * libpsvr2 — userspace access to the PSVR2 kernel module's device nodes.
 *
 * Copyright (C) 2026 PSVR2 Linux project
 */
#include <dirent.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libpsvr2.h"
#include "psvr2_uapi.h"		/* kernel wire structs (-I kernel/) */

#define POSE_DEV "/dev/psvr2-pose"
#define GAZE_DEV "/dev/psvr2-gaze"
#define IIO_DIR  "/sys/bus/iio/devices"
#define V4L_DIR  "/sys/class/video4linux"
#define DRV_DIR  "/sys/bus/usb/drivers/psvr2"

struct psvr2 {
	int	pose_fd;
	int	gaze_fd;
	char	imu_dir[300];		/* IIO device dir, "" if none */
	double	accel_scale;
	double	gyro_scale;
	char	cam_path[300];		/* "/dev/videoN", "" if none */
	char	bright_path[400];	/* brightness sysfs attr, "" if none */
};

/* ---- small sysfs helpers ------------------------------------------------ */

static int read_file_str(const char *path, char *buf, size_t len)
{
	int fd = open(path, O_RDONLY);
	ssize_t n;

	if (fd < 0)
		return -1;
	n = read(fd, buf, len - 1);
	close(fd);
	if (n < 0)
		return -1;
	buf[n] = '\0';
	while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == ' '))
		buf[--n] = '\0';
	return 0;
}

static int name_matches(const char *dir, const char *entry, const char *want)
{
	char path[512], name[128];

	snprintf(path, sizeof(path), "%s/%s/name", dir, entry);
	if (read_file_str(path, name, sizeof(name)))
		return 0;
	return strcmp(name, want) == 0;
}

static float f_from_le(uint32_t le)
{
	uint32_t host = le32toh(le);
	float f;

	memcpy(&f, &host, sizeof(f));
	return f;
}

/* ---- discovery ---------------------------------------------------------- */

static void find_imu(struct psvr2 *p)
{
	DIR *d = opendir(IIO_DIR);
	struct dirent *e;

	if (!d)
		return;
	while ((e = readdir(d))) {
		char path[600], val[64];

		if (strncmp(e->d_name, "iio:device", 10))
			continue;
		if (!name_matches(IIO_DIR, e->d_name, "psvr2_imu"))
			continue;
		snprintf(p->imu_dir, sizeof(p->imu_dir), "%s/%s", IIO_DIR,
			 e->d_name);
		snprintf(path, sizeof(path), "%s/in_accel_scale", p->imu_dir);
		if (!read_file_str(path, val, sizeof(val)))
			p->accel_scale = strtod(val, NULL);
		snprintf(path, sizeof(path), "%s/in_anglvel_scale", p->imu_dir);
		if (!read_file_str(path, val, sizeof(val)))
			p->gyro_scale = strtod(val, NULL);
		break;
	}
	closedir(d);
}

static void find_camera(struct psvr2 *p)
{
	DIR *d = opendir(V4L_DIR);
	struct dirent *e;

	if (!d)
		return;
	while ((e = readdir(d))) {
		if (strncmp(e->d_name, "video", 5))
			continue;
		if (!name_matches(V4L_DIR, e->d_name, "PlayStation VR2 Cameras"))
			continue;
		snprintf(p->cam_path, sizeof(p->cam_path), "/dev/%s", e->d_name);
		break;
	}
	closedir(d);
}

static void find_brightness(struct psvr2 *p)
{
	DIR *d = opendir(DRV_DIR);
	struct dirent *e;

	if (!d)
		return;
	while ((e = readdir(d))) {
		char path[400];

		if (e->d_name[0] == '.' || strlen(e->d_name) > 64)
			continue;
		snprintf(path, sizeof(path), "%s/%s/brightness", DRV_DIR,
			 e->d_name);
		if (access(path, W_OK | R_OK) == 0 ||
		    access(path, F_OK) == 0) {
			snprintf(p->bright_path, sizeof(p->bright_path), "%s",
				 path);
			break;
		}
	}
	closedir(d);
}

/* ---- lifecycle ---------------------------------------------------------- */

psvr2_t *psvr2_open(void)
{
	struct psvr2 *p = calloc(1, sizeof(*p));

	if (!p)
		return NULL;

	p->pose_fd = open(POSE_DEV, O_RDONLY | O_NONBLOCK);
	p->gaze_fd = open(GAZE_DEV, O_RDONLY | O_NONBLOCK);
	find_imu(p);
	find_camera(p);
	find_brightness(p);
	return p;
}

void psvr2_close(psvr2_t *p)
{
	if (!p)
		return;
	if (p->pose_fd >= 0)
		close(p->pose_fd);
	if (p->gaze_fd >= 0)
		close(p->gaze_fd);
	free(p);
}

int psvr2_has_pose(const psvr2_t *p) { return p && p->pose_fd >= 0; }
int psvr2_has_gaze(const psvr2_t *p) { return p && p->gaze_fd >= 0; }
int psvr2_has_imu(const psvr2_t *p) { return p && p->imu_dir[0] != '\0'; }

const char *psvr2_camera_path(const psvr2_t *p)
{
	return (p && p->cam_path[0]) ? p->cam_path : NULL;
}

int psvr2_pose_fd(const psvr2_t *p) { return p ? p->pose_fd : -1; }
int psvr2_gaze_fd(const psvr2_t *p) { return p ? p->gaze_fd : -1; }

/* ---- streams ------------------------------------------------------------ */

static int read_sample(int fd, void *buf, size_t sz, int block)
{
	ssize_t n;

	if (fd < 0)
		return -1;
	if (block) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };

		if (poll(&pfd, 1, -1) < 0)
			return -1;
	}
	n = read(fd, buf, sz);
	if (n == (ssize_t)sz)
		return 1;
	if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
		return 0;
	return -1;
}

int psvr2_read_pose(psvr2_t *p, struct psvr2_pose *out, int block)
{
	struct psvr2_pose_sample s;
	int r;

	if (!p || !out)
		return -1;
	r = read_sample(p->pose_fd, &s, sizeof(s), block);
	if (r != 1)
		return r;

	out->timestamp_ns = s.timestamp_ns;
	out->device_vts_us = s.device_vts_us;
	out->valid = (s.flags & PSVR2_POSE_FLAG_VALID) != 0;
	for (int i = 0; i < 3; i++)
		out->position[i] = f_from_le(s.position[i]);
	for (int i = 0; i < 4; i++)
		out->orientation[i] = f_from_le(s.orientation[i]);
	return 1;
}

int psvr2_read_gaze(psvr2_t *p, struct psvr2_gaze *out, int block)
{
	struct psvr2_gaze_sample s;
	int r;

	if (!p || !out)
		return -1;
	r = read_sample(p->gaze_fd, &s, sizeof(s), block);
	if (r != 1)
		return r;

	out->timestamp_ns = s.timestamp_ns;
	out->device_timestamp_us = s.device_timestamp_us;
	out->valid = (s.flags & PSVR2_GAZE_FLAG_VALID) != 0;

#define COPY_EYE(dst, src)						\
	do {								\
		(dst).gaze_point_valid = (src).gaze_point_valid;	\
		(dst).gaze_direction_valid = (src).gaze_direction_valid;\
		(dst).pupil_diameter_valid = (src).pupil_diameter_valid;\
		(dst).pupil_diameter_mm =				\
			f_from_le((src).pupil_diameter_mm);		\
		(dst).blink_valid = (src).blink_valid;			\
		(dst).blink = (src).blink;				\
		for (int i = 0; i < 3; i++) {				\
			(dst).gaze_point_mm[i] =			\
				f_from_le((src).gaze_point_mm[i]);	\
			(dst).gaze_direction[i] =			\
				f_from_le((src).gaze_direction[i]);	\
		}							\
	} while (0)

	COPY_EYE(out->left, s.left);
	COPY_EYE(out->right, s.right);
#undef COPY_EYE

	out->combined.gaze_point_valid = s.combined.gaze_point_valid;
	out->combined.gaze_direction_valid = s.combined.gaze_direction_valid;
	for (int i = 0; i < 3; i++) {
		out->combined.gaze_point_mm[i] =
			f_from_le(s.combined.gaze_point_mm[i]);
		out->combined.gaze_direction[i] =
			f_from_le(s.combined.gaze_direction[i]);
	}
	return 1;
}

int psvr2_read_imu(psvr2_t *p, struct psvr2_imu *out)
{
	static const char *accel[3] = {
		"in_accel_x_raw", "in_accel_y_raw", "in_accel_z_raw" };
	static const char *gyro[3] = {
		"in_anglvel_x_raw", "in_anglvel_y_raw", "in_anglvel_z_raw" };

	if (!p || !out || !p->imu_dir[0])
		return -1;

	for (int i = 0; i < 3; i++) {
		char path[400], val[32];

		snprintf(path, sizeof(path), "%s/%s", p->imu_dir, accel[i]);
		if (read_file_str(path, val, sizeof(val)))
			return -1;
		out->accel_m_s2[i] = strtol(val, NULL, 10) * p->accel_scale;

		snprintf(path, sizeof(path), "%s/%s", p->imu_dir, gyro[i]);
		if (read_file_str(path, val, sizeof(val)))
			return -1;
		out->gyro_rad_s[i] = strtol(val, NULL, 10) * p->gyro_scale;
	}
	return 0;
}

int psvr2_set_brightness(psvr2_t *p, int level)
{
	char buf[8];
	int fd, n;

	if (!p || !p->bright_path[0])
		return -1;
	if (level < 0)
		level = 0;
	if (level > 31)
		level = 31;

	fd = open(p->bright_path, O_WRONLY);
	if (fd < 0)
		return -1;
	n = snprintf(buf, sizeof(buf), "%d\n", level);
	n = (write(fd, buf, n) == n) ? 0 : -1;
	close(fd);
	return n;
}
