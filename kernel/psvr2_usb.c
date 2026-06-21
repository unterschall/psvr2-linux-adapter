// SPDX-License-Identifier: GPL-2.0
/*
 * PSVR2 Linux driver — USB core, shared device context and ep0 control.
 *
 * Binds the vendor interfaces of the Sony PSVR2 PC adapter (USB 054c:0cde).
 * Milestone 1 binds only the status/IMU interface (IF7); the per-device
 * registry and reference counting are in place so the remaining stream
 * interfaces can be added without restructuring.
 *
 * Copyright (C) 2026 PSVR2 Linux project
 */
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include "psvr2.h"
#include "psvr2_protocol.h"

/* Registry of live per-headset contexts, keyed by struct usb_device. */
static LIST_HEAD(psvr2_devices);
static DEFINE_MUTEX(psvr2_registry_lock);

static void psvr2_device_release(struct kref *kref)
	__releases(&psvr2_registry_lock)
{
	struct psvr2_device *psvr2 = container_of(kref, struct psvr2_device, kref);

	list_del(&psvr2->node);
	mutex_unlock(&psvr2_registry_lock);

	debugfs_remove_recursive(psvr2->debugfs_dir);
	usb_put_dev(psvr2->udev);
	mutex_destroy(&psvr2->ctrl_lock);
	kfree(psvr2);
}

struct psvr2_device *psvr2_device_get(struct usb_device *udev)
{
	struct psvr2_device *psvr2;

	mutex_lock(&psvr2_registry_lock);
	list_for_each_entry(psvr2, &psvr2_devices, node) {
		if (psvr2->udev == udev && kref_get_unless_zero(&psvr2->kref)) {
			mutex_unlock(&psvr2_registry_lock);
			return psvr2;
		}
	}

	psvr2 = kzalloc(sizeof(*psvr2), GFP_KERNEL);
	if (!psvr2) {
		mutex_unlock(&psvr2_registry_lock);
		return NULL;
	}

	kref_init(&psvr2->kref);
	mutex_init(&psvr2->ctrl_lock);
	psvr2->udev = usb_get_dev(udev);
	psvr2->brightness = 31;
	psvr2->debugfs_dir = debugfs_create_dir("psvr2", NULL);
	list_add(&psvr2->node, &psvr2_devices);
	mutex_unlock(&psvr2_registry_lock);

	return psvr2;
}

void psvr2_device_put(struct psvr2_device *psvr2)
{
	if (psvr2)
		kref_put_mutex(&psvr2->kref, psvr2_device_release,
			       &psvr2_registry_lock);
}

/*
 * ep0 vendor control. The headset expects a sie_ctrl_pkt (8-byte header +
 * payload). No host authentication is required.
 */
static int psvr2_control(struct psvr2_device *psvr2, bool in, u16 report_id,
			 u16 subcmd, void *data, u32 len)
{
	struct sie_ctrl_pkt *pkt;
	int ret;

	if (len > PSVR2_CTRL_DATA_MAX)
		return -EINVAL;

	pkt = kzalloc(sizeof(*pkt), GFP_KERNEL);
	if (!pkt)
		return -ENOMEM;

	pkt->report_id = cpu_to_le16(report_id);
	pkt->subcmd = cpu_to_le16(subcmd);
	pkt->len = cpu_to_le32(len);

	mutex_lock(&psvr2->ctrl_lock);
	if (in) {
		ret = usb_control_msg_recv(psvr2->udev, 0, 0x01,
					   USB_DIR_IN | USB_TYPE_VENDOR |
						   USB_RECIP_ENDPOINT,
					   report_id, 0, pkt, len + 8, 100,
					   GFP_KERNEL);
		if (!ret && len)
			memcpy(data, pkt->data, len);
	} else {
		if (len)
			memcpy(pkt->data, data, len);
		ret = usb_control_msg_send(psvr2->udev, 0, 0x09,
					   USB_DIR_OUT | USB_TYPE_VENDOR |
						   USB_RECIP_ENDPOINT,
					   report_id, 0, pkt, len + 8, 100,
					   GFP_KERNEL);
	}
	mutex_unlock(&psvr2->ctrl_lock);

	kfree(pkt);
	return ret;
}

int psvr2_control_set(struct psvr2_device *psvr2, u16 report_id, u16 subcmd,
		      const void *data, u32 len)
{
	return psvr2_control(psvr2, false, report_id, subcmd, (void *)data, len);
}

int psvr2_control_get(struct psvr2_device *psvr2, u16 report_id, u16 subcmd,
		      void *data, u32 len)
{
	return psvr2_control(psvr2, true, report_id, subcmd, data, len);
}

/* sysfs: panel brightness, a single byte 0..31 (report 0x12, subcmd 1). */
static ssize_t brightness_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct psvr2_device *psvr2 = usb_get_intfdata(to_usb_interface(dev));

	return sysfs_emit(buf, "%u\n", psvr2->brightness);
}

static ssize_t brightness_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct psvr2_device *psvr2 = usb_get_intfdata(to_usb_interface(dev));
	u8 value;
	int ret;

	ret = kstrtou8(buf, 0, &value);
	if (ret)
		return ret;
	value = min_t(u8, value, 31);

	ret = psvr2_control_set(psvr2, PSVR2_REPORT_SET_BRIGHTNESS, 1, &value,
				sizeof(value));
	if (ret)
		return ret;

	psvr2->brightness = value;
	return count;
}
static DEVICE_ATTR_RW(brightness);

static struct attribute *psvr2_attrs[] = {
	&dev_attr_brightness.attr,
	NULL,
};
ATTRIBUTE_GROUPS(psvr2);

/*
 * Bring up the status/IMU interface (IF7): the IIO and input devices are
 * devm-managed against &intf->dev, so they unwind automatically on probe
 * failure or disconnect; only the status URBs need explicit teardown.
 */
static int psvr2_probe_status(struct psvr2_device *psvr2,
			      struct usb_interface *intf)
{
	int ret;

	ret = psvr2_imu_register(psvr2, &intf->dev);
	if (ret)
		return ret;

	ret = psvr2_input_register(psvr2, &intf->dev);
	if (ret)
		return ret;

	return psvr2_status_start(psvr2, intf);
}

static int psvr2_probe(struct usb_interface *intf,
		       const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	u8 ifnum = intf->cur_altsetting->desc.bInterfaceNumber;
	struct psvr2_device *psvr2;
	int ret;

	psvr2 = psvr2_device_get(udev);
	if (!psvr2)
		return -ENOMEM;

	usb_set_intfdata(intf, psvr2);

	switch (ifnum) {
	case PSVR2_IF_STATUS:
		ret = psvr2_probe_status(psvr2, intf);
		break;
	case PSVR2_IF_SLAM:
		ret = psvr2_slam_start(psvr2, intf);
		break;
	case PSVR2_IF_CAMERA:
		ret = psvr2_camera_start(psvr2, intf);
		break;
	case PSVR2_IF_GAZE:
		ret = psvr2_gaze_start(psvr2, intf);
		break;
	case PSVR2_IF_LD:
	case PSVR2_IF_RP:
	case PSVR2_IF_VD:
		ret = psvr2_aux_start(psvr2, intf);
		break;
	default:
		ret = -ENODEV;
		break;
	}
	if (ret)
		goto err_free;

	dev_info(&intf->dev, "PSVR2 interface %u ready\n", ifnum);
	return 0;

err_free:
	usb_set_intfdata(intf, NULL);
	psvr2_device_put(psvr2);
	return ret;
}

static void psvr2_disconnect(struct usb_interface *intf)
{
	struct psvr2_device *psvr2 = usb_get_intfdata(intf);
	u8 ifnum = intf->cur_altsetting->desc.bInterfaceNumber;

	if (!psvr2)
		return;

	switch (ifnum) {
	case PSVR2_IF_STATUS:
		psvr2_status_stop(psvr2);
		break;
	case PSVR2_IF_SLAM:
		psvr2_slam_stop(psvr2);
		break;
	case PSVR2_IF_CAMERA:
		psvr2_camera_stop(psvr2);
		break;
	case PSVR2_IF_GAZE:
		psvr2_gaze_stop(psvr2);
		break;
	case PSVR2_IF_LD:
	case PSVR2_IF_RP:
	case PSVR2_IF_VD:
		psvr2_aux_stop(psvr2, intf);
		break;
	}

	usb_set_intfdata(intf, NULL);
	psvr2_device_put(psvr2);

	dev_info(&intf->dev, "PSVR2 interface %u removed\n", ifnum);
}

static const struct usb_device_id psvr2_id_table[] = {
	{ USB_DEVICE_INTERFACE_NUMBER(PSVR2_VENDOR_ID, PSVR2_PRODUCT_ID,
				      PSVR2_IF_STATUS) },
	{ USB_DEVICE_INTERFACE_NUMBER(PSVR2_VENDOR_ID, PSVR2_PRODUCT_ID,
				      PSVR2_IF_SLAM) },
	{ USB_DEVICE_INTERFACE_NUMBER(PSVR2_VENDOR_ID, PSVR2_PRODUCT_ID,
				      PSVR2_IF_CAMERA) },
	{ USB_DEVICE_INTERFACE_NUMBER(PSVR2_VENDOR_ID, PSVR2_PRODUCT_ID,
				      PSVR2_IF_GAZE) },
	{ USB_DEVICE_INTERFACE_NUMBER(PSVR2_VENDOR_ID, PSVR2_PRODUCT_ID,
				      PSVR2_IF_LD) },
	{ USB_DEVICE_INTERFACE_NUMBER(PSVR2_VENDOR_ID, PSVR2_PRODUCT_ID,
				      PSVR2_IF_RP) },
	{ USB_DEVICE_INTERFACE_NUMBER(PSVR2_VENDOR_ID, PSVR2_PRODUCT_ID,
				      PSVR2_IF_VD) },
	{ }
};
MODULE_DEVICE_TABLE(usb, psvr2_id_table);

static struct usb_driver psvr2_driver = {
	.name		= "psvr2",
	.id_table	= psvr2_id_table,
	.probe		= psvr2_probe,
	.disconnect	= psvr2_disconnect,
	.dev_groups	= psvr2_groups,
};
module_usb_driver(psvr2_driver);

MODULE_AUTHOR("PSVR2 Linux project");
MODULE_DESCRIPTION("Sony PlayStation VR2 headset driver (IMU, buttons, control)");
MODULE_LICENSE("GPL");
