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

#ifndef _PSVR2_ADAPTER_H_
#define _PSVR2_ADAPTER_H_

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/cdev.h>  /* For character device support */
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux//hid.h>

/* Sony PSVR2 Adapter VID/PID */
#define PSVR2_ADAPTER_VID          0x054C  /* Sony Corp. */
#define PSVR2_ADAPTER_PID          0x0CDE  /* PlayStation VR2 */

/* Feature report sizes and commands - to be discovered through protocol analysis */
#define PSVR2_FEATURE_REPORT_SIZE  64
#define PSVR2_GET_STATUS           0x01
#define PSVR2_SET_DISPLAY_MODE     0x02
#define PSVR2_GET_VERSION          0x03

/* IOCTL commands */
#define PSVR2_IOC_MAGIC            'P'
#define PSVR2_IOCTL_GET_STATUS     _IOR(PSVR2_IOC_MAGIC, 1, struct psvr2_status)
#define PSVR2_IOCTL_SET_MODE       _IOW(PSVR2_IOC_MAGIC, 2, struct psvr2_mode)
#define PSVR2_IOCTL_RESET          _IO(PSVR2_IOC_MAGIC, 3)

/* Device status struct */
struct psvr2_status {
    uint8_t connected;     /* 1 if headset is connected */
    uint8_t display_active; /* 1 if display is active */
    uint8_t tracking_active; /* 1 if tracking is active */
    uint32_t error_code;    /* Error code if any */
};

/* Display mode struct */
struct psvr2_mode {
    uint16_t width;        /* Display width in pixels */
    uint16_t height;       /* Display height in pixels */
    uint8_t refresh_rate;  /* Refresh rate in Hz */
    uint8_t flags;         /* Additional flags */
};

/* Main device structure */
struct psvr2_device {
    struct usb_device *udev;        /* USB device */
    struct usb_interface *interface; /* USB interface */
    struct mutex lock;              /* Device lock */
    
    /* USB endpoints */
    unsigned int control_ep;        /* Control endpoint */
    unsigned int input_ep;          /* Input endpoint for tracking data */
    unsigned int output_ep;         /* Output endpoint for commands */
    
    /* Device state */
    struct psvr2_status status;     /* Current device status */
    struct psvr2_mode current_mode; /* Current display mode */
    
    /* Buffers */
    uint8_t *control_buffer;        /* Buffer for control transfers */
    uint8_t *input_buffer;          /* Buffer for input transfers */
    
    /* Character device */
    dev_t dev_num;                  /* Device number */
    struct cdev cdev;               /* Character device */
    struct class *class;            /* Device class */
};

/* Function prototypes */
extern int psvr2_adapter_probe(struct usb_interface *interface, const struct usb_device_id *id);
extern void psvr2_adapter_disconnect(struct usb_interface *interface);
extern int psvr2_adapter_open(struct inode *inode, struct file *file);
extern int psvr2_adapter_release(struct inode *inode, struct file *file);
extern long psvr2_adapter_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

/* Display functions */
extern int psvr2_display_init(struct psvr2_device *dev);
extern void psvr2_display_cleanup(struct psvr2_device *dev);
extern int psvr2_display_set_mode(struct psvr2_device *dev, struct psvr2_mode *mode);

/* Input functions */
extern int psvr2_input_init(struct psvr2_device *dev);
extern void psvr2_input_cleanup(struct psvr2_device *dev);
extern int psvr2_input_start(struct psvr2_device *dev);
extern int psvr2_input_stop(struct psvr2_device *dev);

#endif /* _PSVR2_ADAPTER_H_ */