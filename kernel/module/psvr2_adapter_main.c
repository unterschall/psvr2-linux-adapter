/*
 * PSVR2 Adapter Driver for Linux
 *
 * Copyright (C) 2025 PSVR2 Linux Project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <psvr2/psvr2_adapter.h>

#define DRIVER_AUTHOR "PSVR2 Linux Project"
#define DRIVER_DESC "Sony PSVR2 PC Adapter Driver"
#define DRIVER_VERSION "0.1"

/* Module parameters */
int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-3)");
EXPORT_SYMBOL(debug);

/* USB device IDs */
static const struct usb_device_id psvr2_adapter_table[] = {
    { USB_DEVICE(PSVR2_ADAPTER_VID, PSVR2_ADAPTER_PID) }, /* Sony Corp. PlayStation VR2 */
    { }
};
MODULE_DEVICE_TABLE(usb, psvr2_adapter_table);

/* Major device number, dynamically allocated */
static int major;

/* File operations */
static const struct file_operations psvr2_adapter_fops = {
    .owner =        THIS_MODULE,
    .open =         psvr2_adapter_open,
    .release =      psvr2_adapter_release,
    .unlocked_ioctl = psvr2_adapter_ioctl,
};

/* USB driver structure */
static struct usb_driver psvr2_adapter_driver = {
    .name =         "psvr2_adapter",
    .id_table =     psvr2_adapter_table,
    .probe =        psvr2_adapter_probe,
    .disconnect =   psvr2_adapter_disconnect,
};

/* Debug print macro */
#define psvr2_dbg(level, fmt, args...) \
    do { if (debug >= level) pr_info("psvr2_adapter: " fmt, ##args); } while (0)

/*
 * USB probe function - called when the adapter is connected
 */
int psvr2_adapter_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
    const int interface_num = interface->cur_altsetting->desc.bInterfaceNumber;

    /* Only attach to specific interfaces that we need */
    if (interface_num != 0 && interface_num != 3) {
        return -ENODEV; /* Skip this interface */
    }

    struct usb_host_interface *iface_desc = interface->cur_altsetting;
    int i;

    pr_info("psvr2_adapter: Interface %d has %d endpoints:\n",
            iface_desc->desc.bInterfaceNumber,
            iface_desc->desc.bNumEndpoints);

    for (i = 0; i < iface_desc->desc.bNumEndpoints; i++) {
        struct usb_endpoint_descriptor *ep = &iface_desc->endpoint[i].desc;
        pr_info("  EP %d: addr=0x%02x, type=%s, max_packet=%d\n",
                i, ep->bEndpointAddress,
                usb_ep_type_string(ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK),
                le16_to_cpu(ep->wMaxPacketSize));
    }

    struct psvr2_device *dev;
    struct usb_device *udev = interface_to_usbdev(interface);
    struct usb_endpoint_descriptor *ep_desc;
    int ret;

    psvr2_dbg(1, "Probing PSVR2 adapter\n");

    /* Allocate device structure */
    dev = kzalloc(sizeof(struct psvr2_device), GFP_KERNEL);
    if (!dev) {
        pr_err("psvr2_adapter: Out of memory\n");
        return -ENOMEM;
    }

    /* Initialize mutex */
    mutex_init(&dev->lock);

    /* Store USB device and interface */
    dev->udev = usb_get_dev(udev);
    dev->interface = interface;

    /* Set up endpoints */
    for (i = 0; i < interface->cur_altsetting->desc.bNumEndpoints; i++) {
        ep_desc = &interface->cur_altsetting->endpoint[i].desc;

        if (usb_endpoint_is_int_in(ep_desc)) {
            dev->input_ep = ep_desc->bEndpointAddress;
            psvr2_dbg(2, "Found input endpoint: 0x%02x\n", dev->input_ep);
        } else if (usb_endpoint_is_int_out(ep_desc)) {
            dev->output_ep = ep_desc->bEndpointAddress;
            psvr2_dbg(2, "Found output endpoint: 0x%02x\n", dev->output_ep);
        }
    }

    /* Allocate buffers */
    dev->control_buffer = kmalloc(PSVR2_FEATURE_REPORT_SIZE, GFP_KERNEL);
    dev->input_buffer = kmalloc(PSVR2_FEATURE_REPORT_SIZE, GFP_KERNEL);
    if (!dev->control_buffer || !dev->input_buffer) {
        pr_err("psvr2_adapter: Out of memory for buffers\n");
        ret = -ENOMEM;
        goto error_buffers;
    }

    /* Create device file */
    ret = alloc_chrdev_region(&dev->dev_num, 0, 1, "psvr2");
    if (ret < 0) {
        pr_err("psvr2_adapter: Failed to allocate device number\n");
        goto error_dev_num;
    }
    major = MAJOR(dev->dev_num);

    cdev_init(&dev->cdev, &psvr2_adapter_fops);
    dev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&dev->cdev, dev->dev_num, 1);
    if (ret < 0) {
        pr_err("psvr2_adapter: Failed to add character device\n");
        goto error_cdev;
    }

    /* Create device class */
    dev->class = class_create("psvr2");
    if (IS_ERR(dev->class)) {
        pr_err("psvr2_adapter: Failed to create device class\n");
        ret = PTR_ERR(dev->class);
        goto error_class;
    }

    /* Create device file */
    if (IS_ERR(device_create(dev->class, NULL, dev->dev_num, NULL, "psvr2"))) {
        pr_err("psvr2_adapter: Failed to create device file\n");
        ret = PTR_ERR(dev->class);
        goto error_device;
    }

    /* Initialize display and input subsystems */
    ret = psvr2_display_init(dev);
    if (ret < 0) {
        pr_err("psvr2_adapter: Failed to initialize display\n");
        goto error_display;
    }

    ret = psvr2_input_init(dev);
    if (ret < 0) {
        pr_err("psvr2_adapter: Failed to initialize input\n");
        goto error_input;
    }

    /* Set default mode */
    dev->current_mode.width = 2000; /* Placeholder values */
    dev->current_mode.height = 2040;
    dev->current_mode.refresh_rate = 90;
    dev->current_mode.flags = 0;

    /* Save device pointer in interface */
    usb_set_intfdata(interface, dev);

    psvr2_dbg(1, "PSVR2 adapter connected\n");
    return 0;

error_input:
    psvr2_display_cleanup(dev);
error_display:
    device_destroy(dev->class, dev->dev_num);
error_device:
    class_destroy(dev->class);
error_class:
    cdev_del(&dev->cdev);
error_cdev:
    unregister_chrdev_region(dev->dev_num, 1);
error_dev_num:
    kfree(dev->control_buffer);
    kfree(dev->input_buffer);
error_buffers:
    usb_put_dev(dev->udev);
    mutex_destroy(&dev->lock);
    kfree(dev);
    return ret;
}

/*
 * USB disconnect function - called when the adapter is disconnected
 */
void psvr2_adapter_disconnect(struct usb_interface *interface)
{
    struct psvr2_device *dev = usb_get_intfdata(interface);

    if (!dev)
        return;

    psvr2_dbg(1, "Disconnecting PSVR2 adapter\n");

    /* Stop and cleanup subsystems */
    psvr2_input_cleanup(dev);
    psvr2_display_cleanup(dev);

    /* Remove device file */
    device_destroy(dev->class, dev->dev_num);
    class_destroy(dev->class);
    cdev_del(&dev->cdev);
    unregister_chrdev_region(dev->dev_num, 1);

    /* Free resources */
    kfree(dev->control_buffer);
    kfree(dev->input_buffer);
    usb_put_dev(dev->udev);
    mutex_destroy(&dev->lock);
    kfree(dev);

    psvr2_dbg(1, "PSVR2 adapter disconnected\n");
}

/*
 * Device file open function
 */
int psvr2_adapter_open(struct inode *inode, struct file *file)
{
    struct psvr2_device *dev;

    dev = container_of(inode->i_cdev, struct psvr2_device, cdev);
    file->private_data = dev;

    psvr2_dbg(2, "Device opened\n");
    return 0;
}

/*
 * Device file release function
 */
int psvr2_adapter_release(struct inode *inode, struct file *file)
{
    psvr2_dbg(2, "Device released\n");
    return 0;
}

/*
 * IOCTL handler
 */
long psvr2_adapter_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct psvr2_device *dev = file->private_data;
    void __user *argp = (void __user *)arg;
    int ret = 0;

    mutex_lock(&dev->lock);

    switch (cmd) {
    case PSVR2_IOCTL_GET_STATUS:
        if (copy_to_user(argp, &dev->status, sizeof(struct psvr2_status)))
            ret = -EFAULT;
        break;

    case PSVR2_IOCTL_SET_MODE:
        {
            struct psvr2_mode mode;
            if (copy_from_user(&mode, argp, sizeof(struct psvr2_mode))) {
                ret = -EFAULT;
                break;
            }
            ret = psvr2_display_set_mode(dev, &mode);
            if (ret == 0) {
                /* Update current mode if successful */
                memcpy(&dev->current_mode, &mode, sizeof(struct psvr2_mode));
            }
        }
        break;

    case PSVR2_IOCTL_RESET:
        /* TODO: Implement device reset */
        break;

    default:
        ret = -ENOTTY;
    }

    mutex_unlock(&dev->lock);
    return ret;
}

static const struct hid_device_id psvr2_hid_table[] = {
    { HID_USB_DEVICE(PSVR2_ADAPTER_VID, PSVR2_ADAPTER_PID) },
    { }
};
MODULE_DEVICE_TABLE(hid, psvr2_hid_table);

/*
 * Module initialization
 */
static int __init psvr2_adapter_init(void)
{
    int ret;

    pr_info("PSVR2 adapter driver version %s\n", DRIVER_VERSION);

    /* Create a proper hid_driver instance before unregistering */
    static struct hid_driver psvr2_hid_driver = {
        .name = "psvr2_hid",
        .id_table = psvr2_hid_table,
    };
    hid_unregister_driver(&psvr2_hid_driver);

    ret = usb_register(&psvr2_adapter_driver);
    if (ret) {
        pr_err("psvr2_adapter: USB registration failed: %d\n", ret);
        return ret;
    }

    return 0;
}

/*
 * Module cleanup
 */
static void __exit psvr2_adapter_exit(void)
{
    usb_deregister(&psvr2_adapter_driver);
    pr_info("PSVR2 adapter driver unloaded\n");
}

module_init(psvr2_adapter_init);
module_exit(psvr2_adapter_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);