// SPDX-License-Identifier: GPL-2.0
/*
 * PSVR2 Linux driver — auxiliary tracking interface drains (IF8/9/10).
 *
 * The headset's inside-out tracking pipeline spans more than the SLAM/camera
 * interfaces: the LED detector (IF8), relocalizer (IF9) and vendor-data (IF10)
 * bulk IN endpoints must also be continuously read, the way the host-side
 * reference drivers do, or the tracker stalls — it won't switch into a tracking
 * camera mode and emits no SLAM poses. We submit a bulk URB per interface and
 * simply discard the data, keeping the endpoints drained.
 *
 * Copyright (C) 2026 PSVR2 Linux project
 */
#include <linux/slab.h>
#include <linux/usb.h>

#include "psvr2.h"

struct psvr2_aux {
	struct psvr2_device	*psvr2;
	struct usb_device	*udev;
	struct urb		*urb;
	u8			*buf;
	size_t			buf_size;
	u8			ifnum;
};

static int psvr2_aux_index(u8 ifnum)
{
	switch (ifnum) {
	case PSVR2_IF_LD:
		return 0;
	case PSVR2_IF_RP:
		return 1;
	case PSVR2_IF_VD:
		return 2;
	default:
		return -1;
	}
}

static void psvr2_aux_complete(struct urb *urb)
{
	struct psvr2_aux *aux = urb->context;
	int ret;

	switch (urb->status) {
	case 0:
	case -EOVERFLOW:	/* short/large packet — keep draining */
		break;
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		return; /* unlinked / device gone */
	default:
		dev_dbg(&aux->udev->dev, "aux IF%u URB error %d\n", aux->ifnum,
			urb->status);
		break;
	}

	/* Data is intentionally discarded; just resubmit to keep it flowing. */
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret && ret != -EPERM)
		dev_err(&aux->udev->dev, "failed to resubmit aux IF%u URB: %d\n",
			aux->ifnum, ret);
}

int psvr2_aux_start(struct psvr2_device *psvr2, struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	u8 ifnum = intf->cur_altsetting->desc.bInterfaceNumber;
	struct usb_endpoint_descriptor *ep;
	struct psvr2_aux *aux;
	int idx, ret;

	idx = psvr2_aux_index(ifnum);
	if (idx < 0)
		return -ENODEV;

	aux = kzalloc(sizeof(*aux), GFP_KERNEL);
	if (!aux)
		return -ENOMEM;

	aux->psvr2 = psvr2;
	aux->udev = udev;
	aux->ifnum = ifnum;
	aux->buf_size = PSVR2_AUX_XFER_SIZE;

	/*
	 * Issue the explicit SET_INTERFACE the DMJC fork sends. There is only one
	 * alt setting, so usbcore leaves it selected by default without putting a
	 * request on the wire; the fork sends one anyway, and that may be the
	 * trigger that arms the tracking pipeline. Non-fatal if it fails.
	 */
	ret = usb_set_interface(udev, ifnum, PSVR2_AUX_ALT);
	if (ret)
		dev_warn(&intf->dev, "failed to select IF%u alt %d: %d\n",
			 ifnum, PSVR2_AUX_ALT, ret);

	ret = usb_find_bulk_in_endpoint(intf->cur_altsetting, &ep);
	if (ret) {
		dev_err(&intf->dev, "no bulk IN endpoint on IF%u\n", ifnum);
		goto err_free;
	}

	aux->urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!aux->urb) {
		ret = -ENOMEM;
		goto err_free;
	}

	aux->buf = usb_alloc_coherent(udev, aux->buf_size, GFP_KERNEL,
				      &aux->urb->transfer_dma);
	if (!aux->buf) {
		ret = -ENOMEM;
		goto err_urb;
	}

	usb_fill_bulk_urb(aux->urb, udev,
			  usb_rcvbulkpipe(udev, ep->bEndpointAddress),
			  aux->buf, aux->buf_size, psvr2_aux_complete, aux);
	aux->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	ret = usb_submit_urb(aux->urb, GFP_KERNEL);
	if (ret) {
		dev_err(&intf->dev, "failed to submit aux IF%u URB: %d\n",
			ifnum, ret);
		goto err_buf;
	}

	psvr2->aux[idx] = aux;
	dev_dbg(&intf->dev, "draining aux tracking interface %u\n", ifnum);
	return 0;

err_buf:
	usb_free_coherent(udev, aux->buf_size, aux->buf, aux->urb->transfer_dma);
err_urb:
	usb_free_urb(aux->urb);
err_free:
	kfree(aux);
	return ret;
}

void psvr2_aux_stop(struct psvr2_device *psvr2, struct usb_interface *intf)
{
	u8 ifnum = intf->cur_altsetting->desc.bInterfaceNumber;
	int idx = psvr2_aux_index(ifnum);
	struct psvr2_aux *aux;

	if (idx < 0)
		return;
	aux = psvr2->aux[idx];
	if (!aux)
		return;
	psvr2->aux[idx] = NULL;

	usb_kill_urb(aux->urb);
	usb_free_coherent(aux->udev, aux->buf_size, aux->buf,
			  aux->urb->transfer_dma);
	usb_free_urb(aux->urb);
	kfree(aux);
}
