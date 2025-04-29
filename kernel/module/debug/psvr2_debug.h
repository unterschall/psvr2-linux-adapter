/*
 * PSVR2 Adapter Driver for Linux - Debug Infrastructure
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

#ifndef PSVR2_DEBUG_H
#define PSVR2_DEBUG_H

#include <linux/module.h>

/* Debug levels */
#define DBG_NONE  0
#define DBG_ERROR 1
#define DBG_WARN  2
#define DBG_INFO  3
#define DBG_DEBUG 4

/* Feature flags for selective enabling */
#define FEAT_USB_INIT     0x0001
#define FEAT_DEVICE_INIT  0x0002
#define FEAT_INPUT        0x0004
#define FEAT_OUTPUT       0x0008
#define FEAT_HID          0x0010
#define FEAT_SENSORS      0x0020
#define FEAT_DISPLAY      0x0040
#define FEAT_TRACKING     0x0080
#define FEAT_AUDIO        0x0100

/* Declare module parameters (to be defined in main .c file) */
extern int debug_level;
extern int features_enabled;

/* Debug macros */
#define DBG(level, fmt, ...) \
    do { \
        if (level <= debug_level) \
            printk(KERN_DEBUG "psvr2_adapter: " fmt, ##__VA_ARGS__); \
    } while (0)

/* Shorthand macros for different debug levels */
#define DBG_ERR(fmt, ...)  DBG(DBG_ERROR, fmt, ##__VA_ARGS__)
#define DBG_WRN(fmt, ...)  DBG(DBG_WARN,  fmt, ##__VA_ARGS__)
#define DBG_INF(fmt, ...)  DBG(DBG_INFO,  fmt, ##__VA_ARGS__)
#define DBG_DBG(fmt, ...)  DBG(DBG_DEBUG, fmt, ##__VA_ARGS__)

/* Feature check macro */
#define FEATURE_ENABLED(feat) (features_enabled & (feat))

/* Function entry/exit debugging */
#define DBG_FUNC_ENTRY() DBG_DBG("ENTER: %s\n", __func__)
#define DBG_FUNC_EXIT()  DBG_DBG("EXIT: %s\n", __func__)
#define DBG_FUNC_EXIT_ERR(err) DBG_ERR("EXIT: %s with error %d\n", __func__, err)

/* Memory debugging */
#define DBG_MEM_ALLOC(ptr, size) \
    DBG_DBG("MEM ALLOC: %p, size %zu at %s:%d\n", ptr, (size_t)size, __FILE__, __LINE__)
    
#define DBG_MEM_FREE(ptr) \
    DBG_DBG("MEM FREE: %p at %s:%d\n", ptr, __FILE__, __LINE__)

/* USB debugging */
#define DBG_USB_SUBMIT(urb) \
    DBG_DBG("USB SUBMIT: endpoint 0x%02X, buffer %p, length %d\n", \
        usb_endpoint_num(&urb->ep->desc), urb->transfer_buffer, urb->transfer_buffer_length)
        
#define DBG_USB_COMPLETE(urb) \
    DBG_DBG("USB COMPLETE: endpoint 0x%02X, status %d, actual length %d\n", \
        usb_endpoint_num(&urb->ep->desc), urb->status, urb->actual_length)

/* Assert-like macro (safer than BUG_ON) */
#define DBG_ASSERT(cond, fmt, ...) \
    do { \
        if (!(cond)) { \
            DBG_ERR("Assertion failed: %s - " fmt, #cond, ##__VA_ARGS__); \
            WARN_ON(1); \
        } \
    } while(0)

#endif /* PSVR2_DEBUG_H */