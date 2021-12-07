// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Synopsys DesignWare I2C adapter driver (master only).
 *
 * Based on the TI DAVINCI I2C adapter driver.
 *
 * Copyright (C) 2006 Texas Instruments.
 * Copyright (C) 2007 MontaVista Software Inc.
 * Copyright (C) 2009 Provigent Ltd.
 * Copyright (C) 2011, 2015, 2016 Intel Corporation.
 */
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "i2c-designware-core.h"
#include "i2c-ccgx-ucsi.h"

#define DRIVER_NAME "i2c-designware-pci"
#define AMD_CLK_RATE_HZ	100000

enum dw_pci_ctl_id_t {
	medfield,
	merrifield,
	baytrail,
	cherrytrail,
	haswell,
	elkhartlake,
	navi_amd,
};

struct dw_scl_sda_cfg {
	u32 ss_hcnt;
	u32 fs_hcnt;
	u32 ss_lcnt;
	u32 fs_lcnt;
	u32 sda_hold;
};

struct dw_pci_controller {
	u32 bus_num;
	u32 flags;
	struct dw_scl_sda_cfg *scl_sda_cfg;
	int (*setup)(struct pci_dev *pdev, struct dw_pci_controller *c);
	u32 (*get_clk_rate_khz)(struct dw_i2c_dev *i_dev);
};

/* Merrifield HCNT/LCNT/SDA hold time */
static struct dw_scl_sda_cfg mrfld_config = {
	.ss_hcnt = 0x2f8,
	.fs_hcnt = 0x87,
	.ss_lcnt = 0x37b,
	.fs_lcnt = 0x10a,
};

/* BayTrail HCNT/LCNT/SDA hold time */
static struct dw_scl_sda_cfg byt_config = {
	.ss_hcnt = 0x200,
	.fs_hcnt = 0x55,
	.ss_lcnt = 0x200,
	.fs_lcnt = 0x99,
	.sda_hold = 0x6,
};

/* Haswell HCNT/LCNT/SDA hold time */
static struct dw_scl_sda_cfg hsw_config = {
	.ss_hcnt = 0x01b0,
	.fs_hcnt = 0x48,
	.ss_lcnt = 0x01fb,
	.fs_lcnt = 0xa0,
	.sda_hold = 0x9,
};

/* NAVI-AMD HCNT/LCNT/SDA hold time */
static struct dw_scl_sda_cfg navi_amd_config = {
	.ss_hcnt = 0x1ae,
	.ss_lcnt = 0x23a,
	.sda_hold = 0x9,
};

static u32 mfld_get_clk_rate_khz(struct dw_i2c_dev *i_dev)
{
	return 25000;
}

static u32 navi_amd_get_clk_rate_khz(struct dw_i2c_dev *i_dev)
{
	return AMD_CLK_RATE_HZ;
}

static int mfld_setup(struct pci_dev *pdev, struct dw_pci_controller *c)
{
	struct dw_i2c_dev *i_dev = dev_get_drvdata(&pdev->dev);

	switch (pdev->device) {
	case 0x0817:
		i_dev->timings.bus_freq_hz = I2C_MAX_STANDARD_MODE_FREQ;
		fallthrough;
	case 0x0818:
	case 0x0819:
		c->bus_num = pdev->device - 0x817 + 3;
		return 0;
	case 0x082C:
	case 0x082D:
	case 0x082E:
		c->bus_num = pdev->device - 0x82C + 0;
		return 0;
	}
	return -ENODEV;
}

static int navi_amd_setup(struct pci_dev *pdev, struct dw_pci_controller *c)
{
	struct dw_i2c_dev *i_dev = dev_get_drvdata(&pdev->dev);

	i_dev->flags |= MODEL_AMD_NAVI_GPU;
	i_dev->timings.bus_freq_hz = I2C_MAX_STANDARD_MODE_FREQ;
	return 0;
}

static int mrfld_setup(struct pci_dev *pdev, struct dw_pci_controller *c)
{
	/*
	 * On Intel Merrifield the user visible i2c buses are enumerated
	 * [1..7]. So, we add 1 to shift the default range. Besides that the
	 * first PCI slot provides 4 functions, that's why we have to add 0 to
	 * the first slot and 4 to the next one.
	 */
	switch (PCI_SLOT(pdev->devfn)) {
	case 8:
		c->bus_num = PCI_FUNC(pdev->devfn) + 0 + 1;
		return 0;
	case 9:
		c->bus_num = PCI_FUNC(pdev->devfn) + 4 + 1;
		return 0;
	}
	return -ENODEV;
}

static u32 ehl_get_clk_rate_khz(struct dw_i2c_dev *dev)
{
	return 100000;
}

static struct dw_pci_controller dw_pci_controllers[] = {
	[medfield] = {
		.bus_num = -1,
		.setup = mfld_setup,
		.get_clk_rate_khz = mfld_get_clk_rate_khz,
	},
	[merrifield] = {
		.bus_num = -1,
		.scl_sda_cfg = &mrfld_config,
		.setup = mrfld_setup,
	},
	[baytrail] = {
		.bus_num = -1,
		.scl_sda_cfg = &byt_config,
	},
	[haswell] = {
		.bus_num = -1,
		.scl_sda_cfg = &hsw_config,
	},
	[cherrytrail] = {
		.bus_num = -1,
		.scl_sda_cfg = &byt_config,
	},
	[elkhartlake] = {
		.bus_num = -1,
		.get_clk_rate_khz = ehl_get_clk_rate_khz,
	},
	[navi_amd] = {
		.bus_num = -1,
		.scl_sda_cfg = &navi_amd_config,
		.setup =  navi_amd_setup,
		.get_clk_rate_khz = navi_amd_get_clk_rate_khz,
	},
};

#ifdef CONFIG_PM
static int i2c_dw_pci_suspend(struct device *dev)
{
	struct dw_i2c_dev *i_dev = dev_get_drvdata(dev);

	i_dev->suspended = true;
	i_dev->disable(i_dev);

	return 0;
}

static int i2c_dw_pci_resume(struct device *dev)
{
	struct dw_i2c_dev *i_dev = dev_get_drvdata(dev);
	int ret;

	ret = i_dev->init(i_dev);
	i_dev->suspended = false;

	return ret;
}
#endif

static UNIVERSAL_DEV_PM_OPS(i2c_dw_pm_ops, i2c_dw_pci_suspend,
			    i2c_dw_pci_resume, NULL);

static int i2c_dw_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct i2c_adapter *adap;
	int r;
	struct dw_pci_controller *controller;
	struct dw_scl_sda_cfg *cfg;
	struct dw_i2c_dev *i_dev;

	if (id->driver_data >= ARRAY_SIZE(dw_pci_controllers)) {
		dev_err(dev, "%s: invalid driver data %ld\n", __func__,
			id->driver_data);
		return -EINVAL;
	}

	controller = &dw_pci_controllers[id->driver_data];

	r = pcim_enable_device(pdev);
	if (r) {
		dev_err(dev, "Failed to enable I2C PCI device (%d)\n",
			r);
		return r;
	}

	pci_set_master(pdev);

	r = pcim_iomap_regions(pdev, 1 << 0, pci_name(pdev));
	if (r) {
		dev_err(dev, "I/O memory remapping failed\n");
		return r;
	}

	i_dev = devm_kzalloc(dev, sizeof(*i_dev), GFP_KERNEL);
	if (!i_dev)
		return -ENOMEM;

	r = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (r < 0)
		return r;

	i_dev->get_clk_rate_khz = controller->get_clk_rate_khz;
	i_dev->timings.bus_freq_hz = I2C_MAX_FAST_MODE_FREQ;
	i_dev->base = pcim_iomap_table(pdev)[0];
	i_dev->dev = dev;
	i_dev->irq = pci_irq_vector(pdev, 0);
	i_dev->flags |= controller->flags;

	pci_set_drvdata(pdev, i_dev);

	if (controller->setup) {
		r = controller->setup(pdev, controller);
		if (r) {
			pci_free_irq_vectors(pdev);
			return r;
		}
	}

	i2c_dw_adjust_bus_speed(i_dev);

	if (has_acpi_companion(dev))
		i2c_dw_acpi_configure(dev);

	r = i2c_dw_validate_speed(i_dev);
	if (r) {
		pci_free_irq_vectors(pdev);
		return r;
	}

	i2c_dw_configure(i_dev);

	if (controller->scl_sda_cfg) {
		cfg = controller->scl_sda_cfg;
		i_dev->ss_hcnt = cfg->ss_hcnt;
		i_dev->fs_hcnt = cfg->fs_hcnt;
		i_dev->ss_lcnt = cfg->ss_lcnt;
		i_dev->fs_lcnt = cfg->fs_lcnt;
		i_dev->sda_hold_time = cfg->sda_hold;
	}

	adap = &i_dev->adapter;
	adap->owner = THIS_MODULE;
	adap->class = 0;
	ACPI_COMPANION_SET(&adap->dev, ACPI_COMPANION(dev));
	adap->nr = controller->bus_num;

	r = i2c_dw_probe(i_dev);
	if (r) {
		pci_free_irq_vectors(pdev);
		return r;
	}

	if ((i_dev->flags & MODEL_MASK) == MODEL_AMD_NAVI_GPU) {
		i_dev->slave = i2c_new_ccgx_ucsi(&i_dev->adapter, i_dev->irq, NULL);
		if (IS_ERR(i_dev->slave))
			return dev_err_probe(dev, PTR_ERR(i_dev->slave), "register UCSI failed\n");
	}

	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_put_autosuspend(dev);
	pm_runtime_allow(dev);

	return 0;
}

static void i2c_dw_pci_remove(struct pci_dev *pdev)
{
	struct dw_i2c_dev *i_dev = pci_get_drvdata(pdev);
	struct device *dev = &pdev->dev;

	i_dev->disable(i_dev);
	pm_runtime_forbid(dev);
	pm_runtime_get_noresume(dev);

	i2c_del_adapter(&i_dev->adapter);
	devm_free_irq(dev, i_dev->irq, i_dev);
	pci_free_irq_vectors(pdev);
}

/* work with hotplug and coldplug */
MODULE_ALIAS("i2c_designware-pci");

static const struct pci_device_id i2_designware_pci_ids[] = {
	/* Medfield */
	{ PCI_VDEVICE(INTEL, 0x0817), medfield },
	{ PCI_VDEVICE(INTEL, 0x0818), medfield },
	{ PCI_VDEVICE(INTEL, 0x0819), medfield },
	{ PCI_VDEVICE(INTEL, 0x082C), medfield },
	{ PCI_VDEVICE(INTEL, 0x082D), medfield },
	{ PCI_VDEVICE(INTEL, 0x082E), medfield },
	/* Merrifield */
	{ PCI_VDEVICE(INTEL, 0x1195), merrifield },
	{ PCI_VDEVICE(INTEL, 0x1196), merrifield },
	/* Baytrail */
	{ PCI_VDEVICE(INTEL, 0x0F41), baytrail },
	{ PCI_VDEVICE(INTEL, 0x0F42), baytrail },
	{ PCI_VDEVICE(INTEL, 0x0F43), baytrail },
	{ PCI_VDEVICE(INTEL, 0x0F44), baytrail },
	{ PCI_VDEVICE(INTEL, 0x0F45), baytrail },
	{ PCI_VDEVICE(INTEL, 0x0F46), baytrail },
	{ PCI_VDEVICE(INTEL, 0x0F47), baytrail },
	/* Haswell */
	{ PCI_VDEVICE(INTEL, 0x9c61), haswell },
	{ PCI_VDEVICE(INTEL, 0x9c62), haswell },
	/* Braswell / Cherrytrail */
	{ PCI_VDEVICE(INTEL, 0x22C1), cherrytrail },
	{ PCI_VDEVICE(INTEL, 0x22C2), cherrytrail },
	{ PCI_VDEVICE(INTEL, 0x22C3), cherrytrail },
	{ PCI_VDEVICE(INTEL, 0x22C4), cherrytrail },
	{ PCI_VDEVICE(INTEL, 0x22C5), cherrytrail },
	{ PCI_VDEVICE(INTEL, 0x22C6), cherrytrail },
	{ PCI_VDEVICE(INTEL, 0x22C7), cherrytrail },
	/* Elkhart Lake (PSE I2C) */
	{ PCI_VDEVICE(INTEL, 0x4bb9), elkhartlake },
	{ PCI_VDEVICE(INTEL, 0x4bba), elkhartlake },
	{ PCI_VDEVICE(INTEL, 0x4bbb), elkhartlake },
	{ PCI_VDEVICE(INTEL, 0x4bbc), elkhartlake },
	{ PCI_VDEVICE(INTEL, 0x4bbd), elkhartlake },
	{ PCI_VDEVICE(INTEL, 0x4bbe), elkhartlake },
	{ PCI_VDEVICE(INTEL, 0x4bbf), elkhartlake },
	{ PCI_VDEVICE(INTEL, 0x4bc0), elkhartlake },
	{ PCI_VDEVICE(ATI,  0x7314), navi_amd },
	{ PCI_VDEVICE(ATI,  0x73a4), navi_amd },
	{ PCI_VDEVICE(ATI,  0x73e4), navi_amd },
	{ PCI_VDEVICE(ATI,  0x73c4), navi_amd },
	{ 0,}
};
MODULE_DEVICE_TABLE(pci, i2_designware_pci_ids);

static struct pci_driver dw_i2c_driver = {
	.name		= DRIVER_NAME,
	.id_table	= i2_designware_pci_ids,
	.probe		= i2c_dw_pci_probe,
	.remove		= i2c_dw_pci_remove,
	.driver         = {
		.pm     = &i2c_dw_pm_ops,
	},
};

module_pci_driver(dw_i2c_driver);

MODULE_AUTHOR("Baruch Siach <baruch@tkos.co.il>");
MODULE_DESCRIPTION("Synopsys DesignWare PCI I2C bus adapter");
MODULE_LICENSE("GPL");
