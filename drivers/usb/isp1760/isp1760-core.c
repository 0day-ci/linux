// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the NXP ISP1760 chip
 *
 * Copyright 2014 Laurent Pinchart
 * Copyright 2007 Sebastian Siewior
 *
 * Contacts:
 *	Sebastian Siewior <bigeasy@linutronix.de>
 *	Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include "isp1760-core.h"
#include "isp1760-hcd.h"
#include "isp1760-regs.h"
#include "isp1760-udc.h"

static void isp1760_init_core(struct isp1760_device *isp)
{
	struct isp1760_hcd *hcd = &isp->hcd;
	struct isp1760_udc *udc = &isp->udc;

	/* Low-level chip reset */
	if (isp->rst_gpio) {
		gpiod_set_value_cansleep(isp->rst_gpio, 1);
		msleep(50);
		gpiod_set_value_cansleep(isp->rst_gpio, 0);
	}

	/*
	 * Reset the host controller, including the CPU interface
	 * configuration.
	 */
	isp1760_field_set(hcd->fields, SW_RESET_RESET_ALL);
	msleep(100);

	/* Setup HW Mode Control: This assumes a level active-low interrupt */
	if (isp->devflags & ISP1760_FLAG_BUS_WIDTH_16)
		isp1760_field_clear(hcd->fields, HW_DATA_BUS_WIDTH);
	if (isp->devflags & ISP1760_FLAG_ANALOG_OC)
		isp1760_field_set(hcd->fields, HW_ANA_DIGI_OC);
	if (isp->devflags & ISP1760_FLAG_DACK_POL_HIGH)
		isp1760_field_set(hcd->fields, HW_DACK_POL_HIGH);
	if (isp->devflags & ISP1760_FLAG_DREQ_POL_HIGH)
		isp1760_field_set(hcd->fields, HW_DREQ_POL_HIGH);
	if (isp->devflags & ISP1760_FLAG_INTR_POL_HIGH)
		isp1760_field_set(hcd->fields, HW_INTR_HIGH_ACT);
	if (isp->devflags & ISP1760_FLAG_INTR_EDGE_TRIG)
		isp1760_field_set(hcd->fields, HW_INTR_EDGE_TRIG);

	/*
	 * The ISP1761 has a dedicated DC IRQ line but supports sharing the HC
	 * IRQ line for both the host and device controllers. Hardcode IRQ
	 * sharing for now and disable the DC interrupts globally to avoid
	 * spurious interrupts during HCD registration.
	 */
	if (isp->devflags & ISP1760_FLAG_ISP1761) {
		isp1760_reg_write(udc->regs, ISP176x_DC_MODE, 0);
		isp1760_field_set(hcd->fields, HW_COMN_IRQ);
	}

	/*
	 * PORT 1 Control register of the ISP1760 is the OTG control register
	 * on ISP1761.
	 *
	 * TODO: Really support OTG. For now we configure port 1 in device mode
	 * when OTG is requested.
	 */
	if ((isp->devflags & ISP1760_FLAG_ISP1761) &&
	    (isp->devflags & ISP1760_FLAG_OTG_EN)) {
		isp1760_field_set(hcd->fields, HW_DM_PULLDOWN);
		isp1760_field_set(hcd->fields, HW_DP_PULLDOWN);
		isp1760_field_set(hcd->fields, HW_OTG_DISABLE);
	} else {
		isp1760_field_set(hcd->fields, HW_SW_SEL_HC_DC);
		isp1760_field_set(hcd->fields, HW_VBUS_DRV);
		isp1760_field_set(hcd->fields, HW_SEL_CP_EXT);
	}

	dev_info(isp->dev, "bus width: %u, oc: %s\n",
		 isp->devflags & ISP1760_FLAG_BUS_WIDTH_16 ? 16 : 32,
		 isp->devflags & ISP1760_FLAG_ANALOG_OC ? "analog" : "digital");
}

void isp1760_set_pullup(struct isp1760_device *isp, bool enable)
{
	struct isp1760_udc *udc = &isp->udc;

	if (enable)
		isp1760_field_set(udc->fields, HW_DP_PULLUP);
	else
		isp1760_field_set(udc->fields, HW_DP_PULLUP_CLEAR);
}

static struct regmap_config isp1760_hc_regmap_conf = {
	.name = "isp1760-hc",
	.reg_bits = 16,
	.val_bits = 32,
	.fast_io = true,
	.max_register = ISP176x_HC_MEMORY,
	.volatile_table = &isp176x_hc_volatile_table,
};

static struct regmap_config isp1761_dc_regmap_conf = {
	.name = "isp1761-dc",
	.reg_bits = 16,
	.val_bits = 32,
	.fast_io = true,
	.max_register = ISP1761_DC_OTG_CTRL_CLEAR,
	.volatile_table = &isp176x_dc_volatile_table,
};

int isp1760_register(struct resource *mem, int irq, unsigned long irqflags,
		     struct device *dev, unsigned int devflags)
{
	struct isp1760_device *isp;
	struct isp1760_hcd *hcd;
	struct isp1760_udc *udc;
	bool udc_disabled = !(devflags & ISP1760_FLAG_ISP1761);
	struct regmap_field *f;
	void __iomem *base;
	int ret;
	int i;

	/*
	 * If neither the HCD not the UDC is enabled return an error, as no
	 * device would be registered.
	 */
	if ((!IS_ENABLED(CONFIG_USB_ISP1760_HCD) || usb_disabled()) &&
	    (!IS_ENABLED(CONFIG_USB_ISP1761_UDC) || udc_disabled))
		return -ENODEV;

	isp = devm_kzalloc(dev, sizeof(*isp), GFP_KERNEL);
	if (!isp)
		return -ENOMEM;

	isp->dev = dev;
	isp->devflags = devflags;
	hcd = &isp->hcd;
	udc = &isp->udc;

	if (devflags & ISP1760_FLAG_BUS_WIDTH_16) {
		isp1760_hc_regmap_conf.val_bits = 16;
		isp1761_dc_regmap_conf.val_bits = 16;
	}

	isp->rst_gpio = devm_gpiod_get_optional(dev, NULL, GPIOD_OUT_HIGH);
	if (IS_ERR(isp->rst_gpio))
		return PTR_ERR(isp->rst_gpio);

	hcd->base = devm_ioremap_resource(dev, mem);
	if (IS_ERR(hcd->base))
		return PTR_ERR(hcd->base);

	hcd->regs = devm_regmap_init_mmio(dev, base, &isp1760_hc_regmap_conf);
	if (IS_ERR(hcd->regs))
		return PTR_ERR(hcd->regs);

	for (i = 0; i < HC_FIELD_MAX; i++) {
		f = devm_regmap_field_alloc(dev, hcd->regs,
					    isp1760_hc_reg_fields[i]);
		if (IS_ERR(f))
			return PTR_ERR(f);

		hcd->fields[i] = f;
	}

	udc->regs = devm_regmap_init_mmio(dev, base, &isp1761_dc_regmap_conf);
	if (IS_ERR(udc->regs))
		return PTR_ERR(udc->regs);

	for (i = 0; i < DC_FIELD_MAX; i++) {
		f = devm_regmap_field_alloc(dev, udc->regs,
					    isp1761_dc_reg_fields[i]);
		if (IS_ERR(f))
			return PTR_ERR(f);

		udc->fields[i] = f;
	}

	isp1760_init_core(isp);

	if (IS_ENABLED(CONFIG_USB_ISP1760_HCD) && !usb_disabled()) {
		ret = isp1760_hcd_register(hcd, mem, irq,
					   irqflags | IRQF_SHARED, dev);
		if (ret < 0)
			return ret;
	}

	if (IS_ENABLED(CONFIG_USB_ISP1761_UDC) && !udc_disabled) {
		ret = isp1760_udc_register(isp, irq, irqflags);
		if (ret < 0) {
			isp1760_hcd_unregister(hcd);
			return ret;
		}
	}

	dev_set_drvdata(dev, isp);

	return 0;
}

void isp1760_unregister(struct device *dev)
{
	struct isp1760_device *isp = dev_get_drvdata(dev);

	isp1760_udc_unregister(isp);
	isp1760_hcd_unregister(&isp->hcd);
}

MODULE_DESCRIPTION("Driver for the ISP1760 USB-controller from NXP");
MODULE_AUTHOR("Sebastian Siewior <bigeasy@linuxtronix.de>");
MODULE_LICENSE("GPL v2");
