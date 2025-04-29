/*
 * Simple Test Module for PSVR2 Debugging
 *
 * Copyright (C) 2025 PSVR2 Linux Project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("PSVR2 Linux Project");
MODULE_DESCRIPTION("Minimal Test Module for PSVR2 Debugging");

static int __init test_init(void)
{
    printk(KERN_INFO "Test module initialized\n");
    return 0;
}

static void __exit test_exit(void)
{
    printk(KERN_INFO "Test module unloaded\n");
}

module_init(test_init);
module_exit(test_exit);