// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the NXP ISP1760 chip
 *
 * Copyright 2021 Linaro, Rui Miguel Silva
 * Copyright 2014 Laurent Pinchart
 * Copyright 2007 Sebastian Siewior
 *
 * Contacts:
 *	Sebastian Siewior <bigeasy@linutronix.de>
 *	Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	Rui Miguel Silva <rui.silva@linaro.org>
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

static int isp1760_init_core(struct isp1760_device *isp)
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
	if ((isp->devflags & ISP1760_FLAG_ANALOG_OC) && hcd->is_isp1763) {
		dev_err(isp->dev, "isp1763 analog overcurrent not available\n");
		return -EINVAL;
	}

	if (isp->devflags & ISP1760_FLAG_BUS_WIDTH_16)
		isp1760_field_clear(hcd->fields, HW_DATA_BUS_WIDTH);
	if (isp->devflags & ISP1760_FLAG_BUS_WIDTH_8)
		isp1760_field_set(hcd->fields, HW_DATA_BUS_WIDTH);
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
	 */
	if ((isp->devflags & ISP1760_FLAG_ISP1761) &&
	    (isp->devflags & ISP1760_FLAG_PERIPHERAL_EN)) {
		isp1760_field_set(hcd->fields, HW_DM_PULLDOWN);
		isp1760_field_set(hcd->fields, HW_DP_PULLDOWN);
		isp1760_field_set(hcd->fields, HW_OTG_DISABLE);
	} else {
		isp1760_field_set(hcd->fields, HW_SW_SEL_HC_DC);
		isp1760_field_set(hcd->fields, HW_VBUS_DRV);
		isp1760_field_set(hcd->fields, HW_SEL_CP_EXT);
	}

	dev_info(isp->dev, "%s bus width: %u, oc: %s\n",
		 hcd->is_isp1763 ? "isp1763" : "isp1760",
		 isp->devflags & ISP1760_FLAG_BUS_WIDTH_8 ? 8 :
		 isp->devflags & ISP1760_FLAG_BUS_WIDTH_16 ? 16 : 32,
		 hcd->is_isp1763 ? "not available" :
		 isp->devflags & ISP1760_FLAG_ANALOG_OC ? "analog" : "digital");

	return 0;
}

void isp1760_set_pullup(struct isp1760_device *isp, bool enable)
{
	struct isp1760_udc *udc = &isp->udc;

	if (enable)
		isp1760_field_set(udc->fields, HW_DP_PULLUP);
	else
		isp1760_field_set(udc->fields, HW_DP_PULLUP_CLEAR);
}

/*
 * ISP1760/61:
 *
 * 60kb divided in:
 * - 32 blocks @ 256  bytes
 * - 20 blocks @ 1024 bytes
 * -  4 blocks @ 8192 bytes
 */
static const struct isp1760_memory_layout isp176x_memory_conf = {
	.blocks[0]		= 32,
	.blocks_size[0]		= 256,
	.blocks[1]		= 20,
	.blocks_size[1]		= 1024,
	.blocks[2]		= 4,
	.blocks_size[2]		= 8192,

	.slot_num		= 32,
	.payload_blocks		= 32 + 20 + 4,
	.payload_area_size	= 0xf000,
};

/*
 * ISP1763:
 *
 * 20kb divided in:
 * - 8 blocks @ 256  bytes
 * - 2 blocks @ 1024 bytes
 * - 4 blocks @ 4096 bytes
 */
static const struct isp1760_memory_layout isp1763_memory_conf = {
	.blocks[0]		= 8,
	.blocks_size[0]		= 256,
	.blocks[1]		= 2,
	.blocks_size[1]		= 1024,
	.blocks[2]		= 4,
	.blocks_size[2]		= 4096,

	.slot_num		= 16,
	.payload_blocks		= 8 + 2 + 4,
	.payload_area_size	= 0x5000,
};

static struct regmap_config isp1760_hc_regmap_conf = {
	.name = "isp1760-hc",
	.reg_bits = 16,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.max_register = ISP176x_HC_OTG_CTRL_CLEAR,
	.volatile_table = &isp176x_hc_volatile_table,
};

static struct regmap_config isp1763_hc_regmap_conf = {
	.name = "isp1763-hc",
	.reg_bits = 8,
	.reg_stride = 2,
	.val_bits = 16,
	.fast_io = true,
	.max_register = ISP1763_HC_OTG_CTRL_CLEAR,
	.volatile_table = &isp1763_hc_volatile_table,
};

static struct regmap_config isp1761_dc_regmap_conf = {
	.name = "isp1761-dc",
	.reg_bits = 16,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.max_register = ISP176x_DC_TESTMODE,
	.volatile_table = &isp176x_dc_volatile_table,
};

int isp1760_register(struct resource *mem, int irq, unsigned long irqflags,
		     struct device *dev, unsigned int devflags)
{
	bool udc_disabled = !(devflags & ISP1760_FLAG_ISP1761);
	const struct regmap_config *hc_regmap;
	const struct reg_field *hc_reg_fields;
	struct isp1760_device *isp;
	struct isp1760_hcd *hcd;
	struct isp1760_udc *udc;
	struct regmap_field *f;
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

	hcd->is_isp1763 = !!(devflags & ISP1760_FLAG_ISP1763);

	if (!hcd->is_isp1763 && (devflags & ISP1760_FLAG_BUS_WIDTH_8)) {
		dev_err(dev, "isp1760/61 do not support data width 8\n");
		return -EINVAL;
	}

	if (devflags & ISP1760_FLAG_BUS_WIDTH_8)
		isp1763_hc_regmap_conf.val_bits = 8;

	if (hcd->is_isp1763) {
		hc_regmap = &isp1763_hc_regmap_conf;
		hc_reg_fields = &isp1763_hc_reg_fields[0];
	} else {
		hc_regmap = &isp1760_hc_regmap_conf;
		hc_reg_fields = &isp1760_hc_reg_fields[0];
	}

	isp->rst_gpio = devm_gpiod_get_optional(dev, NULL, GPIOD_OUT_HIGH);
	if (IS_ERR(isp->rst_gpio))
		return PTR_ERR(isp->rst_gpio);

	hcd->base = devm_ioremap_resource(dev, mem);
	if (IS_ERR(hcd->base))
		return PTR_ERR(hcd->base);

	hcd->regs = devm_regmap_init_mmio(dev, hcd->base, hc_regmap);
	if (IS_ERR(hcd->regs))
		return PTR_ERR(hcd->regs);

	for (i = 0; i < HC_FIELD_MAX; i++) {
		f = devm_regmap_field_alloc(dev, hcd->regs, hc_reg_fields[i]);
		if (IS_ERR(f))
			return PTR_ERR(f);

		hcd->fields[i] = f;
	}

	udc->regs = devm_regmap_init_mmio(dev, hcd->base,
					  &isp1761_dc_regmap_conf);
	if (IS_ERR(udc->regs))
		return PTR_ERR(udc->regs);

	for (i = 0; i < DC_FIELD_MAX; i++) {
		f = devm_regmap_field_alloc(dev, udc->regs,
					    isp1761_dc_reg_fields[i]);
		if (IS_ERR(f))
			return PTR_ERR(f);

		udc->fields[i] = f;
	}

	if (hcd->is_isp1763)
		hcd->memory_layout = &isp1763_memory_conf;
	else
		hcd->memory_layout = &isp176x_memory_conf;

	ret = isp1760_init_core(isp);
	if (ret < 0)
		return ret;

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
