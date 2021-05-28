/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SUNIX SDC mfd driver.
 *
 * Copyright (C) 2021, SUNIX Co., Ltd.
 *
 * Based on Intel Sunrisepoint LPSS core driver written by
 * - Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 * - Mika Westerberg <mika.westerberg@linux.intel.com>
 * - Heikki Krogerus <heikki.krogerus@linux.intel.com>
 * - Jarkko Nikula <jarkko.nikula@linux.intel.com>
 * Copyright (C) 2015, Intel Corporation
 */

#ifndef __SDC_MFD_H
#define __SDC_MFD_H

#include <linux/pm.h>

struct device;
struct resource;
struct property_entry;

struct sdc_platform_info {
	struct pci_dev *pdev;
	int bus_number;
	int device_number;
	int irq;
};

int sdc_probe(struct device *dev, struct sdc_platform_info *info);
void sdc_remove(struct device *dev);

#ifdef CONFIG_PM
int sdc_prepare(struct device *dev);
int sdc_suspend(struct device *dev);
int sdc_resume(struct device *dev);

#ifdef CONFIG_PM_SLEEP
#define SDC_SLEEP_PM_OPS				\
	.prepare = sdc_prepare,				\
	SET_LATE_SYSTEM_SLEEP_PM_OPS(sdc_suspend, sdc_resume)
#else
#define SDC_SLEEP_PM_OPS
#endif // CONFIG_PM_SLEEP

#define SDC_RUNTIME_PM_OPS				\
	.runtime_suspend = sdc_suspend,		\
	.runtime_resume = sdc_resume,
#else // !CONFIG_PM
#define SDC_SLEEP_PM_OPS
#define SDC_RUNTIME_PM_OPS
#endif // CONFIG_PM

#define SDC_PM_OPS(name)				\
const struct dev_pm_ops name = {		\
	SDC_SLEEP_PM_OPS					\
	SDC_RUNTIME_PM_OPS					\
}

#endif // __SDC_MFD_H
