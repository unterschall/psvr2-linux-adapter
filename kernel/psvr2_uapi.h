/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * PSVR2 userspace ABI.
 *
 * The character device /dev/psvr2-pose delivers a stream of fixed-size
 * struct psvr2_pose_sample records (read() returns whole samples; poll()
 * signals POLLIN when samples are available). These come from the headset's
 * onboard 6DoF tracker (USB interface 3, "SLA" packets).
 *
 * Copyright (C) 2026 PSVR2 Linux project
 */
#ifndef _UAPI_PSVR2_H_
#define _UAPI_PSVR2_H_

#include <linux/types.h>

/*
 * position[] and orientation[] are IEEE-754 little-endian 32-bit floats carried
 * as raw bit patterns (reinterpret in userspace, e.g. memcpy into float[]).
 * They are reported in the device's native wire order.
 *
 * Axis convention (device-native raw axes), per the Monado PSVR2 driver's
 * process_slam_record remap, which we adopt as the reference:
 *   position[0]: FORWARD   position[1]: UP        position[2]: RIGHT
 *   orientation[0..3] = quaternion (w, x, y, z) in the same raw frame.
 *
 * To convert into a right-handed +x=right, +y=up, -z=forward frame (OpenVR /
 * OpenXR), apply:
 *   out.position    = ( pos[2],  pos[1], -pos[0] )
 *   out.orientation = ( w=orient[0], x=-orient[2], y=-orient[1], z=orient[3] )
 * See steamvr/driver_psvr2/src/pose_source.cpp for the implementation.
 *
 * The module itself applies NO transform — it carries the raw wire values
 * through; the remap lives in userspace so the convention can change without a
 * module rebuild.
 */
struct psvr2_pose_sample {
	__u64	timestamp_ns;	/* host CLOCK_MONOTONIC receive time */
	__u32	device_vts_us;	/* device video timestamp (microseconds) */
	__u32	flags;		/* PSVR2_POSE_FLAG_* */
	__le32	position[3];	/* float32 LE bit patterns */
	__le32	orientation[4];	/* float32 LE bit patterns, [0] = w */
};

#define PSVR2_POSE_FLAG_VALID	(1u << 0)	/* well-formed SLP record */

/*
 * Eye / gaze tracking. The character device /dev/psvr2-gaze delivers a stream
 * of struct psvr2_gaze_sample records (read() returns whole samples; poll()
 * signals POLLIN). Only the well-understood fields are surfaced.
 *
 * All vectors/scalars are IEEE-754 little-endian floats carried as raw bits.
 * Positions and pupil diameter are in millimetres, in the device's native
 * wire frame (Monado negates x and z — apply in userspace if desired).
 * Directions are unnormalised gaze vectors. Each field has a *_valid flag.
 */
struct psvr2_gaze_eye {
	__u32	gaze_point_valid;
	__le32	gaze_point_mm[3];
	__u32	gaze_direction_valid;
	__le32	gaze_direction[3];
	__u32	pupil_diameter_valid;
	__le32	pupil_diameter_mm;
	__u32	blink_valid;
	__u32	blink;			/* 0 = open, 1 = closed */
};

struct psvr2_gaze_combined {
	__u32	gaze_point_valid;
	__le32	gaze_point_mm[3];
	__u32	gaze_direction_valid;
	__le32	gaze_direction[3];	/* normalised gaze */
};

struct psvr2_gaze_sample {
	__u64	timestamp_ns;		/* host CLOCK_MONOTONIC receive time */
	__u32	device_timestamp_us;	/* device sample timestamp */
	__u32	flags;			/* PSVR2_GAZE_FLAG_* */
	struct psvr2_gaze_eye		left;
	struct psvr2_gaze_eye		right;
	struct psvr2_gaze_combined	combined;
};

#define PSVR2_GAZE_FLAG_VALID	(1u << 0)	/* well-formed "GS" packet */

#endif /* _UAPI_PSVR2_H_ */
