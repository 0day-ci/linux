// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Time-Aware GPIO Controller Driver
 * Copyright (C) 2021 Intel Corporation
 */

#include <linux/acpi.h>
#include <linux/debugfs.h>
#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <uapi/linux/gpio.h>

#define TGPIOCTL		0x00
#define TGPIOCOMPV31_0		0x10
#define TGPIOCOMPV63_32		0x14
#define TGPIOPIV31_0		0x18
#define TGPIOPIV63_32		0x1c
#define TGPIOTCV31_0		0x20
#define TGPIOTCV63_32		0x24 /* Not used */
#define TGPIOECCV31_0		0x28
#define TGPIOECCV63_32		0x2c
#define TGPIOEC31_0		0x30
#define TGPIOEC63_32		0x34

/* Control Register */
#define TGPIOCTL_EN			BIT(0)
#define TGPIOCTL_DIR			BIT(1)
#define TGPIOCTL_EP			GENMASK(3, 2)
#define TGPIOCTL_EP_RISING_EDGE		(0 << 2)
#define TGPIOCTL_EP_FALLING_EDGE	BIT(2)
#define TGPIOCTL_EP_TOGGLE_EDGE		BIT(3)
#define TGPIOCTL_PM			BIT(4)

#define DRIVER_NAME		"intel-pmc-tio"

struct intel_pmc_tio_chip {
	struct gpio_chip gch;
	struct platform_device *pdev;
	struct dentry *root;
	struct debugfs_regset32 *regset;
	void __iomem *base;
};

static const struct debugfs_reg32 intel_pmc_tio_regs[] = {
	{
		.name = "TGPIOCTL",
		.offset = TGPIOCTL
	},
	{
		.name = "TGPIOCOMPV31_0",
		.offset = TGPIOCOMPV31_0
	},
	{
		.name = "TGPIOCOMPV63_32",
		.offset = TGPIOCOMPV63_32
	},
	{
		.name = "TGPIOPIV31_0",
		.offset = TGPIOPIV31_0
	},
	{
		.name = "TGPIOPIV63_32",
		.offset = TGPIOPIV63_32
	},
	{
		.name = "TGPIOECCV31_0",
		.offset = TGPIOECCV31_0
	},
	{
		.name = "TGPIOECCV63_32",
		.offset = TGPIOECCV63_32
	},
	{
		.name = "TGPIOEC31_0",
		.offset = TGPIOEC31_0
	},
	{
		.name = "TGPIOEC63_32",
		.offset = TGPIOEC63_32
	},
};

static int intel_pmc_tio_probe(struct platform_device *pdev)
{
	struct intel_pmc_tio_chip *tio;
	int err;

	tio = devm_kzalloc(&pdev->dev, sizeof(*tio), GFP_KERNEL);
	if (!tio)
		return -ENOMEM;
	tio->pdev = pdev;

	tio->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(tio->base))
		return PTR_ERR(tio->base);

	tio->regset = devm_kzalloc
		(&pdev->dev, sizeof(*tio->regset), GFP_KERNEL);
	if (!tio->regset)
		return -ENOMEM;

	tio->regset->regs = intel_pmc_tio_regs;
	tio->regset->nregs = ARRAY_SIZE(intel_pmc_tio_regs);
	tio->regset->base = tio->base;

	tio->root = debugfs_create_dir(pdev->name, NULL);
	if (IS_ERR(tio->root))
		return PTR_ERR(tio->root);

	debugfs_create_regset32("regdump", 0444, tio->root, tio->regset);

	tio->gch.label = pdev->name;
	tio->gch.ngpio = 0;
	tio->gch.base = -1;

	platform_set_drvdata(pdev, tio);

	err = devm_gpiochip_add_data(&pdev->dev, &tio->gch, tio);
	if (err < 0)
		goto out_recurse_remove_tio_root;

	return 0;

out_recurse_remove_tio_root:
	debugfs_remove_recursive(tio->root);

	return err;
}

static int intel_pmc_tio_remove(struct platform_device *pdev)
{
	struct intel_pmc_tio_chip *tio;

	tio = platform_get_drvdata(pdev);
	if (!tio)
		return -ENODEV;

	debugfs_remove_recursive(tio->root);

	return 0;
}

static const struct acpi_device_id intel_pmc_tio_acpi_match[] = {
	{ "INTC1021", 0 }, /* EHL */
	{ "INTC1022", 0 }, /* EHL */
	{ "INTC1023", 0 }, /* TGL */
	{ "INTC1024", 0 }, /* TGL */
	{  }
};

static struct platform_driver intel_pmc_tio_driver = {
	.probe          = intel_pmc_tio_probe,
	.remove         = intel_pmc_tio_remove,
	.driver         = {
		.name                   = DRIVER_NAME,
		.acpi_match_table       = intel_pmc_tio_acpi_match,
	},
};

static int intel_pmc_tio_init(void)
{
	/* To ensure ART to TSC conversion is correct */
	if (!boot_cpu_has(X86_FEATURE_TSC_KNOWN_FREQ))
		return -ENXIO;

	return platform_driver_register(&intel_pmc_tio_driver);
}

static void intel_pmc_tio_exit(void)
{
	platform_driver_unregister(&intel_pmc_tio_driver);
}

module_init(intel_pmc_tio_init);
module_exit(intel_pmc_tio_exit);

MODULE_AUTHOR("Christopher Hall <christopher.s.hall@intel.com>");
MODULE_AUTHOR("Tamal Saha <tamal.saha@intel.com>");
MODULE_AUTHOR("Lakshmi Sowjanya D <lakshmi.sowjanya.d@intel.com>");
MODULE_DESCRIPTION("Intel PMC Time-Aware GPIO Controller Driver");
MODULE_LICENSE("GPL v2");
