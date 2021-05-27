// SPDX-License-Identifier: GPL-2.0
/*
 * Window Watchdog Device Driver for Xilinx Versal WWDT
 *
 * (C) Copyright 2021 Xilinx, Inc.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/module.h>
#include <linux/watchdog.h>

#define XWWDT_DEFAULT_TIMEOUT	40
#define XWWDT_MIN_TIMEOUT	1
#define XWWDT_MAX_TIMEOUT	42

/* Register offsets for the WWdt device */
#define XWWDT_MWR_OFFSET	0x00
#define XWWDT_ESR_OFFSET	0x04
#define XWWDT_FCR_OFFSET	0x08
#define XWWDT_FWR_OFFSET	0x0c
#define XWWDT_SWR_OFFSET	0x10

/* Master Write Control Register Masks */
#define XWWDT_MWR_MASK		BIT(0)

/* Enable and Status Register Masks */
#define XWWDT_ESR_WINT_MASK	BIT(16)
#define XWWDT_ESR_WSW_MASK	BIT(8)
#define XWWDT_ESR_WEN_MASK	BIT(0)

/* Function control Register Masks */
#define XWWDT_SBC_MASK		0xFF00
#define XWWDT_SBC_SHIFT		16
#define XWWDT_BSS_MASK		0xC0

static int wwdt_timeout;

module_param(wwdt_timeout, int, 0644);
MODULE_PARM_DESC(wwdt_timeout,
		 "Watchdog time in seconds. (default="
		 __MODULE_STRING(XWWDT_DEFAULT_TIMEOUT) ")");

struct xwwdt_device {
	void __iomem *base;
	spinlock_t spinlock; /* spinlock for register handling */
	struct watchdog_device xilinx_wwdt_wdd;
	struct clk *clk;
	int irq;
};

static int is_wwdt_in_closed_window(struct watchdog_device *wdd)
{
	u32 control_status_reg;
	struct xwwdt_device *xdev = watchdog_get_drvdata(wdd);

	spin_lock(&xdev->spinlock);
	control_status_reg = ioread32(xdev->base + XWWDT_ESR_OFFSET);
	spin_unlock(&xdev->spinlock);
	if (control_status_reg & XWWDT_ESR_WEN_MASK)
		if (!(control_status_reg & XWWDT_ESR_WSW_MASK))
			return 0;

	return 1;
}

static int xilinx_wwdt_start(struct watchdog_device *wdd)
{
	struct xwwdt_device *xdev = watchdog_get_drvdata(wdd);
	struct watchdog_device *xilinx_wwdt_wdd = &xdev->xilinx_wwdt_wdd;
	u64 time_out, pre_timeout, count;
	u32 control_status_reg, fcr;
	int ret;

	count = clk_get_rate(xdev->clk);
	if (!count)
		return -EINVAL;

	/* Calculate timeout count */
	pre_timeout = count * wdd->pretimeout;
	time_out = count * wdd->timeout;
	if (!watchdog_active(xilinx_wwdt_wdd)) {
		ret  = clk_enable(xdev->clk);
		if (ret) {
			dev_err(wdd->parent, "Failed to enable clock\n");
			return ret;
		}
	}

	spin_lock(&xdev->spinlock);
	iowrite32(XWWDT_MWR_MASK, xdev->base + XWWDT_MWR_OFFSET);
	iowrite32(~(u32)XWWDT_ESR_WEN_MASK,
		  xdev->base + XWWDT_ESR_OFFSET);

	if (pre_timeout) {
		iowrite32((u32)(time_out - pre_timeout),
			  xdev->base + XWWDT_FWR_OFFSET);
		iowrite32((u32)pre_timeout, xdev->base + XWWDT_SWR_OFFSET);
		fcr = ioread32(xdev->base + XWWDT_SWR_OFFSET);
		fcr = (fcr >> XWWDT_SBC_SHIFT) & XWWDT_SBC_MASK;
		fcr = fcr | XWWDT_BSS_MASK;
		iowrite32(fcr, xdev->base + XWWDT_FCR_OFFSET);
	} else {
		iowrite32((u32)pre_timeout,
			  xdev->base + XWWDT_FWR_OFFSET);
		iowrite32((u32)time_out, xdev->base + XWWDT_SWR_OFFSET);
		iowrite32(0x0, xdev->base + XWWDT_FCR_OFFSET);
	}

	/* Enable the window watchdog timer */
	control_status_reg = ioread32(xdev->base + XWWDT_ESR_OFFSET);
	control_status_reg |= XWWDT_ESR_WEN_MASK;
	iowrite32(control_status_reg, xdev->base + XWWDT_ESR_OFFSET);

	spin_unlock(&xdev->spinlock);

	dev_dbg(xilinx_wwdt_wdd->parent, "Watchdog Started!\n");

	return 0;
}

static int xilinx_wwdt_stop(struct watchdog_device *wdd)
{
	struct xwwdt_device *xdev = watchdog_get_drvdata(wdd);
	struct watchdog_device *xilinx_wwdt_wdd = &xdev->xilinx_wwdt_wdd;

	if (!is_wwdt_in_closed_window(wdd)) {
		dev_warn(xilinx_wwdt_wdd->parent, "timer in closed window");
		return -EINVAL;
	}

	spin_lock(&xdev->spinlock);

	iowrite32(XWWDT_MWR_MASK, xdev->base + XWWDT_MWR_OFFSET);
	/* Disable the Window watchdog timer */
	iowrite32(~(u32)XWWDT_ESR_WEN_MASK,
		  xdev->base + XWWDT_ESR_OFFSET);

	spin_unlock(&xdev->spinlock);

	if (watchdog_active(xilinx_wwdt_wdd))
		clk_disable(xdev->clk);

	dev_dbg(xilinx_wwdt_wdd->parent, "Watchdog Stopped!\n");

	return 0;
}

static int xilinx_wwdt_keepalive(struct watchdog_device *wdd)
{
	u32 control_status_reg;
	struct xwwdt_device *xdev = watchdog_get_drvdata(wdd);

	/* Refresh in open window is ignored */
	if (!is_wwdt_in_closed_window(wdd))
		return 0;

	spin_lock(&xdev->spinlock);

	iowrite32(XWWDT_MWR_MASK, xdev->base + XWWDT_MWR_OFFSET);
	control_status_reg = ioread32(xdev->base + XWWDT_ESR_OFFSET);
	control_status_reg |= XWWDT_ESR_WINT_MASK;
	control_status_reg &= ~XWWDT_ESR_WSW_MASK;
	iowrite32(control_status_reg, xdev->base + XWWDT_ESR_OFFSET);
	control_status_reg = ioread32(xdev->base + XWWDT_ESR_OFFSET);
	control_status_reg |= XWWDT_ESR_WSW_MASK;
	iowrite32(control_status_reg, xdev->base + XWWDT_ESR_OFFSET);

	spin_unlock(&xdev->spinlock);

	return 0;
}

static int xilinx_wwdt_set_timeout(struct watchdog_device *wdd,
				   unsigned int new_time)
{
	u32 ret = 0;
	struct xwwdt_device *xdev = watchdog_get_drvdata(wdd);
	struct watchdog_device *xilinx_wwdt_wdd = &xdev->xilinx_wwdt_wdd;

	if (!is_wwdt_in_closed_window(wdd)) {
		dev_warn(xilinx_wwdt_wdd->parent, "timer in closed window");
		return -EINVAL;
	}

	if (new_time < XWWDT_MIN_TIMEOUT ||
	    new_time > XWWDT_MAX_TIMEOUT) {
		dev_warn(xilinx_wwdt_wdd->parent,
			 "timeout value must be %d<=x<=%d, using %d\n",
			 XWWDT_MIN_TIMEOUT,
			 XWWDT_MAX_TIMEOUT, new_time);
		return -EINVAL;
	}

	wdd->timeout = new_time;
	wdd->pretimeout = 0;

	if (watchdog_active(xilinx_wwdt_wdd)) {
		ret = xilinx_wwdt_start(wdd);
		if (ret)
			dev_dbg(xilinx_wwdt_wdd->parent, "timer start failed");
	}

	return ret;
}

static int xilinx_wwdt_set_pretimeout(struct watchdog_device *wdd,
				      u32 new_pretimeout)
{
	u32 ret = 0;
	struct xwwdt_device *xdev = watchdog_get_drvdata(wdd);
	struct watchdog_device *xilinx_wwdt_wdd = &xdev->xilinx_wwdt_wdd;

	if (!is_wwdt_in_closed_window(wdd)) {
		dev_warn(xilinx_wwdt_wdd->parent, "timer in closed window");
		return -EINVAL;
	}

	if (new_pretimeout < wdd->min_timeout ||
	    new_pretimeout >= wdd->timeout)
		return -EINVAL;

	wdd->pretimeout = new_pretimeout;

	if (watchdog_active(xilinx_wwdt_wdd)) {
		ret = xilinx_wwdt_start(wdd);
		if (ret)
			dev_dbg(xilinx_wwdt_wdd->parent, "timer start failed");
	}

	return ret;
}

static void xwwdt_clk_disable_unprepare(void *data)
{
	clk_disable_unprepare(data);
}

static irqreturn_t xilinx_wwdt_isr(int irq, void *wdog_arg)
{
	struct xwwdt_device *xdev = wdog_arg;

	watchdog_notify_pretimeout(&xdev->xilinx_wwdt_wdd);

	return IRQ_HANDLED;
}

static const struct watchdog_info xilinx_wwdt_ident = {
	.options = WDIOF_MAGICCLOSE |
		   WDIOF_KEEPALIVEPING |
		   WDIOF_SETTIMEOUT,
	.firmware_version = 1,
	.identity = "xlnx_window watchdog",
};

static const struct watchdog_info xilinx_wwdt_pretimeout_ident = {
	.options = WDIOF_MAGICCLOSE |
		   WDIOF_KEEPALIVEPING |
		   WDIOF_PRETIMEOUT |
		   WDIOF_SETTIMEOUT,
	.firmware_version = 1,
	.identity = "xlnx_window watchdog",
};

static const struct watchdog_ops xilinx_wwdt_ops = {
	.owner = THIS_MODULE,
	.start = xilinx_wwdt_start,
	.stop = xilinx_wwdt_stop,
	.ping = xilinx_wwdt_keepalive,
	.set_timeout = xilinx_wwdt_set_timeout,
	.set_pretimeout = xilinx_wwdt_set_pretimeout,
};

static int xwwdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct watchdog_device *xilinx_wwdt_wdd;
	struct xwwdt_device *xdev;
	u32 pre_timeout = 0;
	int ret;

	xdev = devm_kzalloc(dev, sizeof(*xdev), GFP_KERNEL);
	if (!xdev)
		return -ENOMEM;

	xilinx_wwdt_wdd = &xdev->xilinx_wwdt_wdd;
	xilinx_wwdt_wdd->info = &xilinx_wwdt_ident;
	xilinx_wwdt_wdd->ops = &xilinx_wwdt_ops;
	xilinx_wwdt_wdd->parent = dev;

	xdev->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(xdev->base))
		return PTR_ERR(xdev->base);

	ret = of_property_read_u32(dev->of_node, "pretimeout-sec",
				   &pre_timeout);
	if (ret)
		dev_dbg(dev,
			"Parameter \"pretimeout-sec\" not found\n");

	xdev->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(xdev->clk))
		return PTR_ERR(xdev->clk);

	ret = clk_get_rate(xdev->clk);
	if (!ret)
		return -EINVAL;

	ret = clk_prepare_enable(xdev->clk);
	if (ret) {
		dev_err(dev, "unable to enable clock\n");
		return ret;
	}
	ret = devm_add_action_or_reset(dev, xwwdt_clk_disable_unprepare,
				       xdev->clk);
	if (ret)
		goto err_clk_disable;

	xilinx_wwdt_wdd->pretimeout = pre_timeout;
	xilinx_wwdt_wdd->timeout = XWWDT_DEFAULT_TIMEOUT;
	xilinx_wwdt_wdd->min_timeout = XWWDT_MIN_TIMEOUT;
	xilinx_wwdt_wdd->max_timeout = XWWDT_MAX_TIMEOUT;

	xdev->irq = platform_get_irq_byname(pdev, "wdt");
	if (xdev->irq < 0) {
		ret = xdev->irq;
		goto err_clk_disable;
	}

	if (!devm_request_irq(dev, xdev->irq, xilinx_wwdt_isr,
			      0, dev_name(dev), xdev)) {
		xilinx_wwdt_wdd->info = &xilinx_wwdt_pretimeout_ident;
	}

	ret = watchdog_init_timeout(xilinx_wwdt_wdd,
				    wwdt_timeout, &pdev->dev);
	if (ret)
		dev_info(&pdev->dev, "Configured default timeout value\n");

	spin_lock_init(&xdev->spinlock);
	watchdog_set_drvdata(xilinx_wwdt_wdd, xdev);

	ret = devm_watchdog_register_device(dev, xilinx_wwdt_wdd);
	if (ret)
		goto err_clk_disable;

	clk_disable_unprepare(xdev->clk);

	dev_info(dev, "Xilinx Window Watchdog Timer with timeout %ds\n",
		 xilinx_wwdt_wdd->timeout);

	return 0;

err_clk_disable:
	clk_disable_unprepare(xdev->clk);
	return ret;
}

/* Mat for of_platform binding */
static const struct of_device_id xwwdt_of_match[] = {
	{ .compatible = "xlnx,versal-wwdt-1.0", },
	{},
};
MODULE_DEVICE_TABLE(of, xwwdt_of_match);

static struct platform_driver xwwdt_driver = {
	.probe = xwwdt_probe,
	.driver = {
		.name = "Xilinx Window Watchdog",
		.of_match_table = xwwdt_of_match,
	},
};

module_platform_driver(xwwdt_driver);

MODULE_AUTHOR("Neeli Srinivas <sneeli@xilinx.com>");
MODULE_DESCRIPTION("Xilinx Window Watchdog driver");
MODULE_LICENSE("GPL");
