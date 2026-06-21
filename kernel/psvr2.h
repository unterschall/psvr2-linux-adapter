/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PSVR2 Linux driver — shared definitions
 *
 * Copyright (C) 2026 PSVR2 Linux project
 *
 * Device facts (USB 054c:0cde, official Sony PSVR2 PC adapter) are derived from:
 *   - Sony SIE GPL OSS release (psvr2-sie-kernel-modules), device-side source
 *   - The Monado host-side PSVR2 driver (BSL-1.0), reverse-engineered protocol
 * See docs/references.md for full provenance.
 */
#ifndef _PSVR2_H_
#define _PSVR2_H_

#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/usb.h>

#define PSVR2_VENDOR_ID		0x054c
#define PSVR2_PRODUCT_ID	0x0cde

/*
 * USB interface map (13 interfaces total). IF0 = HID control, IF1/IF2 = USB
 * audio (left to snd-usb-audio), IF3..IF12 = vendor-specific bulk/interrupt
 * streams. We only claim the interfaces we actually drive.
 *
 * Milestone 1 owns only the status/IMU interface (IF7). The remaining stream
 * interfaces are listed for when they are implemented.
 */
#define PSVR2_IF_SLAM		3	/* bulk IN, 6DoF "SLA" pose packets   */
#define PSVR2_IF_GAZE		5	/* eye tracking                       */
#define PSVR2_IF_CAMERA		6	/* camera frames                      */
#define PSVR2_IF_STATUS		7	/* status header + IMU records (M1)   */
#define PSVR2_IF_LD		8	/* LED detector                       */
#define PSVR2_IF_RP		9	/* relocalizer                        */
#define PSVR2_IF_VD		10	/* vendor data                        */

/* Status/IMU interface: alt setting 1 exposes the interrupt IN endpoint. */
#define PSVR2_STATUS_ALT	1
#define PSVR2_STATUS_EP_IN	0x88
#define PSVR2_STATUS_XFER_SIZE	1024

/* SLAM interface: alt setting 0, bulk IN endpoint 0x83, 512-byte records. */
#define PSVR2_SLAM_ALT		0
#define PSVR2_SLAM_EP_IN	0x83
#define PSVR2_SLAM_XFER_SIZE	1024

/*
 * Gaze interface: alt 0, bulk IN endpoint 0x85. The stream must be kept alive
 * by re-sending the enable command roughly once a second.
 */
#define PSVR2_GAZE_ALT			0
#define PSVR2_GAZE_EP_IN		0x85
#define PSVR2_GAZE_XFER_SIZE		32768
#define PSVR2_GAZE_KEEPALIVE_MS		1000

/*
 * Auxiliary inside-out tracking interfaces (LED detector, relocalizer, vendor
 * data). The headset's tracker appears to require these bulk IN endpoints to be
 * continuously drained, like the host-side reference drivers do, before it will
 * enter a tracking camera mode and emit SLAM poses. We read and discard them.
 * A single generously-sized buffer covers all three (relocalizer is the largest
 * at ~821120 bytes).
 *
 * Alt 0 is the only setting, but the DMJC fork issues an explicit SET_INTERFACE
 * on each of these (usbmon-verified) where usbcore would otherwise leave the
 * default selected silently. We replicate it to match the reference driver's
 * on-the-wire behaviour; it is harmless if redundant.
 */
#define PSVR2_AUX_ALT			0
#define PSVR2_AUX_XFER_SIZE		(1024 * 1024)

/*
 * Camera interface: alt 0, bulk IN endpoint 0x87. One bulk transfer carries one
 * frame; the transfer length identifies the mode. We size URB buffers for the
 * largest mode and, for now, decode the simplest one (mode 1).
 */
#define PSVR2_CAMERA_ALT		0
#define PSVR2_CAMERA_EP_IN		0x87
#define PSVR2_CAMERA_MAX_XFER_SIZE	1040640	/* mode 10, the largest */
#define PSVR2_CAMERA_HEADER_SIZE	256
/* Mode 1 (BOTTOM_SBS_CROPPED): 1280x640 8-bit greyscale after the header. */
#define PSVR2_CAM_MODE1_XFER_SIZE	819456
#define PSVR2_CAM_MODE1_WIDTH		1280
#define PSVR2_CAM_MODE1_HEIGHT		640

/*
 * IMU scaling (raw __s16 -> physical units), from the Monado driver.
 *   gyro:  deg/s  = raw * 2000 / 32767
 *   accel: m/s^2  = raw * 4 * g / 32767
 * Exposed to userspace as IIO *_scale so the raw register values stay intact.
 */
#define PSVR2_GRAVITY_M_S2		980665		/* g * 1e5 */
#define PSVR2_GYRO_FS_DEG_S		2000
#define PSVR2_ACCEL_FS_G		4
#define PSVR2_IMU_FULL_SCALE		32767
#define PSVR2_IMU_INVALID		((__s16)0x8000)	/* FIFO sentinel */
#define PSVR2_IMU_FREQ_HZ		2000

/* IPD dial range in millimetres (from the status header). */
#define PSVR2_IPD_MIN_MM	59
#define PSVR2_IPD_MAX_MM	72

struct psvr2_imu;
struct psvr2_input;
struct psvr2_status;
struct psvr2_slam;
struct psvr2_camera;
struct psvr2_gaze;
struct psvr2_aux;

/* Number of auxiliary drain interfaces (LED detector, relocalizer, VD). */
#define PSVR2_AUX_COUNT		3

/*
 * One psvr2_device exists per physical headset and is shared across the USB
 * interfaces we bind. It is reference counted: the first interface to probe
 * creates it, later interfaces grab a reference, and it is torn down when the
 * last interface disconnects.
 */
struct psvr2_device {
	struct kref		kref;
	struct list_head	node;		/* entry in the device registry */
	struct usb_device	*udev;		/* for ep0 control transfers */
	struct mutex		ctrl_lock;	/* serialises ep0 control    */
	u8			brightness;	/* last value written (0..31) */

	struct psvr2_status	*status;	/* IF7 stream context        */
	struct psvr2_imu	*imu;		/* IIO device                */
	struct psvr2_input	*input;		/* input device              */
	struct psvr2_slam	*slam;		/* IF3 stream context        */
	struct psvr2_camera	*camera;	/* IF6 V4L2 device           */
	struct psvr2_gaze	*gaze;		/* IF5 stream context        */
	struct psvr2_aux	*aux[PSVR2_AUX_COUNT];	/* IF8/9/10 drains   */

	struct dentry		*debugfs_dir;	/* created with the device   */
};

/* psvr2_usb.c — shared context lifecycle + ep0 vendor control. */
struct psvr2_device *psvr2_device_get(struct usb_device *udev);
void psvr2_device_put(struct psvr2_device *psvr2);

int psvr2_control_set(struct psvr2_device *psvr2, u16 report_id, u16 subcmd,
		      const void *data, u32 len);
int psvr2_control_get(struct psvr2_device *psvr2, u16 report_id, u16 subcmd,
		      void *data, u32 len);

/* psvr2_status.c — IF7 interrupt stream. */
int psvr2_status_start(struct psvr2_device *psvr2, struct usb_interface *intf);
void psvr2_status_stop(struct psvr2_device *psvr2);

/* psvr2_slam.c — IF3 bulk stream + /dev/psvr2-pose char device. */
int psvr2_slam_start(struct psvr2_device *psvr2, struct usb_interface *intf);
void psvr2_slam_stop(struct psvr2_device *psvr2);

/* psvr2_camera.c — IF6 V4L2 capture device. */
int psvr2_camera_start(struct psvr2_device *psvr2, struct usb_interface *intf);
void psvr2_camera_stop(struct psvr2_device *psvr2);

/* psvr2_gaze.c — IF5 bulk stream + /dev/psvr2-gaze char device. */
int psvr2_gaze_start(struct psvr2_device *psvr2, struct usb_interface *intf);
void psvr2_gaze_stop(struct psvr2_device *psvr2);

/* psvr2_aux.c — drain the LED detector / relocalizer / VD tracking interfaces. */
int psvr2_aux_start(struct psvr2_device *psvr2, struct usb_interface *intf);
void psvr2_aux_stop(struct psvr2_device *psvr2, struct usb_interface *intf);

/*
 * psvr2_imu.c / psvr2_input.c register devm-managed IIO and input devices
 * against the IF7 interface, so the USB core tears them down automatically on
 * unbind. The push/report helpers are called from the status URB completion.
 */
int psvr2_imu_register(struct psvr2_device *psvr2, struct device *parent);
void psvr2_imu_push(struct psvr2_device *psvr2,
		    const s16 accel[3], const s16 gyro[3], s64 timestamp_ns);

int psvr2_input_register(struct psvr2_device *psvr2, struct device *parent);
void psvr2_input_report(struct psvr2_device *psvr2, bool function_button,
			bool proximity, u8 ipd_mm);

#endif /* _PSVR2_H_ */
