/*
 * PSVR2 Adapter Driver for Linux - Input Module
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
#include <linux/input.h>
#include <linux/usb.h>

/* Debug print macro */
#define psvr2_input_dbg(level, fmt, args...) \
    do { if (debug >= level) pr_info("psvr2_input: " fmt, ##args); } while (0)

/* Module parameter from main module */
extern int debug;

/* USB URB for tracking data */
struct psvr2_input_urb {
    struct psvr2_device *dev;
    struct urb *urb;
    unsigned char *buffer;
    dma_addr_t dma;
};

/* Input data structure */
struct psvr2_tracking_data {
    int16_t accel[3];    /* Accelerometer data (x, y, z) */
    int16_t gyro[3];     /* Gyroscope data (x, y, z) */
    uint32_t timestamp;  /* Timestamp in microseconds */
};

/* Tracking data URB */
static struct psvr2_input_urb *tracking_urb;

/* Input device for tracking */
static struct input_dev *tracking_input_dev;

/* Function prototypes */
static void psvr2_input_complete(struct urb *urb);
static int psvr2_input_submit_urb(struct psvr2_device *dev);

/*
 * Initialize the input subsystem
 */
int psvr2_input_init(struct psvr2_device *dev)
{
    int ret;
    
    psvr2_input_dbg(1, "Initializing input subsystem\n");
    
    /* Allocate and initialize tracking URB */
    tracking_urb = kzalloc(sizeof(struct psvr2_input_urb), GFP_KERNEL);
    if (!tracking_urb) {
        pr_err("psvr2_input: Failed to allocate tracking URB\n");
        return -ENOMEM;
    }
    
    tracking_urb->dev = dev;
    tracking_urb->urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!tracking_urb->urb) {
        pr_err("psvr2_input: Failed to allocate URB\n");
        ret = -ENOMEM;
        goto error_urb_alloc;
    }
    
    tracking_urb->buffer = usb_alloc_coherent(dev->udev, 64, GFP_KERNEL, &tracking_urb->dma);
    if (!tracking_urb->buffer) {
        pr_err("psvr2_input: Failed to allocate URB buffer\n");
        ret = -ENOMEM;
        goto error_buffer_alloc;
    }
    
    /* Create input device for tracking data */
    tracking_input_dev = input_allocate_device();
    if (!tracking_input_dev) {
        pr_err("psvr2_input: Failed to allocate input device\n");
        ret = -ENOMEM;
        goto error_input_alloc;
    }
    
    tracking_input_dev->name = "PSVR2 Tracking";
    tracking_input_dev->phys = "psvr2/input0";
    tracking_input_dev->id.bustype = BUS_USB;
    tracking_input_dev->id.vendor = dev->udev->descriptor.idVendor;
    tracking_input_dev->id.product = dev->udev->descriptor.idProduct;
    tracking_input_dev->id.version = dev->udev->descriptor.bcdDevice;
    tracking_input_dev->dev.parent = &dev->interface->dev;
    
    /* Set up capabilities */
    set_bit(EV_ABS, tracking_input_dev->evbit);
    
    /* Gyroscope */
    input_set_abs_params(tracking_input_dev, ABS_RX, -32768, 32767, 16, 0);
    input_set_abs_params(tracking_input_dev, ABS_RY, -32768, 32767, 16, 0);
    input_set_abs_params(tracking_input_dev, ABS_RZ, -32768, 32767, 16, 0);
    
    /* Accelerometer */
    input_set_abs_params(tracking_input_dev, ABS_X, -32768, 32767, 16, 0);
    input_set_abs_params(tracking_input_dev, ABS_Y, -32768, 32767, 16, 0);
    input_set_abs_params(tracking_input_dev, ABS_Z, -32768, 32767, 16, 0);
    
    /* Register input device */
    ret = input_register_device(tracking_input_dev);
    if (ret) {
        pr_err("psvr2_input: Failed to register input device: %d\n", ret);
        goto error_input_register;
    }
    
    /* Start tracking */
    ret = psvr2_input_start(dev);
    if (ret) {
        pr_err("psvr2_input: Failed to start tracking: %d\n", ret);
        goto error_tracking_start;
    }
    
    /* Set status */
    dev->status.tracking_active = 1;
    
    return 0;
    
error_tracking_start:
    input_unregister_device(tracking_input_dev);
    tracking_input_dev = NULL;  /* input_unregister_device() frees the device */
    goto error_input_alloc;
error_input_register:
    input_free_device(tracking_input_dev);
error_input_alloc:
    usb_free_coherent(dev->udev, 64, tracking_urb->buffer, tracking_urb->dma);
error_buffer_alloc:
    usb_free_urb(tracking_urb->urb);
error_urb_alloc:
    kfree(tracking_urb);
    tracking_urb = NULL;
    return ret;
}

/*
 * Clean up the input subsystem
 */
void psvr2_input_cleanup(struct psvr2_device *dev)
{
    psvr2_input_dbg(1, "Cleaning up input subsystem\n");
    
    /* Stop tracking */
    psvr2_input_stop(dev);
    
    /* Unregister input device */
    if (tracking_input_dev) {
        input_unregister_device(tracking_input_dev);
        tracking_input_dev = NULL;
    }
    
    /* Free URB resources */
    if (tracking_urb) {
        if (tracking_urb->buffer) {
            usb_free_coherent(dev->udev, 64, tracking_urb->buffer, tracking_urb->dma);
        }
        if (tracking_urb->urb) {
            usb_free_urb(tracking_urb->urb);
        }
        kfree(tracking_urb);
        tracking_urb = NULL;
    }
    
    /* Set status */
    dev->status.tracking_active = 0;
}

/*
 * Submit URB for tracking data
 */
static int psvr2_input_submit_urb(struct psvr2_device *dev)
{
    int ret;
    
    /* Verify the endpoint is valid */
    if (!dev->input_ep) {
        pr_err("psvr2_input: No valid input endpoint found\n");
        return -EINVAL;
    }

    /* Make sure we're using the right transfer type - try interrupt instead of bulk */
    usb_fill_int_urb(tracking_urb->urb, dev->udev,
                   usb_rcvintpipe(dev->udev, dev->input_ep),
                   tracking_urb->buffer, 64,
                   psvr2_input_complete, tracking_urb,
                   1); /* 1ms interval */

    /* Add a delay before submitting the URB */
    msleep(10);

    /* Submit URB */
    ret = usb_submit_urb(tracking_urb->urb, GFP_KERNEL);
    if (ret) {
        pr_err("psvr2_input: Failed to submit URB: %d\n", ret);
    }
    
    return ret;
}

/*
 * Process tracking data and report to input subsystem
 */
static void psvr2_input_process_data(struct psvr2_device *dev, unsigned char *data, int size)
{
    struct psvr2_tracking_data tracking;
    
    /*
     * This is a placeholder for now. In a real implementation, this would:
     * 1. Parse the tracking data from the USB packet
     * 2. Process and convert to appropriate coordinate system
     * 3. Report to input subsystem
     */
    
    /* Placeholder - extract data from buffer */
    /* This is based on the format seen in sie_icm426.c, but actual format needs to be determined */
    if (size >= 14) {
        /* Extract accelerometer data */
        tracking.accel[0] = (data[0] << 8) | data[1];
        tracking.accel[1] = (data[2] << 8) | data[3];
        tracking.accel[2] = (data[4] << 8) | data[5];
        
        /* Extract gyroscope data */
        tracking.gyro[0] = (data[6] << 8) | data[7];
        tracking.gyro[1] = (data[8] << 8) | data[9];
        tracking.gyro[2] = (data[10] << 8) | data[11];
        
        /* Extract timestamp */
        tracking.timestamp = (data[12] << 8) | data[13];
        
        /* Report data to input subsystem */
        input_report_abs(tracking_input_dev, ABS_X, tracking.accel[0]);
        input_report_abs(tracking_input_dev, ABS_Y, tracking.accel[1]);
        input_report_abs(tracking_input_dev, ABS_Z, tracking.accel[2]);
        
        input_report_abs(tracking_input_dev, ABS_RX, tracking.gyro[0]);
        input_report_abs(tracking_input_dev, ABS_RY, tracking.gyro[1]);
        input_report_abs(tracking_input_dev, ABS_RZ, tracking.gyro[2]);
        
        input_sync(tracking_input_dev);
    }
}

/*
 * URB completion callback
 */
static void psvr2_input_complete(struct urb *urb)
{
    struct psvr2_input_urb *input_urb = urb->context;
    struct psvr2_device *dev = input_urb->dev;
    
    switch (urb->status) {
    case 0:
        /* Success - process data */
        psvr2_input_process_data(dev, input_urb->buffer, urb->actual_length);
        break;
        
    case -ECONNRESET:
    case -ENOENT:
    case -ESHUTDOWN:
        /* URB was canceled - don't resubmit */
        psvr2_input_dbg(2, "URB canceled\n");
        return;
        
    default:
        /* Error - log and resubmit */
        psvr2_input_dbg(1, "URB error %d\n", urb->status);
        break;
    }
    
    /* Resubmit URB */
    psvr2_input_submit_urb(dev);
}

/*
 * Start tracking
 */
int psvr2_input_start(struct psvr2_device *dev)
{
    int ret;
    
    psvr2_input_dbg(1, "Starting tracking\n");
    
    /* Send command to adapter to start tracking */
    /* This is a placeholder - actual command needs to be determined */
    dev->control_buffer[0] = 0x01;  /* Command: start tracking */
    
    ret = usb_control_msg(dev->udev,
                         usb_sndctrlpipe(dev->udev, 0),
                         0x09,    /* HID_REQ_SET_REPORT */
                         0x21,    /* USB direction and type */
                         0x0301,  /* Report type and ID */
                         0,       /* Interface index */
                         dev->control_buffer,
                         1,
                         1000);   /* 1 second timeout */
    
    if (ret < 0) {
        pr_err("psvr2_input: Failed to send start tracking command: %d\n", ret);
        return ret;
    }
    
    /* Submit URB to start receiving tracking data */
    ret = psvr2_input_submit_urb(dev);
    if (ret) {
        pr_err("psvr2_input: Failed to submit tracking URB: %d\n", ret);
        return ret;
    }
    
    /* Set status */
    dev->status.tracking_active = 1;
    
    return 0;
}

/*
 * Stop tracking
 */
int psvr2_input_stop(struct psvr2_device *dev)
{
    int ret;
    
    psvr2_input_dbg(1, "Stopping tracking\n");
    
    /* Cancel URB */
    if (tracking_urb && tracking_urb->urb) {
        usb_kill_urb(tracking_urb->urb);
    }
    
    /* Send command to adapter to stop tracking */
    /* This is a placeholder - actual command needs to be determined */
    dev->control_buffer[0] = 0x02;  /* Command: stop tracking */
    
    ret = usb_control_msg(dev->udev,
                         usb_sndctrlpipe(dev->udev, 0),
                         0x09,    /* HID_REQ_SET_REPORT */
                         0x21,    /* USB direction and type */
                         0x0301,  /* Report type and ID */
                         0,       /* Interface index */
                         dev->control_buffer,
                         1,
                         1000);   /* 1 second timeout */
    
    if (ret < 0) {
        pr_err("psvr2_input: Failed to send stop tracking command: %d\n", ret);
        return ret;
    }
    
    /* Set status */
    dev->status.tracking_active = 0;
    
    return 0;
}