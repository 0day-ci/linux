// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019 Intel Corporation */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "iecm.h"

#define DRV_SUMMARY	"Intel(R) Ethernet Common Module"
static const char iecm_driver_string[] = DRV_SUMMARY;
static const char iecm_copyright[] = "Copyright (c) 2020, Intel Corporation.";

MODULE_DESCRIPTION(DRV_SUMMARY);
MODULE_LICENSE("GPL v2");

/**
 * iecm_module_init - Driver registration routine
 *
 * iecm_module_init is the first routine called when the driver is
 * loaded. All it does is register with the PCI subsystem.
 */
static int __init iecm_module_init(void)
{
	pr_info("%s - version %d\n", iecm_driver_string, LINUX_VERSION_CODE);
	pr_info("%s\n", iecm_copyright);

	return 0;
}
module_init(iecm_module_init);

/**
 * iecm_module_exit - Driver exit cleanup routine
 *
 * iecm_module_exit is called just before the driver is removed
 * from memory.
 */
static void __exit iecm_module_exit(void)
{
	pr_info("module unloaded\n");
}
module_exit(iecm_module_exit);
