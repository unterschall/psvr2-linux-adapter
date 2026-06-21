# SPDX-License-Identifier: GPL-2.0
#
# Out-of-tree build for the PSVR2 kernel module.
# Dual-purpose: kbuild reads the obj-m lines; a direct `make` runs the targets.

obj-m := psvr2.o
psvr2-y := psvr2_usb.o psvr2_status.o psvr2_imu.o psvr2_input.o psvr2_slam.o \
	   psvr2_camera.o psvr2_gaze.o psvr2_aux.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

modules_install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install

.PHONY: all clean modules_install
