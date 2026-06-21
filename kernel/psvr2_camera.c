// SPDX-License-Identifier: GPL-2.0
/*
 * PSVR2 Linux driver — IF6 tracking/passthrough cameras via V4L2.
 *
 * Interface 6 alt 0 exposes a bulk IN endpoint (0x87). Each bulk transfer
 * carries exactly one frame; the transfer length identifies the camera mode.
 * The headset supports many exotic interleaved packings; this driver decodes
 * the simplest, mode 1 (PSVR2_CAMERA_MODE_BOTTOM_SBS_CROPPED): a 1280x640 8-bit
 * greyscale image (two 640x640 bottom-camera views side by side) following a
 * 256-byte header.
 *
 * Frames are delivered through videobuf2 (vmalloc backing). A small pool of
 * bulk URBs is submitted on VIDIOC_STREAMON (after switching the headset into
 * camera mode 1) and killed on STREAMOFF (switching the cameras back off).
 *
 * Copyright (C) 2026 PSVR2 Linux project
 */
#include <linux/slab.h>
#include <linux/usb.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>

#include "psvr2.h"
#include "psvr2_protocol.h"

#define PSVR2_CAM_NUM_URBS	4
#define PSVR2_CAM_FRAME_SIZE	(PSVR2_CAM_MODE1_WIDTH * PSVR2_CAM_MODE1_HEIGHT)

struct psvr2_cam_buffer {
	struct vb2_v4l2_buffer	vb;
	struct list_head	list;
};

struct psvr2_camera {
	struct psvr2_device	*psvr2;
	struct usb_device	*udev;
	u8			ep_addr;

	struct v4l2_device	v4l2_dev;
	struct video_device	vdev;
	struct vb2_queue	queue;
	struct mutex		lock;		/* serialises ioctls + vb2 + URBs */

	spinlock_t		buf_lock;	/* protects buf_list */
	struct list_head	buf_list;	/* queued vb2 buffers */

	struct urb		*urbs[PSVR2_CAM_NUM_URBS];
	u8			*urb_bufs[PSVR2_CAM_NUM_URBS];
	size_t			urb_buf_size;

	unsigned int		sequence;
	bool			streaming;
};

static int psvr2_cam_set_mode(struct psvr2_camera *cam,
			      enum psvr2_camera_mode mode)
{
	__le32 cmd[2] = { cpu_to_le32(0x1), cpu_to_le32(mode) };

	return psvr2_control_set(cam->psvr2, PSVR2_REPORT_SET_CAMERA_MODE, 0x1,
				 cmd, sizeof(cmd));
}

/*
 * URB completion (atomic): one transfer is one frame. For mode 1 we copy the
 * greyscale payload (skipping the 256-byte header) into the next queued buffer.
 */
static void psvr2_cam_complete(struct urb *urb)
{
	struct psvr2_camera *cam = urb->context;
	struct psvr2_cam_buffer *buf;
	unsigned long flags;
	unsigned int seq;
	int ret;

	switch (urb->status) {
	case 0:
		break;
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		return; /* unlinked / device gone */
	default:
		dev_dbg(&cam->udev->dev, "camera URB error %d\n", urb->status);
		goto resubmit;
	}

	if (urb->actual_length < PSVR2_CAM_MODE1_XFER_SIZE)
		goto resubmit; /* not a mode-1 frame; ignore for now */

	spin_lock_irqsave(&cam->buf_lock, flags);
	buf = list_first_entry_or_null(&cam->buf_list, struct psvr2_cam_buffer,
				       list);
	if (buf)
		list_del(&buf->list);
	seq = cam->sequence++;
	spin_unlock_irqrestore(&cam->buf_lock, flags);

	if (buf) {
		void *vaddr = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);

		if (vaddr)
			memcpy(vaddr, urb->transfer_buffer +
				      PSVR2_CAMERA_HEADER_SIZE,
			       PSVR2_CAM_FRAME_SIZE);

		vb2_set_plane_payload(&buf->vb.vb2_buf, 0, PSVR2_CAM_FRAME_SIZE);
		buf->vb.vb2_buf.timestamp = ktime_get_ns();
		buf->vb.sequence = seq;
		buf->vb.field = V4L2_FIELD_NONE;
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}

resubmit:
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret && ret != -EPERM && ret != -ESHUTDOWN)
		dev_err(&cam->udev->dev, "failed to resubmit camera URB: %d\n",
			ret);
}

static void psvr2_cam_free_urbs(struct psvr2_camera *cam)
{
	int i;

	for (i = 0; i < PSVR2_CAM_NUM_URBS; i++) {
		if (cam->urb_bufs[i])
			usb_free_coherent(cam->udev, cam->urb_buf_size,
					  cam->urb_bufs[i],
					  cam->urbs[i]->transfer_dma);
		usb_free_urb(cam->urbs[i]);
		cam->urbs[i] = NULL;
		cam->urb_bufs[i] = NULL;
	}
}

static int psvr2_cam_alloc_urbs(struct psvr2_camera *cam)
{
	int i;

	cam->urb_buf_size = PSVR2_CAMERA_MAX_XFER_SIZE;

	for (i = 0; i < PSVR2_CAM_NUM_URBS; i++) {
		struct urb *urb = usb_alloc_urb(0, GFP_KERNEL);

		if (!urb)
			goto err;
		cam->urbs[i] = urb;

		cam->urb_bufs[i] = usb_alloc_coherent(cam->udev,
						      cam->urb_buf_size,
						      GFP_KERNEL,
						      &urb->transfer_dma);
		if (!cam->urb_bufs[i])
			goto err;

		usb_fill_bulk_urb(urb, cam->udev,
				  usb_rcvbulkpipe(cam->udev, cam->ep_addr),
				  cam->urb_bufs[i], cam->urb_buf_size,
				  psvr2_cam_complete, cam);
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	}
	return 0;

err:
	psvr2_cam_free_urbs(cam);
	return -ENOMEM;
}

static void psvr2_cam_return_buffers(struct psvr2_camera *cam,
				     enum vb2_buffer_state state)
{
	struct psvr2_cam_buffer *buf, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&cam->buf_lock, flags);
	list_for_each_entry_safe(buf, tmp, &cam->buf_list, list) {
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb.vb2_buf, state);
	}
	spin_unlock_irqrestore(&cam->buf_lock, flags);
}

/*
 * videobuf2 operations.
 */
static int psvr2_cam_queue_setup(struct vb2_queue *q, unsigned int *nbuffers,
				 unsigned int *nplanes, unsigned int sizes[],
				 struct device *alloc_devs[])
{
	if (*nplanes)
		return sizes[0] < PSVR2_CAM_FRAME_SIZE ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = PSVR2_CAM_FRAME_SIZE;
	return 0;
}

static int psvr2_cam_buf_prepare(struct vb2_buffer *vb)
{
	if (vb2_plane_size(vb, 0) < PSVR2_CAM_FRAME_SIZE)
		return -EINVAL;
	vb2_set_plane_payload(vb, 0, PSVR2_CAM_FRAME_SIZE);
	return 0;
}

static void psvr2_cam_buf_queue(struct vb2_buffer *vb)
{
	struct psvr2_camera *cam = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct psvr2_cam_buffer *buf =
		container_of(vbuf, struct psvr2_cam_buffer, vb);
	unsigned long flags;

	spin_lock_irqsave(&cam->buf_lock, flags);
	list_add_tail(&buf->list, &cam->buf_list);
	spin_unlock_irqrestore(&cam->buf_lock, flags);
}

static int psvr2_cam_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct psvr2_camera *cam = vb2_get_drv_priv(q);
	int i, ret;

	cam->sequence = 0;

	ret = psvr2_cam_alloc_urbs(cam);
	if (ret)
		goto err_return;

	ret = psvr2_cam_set_mode(cam, PSVR2_CAMERA_MODE_BOTTOM_SBS_CROPPED);
	if (ret) {
		dev_err(&cam->udev->dev, "failed to set camera mode: %d\n", ret);
		goto err_urbs;
	}

	for (i = 0; i < PSVR2_CAM_NUM_URBS; i++) {
		ret = usb_submit_urb(cam->urbs[i], GFP_KERNEL);
		if (ret) {
			dev_err(&cam->udev->dev,
				"failed to submit camera URB %d: %d\n", i, ret);
			goto err_kill;
		}
	}

	cam->streaming = true;
	return 0;

err_kill:
	while (i--)
		usb_kill_urb(cam->urbs[i]);
	psvr2_cam_set_mode(cam, PSVR2_CAMERA_MODE_OFF);
err_urbs:
	psvr2_cam_free_urbs(cam);
err_return:
	psvr2_cam_return_buffers(cam, VB2_BUF_STATE_QUEUED);
	return ret;
}

static void psvr2_cam_stop_streaming(struct vb2_queue *q)
{
	struct psvr2_camera *cam = vb2_get_drv_priv(q);
	int i;

	cam->streaming = false;

	for (i = 0; i < PSVR2_CAM_NUM_URBS; i++)
		usb_kill_urb(cam->urbs[i]);

	psvr2_cam_set_mode(cam, PSVR2_CAMERA_MODE_OFF);
	psvr2_cam_free_urbs(cam);
	psvr2_cam_return_buffers(cam, VB2_BUF_STATE_ERROR);
}

static const struct vb2_ops psvr2_cam_qops = {
	.queue_setup		= psvr2_cam_queue_setup,
	.buf_prepare		= psvr2_cam_buf_prepare,
	.buf_queue		= psvr2_cam_buf_queue,
	.start_streaming	= psvr2_cam_start_streaming,
	.stop_streaming		= psvr2_cam_stop_streaming,
};

/*
 * V4L2 ioctl operations. The format is fixed (mode 1: 1280x640 GREY).
 */
static void psvr2_cam_fill_fmt(struct v4l2_pix_format *pix)
{
	pix->width = PSVR2_CAM_MODE1_WIDTH;
	pix->height = PSVR2_CAM_MODE1_HEIGHT;
	pix->pixelformat = V4L2_PIX_FMT_GREY;
	pix->field = V4L2_FIELD_NONE;
	pix->bytesperline = PSVR2_CAM_MODE1_WIDTH;
	pix->sizeimage = PSVR2_CAM_FRAME_SIZE;
	pix->colorspace = V4L2_COLORSPACE_RAW;
}

static int psvr2_cam_querycap(struct file *file, void *priv,
			      struct v4l2_capability *cap)
{
	strscpy(cap->driver, "psvr2", sizeof(cap->driver));
	strscpy(cap->card, "PlayStation VR2 Cameras", sizeof(cap->card));
	return 0;
}

static int psvr2_cam_enum_fmt(struct file *file, void *priv,
			      struct v4l2_fmtdesc *f)
{
	if (f->index)
		return -EINVAL;
	f->pixelformat = V4L2_PIX_FMT_GREY;
	return 0;
}

static int psvr2_cam_g_fmt(struct file *file, void *priv,
			   struct v4l2_format *f)
{
	psvr2_cam_fill_fmt(&f->fmt.pix);
	return 0;
}

static int psvr2_cam_enum_framesizes(struct file *file, void *priv,
				     struct v4l2_frmsizeenum *fsize)
{
	if (fsize->index || fsize->pixel_format != V4L2_PIX_FMT_GREY)
		return -EINVAL;
	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.width = PSVR2_CAM_MODE1_WIDTH;
	fsize->discrete.height = PSVR2_CAM_MODE1_HEIGHT;
	return 0;
}

static int psvr2_cam_enum_input(struct file *file, void *priv,
				struct v4l2_input *inp)
{
	if (inp->index)
		return -EINVAL;
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	strscpy(inp->name, "PSVR2 Cameras", sizeof(inp->name));
	return 0;
}

static int psvr2_cam_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int psvr2_cam_s_input(struct file *file, void *priv, unsigned int i)
{
	return i ? -EINVAL : 0;
}

static const struct v4l2_ioctl_ops psvr2_cam_ioctl_ops = {
	.vidioc_querycap		= psvr2_cam_querycap,
	.vidioc_enum_fmt_vid_cap	= psvr2_cam_enum_fmt,
	.vidioc_g_fmt_vid_cap		= psvr2_cam_g_fmt,
	.vidioc_s_fmt_vid_cap		= psvr2_cam_g_fmt,
	.vidioc_try_fmt_vid_cap		= psvr2_cam_g_fmt,
	.vidioc_enum_framesizes		= psvr2_cam_enum_framesizes,
	.vidioc_enum_input		= psvr2_cam_enum_input,
	.vidioc_g_input			= psvr2_cam_g_input,
	.vidioc_s_input			= psvr2_cam_s_input,

	.vidioc_reqbufs			= vb2_ioctl_reqbufs,
	.vidioc_create_bufs		= vb2_ioctl_create_bufs,
	.vidioc_prepare_buf		= vb2_ioctl_prepare_buf,
	.vidioc_querybuf		= vb2_ioctl_querybuf,
	.vidioc_qbuf			= vb2_ioctl_qbuf,
	.vidioc_dqbuf			= vb2_ioctl_dqbuf,
	.vidioc_expbuf			= vb2_ioctl_expbuf,
	.vidioc_streamon		= vb2_ioctl_streamon,
	.vidioc_streamoff		= vb2_ioctl_streamoff,
};

static const struct v4l2_file_operations psvr2_cam_fops = {
	.owner		= THIS_MODULE,
	.open		= v4l2_fh_open,
	.release	= vb2_fop_release,
	.read		= vb2_fop_read,
	.poll		= vb2_fop_poll,
	.mmap		= vb2_fop_mmap,
	.unlocked_ioctl	= video_ioctl2,
};

/*
 * Frees the whole context once the last reference (registration ref + any open
 * file handle) is dropped. Called via v4l2_device_put().
 */
static void psvr2_cam_v4l2_release(struct v4l2_device *v4l2_dev)
{
	struct psvr2_camera *cam =
		container_of(v4l2_dev, struct psvr2_camera, v4l2_dev);

	v4l2_device_unregister(&cam->v4l2_dev);
	mutex_destroy(&cam->lock);
	kfree(cam);
}

int psvr2_camera_start(struct psvr2_device *psvr2, struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *ep;
	struct psvr2_camera *cam;
	struct vb2_queue *q;
	int ret;

	cam = kzalloc(sizeof(*cam), GFP_KERNEL);
	if (!cam)
		return -ENOMEM;

	cam->psvr2 = psvr2;
	cam->udev = udev;
	mutex_init(&cam->lock);
	spin_lock_init(&cam->buf_lock);
	INIT_LIST_HEAD(&cam->buf_list);

	ret = usb_set_interface(udev, PSVR2_IF_CAMERA, PSVR2_CAMERA_ALT);
	if (ret) {
		dev_err(&intf->dev, "failed to select IF6 alt %d: %d\n",
			PSVR2_CAMERA_ALT, ret);
		goto err_free;
	}

	ret = usb_find_bulk_in_endpoint(intf->cur_altsetting, &ep);
	if (ret) {
		dev_err(&intf->dev, "no bulk IN endpoint on IF6\n");
		goto err_free;
	}
	cam->ep_addr = ep->bEndpointAddress;

	cam->v4l2_dev.release = psvr2_cam_v4l2_release;
	ret = v4l2_device_register(&intf->dev, &cam->v4l2_dev);
	if (ret)
		goto err_free;

	q = &cam->queue;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ;
	q->drv_priv = cam;
	q->buf_struct_size = sizeof(struct psvr2_cam_buffer);
	q->ops = &psvr2_cam_qops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_queued_buffers = 2;
	q->lock = &cam->lock;
	ret = vb2_queue_init(q);
	if (ret)
		goto err_put;

	strscpy(cam->vdev.name, "psvr2-camera", sizeof(cam->vdev.name));
	cam->vdev.v4l2_dev = &cam->v4l2_dev;
	cam->vdev.fops = &psvr2_cam_fops;
	cam->vdev.ioctl_ops = &psvr2_cam_ioctl_ops;
	cam->vdev.release = video_device_release_empty;
	cam->vdev.lock = &cam->lock;
	cam->vdev.queue = q;
	cam->vdev.device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
				V4L2_CAP_READWRITE;
	video_set_drvdata(&cam->vdev, cam);

	ret = video_register_device(&cam->vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(&intf->dev, "failed to register video device: %d\n",
			ret);
		goto err_put;
	}

	psvr2->camera = cam;
	dev_info(&intf->dev, "PSVR2 camera registered as /dev/video%d\n",
		 cam->vdev.num);
	return 0;

err_put:
	/* Drops the registration reference; psvr2_cam_v4l2_release frees cam. */
	v4l2_device_put(&cam->v4l2_dev);
	return ret;
err_free:
	mutex_destroy(&cam->lock);
	kfree(cam);
	return ret;
}

void psvr2_camera_stop(struct psvr2_device *psvr2)
{
	struct psvr2_camera *cam = psvr2->camera;

	if (!cam)
		return;
	psvr2->camera = NULL;

	/*
	 * Detach from the (disappearing) USB parent, unregister the node, then
	 * drop the registration reference. vb2 stop_streaming kills the URBs
	 * and switches the cameras off; the context is freed by
	 * psvr2_cam_v4l2_release once the last open handle is also gone.
	 */
	v4l2_device_disconnect(&cam->v4l2_dev);
	video_unregister_device(&cam->vdev);
	v4l2_device_put(&cam->v4l2_dev);
}
