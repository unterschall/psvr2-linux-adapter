// SPDX-License-Identifier: GPL-2.0
/*
 * PSVR2 Linux driver — IF3 SLAM 6DoF pose stream.
 *
 * Interface 3 alt 0 exposes a bulk IN endpoint (0x83) that delivers the
 * headset's onboard tracker output: 512-byte "SLP" records carrying a position
 * vector and orientation quaternion (IEEE-754 floats). Each record is turned
 * into a struct psvr2_pose_sample and queued for userspace on the character
 * device /dev/psvr2-pose (blocking read of whole samples, with poll support).
 * The most recent raw record is also exposed via debugfs.
 *
 * The context is reference counted so that a reader blocked in read()/poll()
 * keeps the queue alive across a disconnect; the USB resources themselves are
 * torn down synchronously in psvr2_slam_stop().
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

#include "psvr2.h"
#include "psvr2_protocol.h"
#include "psvr2_uapi.h"

#define PSVR2_POSE_FIFO_DEPTH	256

struct psvr2_slam {
	struct kref		kref;
	struct psvr2_device	*psvr2;
	struct usb_device	*udev;

	struct urb		*urb;
	u8			*buf;
	size_t			buf_size;

	/* Character device exposing the pose sample stream. */
	struct miscdevice	miscdev;
	char			devname[16];
	DECLARE_KFIFO_PTR(fifo, struct psvr2_pose_sample);
	spinlock_t		fifo_lock;
	wait_queue_head_t	readq;
	bool			dead;		/* device gone; readers see EOF */

	/* Snapshot of the most recent raw record, for debugfs. */
	struct dentry		*raw_dentry;
	struct mutex		raw_lock;
	u8			*raw_copy;
	size_t			raw_len;
};

static void psvr2_slam_free(struct kref *kref)
{
	struct psvr2_slam *sl = container_of(kref, struct psvr2_slam, kref);

	kfifo_free(&sl->fifo);
	kfree(sl);
}

/*
 * Character device.  file->private_data is set to the miscdevice by the misc
 * core before open() runs; we narrow it to our context and pin it for the life
 * of the open file.
 */
static int psvr2_pose_open(struct inode *inode, struct file *file)
{
	struct psvr2_slam *sl =
		container_of(file->private_data, struct psvr2_slam, miscdev);

	kref_get(&sl->kref);
	file->private_data = sl;
	return stream_open(inode, file);
}

static int psvr2_pose_release(struct inode *inode, struct file *file)
{
	struct psvr2_slam *sl = file->private_data;

	kref_put(&sl->kref, psvr2_slam_free);
	return 0;
}

static ssize_t psvr2_pose_read(struct file *file, char __user *ubuf,
			       size_t count, loff_t *ppos)
{
	struct psvr2_slam *sl = file->private_data;
	struct psvr2_pose_sample sample;
	size_t total = 0;
	int ret;

	if (count < sizeof(sample))
		return -EINVAL;

	if (kfifo_is_empty(&sl->fifo)) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		ret = wait_event_interruptible(sl->readq,
					       !kfifo_is_empty(&sl->fifo) ||
						       sl->dead);
		if (ret)
			return ret;
		if (sl->dead && kfifo_is_empty(&sl->fifo))
			return 0; /* EOF */
	}

	while (count - total >= sizeof(sample)) {
		if (!kfifo_out_spinlocked(&sl->fifo, &sample, 1, &sl->fifo_lock))
			break;
		if (copy_to_user(ubuf + total, &sample, sizeof(sample)))
			return total ? total : -EFAULT;
		total += sizeof(sample);
	}

	return total;
}

static __poll_t psvr2_pose_poll(struct file *file, poll_table *wait)
{
	struct psvr2_slam *sl = file->private_data;
	__poll_t mask = 0;

	poll_wait(file, &sl->readq, wait);
	if (!kfifo_is_empty(&sl->fifo))
		mask |= EPOLLIN | EPOLLRDNORM;
	if (sl->dead)
		mask |= EPOLLHUP;
	return mask;
}

static const struct file_operations psvr2_pose_fops = {
	.owner		= THIS_MODULE,
	.open		= psvr2_pose_open,
	.release	= psvr2_pose_release,
	.read		= psvr2_pose_read,
	.poll		= psvr2_pose_poll,
	.llseek		= noop_llseek,
};

/* debugfs: raw bytes of the most recent SLAM pose record. */
static ssize_t psvr2_raw_slam_read(struct file *file, char __user *ubuf,
				   size_t count, loff_t *ppos)
{
	struct psvr2_slam *sl = file->private_data;
	ssize_t ret;
	void *snap;
	size_t len;

	snap = kmalloc(sl->buf_size, GFP_KERNEL);
	if (!snap)
		return -ENOMEM;

	mutex_lock(&sl->raw_lock);
	len = sl->raw_len;
	memcpy(snap, sl->raw_copy, len);
	mutex_unlock(&sl->raw_lock);

	ret = simple_read_from_buffer(ubuf, count, ppos, snap, len);
	kfree(snap);
	return ret;
}

static const struct file_operations psvr2_raw_slam_fops = {
	.owner		= THIS_MODULE,
	.open		= simple_open,
	.read		= psvr2_raw_slam_read,
	.llseek		= default_llseek,
};

static void psvr2_slam_process(struct psvr2_slam *sl, int len)
{
	struct psvr2_slam_record *rec = (void *)sl->buf;
	struct psvr2_pose_sample sample;
	unsigned long flags;

	if (len < PSVR2_SLAM_RECORD_SIZE)
		return;		/* not a full record */

	/*
	 * Parse by offset; do NOT gate on the record's leading magic. The
	 * onboard tracker streams 512-byte records whose header is "SLP" (not
	 * the "SLA" the field name suggests). An over-strict magic check here
	 * silently dropped every pose while data was streaming; the reference
	 * Monado driver likewise reads the fields by offset without checking it.
	 * Field offsets are known-good (pos at +16, orient at +28).
	 */
	mutex_lock(&sl->raw_lock);
	memcpy(sl->raw_copy, sl->buf, PSVR2_SLAM_RECORD_SIZE);
	sl->raw_len = PSVR2_SLAM_RECORD_SIZE;
	mutex_unlock(&sl->raw_lock);

	memset(&sample, 0, sizeof(sample));
	sample.timestamp_ns = ktime_get_ns();
	sample.device_vts_us = le32_to_cpu(rec->vts_ts_us);
	sample.flags = PSVR2_POSE_FLAG_VALID;
	/* Carry the float bit patterns through untouched (no FPU in kernel). */
	memcpy(sample.position, rec->pos, sizeof(sample.position));
	memcpy(sample.orientation, rec->orient, sizeof(sample.orientation));

	spin_lock_irqsave(&sl->fifo_lock, flags);
	if (kfifo_is_full(&sl->fifo))
		kfifo_skip(&sl->fifo);		/* drop oldest, keep latest */
	kfifo_in(&sl->fifo, &sample, 1);
	spin_unlock_irqrestore(&sl->fifo_lock, flags);

	wake_up_interruptible(&sl->readq);
}

static void psvr2_slam_complete(struct urb *urb)
{
	struct psvr2_slam *sl = urb->context;
	int ret;

	switch (urb->status) {
	case 0:
		break;
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		return; /* unlinked / device gone */
	default:
		dev_dbg(&sl->udev->dev, "SLAM URB error %d\n", urb->status);
		goto resubmit;
	}

	if (urb->actual_length)
		psvr2_slam_process(sl, urb->actual_length);

resubmit:
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret && ret != -EPERM)
		dev_err(&sl->udev->dev, "failed to resubmit SLAM URB: %d\n",
			ret);
}

int psvr2_slam_start(struct psvr2_device *psvr2, struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *ep;
	struct psvr2_slam *sl;
	int ret;

	sl = kzalloc(sizeof(*sl), GFP_KERNEL);
	if (!sl)
		return -ENOMEM;

	kref_init(&sl->kref);
	sl->psvr2 = psvr2;
	sl->udev = udev;
	sl->buf_size = PSVR2_SLAM_XFER_SIZE;
	spin_lock_init(&sl->fifo_lock);
	mutex_init(&sl->raw_lock);
	init_waitqueue_head(&sl->readq);

	ret = kfifo_alloc(&sl->fifo, PSVR2_POSE_FIFO_DEPTH, GFP_KERNEL);
	if (ret)
		goto err_free;

	ret = usb_set_interface(udev, PSVR2_IF_SLAM, PSVR2_SLAM_ALT);
	if (ret) {
		dev_err(&intf->dev, "failed to select IF3 alt %d: %d\n",
			PSVR2_SLAM_ALT, ret);
		goto err_fifo;
	}

	ret = usb_find_bulk_in_endpoint(intf->cur_altsetting, &ep);
	if (ret) {
		dev_err(&intf->dev, "no bulk IN endpoint on IF3\n");
		goto err_fifo;
	}

	sl->raw_copy = kzalloc(sl->buf_size, GFP_KERNEL);
	sl->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!sl->raw_copy || !sl->urb) {
		ret = -ENOMEM;
		goto err_urb;
	}

	sl->buf = usb_alloc_coherent(udev, sl->buf_size, GFP_KERNEL,
				     &sl->urb->transfer_dma);
	if (!sl->buf) {
		ret = -ENOMEM;
		goto err_urb;
	}

	usb_fill_bulk_urb(sl->urb, udev,
			  usb_rcvbulkpipe(udev, ep->bEndpointAddress),
			  sl->buf, sl->buf_size, psvr2_slam_complete, sl);
	sl->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	/* Unique-enough name for the single-headset case. */
	scnprintf(sl->devname, sizeof(sl->devname), "psvr2-pose");
	sl->miscdev.minor = MISC_DYNAMIC_MINOR;
	sl->miscdev.name = sl->devname;
	sl->miscdev.fops = &psvr2_pose_fops;
	ret = misc_register(&sl->miscdev);
	if (ret) {
		dev_err(&intf->dev, "failed to register /dev/%s: %d\n",
			sl->devname, ret);
		goto err_buf;
	}

	ret = usb_submit_urb(sl->urb, GFP_KERNEL);
	if (ret) {
		dev_err(&intf->dev, "failed to submit SLAM URB: %d\n", ret);
		goto err_misc;
	}

	sl->raw_dentry = debugfs_create_file("raw_slam", 0400,
					     psvr2->debugfs_dir, sl,
					     &psvr2_raw_slam_fops);

	psvr2->slam = sl;
	return 0;

err_misc:
	misc_deregister(&sl->miscdev);
err_buf:
	usb_free_coherent(udev, sl->buf_size, sl->buf, sl->urb->transfer_dma);
err_urb:
	usb_free_urb(sl->urb);
	kfree(sl->raw_copy);
err_fifo:
	kfifo_free(&sl->fifo);
err_free:
	mutex_destroy(&sl->raw_lock);
	kfree(sl);
	return ret;
}

void psvr2_slam_stop(struct psvr2_device *psvr2)
{
	struct psvr2_slam *sl = psvr2->slam;

	if (!sl)
		return;
	psvr2->slam = NULL;

	/* Stop USB activity and tear down all USB-tied resources now. */
	usb_kill_urb(sl->urb);
	usb_free_coherent(sl->udev, sl->buf_size, sl->buf,
			  sl->urb->transfer_dma);
	usb_free_urb(sl->urb);

	debugfs_remove(sl->raw_dentry);
	kfree(sl->raw_copy);
	mutex_destroy(&sl->raw_lock);

	/* No new opens; wake any blocked readers so they observe EOF. */
	misc_deregister(&sl->miscdev);
	sl->dead = true;
	wake_up_interruptible(&sl->readq);

	/* Drop the device's reference; freed once the last reader closes. */
	kref_put(&sl->kref, psvr2_slam_free);
}
