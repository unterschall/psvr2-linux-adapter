// SPDX-License-Identifier: GPL-2.0
/*
 * psvr2-imu-test — smoke test for the psvr2 kernel module.
 *
 * Prints the latest scaled accelerometer/gyroscope values from the IIO device
 * and live function-button / proximity / IPD transitions from the input device.
 * This reads the IIO sysfs "raw" + "scale" attributes (no libiio dependency);
 * for high-rate buffered capture use `iio_readdev` against the same device.
 *
 * Build:  cc -O2 -o psvr2-imu-test psvr2-imu-test.c
 * Run:    ./psvr2-imu-test
 *
 * Copyright (C) 2026 PSVR2 Linux project
 */
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define IIO_DIR "/sys/bus/iio/devices"
#define INPUT_DIR "/dev/input"

/* Find the iio:deviceN whose "name" attribute equals want. */
static int find_iio_device(const char *want, char *out, size_t outsz)
{
	DIR *d = opendir(IIO_DIR);
	struct dirent *e;
	int found = -1;

	if (!d)
		return -1;
	while ((e = readdir(d))) {
		char path[512], name[128] = {0};
		FILE *f;

		if (strncmp(e->d_name, "iio:device", 10))
			continue;
		snprintf(path, sizeof(path), "%s/%s/name", IIO_DIR, e->d_name);
		f = fopen(path, "r");
		if (!f)
			continue;
		if (fgets(name, sizeof(name), f))
			name[strcspn(name, "\n")] = 0;
		fclose(f);
		if (!strcmp(name, want)) {
			snprintf(out, outsz, "%s/%s", IIO_DIR, e->d_name);
			found = 0;
			break;
		}
	}
	closedir(d);
	return found;
}

static double read_attr(const char *dir, const char *attr)
{
	char path[512];
	double v = 0;
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s", dir, attr);
	f = fopen(path, "r");
	if (!f)
		return 0;
	if (fscanf(f, "%lf", &v) != 1)
		v = 0;
	fclose(f);
	return v;
}

/* Find the /dev/input/eventN whose name matches the PSVR2 controls device. */
static int open_input_device(void)
{
	DIR *d = opendir(INPUT_DIR);
	struct dirent *e;
	int fd = -1;

	if (!d)
		return -1;
	while ((e = readdir(d))) {
		char path[512], name[256] = {0};
		int test;

		if (strncmp(e->d_name, "event", 5))
			continue;
		snprintf(path, sizeof(path), "%s/%s", INPUT_DIR, e->d_name);
		test = open(path, O_RDONLY | O_NONBLOCK);
		if (test < 0)
			continue;
		if (ioctl(test, EVIOCGNAME(sizeof(name)), name) >= 0 &&
		    strstr(name, "PlayStation VR2")) {
			fd = test;
			break;
		}
		close(test);
	}
	closedir(d);
	return fd;
}

int main(void)
{
	char iio[512];
	int input_fd;
	double accel_scale, gyro_scale;

	if (find_iio_device("psvr2_imu", iio, sizeof(iio))) {
		fprintf(stderr,
			"psvr2_imu IIO device not found — is the module loaded "
			"and the headset connected?\n");
		return 1;
	}
	printf("IIO device: %s\n", iio);

	accel_scale = read_attr(iio, "in_accel_scale");
	gyro_scale = read_attr(iio, "in_anglvel_scale");
	printf("accel scale = %g m/s^2/LSB, gyro scale = %g rad/s/LSB\n",
	       accel_scale, gyro_scale);

	input_fd = open_input_device();
	if (input_fd < 0)
		fprintf(stderr, "warning: PSVR2 input device not found\n");

	printf("\nStreaming (Ctrl-C to stop)...\n");
	for (;;) {
		struct pollfd pfd = { .fd = input_fd, .events = POLLIN };
		double ax, ay, az, gx, gy, gz;

		ax = read_attr(iio, "in_accel_x_raw") * accel_scale;
		ay = read_attr(iio, "in_accel_y_raw") * accel_scale;
		az = read_attr(iio, "in_accel_z_raw") * accel_scale;
		gx = read_attr(iio, "in_anglvel_x_raw") * gyro_scale;
		gy = read_attr(iio, "in_anglvel_y_raw") * gyro_scale;
		gz = read_attr(iio, "in_anglvel_z_raw") * gyro_scale;

		printf("\raccel[% .2f % .2f % .2f] m/s^2  gyro[% .3f % .3f % .3f] rad/s   ",
		       ax, ay, az, gx, gy, gz);
		fflush(stdout);

		if (input_fd >= 0 && poll(&pfd, 1, 100) > 0) {
			struct input_event ev;

			while (read(input_fd, &ev, sizeof(ev)) == sizeof(ev)) {
				if (ev.type == EV_KEY && ev.code == BTN_MODE)
					printf("\n[function button %s]\n",
					       ev.value ? "pressed" : "released");
				else if (ev.type == EV_SW &&
					 ev.code == SW_FRONT_PROXIMITY)
					printf("\n[headset %s]\n",
					       ev.value ? "worn" : "removed");
				else if (ev.type == EV_ABS && ev.code == ABS_MISC)
					printf("\n[IPD = %d mm]\n", ev.value);
			}
		} else {
			usleep(100000);
		}
	}
	return 0;
}
