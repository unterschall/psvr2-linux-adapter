// SPDX-License-Identifier: GPL-2.0
/*
 * psvr2-pose-test — read the 6DoF pose stream from /dev/psvr2-pose.
 *
 * The kernel module delivers fixed-size struct psvr2_pose_sample records from
 * the headset's onboard SLAM tracker. Position/orientation are little-endian
 * IEEE-754 floats carried as raw bits; we reinterpret them here.
 *
 * Build:  cc -O2 -o psvr2-pose-test psvr2-pose-test.c
 * Run:    ./psvr2-pose-test           (needs read access to /dev/psvr2-pose)
 *
 * Copyright (C) 2026 PSVR2 Linux project
 */
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Mirrors struct psvr2_pose_sample from kernel/psvr2_uapi.h. */
struct psvr2_pose_sample {
	uint64_t timestamp_ns;
	uint32_t device_vts_us;
	uint32_t flags;
	uint32_t position[3];    /* float32 LE bit patterns */
	uint32_t orientation[4]; /* float32 LE bit patterns, [0] = w */
};

#define PSVR2_POSE_FLAG_VALID 0x1u
#define DEV "/dev/psvr2-pose"

static float as_float(uint32_t bits)
{
	float f;

	memcpy(&f, &bits, sizeof(f));
	return f;
}

int main(void)
{
	struct psvr2_pose_sample s;
	int fd;

	fd = open(DEV, O_RDONLY);
	if (fd < 0) {
		perror("open " DEV);
		fprintf(stderr,
			"Is the module loaded and the headset connected?\n");
		return 1;
	}

	printf("Streaming pose from %s (Ctrl-C to stop)...\n", DEV);
	for (;;) {
		ssize_t n = read(fd, &s, sizeof(s));

		if (n < 0) {
			perror("read");
			break;
		}
		if (n == 0) {
			fprintf(stderr, "\ndevice disconnected\n");
			break;
		}
		if (n != sizeof(s))
			continue;

		printf("\rt=%llu vts=%u pos[% .3f % .3f % .3f] "
		       "quat[w% .3f x% .3f y% .3f z% .3f] %s   ",
		       (unsigned long long)s.timestamp_ns, s.device_vts_us,
		       as_float(s.position[0]), as_float(s.position[1]),
		       as_float(s.position[2]), as_float(s.orientation[0]),
		       as_float(s.orientation[1]), as_float(s.orientation[2]),
		       as_float(s.orientation[3]),
		       (s.flags & PSVR2_POSE_FLAG_VALID) ? "" : "(invalid)");
		fflush(stdout);
	}

	close(fd);
	return 0;
}
