// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2017-2021 Intel Corporation

#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "gna_device.h"
#include "gna_driver.h"

static int recovery_timeout = 60;
module_param(recovery_timeout, int, 0644);
MODULE_PARM_DESC(recovery_timeout, "Recovery timeout in seconds");

struct gna_driver_private gna_drv_priv;

static struct pci_driver gna_driver = {
	.name = GNA_DV_NAME,
	.id_table = gna_pci_ids,
	.probe = gna_probe,
	.remove = gna_remove,
};

static int __init gna_drv_init(void)
{
	atomic_set(&gna_drv_priv.dev_last_idx, -1);

	gna_drv_priv.recovery_timeout_jiffies = msecs_to_jiffies(recovery_timeout * 1000);

	return pci_register_driver(&gna_driver);
}

static void __exit gna_drv_exit(void)
{
	pci_unregister_driver(&gna_driver);
}

module_init(gna_drv_init);
module_exit(gna_drv_exit);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) Gaussian & Neural Accelerator (Intel(R) GNA) Driver");
MODULE_LICENSE("GPL");
