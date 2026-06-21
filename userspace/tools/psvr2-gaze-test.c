// SPDX-License-Identifier: GPL-2.0
/*
 * psvr2-gaze-test — read the eye-tracking stream from /dev/psvr2-gaze.
 *
 * The kernel module delivers fixed-size struct psvr2_gaze_sample records.
 * Vectors/scalars are little-endian IEEE-754 floats carried as raw bits;
 * positions/pupil are in millimetres.
 *
 * Build:  cc -O2 -o psvr2-gaze-test psvr2-gaze-test.c
 * Run:    ./psvr2-gaze-test
 *
 * Copyright (C) 2026 PSVR2 Linux project
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct psvr2_gaze_eye {
	uint32_t gaze_point_valid;
	uint32_t gaze_point_mm[3];
	uint32_t gaze_direction_valid;
	uint32_t gaze_direction[3];
	uint32_t pupil_diameter_valid;
	uint32_t pupil_diameter_mm;
	uint32_t blink_valid;
	uint32_t blink;
};

struct psvr2_gaze_combined {
	uint32_t gaze_point_valid;
	uint32_t gaze_point_mm[3];
	uint32_t gaze_direction_valid;
	uint32_t gaze_direction[3];
};

struct psvr2_gaze_sample {
	uint64_t timestamp_ns;
	uint32_t device_timestamp_us;
	uint32_t flags;
	struct psvr2_gaze_eye left;
	struct psvr2_gaze_eye right;
	struct psvr2_gaze_combined combined;
};

#define DEV "/dev/psvr2-gaze"

static float f(uint32_t bits)
{
	float v;

	memcpy(&v, &bits, sizeof(v));
	return v;
}

int main(void)
{
	struct psvr2_gaze_sample s;
	int fd = open(DEV, O_RDONLY);

	if (fd < 0) {
		perror("open " DEV);
		fprintf(stderr, "Is the module loaded and headset connected?\n");
		return 1;
	}

	printf("Streaming gaze from %s (Ctrl-C to stop)...\n", DEV);
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

		printf("\rcombined dir[% .3f % .3f % .3f] valid=%u | "
		       "L pupil %.2fmm blink %u | R pupil %.2fmm blink %u   ",
		       f(s.combined.gaze_direction[0]),
		       f(s.combined.gaze_direction[1]),
		       f(s.combined.gaze_direction[2]),
		       s.combined.gaze_direction_valid,
		       f(s.left.pupil_diameter_mm), s.left.blink,
		       f(s.right.pupil_diameter_mm), s.right.blink);
		fflush(stdout);
	}

	close(fd);
	return 0;
}
