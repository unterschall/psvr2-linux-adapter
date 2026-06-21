// SPDX-License-Identifier: GPL-2.0
/*
 * PSVR2 Linux driver — IF7 status/IMU interrupt stream.
 *
 * Interface 7 alt setting 1 exposes an interrupt IN endpoint (0x88) delivering
 * 1024-byte transfers. Each transfer is a status header (DP/proximity/function
 * button/IPD) followed by an array of 24-byte IMU records at ~2 kHz. The header
 * feeds the input device; the IMU records feed the IIO device. The most recent
 * raw frame is also exposed via debugfs for protocol validation.
 *
 * Copyright (C) 2026 PSVR2 Linux project
 */
#include <linux/debugfs.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include "psvr2.h"
#include "psvr2_protocol.h"

#define PSVR2_IMU_PERIOD_NS	(NSEC_PER_SEC / PSVR2_IMU_FREQ_HZ)

struct psvr2_status {
	struct psvr2_device	*psvr2;
	struct usb_device	*udev;
	struct urb		*urb;
	u8			*buf;
	size_t			buf_size;

	/* Snapshot of the most recent raw frame, for debugfs. */
	struct mutex		raw_lock;
	u8			*raw_copy;
	size_t			raw_len;
};

static void psvr2_status_process(struct psvr2_status *st, int len, s64 now_ns)
{
	struct psvr2_device *psvr2 = st->psvr2;
	struct psvr2_status_record_hdr *hdr;
	unsigned int num_imu, i;
	u8 *cur;

	if (len < (int)sizeof(*hdr))
		return;

	hdr = (struct psvr2_status_record_hdr *)st->buf;
	psvr2_input_report(psvr2, hdr->function_button, hdr->prox_sensor_flag,
			   hdr->ipd_dial_mm);

	cur = st->buf + sizeof(*hdr);
	num_imu = (len - sizeof(*hdr)) / sizeof(struct psvr2_imu_record);

	for (i = 0; i < num_imu; i++) {
		struct psvr2_imu_record *rec = (void *)cur;
		s16 accel[3], gyro[3];
		s64 ts;
		int a;

		cur += sizeof(*rec);

		if (le16_to_cpu(rec->status) & PSVR2_IMU_STATUS_INVALID)
			continue;

		for (a = 0; a < 3; a++) {
			accel[a] = (s16)le16_to_cpu(rec->accel[a]);
			gyro[a] = (s16)le16_to_cpu(rec->gyro[a]);
		}
		if (accel[0] == PSVR2_IMU_INVALID || gyro[0] == PSVR2_IMU_INVALID)
			continue;

		/* Back-date earlier samples in the batch from the rx time. */
		ts = now_ns - (s64)(num_imu - 1 - i) * PSVR2_IMU_PERIOD_NS;
		psvr2_imu_push(psvr2, accel, gyro, ts);
	}
}

static void psvr2_status_complete(struct urb *urb)
{
	struct psvr2_status *st = urb->context;
	s64 now_ns = ktime_get_ns();
	int ret;

	switch (urb->status) {
	case 0:
		break;
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		return; /* unlinked / device gone — do not resubmit */
	default:
		dev_dbg(&st->udev->dev, "status URB error %d\n", urb->status);
		goto resubmit;
	}

	if (urb->actual_length) {
		mutex_lock(&st->raw_lock);
		memcpy(st->raw_copy, st->buf, urb->actual_length);
		st->raw_len = urb->actual_length;
		mutex_unlock(&st->raw_lock);

		psvr2_status_process(st, urb->actual_length, now_ns);
	}

resubmit:
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret && ret != -EPERM)
		dev_err(&st->udev->dev, "failed to resubmit status URB: %d\n",
			ret);
}

static ssize_t psvr2_raw_status_read(struct file *file, char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct psvr2_status *st = file->private_data;
	ssize_t ret;
	void *snap;
	size_t len;

	snap = kmalloc(st->buf_size, GFP_KERNEL);
	if (!snap)
		return -ENOMEM;

	mutex_lock(&st->raw_lock);
	len = st->raw_len;
	memcpy(snap, st->raw_copy, len);
	mutex_unlock(&st->raw_lock);

	ret = simple_read_from_buffer(user_buf, count, ppos, snap, len);
	kfree(snap);
	return ret;
}

static const struct file_operations psvr2_raw_status_fops = {
	.owner		= THIS_MODULE,
	.open		= simple_open,
	.read		= psvr2_raw_status_read,
	.llseek		= default_llseek,
};

int psvr2_status_start(struct psvr2_device *psvr2, struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *ep;
	struct psvr2_status *st;
	int ret;

	st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		return -ENOMEM;

	st->psvr2 = psvr2;
	st->udev = udev;
	st->buf_size = PSVR2_STATUS_XFER_SIZE;
	mutex_init(&st->raw_lock);

	ret = usb_set_interface(udev, PSVR2_IF_STATUS, PSVR2_STATUS_ALT);
	if (ret) {
		dev_err(&intf->dev, "failed to select IF7 alt %d: %d\n",
			PSVR2_STATUS_ALT, ret);
		goto err_free;
	}

	ret = usb_find_int_in_endpoint(intf->cur_altsetting, &ep);
	if (ret) {
		dev_err(&intf->dev, "no interrupt IN endpoint on IF7 alt %d\n",
			PSVR2_STATUS_ALT);
		goto err_free;
	}

	st->raw_copy = kzalloc(st->buf_size, GFP_KERNEL);
	st->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!st->raw_copy || !st->urb) {
		ret = -ENOMEM;
		goto err_buffers;
	}

	st->buf = usb_alloc_coherent(udev, st->buf_size, GFP_KERNEL,
				     &st->urb->transfer_dma);
	if (!st->buf) {
		ret = -ENOMEM;
		goto err_buffers;
	}

	usb_fill_int_urb(st->urb, udev,
			 usb_rcvintpipe(udev, ep->bEndpointAddress),
			 st->buf, st->buf_size, psvr2_status_complete, st,
			 ep->bInterval);
	st->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	ret = usb_submit_urb(st->urb, GFP_KERNEL);
	if (ret) {
		dev_err(&intf->dev, "failed to submit status URB: %d\n", ret);
		goto err_buffers;
	}

	debugfs_create_file("raw_status", 0400, psvr2->debugfs_dir, st,
			    &psvr2_raw_status_fops);

	psvr2->status = st;
	return 0;

err_buffers:
	if (st->buf)
		usb_free_coherent(udev, st->buf_size, st->buf,
				  st->urb->transfer_dma);
	usb_free_urb(st->urb);
	kfree(st->raw_copy);
err_free:
	mutex_destroy(&st->raw_lock);
	kfree(st);
	return ret;
}

void psvr2_status_stop(struct psvr2_device *psvr2)
{
	struct psvr2_status *st = psvr2->status;

	if (!st)
		return;
	psvr2->status = NULL;

	usb_kill_urb(st->urb);
	usb_free_coherent(st->udev, st->buf_size, st->buf,
			  st->urb->transfer_dma);
	usb_free_urb(st->urb);
	kfree(st->raw_copy);
	mutex_destroy(&st->raw_lock);
	kfree(st);
}
