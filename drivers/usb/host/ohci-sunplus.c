// SPDX-License-Identifier: GPL-2.0

/*
 * The USB HOST OHCI driver for Sunplus SP7021
 *
 * Copyright (C) 2021 Sunplus Technology Inc., All rights reserved.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/usb/ohci_pdriver.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>

#include "ohci.h"

#define hcd_to_sp_ohci_priv(h) \
	((struct sp_ohci_priv *)hcd_to_ohci(h)->priv)

struct sp_ohci_priv {
	struct clk *ohci_clk;
	struct reset_control *ohci_rstc;
};

static struct hc_driver __read_mostly ohci_sunplus_driver;

static const struct ohci_driver_overrides ohci_sunplus_overrides __initconst = {
	.extra_priv_size =	sizeof(struct sp_ohci_priv),
};

static int ohci_sunplus_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sp_ohci_priv *sp_priv;
	struct resource *res_mem;
	struct usb_hcd *hcd;
	int irq;
	int ret;

	if (usb_disabled())
		return -ENODEV;

	hcd = usb_create_hcd(&ohci_sunplus_driver, dev, dev_name(&pdev->dev));
	if (!hcd)
		return -ENOMEM;

	platform_set_drvdata(pdev, hcd);
	sp_priv = hcd_to_sp_ohci_priv(hcd);

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hcd->regs = devm_ioremap_resource(dev, res_mem);
	if (IS_ERR(hcd->regs)) {
		ret = PTR_ERR(hcd->regs);
		goto err_put_hcd;
	}

	hcd->rsrc_start = res_mem->start;
	hcd->rsrc_len = resource_size(res_mem);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		pr_err("no irq provieded\n");
		ret = irq;
		goto err_put_hcd;
	}

	sp_priv->ohci_clk = devm_clk_get(dev, NULL);
	if (IS_ERR(sp_priv->ohci_clk)) {
		pr_err("not found clk source\n");
		ret = PTR_ERR(sp_priv->ohci_clk);
		goto err_put_hcd;
	}

	sp_priv->ohci_rstc = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(sp_priv->ohci_rstc)) {
		ret = PTR_ERR(sp_priv->ohci_rstc);
		goto err_put_hcd;
	}

	ret = clk_prepare_enable(sp_priv->ohci_clk);
	if (ret)
		goto err_clk;

	ret = reset_control_deassert(sp_priv->ohci_rstc);
	if (ret)
		goto err_reset;

	ret = usb_add_hcd(hcd, irq, IRQF_SHARED);
	if (ret)
		goto err_reset;

	dev_dbg(dev, "hcd_irq:%d,%d\n", hcd->irq, irq);

	return ret;

err_reset:
	reset_control_assert(sp_priv->ohci_rstc);
err_clk:
	clk_disable_unprepare(sp_priv->ohci_clk);
err_put_hcd:
	usb_put_hcd(hcd);

	return ret;
}

static int ohci_sunplus_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);
	struct sp_ohci_priv *sp_priv = hcd_to_sp_ohci_priv(hcd);

	usb_remove_hcd(hcd);
	usb_put_hcd(hcd);
	platform_set_drvdata(pdev, NULL);

	reset_control_assert(sp_priv->ohci_rstc);
	clk_disable_unprepare(sp_priv->ohci_clk);

	return 0;
}

#ifdef CONFIG_PM
static int ohci_sunplus_drv_suspend(struct device *pdev)
{
	struct usb_hcd *hcd = dev_get_drvdata(pdev);
	struct sp_ohci_priv *sp_priv = hcd_to_sp_ohci_priv(hcd);
	bool do_wakeup = device_may_wakeup(pdev);
	int rc;

	rc = ohci_suspend(hcd, do_wakeup);
	if (rc)
		return rc;

	reset_control_assert(sp_priv->ohci_rstc);
	clk_disable_unprepare(sp_priv->ohci_clk);

	return 0;
}

static int ohci_sunplus_drv_resume(struct device *pdev)
{
	struct usb_hcd *hcd = dev_get_drvdata(pdev);
	struct sp_ohci_priv *sp_priv = hcd_to_sp_ohci_priv(hcd);

	clk_prepare_enable(sp_priv->ohci_clk);
	reset_control_deassert(sp_priv->ohci_rstc);

	ohci_resume(hcd, false);

	return 0;
}

struct dev_pm_ops const ohci_sunplus_pm_ops = {
	.suspend = ohci_sunplus_drv_suspend,
	.resume = ohci_sunplus_drv_resume,
};
#endif

static const struct of_device_id ohci_sunplus_dt_ids[] = {
	{ .compatible = "sunplus,sp7021-usb-ohci" },
	{ }
};
MODULE_DEVICE_TABLE(of, ohci_sunplus_dt_ids);

static struct platform_driver ohci_hcd_sunplus_driver = {
	.probe			= ohci_sunplus_probe,
	.remove			= ohci_sunplus_remove,
	.shutdown		= usb_hcd_platform_shutdown,
	.driver = {
		.name		= "ohci-sunplus",
		.of_match_table = ohci_sunplus_dt_ids,
#ifdef CONFIG_PM
		.pm = &ohci_sunplus_pm_ops,
#endif
	}
};

static int __init ohci_sunplus_init(void)
{
	if (usb_disabled())
		return -ENODEV;

	ohci_init_driver(&ohci_sunplus_driver, &ohci_sunplus_overrides);

	return platform_driver_register(&ohci_hcd_sunplus_driver);
}
module_init(ohci_sunplus_init);

static void __exit ohci_sunplus_cleanup(void)
{
	platform_driver_unregister(&ohci_hcd_sunplus_driver);
}
module_exit(ohci_sunplus_cleanup);

MODULE_AUTHOR("Vincent Shih <vincent.sunplus@gmail.com>");
MODULE_DESCRIPTION("Sunplus USB OHCI driver");
MODULE_LICENSE("GPL");

