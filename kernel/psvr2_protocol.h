/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PSVR2 USB wire protocol structures.
 *
 * Layouts ported from the Monado PSVR2 driver (psvr2_protocol.h, BSL-1.0) and
 * cross-checked against Sony's SIE OSS headers (sieimu.h struct sieimu_data).
 * BSL-1.0 is permissive and compatible with this GPL-2.0 module.
 *
 * Copyright 2023 Jan Schmidt; 2024 Joel Valenciano; 2025 Beyley Cardellio
 * Copyright (C) 2026 PSVR2 Linux project (kernel port)
 */
#ifndef _PSVR2_PROTOCOL_H_
#define _PSVR2_PROTOCOL_H_

#include <linux/types.h>

/* Report IDs for vendor control transfers (ep0). */
enum psvr2_report_id {
	PSVR2_REPORT_SET_PERIPHERAL	= 0x08,	/* subcmd 0x01 = motor/rumble */
	PSVR2_REPORT_SET_CAMERA_MODE	= 0x0b,
	PSVR2_REPORT_SET_GAZE_STREAM	= 0x0c,
	PSVR2_REPORT_SET_GAZE_USER_CAL	= 0x0d,
	PSVR2_REPORT_SET_BRIGHTNESS	= 0x12,
};

#define PSVR2_SET_PERIPHERAL_SUBCMD_MOTOR	0x01

/*
 * Camera modes (report 0x0b). Many modes interleave several views in exotic
 * packings; only a few are decoded. Mode 1 is the simplest: two 640x640 8-bit
 * greyscale bottom-camera images side by side (1280x640) after the header.
 */
enum psvr2_camera_mode {
	PSVR2_CAMERA_MODE_OFF			= 0x0,
	PSVR2_CAMERA_MODE_BOTTOM_SBS_CROPPED	= 0x1,	/* 1280x640 GREY */
	PSVR2_CAMERA_MODE_CONTROLLER_TRACKING	= 0x4,	/* 512x508 fisheye T/B */
	PSVR2_CAMERA_MODE_INTERLEAVED_TB	= 0xa,	/* bottom+top interleaved */
	/*
	 * The SLAM tracking mode (verified on hardware): once the headset is in
	 * VR mode, setting 0x10 streams 1040640-byte frames and the onboard
	 * tracker emits pose records on the SLAM endpoint. (The earlier "0xa is
	 * the SLAM mode" note was a hex/decimal misread.) Frames pack three views
	 * — one L8 254x508 + two R8G8B8, 8-byte interleaved, 256-byte header,
	 * 16-byte line tail; see the reference driver's img_xfer_cb. Decoding
	 * these into V4L2 is future work (docs/roadmap.md "Camera modes > 1").
	 */
	PSVR2_CAMERA_MODE_TRACKING		= 0x10,
};

/*
 * Vendor control transfer payload. Sent/received on ep0 with:
 *   set: bRequestType = VENDOR|RECIPIENT_ENDPOINT,        bRequest = 0x09
 *   get: bRequestType = VENDOR|RECIPIENT_ENDPOINT|DIR_IN, bRequest = 0x01
 *   wValue = report_id, wIndex = 0
 */
#define PSVR2_CTRL_DATA_MAX	(512 - 8)

struct sie_ctrl_pkt {
	__le16	report_id;
	__le16	subcmd;
	__le32	len;
	__u8	data[PSVR2_CTRL_DATA_MAX];
} __packed;

/*
 * IF7 interrupt transfer: one status header followed by an array of IMU
 * records (typically 41 per 1024-byte transfer).
 */
struct psvr2_status_record_hdr {
	__u8	dprx_status;		/* 0 = DP link not ready */
	__u8	prox_sensor_flag;	/* 1 = headset worn      */
	__u8	function_button;	/* 1 = pressed           */
	__u8	empty0[2];
	__u8	ipd_dial_mm;		/* 59..72 mm             */
	__u8	remainder[26];
} __packed;

/* Matches Sony's struct sieimu_data; 24 bytes. */
struct psvr2_imu_record {
	__le32	vts_us;			/* video timestamp (us)  */
	__le16	accel[3];
	__le16	gyro[3];
	__le16	dp_frame_cnt;
	__le16	dp_line_cnt;
	__le16	imu_ts_us;		/* IMU timestamp (us)    */
	__le16	status;			/* bit0 = invalid sample */
} __packed;

#define PSVR2_IMU_STATUS_INVALID	(1 << 0)

/*
 * IF3 (SLAM) bulk transfer: the headset's onboard 6DoF tracker emits one
 * 512-byte record per transfer, prefixed with ASCII "SLA". Position and
 * orientation are IEEE-754 little-endian floats; we carry them as raw __le32
 * so the kernel never touches the FPU. Field order is the wire order — the
 * consumer applies any axis/quaternion convention (see docs/protocol.md).
 */
struct psvr2_slam_record {
	char	slp_hdr[3];	/* "SLP" on hardware (not "SLA"); parser doesn't gate on it */
	__u8	const1;		/* observed constant 0x01 */
	__le32	pkt_size;	/* 0x200 = 512 */
	__le32	vts_ts_us;	/* device timestamp (us) */
	__le32	unknown1;	/* observed constant 3 */
	__le32	pos[3];		/* float32 position (metres) */
	__le32	orient[4];	/* float32 quaternion, [0] = w */
	__u8	remainder[468];
} __packed;

/* Informational: real record magic seen on hardware. The parser deliberately
 * does NOT gate on this (firmware framing isn't guaranteed) — see psvr2_slam.c. */
#define PSVR2_SLAM_HDR_MAGIC	"SLP"
#define PSVR2_SLAM_RECORD_SIZE	512

/*
 * IF5 (eye/gaze tracking) packets. Floats are carried as raw __le32 (no FPU in
 * kernel). "psvr2_eye_bool" is a 32-bit boolean. Many fields are not yet
 * understood and are named unk_*; layout ported verbatim from the Monado
 * driver so byte offsets of the known fields are correct.
 *
 * Enabling the stream requires periodically sending report 0x0c subcmd 0x01
 * (a keepalive that lapses after a few seconds); 0x02 disables it.
 */
enum psvr2_gaze_stream_subcommand {
	PSVR2_GAZE_STREAM_ENABLE	= 0x01,
	PSVR2_GAZE_STREAM_DISABLE	= 0x02,
};

typedef __le32 psvr2_eye_bool;

struct psvr2_levec2 { __le32 x, y; } __packed;
struct psvr2_levec3 { __le32 x, y, z; } __packed;

struct psvr2_pkt_eye_gaze {
	psvr2_eye_bool		gaze_point_mm_valid;
	struct psvr2_levec3	gaze_point_mm;
	psvr2_eye_bool		gaze_direction_valid;
	struct psvr2_levec3	gaze_direction;
	psvr2_eye_bool		pupil_diameter_valid;
	__le32			pupil_diameter_mm;	/* float */
	psvr2_eye_bool		unk_bool_2;
	struct psvr2_levec2	unk_float_2;
	psvr2_eye_bool		unk_bool_3;
	struct psvr2_levec2	unk_float_4;
	psvr2_eye_bool		blink_valid;
	psvr2_eye_bool		blink;
} __packed;

struct psvr2_pkt_gaze_combined {
	psvr2_eye_bool		gaze_point_valid;
	struct psvr2_levec3	gaze_point_3d;
	psvr2_eye_bool		normalized_gaze_valid;
	struct psvr2_levec3	normalized_gaze;
	psvr2_eye_bool		is_valid;
	__le32			timestamp;		/* device us */
	psvr2_eye_bool		unk_bool_7;
	__le32			unk_float_8;
	psvr2_eye_bool		unk_bool_9;
	struct psvr2_levec3	unk_float_12;
	struct psvr2_levec3	unk_float_15;
	struct psvr2_levec3	unk_float_18;
} __packed;

struct psvr2_pkt_gaze_packet_data {
	__le32			size;
	__le32			unk_1;
	__le32			unk_2;
	__le32			unk_3_const;
	__le32			timestamp_1;
	__le32			timestamp_2;
	__le32			timestamp_3;
	psvr2_eye_bool		unk_bool_1;
	__le32			unk_float_1;
	psvr2_eye_bool		unk_bool_2;
	psvr2_eye_bool		unk_bool_3;
	__le32			unk_float_2;
	psvr2_eye_bool		unk_bool_4;
	psvr2_eye_bool		unk_bool_5;
	psvr2_eye_bool		unk_bool_6;
	psvr2_eye_bool		unk_bool_7;
	psvr2_eye_bool		unk_bool_8;
	__le32			unk_float_3;
	psvr2_eye_bool		unk_bool_9;
	__le32			unk_float_4;
	psvr2_eye_bool		unk_bool_10;
	__le32			unk_float_5;
	struct psvr2_pkt_eye_gaze	left;
	struct psvr2_pkt_eye_gaze	right;
	struct psvr2_pkt_gaze_combined	combined;
} __packed;

struct psvr2_pkt_gaze_state {
	__u8	header[2];	/* "GS" */
	__le16	version;
	struct psvr2_pkt_gaze_packet_data packet_data;
} __packed;

#define PSVR2_GAZE_HDR_MAGIC	"GS"

#endif /* _PSVR2_PROTOCOL_H_ */
