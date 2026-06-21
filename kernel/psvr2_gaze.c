// SPDX-License-Identifier: GPL-2.0
/*
 * PSVR2 Linux driver — IF5 eye / gaze tracking stream.
 *
 * Interface 5 alt 0 exposes a bulk IN endpoint (0x85) delivering "GS" gaze
 * packets (per-eye and combined gaze point/direction, pupil diameter, blink).
 * Unlike the other streams, the headset only keeps gaze tracking on while it
 * receives a periodic enable command, so a delayed work item re-sends it about
 * once a second. Curated samples are exposed on the character device
 * /dev/psvr2-gaze (whole-sample read() + poll()); the latest raw packet is also
 * available via debugfs.
 *
 * Copyright (C) 2026 PSVR2 Linux project
 */
#include <linux/debugfs.h>
#include <linux/kfifo.h>
#include <linux/kref.h>
#include <linux/ktime.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include "psvr2.h"
#include "psvr2_protocol.h"
#include "psvr2_uapi.h"

#define PSVR2_GAZE_FIFO_DEPTH	64

struct psvr2_gaze {
	struct kref		kref;
	struct psvr2_device	*psvr2;
	struct usb_device	*udev;

	struct urb		*urb;
	u8			*buf;
	size_t			buf_size;

	struct delayed_work	keepalive;
	bool			stopping;	/* tells keepalive not to re-arm */

	struct miscdevice	miscdev;
	char			devname[16];
	DECLARE_KFIFO_PTR(fifo, struct psvr2_gaze_sample);
	spinlock_t		fifo_lock;
	wait_queue_head_t	readq;
	bool			dead;

	struct dentry		*raw_dentry;
	struct mutex		raw_lock;
	u8			*raw_copy;
	size_t			raw_len;
};

static void psvr2_gaze_free(struct kref *kref)
{
	struct psvr2_gaze *gz = container_of(kref, struct psvr2_gaze, kref);

	kfifo_free(&gz->fifo);
	kfree(gz);
}

/* Keepalive: re-arm the gaze stream roughly once a second. */
static void psvr2_gaze_keepalive(struct work_struct *work)
{
	struct psvr2_gaze *gz =
		container_of(to_delayed_work(work), struct psvr2_gaze, keepalive);

	if (READ_ONCE(gz->stopping))
		return;

	psvr2_control_set(gz->psvr2, PSVR2_REPORT_SET_GAZE_STREAM,
			  PSVR2_GAZE_STREAM_ENABLE, NULL, 0);

	schedule_delayed_work(&gz->keepalive,
			      msecs_to_jiffies(PSVR2_GAZE_KEEPALIVE_MS));
}

/*
 * Character device.
 */
static int psvr2_gaze_open(struct inode *inode, struct file *file)
{
	struct psvr2_gaze *gz =
		container_of(file->private_data, struct psvr2_gaze, miscdev);

	kref_get(&gz->kref);
	file->private_data = gz;
	return stream_open(inode, file);
}

static int psvr2_gaze_release(struct inode *inode, struct file *file)
{
	struct psvr2_gaze *gz = file->private_data;

	kref_put(&gz->kref, psvr2_gaze_free);
	return 0;
}

static ssize_t psvr2_gaze_read(struct file *file, char __user *ubuf,
			       size_t count, loff_t *ppos)
{
	struct psvr2_gaze *gz = file->private_data;
	struct psvr2_gaze_sample sample;
	size_t total = 0;
	int ret;

	if (count < sizeof(sample))
		return -EINVAL;

	if (kfifo_is_empty(&gz->fifo)) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		ret = wait_event_interruptible(gz->readq,
					       !kfifo_is_empty(&gz->fifo) ||
						       gz->dead);
		if (ret)
			return ret;
		if (gz->dead && kfifo_is_empty(&gz->fifo))
			return 0;
	}

	while (count - total >= sizeof(sample)) {
		if (!kfifo_out_spinlocked(&gz->fifo, &sample, 1, &gz->fifo_lock))
			break;
		if (copy_to_user(ubuf + total, &sample, sizeof(sample)))
			return total ? total : -EFAULT;
		total += sizeof(sample);
	}

	return total;
}

static __poll_t psvr2_gaze_poll(struct file *file, poll_table *wait)
{
	struct psvr2_gaze *gz = file->private_data;
	__poll_t mask = 0;

	poll_wait(file, &gz->readq, wait);
	if (!kfifo_is_empty(&gz->fifo))
		mask |= EPOLLIN | EPOLLRDNORM;
	if (gz->dead)
		mask |= EPOLLHUP;
	return mask;
}

static const struct file_operations psvr2_gaze_fops = {
	.owner		= THIS_MODULE,
	.open		= psvr2_gaze_open,
	.release	= psvr2_gaze_release,
	.read		= psvr2_gaze_read,
	.poll		= psvr2_gaze_poll,
	.llseek		= noop_llseek,
};

static ssize_t psvr2_raw_gaze_read(struct file *file, char __user *ubuf,
				   size_t count, loff_t *ppos)
{
	struct psvr2_gaze *gz = file->private_data;
	ssize_t ret;
	void *snap;
	size_t len;

	snap = kmalloc(gz->buf_size, GFP_KERNEL);
	if (!snap)
		return -ENOMEM;

	mutex_lock(&gz->raw_lock);
	len = gz->raw_len;
	memcpy(snap, gz->raw_copy, len);
	mutex_unlock(&gz->raw_lock);

	ret = simple_read_from_buffer(ubuf, count, ppos, snap, len);
	kfree(snap);
	return ret;
}

static const struct file_operations psvr2_raw_gaze_fops = {
	.owner		= THIS_MODULE,
	.open		= simple_open,
	.read		= psvr2_raw_gaze_read,
	.llseek		= default_llseek,
};

/* Copy the float bit patterns through untouched (no FPU in kernel). */
static void psvr2_gaze_fill_eye(struct psvr2_gaze_eye *out,
				const struct psvr2_pkt_eye_gaze *in)
{
	out->gaze_point_valid = le32_to_cpu(in->gaze_point_mm_valid);
	out->gaze_point_mm[0] = in->gaze_point_mm.x;
	out->gaze_point_mm[1] = in->gaze_point_mm.y;
	out->gaze_point_mm[2] = in->gaze_point_mm.z;
	out->gaze_direction_valid = le32_to_cpu(in->gaze_direction_valid);
	out->gaze_direction[0] = in->gaze_direction.x;
	out->gaze_direction[1] = in->gaze_direction.y;
	out->gaze_direction[2] = in->gaze_direction.z;
	out->pupil_diameter_valid = le32_to_cpu(in->pupil_diameter_valid);
	out->pupil_diameter_mm = in->pupil_diameter_mm;
	out->blink_valid = le32_to_cpu(in->blink_valid);
	out->blink = le32_to_cpu(in->blink);
}

static void psvr2_gaze_process(struct psvr2_gaze *gz, int len)
{
	struct psvr2_pkt_gaze_state *st = (void *)gz->buf;
	struct psvr2_pkt_gaze_combined *cmb = &st->packet_data.combined;
	struct psvr2_gaze_sample sample;
	unsigned long flags;

	if (len < (int)sizeof(*st) ||
	    memcmp(st->header, PSVR2_GAZE_HDR_MAGIC, 2) != 0)
		return;

	mutex_lock(&gz->raw_lock);
	memcpy(gz->raw_copy, gz->buf, sizeof(*st));
	gz->raw_len = sizeof(*st);
	mutex_unlock(&gz->raw_lock);

	memset(&sample, 0, sizeof(sample));
	sample.timestamp_ns = ktime_get_ns();
	sample.device_timestamp_us = le32_to_cpu(cmb->timestamp);
	sample.flags = PSVR2_GAZE_FLAG_VALID;

	psvr2_gaze_fill_eye(&sample.left, &st->packet_data.left);
	psvr2_gaze_fill_eye(&sample.right, &st->packet_data.right);

	sample.combined.gaze_point_valid = le32_to_cpu(cmb->gaze_point_valid);
	sample.combined.gaze_point_mm[0] = cmb->gaze_point_3d.x;
	sample.combined.gaze_point_mm[1] = cmb->gaze_point_3d.y;
	sample.combined.gaze_point_mm[2] = cmb->gaze_point_3d.z;
	sample.combined.gaze_direction_valid =
		le32_to_cpu(cmb->normalized_gaze_valid);
	sample.combined.gaze_direction[0] = cmb->normalized_gaze.x;
	sample.combined.gaze_direction[1] = cmb->normalized_gaze.y;
	sample.combined.gaze_direction[2] = cmb->normalized_gaze.z;

	spin_lock_irqsave(&gz->fifo_lock, flags);
	if (kfifo_is_full(&gz->fifo))
		kfifo_skip(&gz->fifo);
	kfifo_in(&gz->fifo, &sample, 1);
	spin_unlock_irqrestore(&gz->fifo_lock, flags);

	wake_up_interruptible(&gz->readq);
}

static void psvr2_gaze_complete(struct urb *urb)
{
	struct psvr2_gaze *gz = urb->context;
	int ret;

	switch (urb->status) {
	case 0:
		break;
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		return;
	default:
		dev_dbg(&gz->udev->dev, "gaze URB error %d\n", urb->status);
		goto resubmit;
	}

	if (urb->actual_length)
		psvr2_gaze_process(gz, urb->actual_length);

resubmit:
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret && ret != -EPERM)
		dev_err(&gz->udev->dev, "failed to resubmit gaze URB: %d\n",
			ret);
}

int psvr2_gaze_start(struct psvr2_device *psvr2, struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *ep;
	struct psvr2_gaze *gz;
	int ret;

	gz = kzalloc(sizeof(*gz), GFP_KERNEL);
	if (!gz)
		return -ENOMEM;

	kref_init(&gz->kref);
	gz->psvr2 = psvr2;
	gz->udev = udev;
	gz->buf_size = PSVR2_GAZE_XFER_SIZE;
	spin_lock_init(&gz->fifo_lock);
	mutex_init(&gz->raw_lock);
	init_waitqueue_head(&gz->readq);
	INIT_DELAYED_WORK(&gz->keepalive, psvr2_gaze_keepalive);

	ret = kfifo_alloc(&gz->fifo, PSVR2_GAZE_FIFO_DEPTH, GFP_KERNEL);
	if (ret)
		goto err_free;

	ret = usb_set_interface(udev, PSVR2_IF_GAZE, PSVR2_GAZE_ALT);
	if (ret) {
		dev_err(&intf->dev, "failed to select IF5 alt %d: %d\n",
			PSVR2_GAZE_ALT, ret);
		goto err_fifo;
	}

	ret = usb_find_bulk_in_endpoint(intf->cur_altsetting, &ep);
	if (ret) {
		dev_err(&intf->dev, "no bulk IN endpoint on IF5\n");
		goto err_fifo;
	}

	gz->raw_copy = kzalloc(gz->buf_size, GFP_KERNEL);
	gz->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!gz->raw_copy || !gz->urb) {
		ret = -ENOMEM;
		goto err_urb;
	}

	gz->buf = usb_alloc_coherent(udev, gz->buf_size, GFP_KERNEL,
				     &gz->urb->transfer_dma);
	if (!gz->buf) {
		ret = -ENOMEM;
		goto err_urb;
	}

	usb_fill_bulk_urb(gz->urb, udev,
			  usb_rcvbulkpipe(udev, ep->bEndpointAddress),
			  gz->buf, gz->buf_size, psvr2_gaze_complete, gz);
	gz->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	scnprintf(gz->devname, sizeof(gz->devname), "psvr2-gaze");
	gz->miscdev.minor = MISC_DYNAMIC_MINOR;
	gz->miscdev.name = gz->devname;
	gz->miscdev.fops = &psvr2_gaze_fops;
	ret = misc_register(&gz->miscdev);
	if (ret) {
		dev_err(&intf->dev, "failed to register /dev/%s: %d\n",
			gz->devname, ret);
		goto err_buf;
	}

	/* Turn the stream on, start receiving, then keep it alive. */
	psvr2_control_set(psvr2, PSVR2_REPORT_SET_GAZE_STREAM,
			  PSVR2_GAZE_STREAM_ENABLE, NULL, 0);

	ret = usb_submit_urb(gz->urb, GFP_KERNEL);
	if (ret) {
		dev_err(&intf->dev, "failed to submit gaze URB: %d\n", ret);
		goto err_misc;
	}

	schedule_delayed_work(&gz->keepalive,
			      msecs_to_jiffies(PSVR2_GAZE_KEEPALIVE_MS));

	gz->raw_dentry = debugfs_create_file("raw_gaze", 0400,
					     psvr2->debugfs_dir, gz,
					     &psvr2_raw_gaze_fops);

	psvr2->gaze = gz;
	return 0;

err_misc:
	psvr2_control_set(psvr2, PSVR2_REPORT_SET_GAZE_STREAM,
			  PSVR2_GAZE_STREAM_DISABLE, NULL, 0);
	misc_deregister(&gz->miscdev);
err_buf:
	usb_free_coherent(udev, gz->buf_size, gz->buf, gz->urb->transfer_dma);
err_urb:
	usb_free_urb(gz->urb);
	kfree(gz->raw_copy);
err_fifo:
	kfifo_free(&gz->fifo);
err_free:
	mutex_destroy(&gz->raw_lock);
	kfree(gz);
	return ret;
}

void psvr2_gaze_stop(struct psvr2_device *psvr2)
{
	struct psvr2_gaze *gz = psvr2->gaze;

	if (!gz)
		return;
	psvr2->gaze = NULL;

	/* Stop the keepalive (and prevent it re-arming) before anything else. */
	WRITE_ONCE(gz->stopping, true);
	cancel_delayed_work_sync(&gz->keepalive);
	psvr2_control_set(psvr2, PSVR2_REPORT_SET_GAZE_STREAM,
			  PSVR2_GAZE_STREAM_DISABLE, NULL, 0);

	usb_kill_urb(gz->urb);
	usb_free_coherent(gz->udev, gz->buf_size, gz->buf,
			  gz->urb->transfer_dma);
	usb_free_urb(gz->urb);

	debugfs_remove(gz->raw_dentry);
	kfree(gz->raw_copy);
	mutex_destroy(&gz->raw_lock);

	misc_deregister(&gz->miscdev);
	gz->dead = true;
	wake_up_interruptible(&gz->readq);

	kref_put(&gz->kref, psvr2_gaze_free);
}
