# SPDX-License-Identifier: GPL-2.0
CFLAGS ?= -O2 -Wall -Wextra
BINS := psvr2-imu-test psvr2-pose-test psvr2-gaze-test psvr2-pose-log

all: $(BINS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

# Atomic-KMS display helper — needs libdrm, so it is a separate explicit target
# (not in `all`) to avoid requiring libdrm everywhere.
psvr2-kms-modeset: psvr2-kms-modeset.c
	$(CC) $(CFLAGS) $(shell pkg-config --cflags libdrm) -o $@ $< $(shell pkg-config --libs libdrm)

# Guided on-headset pose capture — needs libdrm; renders text via font8x8.h.
psvr2-pose-guide: psvr2-pose-guide.c font8x8.h
	$(CC) $(CFLAGS) $(shell pkg-config --cflags libdrm) -o $@ $< $(shell pkg-config --libs libdrm)

clean:
	$(RM) $(BINS) psvr2-kms-modeset psvr2-pose-guide

.PHONY: all clean
