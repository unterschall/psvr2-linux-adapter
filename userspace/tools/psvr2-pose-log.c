// SPDX-License-Identifier: GPL-2.0
/*
 * psvr2-pose-log — log the 6DoF pose stream as newline-separated samples for
 * offline analysis (e.g. verifying the position/quaternion axis convention).
 *
 * Each line:  <rel_ms> px py pz  qw qx qy qz  <valid>
 * with position in the device's native units and the quaternion in wire order
 * (qw first). Rate-limited to ~30 Hz. Optional first arg = device path.
 *
 * Build:  cc -O2 -o psvr2-pose-log psvr2-pose-log.c
 * Run:    ./psvr2-pose-log > /tmp/pose-log.txt
 *
 * Copyright (C) 2026 PSVR2 Linux project
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct psvr2_pose_sample {
	uint64_t timestamp_ns;
	uint32_t device_vts_us;
	uint32_t flags;
	uint32_t position[3];
	uint32_t orientation[4];
};

#define VALID 0x1u
#define MIN_INTERVAL_NS 33000000ULL	/* ~30 Hz */

static float f(uint32_t bits)
{
	float v;

	memcpy(&v, &bits, sizeof(v));
	return v;
}

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/psvr2-pose";
	struct psvr2_pose_sample s;
	uint64_t t0 = 0, last = 0;
	int fd = open(dev, O_RDONLY);

	if (fd < 0) {
		perror("open");
		fprintf(stderr, "%s — is the module loaded and headset in VR mode?\n", dev);
		return 1;
	}

	printf("# rel_ms  px py pz  qw qx qy qz  valid\n");
	for (;;) {
		ssize_t n = read(fd, &s, sizeof(s));

		if (n <= 0)
			break;
		if (n != sizeof(s))
			continue;
		if (!t0)
			t0 = s.timestamp_ns;
		if (s.timestamp_ns - last < MIN_INTERVAL_NS)
			continue;
		last = s.timestamp_ns;

		printf("%8.1f  % .4f % .4f % .4f  % .4f % .4f % .4f % .4f  %d\n",
		       (s.timestamp_ns - t0) / 1e6,
		       f(s.position[0]), f(s.position[1]), f(s.position[2]),
		       f(s.orientation[0]), f(s.orientation[1]),
		       f(s.orientation[2]), f(s.orientation[3]),
		       (s.flags & VALID) ? 1 : 0);
		fflush(stdout);
	}
	close(fd);
	return 0;
}
