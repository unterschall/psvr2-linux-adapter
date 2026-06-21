// SPDX-License-Identifier: GPL-2.0
/*
 * psvr2-monitor — example consumer of libpsvr2.
 *
 * Opens the headset and prints a live one-line status combining IMU, 6DoF pose
 * and combined gaze direction, draining the pose/gaze streams via poll().
 *
 * Build:  make   (in userspace/lib)
 * Run:    ./psvr2-monitor
 *
 * Copyright (C) 2026 PSVR2 Linux project
 */
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

#include "libpsvr2.h"

int main(void)
{
	psvr2_t *p = psvr2_open();
	struct psvr2_pose pose = { 0 };
	struct psvr2_gaze gaze = { 0 };

	if (!p) {
		fprintf(stderr, "psvr2_open failed\n");
		return 1;
	}

	printf("interfaces: imu=%d pose=%d gaze=%d camera=%s\n",
	       psvr2_has_imu(p), psvr2_has_pose(p), psvr2_has_gaze(p),
	       psvr2_camera_path(p) ? psvr2_camera_path(p) : "(none)");

	for (;;) {
		struct pollfd pfds[2];
		int n = 0, pose_idx = -1, gaze_idx = -1;
		struct psvr2_imu imu;

		if (psvr2_pose_fd(p) >= 0) {
			pfds[n].fd = psvr2_pose_fd(p);
			pfds[n].events = POLLIN;
			pose_idx = n++;
		}
		if (psvr2_gaze_fd(p) >= 0) {
			pfds[n].fd = psvr2_gaze_fd(p);
			pfds[n].events = POLLIN;
			gaze_idx = n++;
		}

		if (n)
			poll(pfds, n, 100);
		else
			usleep(100000);

		/* Drain to the most recent sample of each stream. */
		if (pose_idx >= 0 && (pfds[pose_idx].revents & POLLIN))
			while (psvr2_read_pose(p, &pose, 0) == 1)
				;
		if (gaze_idx >= 0 && (pfds[gaze_idx].revents & POLLIN))
			while (psvr2_read_gaze(p, &gaze, 0) == 1)
				;

		printf("\r");
		if (psvr2_read_imu(p, &imu) == 0)
			printf("imu a[% .1f % .1f % .1f] g[% .2f % .2f % .2f] ",
			       imu.accel_m_s2[0], imu.accel_m_s2[1],
			       imu.accel_m_s2[2], imu.gyro_rad_s[0],
			       imu.gyro_rad_s[1], imu.gyro_rad_s[2]);
		if (pose.valid)
			printf("| pos[% .2f % .2f % .2f] ", pose.position[0],
			       pose.position[1], pose.position[2]);
		if (gaze.valid && gaze.combined.gaze_direction_valid)
			printf("| gaze[% .2f % .2f % .2f] ",
			       gaze.combined.gaze_direction[0],
			       gaze.combined.gaze_direction[1],
			       gaze.combined.gaze_direction[2]);
		printf("        ");
		fflush(stdout);
	}

	psvr2_close(p);
	return 0;
}
