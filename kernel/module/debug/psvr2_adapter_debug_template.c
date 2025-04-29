/*
 * PSVR2 Adapter Debug Template - Add to main module file
 *
 * Copyright (C) 2025 PSVR2 Linux Project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

// Add these near the top of your main .c file
#include "debug/psvr2_debug.h"

// Module parameters for debugging
int debug_level = DBG_INFO;
module_param(debug_level, int, 0644);
MODULE_PARM_DESC(debug_level, "Debug level (0-4)");

int features_enabled = 0xFFFF;  // All features by default
module_param(features_enabled, int, 0644);
MODULE_PARM_DESC(features_enabled, "Bit mask for enabled features (default: all)");

// Example usage in probe function:
static int psvr2_adapter_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
    struct psvr2_device *dev;
    int retval = -ENOMEM;
    
    DBG_FUNC_ENTRY();
    
    // Check if device initialization is enabled
    if (!FEATURE_ENABLED(FEAT_DEVICE_INIT)) {
        DBG_INF("Device initialization disabled by module parameter\n");
        return 0;
    }
    
    /* Rest of your probe function with debug calls added */
    
    // Allocate device structure
    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev) {
        DBG_ERR("Failed to allocate memory for device structure\n");
        return -ENOMEM;
    }
    DBG_MEM_ALLOC(dev, sizeof(*dev));
    
    // Check if USB initialization is enabled
    if (!FEATURE_ENABLED(FEAT_USB_INIT)) {
        DBG_INF("USB initialization disabled by module parameter\n");
        // Skip USB-specific initialization
    }
    
    // Before initializing display
    if (!FEATURE_ENABLED(FEAT_DISPLAY)) {
        DBG_INF("Display functionality disabled by module parameter\n");
        // Skip display initialization
    } else {
        // Initialize display
        retval = psvr2_display_init(dev);
        if (retval < 0) {
            DBG_ERR("Failed to initialize display: %d\n", retval);
            goto error_display;
        }
    }
    
    // Before initializing input
    if (!FEATURE_ENABLED(FEAT_INPUT)) {
        DBG_INF("Input functionality disabled by module parameter\n");
        // Skip input initialization
    } else {
        // Initialize input
        retval = psvr2_input_init(dev);
        if (retval < 0) {
            DBG_ERR("Failed to initialize input: %d\n", retval);
            goto error_input;
        }
    }
    
    DBG_FUNC_EXIT();
    return 0;

error_input:
    if (FEATURE_ENABLED(FEAT_DISPLAY))
        psvr2_display_cleanup(dev);
error_display:
    // Other error cleanup
    DBG_MEM_FREE(dev);
    kfree(dev);
    DBG_FUNC_EXIT_ERR(retval);
    return retval;
}

// Example usage in disconnect function:
static void psvr2_adapter_disconnect(struct usb_interface *interface)
{
    struct psvr2_device *dev = usb_get_intfdata(interface);
    
    DBG_FUNC_ENTRY();
    
    // Validate dev pointer
    if (!dev) {
        DBG_ERR("dev is NULL in disconnect\n");
        return;
    }
    
    /* Rest of disconnect function with debug calls added */
    
    // Clean up subsystems based on what was enabled
    if (FEATURE_ENABLED(FEAT_INPUT))
        psvr2_input_cleanup(dev);
    
    if (FEATURE_ENABLED(FEAT_DISPLAY))
        psvr2_display_cleanup(dev);
    
    // Free memory
    DBG_MEM_FREE(dev);
    kfree(dev);
    
    DBG_FUNC_EXIT();
}