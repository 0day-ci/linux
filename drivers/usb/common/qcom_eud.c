// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/usb/role.h>

#define EUD_REG_INT1_EN_MASK	0x0024
#define EUD_REG_INT_STATUS_1	0x0044
#define EUD_REG_CTL_OUT_1	0x0074
#define EUD_REG_VBUS_INT_CLR	0x0080
#define EUD_REG_CSR_EUD_EN	0x1014
#define EUD_REG_SW_ATTACH_DET	0x1018
#define EUD_REG_EUD_EN2         0x0000

#define EUD_ENABLE		BIT(0)
#define EUD_INT_PET_EUD		BIT(0)
#define EUD_INT_VBUS		BIT(2)
#define EUD_INT_SAFE_MODE	BIT(4)
#define EUD_INT_ALL		(EUD_INT_VBUS|EUD_INT_SAFE_MODE)

struct eud_chip {
	struct device			*dev;
	struct usb_role_switch		*role_sw;
	void __iomem			*eud_reg_base;
	void __iomem			*eud_mode_mgr2_phys_base;
	unsigned int			int_status;
	int				enable;
	int				eud_irq;
	bool				usb_attach;

};

static int enable_eud(struct eud_chip *priv)
{
	writel(EUD_ENABLE, priv->eud_reg_base + EUD_REG_CSR_EUD_EN);
	writel(EUD_INT_VBUS | EUD_INT_SAFE_MODE,
			priv->eud_reg_base + EUD_REG_INT1_EN_MASK);
	writel(1, priv->eud_mode_mgr2_phys_base + EUD_REG_EUD_EN2);

	return usb_role_switch_set_role(priv->role_sw, USB_ROLE_DEVICE);
}

static void disable_eud(struct eud_chip *priv)
{
	writel(0, priv->eud_reg_base + EUD_REG_CSR_EUD_EN);
	writel(0, priv->eud_mode_mgr2_phys_base + EUD_REG_EUD_EN2);
}

static ssize_t enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct eud_chip *chip = dev_get_drvdata(dev);

	return sprintf(buf, "%d", chip->enable);
}

static ssize_t enable_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct eud_chip *chip = dev_get_drvdata(dev);
	unsigned long enable;
	int ret;

	if (kstrtoul(buf, 16, &enable))
		return -EINVAL;

	if (enable == 1) {
		ret = enable_eud(chip);
		if (!ret)
			chip->enable = enable;
	} else if (enable == 0) {
		disable_eud(chip);
	} else {
		return -EINVAL;
	}

	return count;
}

static DEVICE_ATTR_RW(enable);

static const struct device_attribute *eud_attrs[] = {
	&dev_attr_enable,
	NULL,
};

static void usb_attach_detach(struct eud_chip *chip)
{
	u32 reg;

	/* read ctl_out_1[4] to find USB attach or detach event */
	reg = readl(chip->eud_reg_base + EUD_REG_CTL_OUT_1);
	if (reg & EUD_INT_SAFE_MODE)
		chip->usb_attach = true;
	else
		chip->usb_attach = false;

	/* set and clear vbus_int_clr[0] to clear interrupt */
	writel(BIT(0), chip->eud_reg_base + EUD_REG_VBUS_INT_CLR);
	writel(0, chip->eud_reg_base + EUD_REG_VBUS_INT_CLR);
}

static void pet_eud(struct eud_chip *chip)
{
	u32 reg;
	int ret;

	/* read sw_attach_det[0] to find attach/detach event */
	reg = readl(chip->eud_reg_base +  EUD_REG_SW_ATTACH_DET);
	if (reg & EUD_INT_PET_EUD) {
		/* Detach & Attach pet for EUD */
		writel(0, chip->eud_reg_base + EUD_REG_SW_ATTACH_DET);
		/* Delay to make sure detach pet is done before attach pet */
		ret = readl_poll_timeout(chip->eud_reg_base + EUD_REG_SW_ATTACH_DET,
					reg, (reg == 0), 1, 100);
		if (ret) {
			dev_err(chip->dev, "Detach pet failed\n");
			return;
		}

		writel(EUD_INT_PET_EUD, chip->eud_reg_base +
				EUD_REG_SW_ATTACH_DET);
	} else {
		/* Attach pet for EUD */
		writel(EUD_INT_PET_EUD, chip->eud_reg_base +
				EUD_REG_SW_ATTACH_DET);
	}
}

static irqreturn_t handle_eud_irq(int irq, void *data)
{
	struct eud_chip *chip = data;
	u32 reg;

	/* read status register and find out which interrupt triggered */
	reg = readl(chip->eud_reg_base +  EUD_REG_INT_STATUS_1);
	switch (reg & EUD_INT_ALL) {
	case EUD_INT_VBUS:
		chip->int_status = EUD_INT_VBUS;
		usb_attach_detach(chip);
		return IRQ_WAKE_THREAD;
	case EUD_INT_SAFE_MODE:
		pet_eud(chip);
		break;
	default:
		return IRQ_NONE;
	}
	return IRQ_HANDLED;
}

static irqreturn_t handle_eud_irq_thread(int irq, void *data)
{
	struct eud_chip *chip = data;
	int ret;

	if (chip->int_status == EUD_INT_VBUS) {
		if (chip->usb_attach)
			ret = usb_role_switch_set_role(chip->role_sw, USB_ROLE_DEVICE);
		else
			ret = usb_role_switch_set_role(chip->role_sw, USB_ROLE_HOST);
		if (ret)
			dev_err(chip->dev, "failed to set role switch\n");
	}

	return IRQ_HANDLED;
}

static int eud_probe(struct platform_device *pdev)
{
	struct eud_chip *chip;
	int ret;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;

	chip->role_sw = usb_role_switch_get(chip->dev);
	if (IS_ERR(chip->role_sw)) {
		if (PTR_ERR(chip->role_sw) != -EPROBE_DEFER)
			dev_err(chip->dev, "failed to get role switch\n");

		return PTR_ERR(chip->role_sw);
	}

	chip->eud_reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(chip->eud_reg_base))
		return PTR_ERR(chip->eud_reg_base);

	chip->eud_mode_mgr2_phys_base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(chip->eud_mode_mgr2_phys_base))
		return PTR_ERR(chip->eud_mode_mgr2_phys_base);

	chip->eud_irq = platform_get_irq(pdev, 0);
	ret = devm_request_threaded_irq(&pdev->dev, chip->eud_irq, handle_eud_irq,
			handle_eud_irq_thread, IRQF_ONESHOT, NULL, chip);
	if (ret)
		return ret;

	device_init_wakeup(&pdev->dev, true);
	enable_irq_wake(chip->eud_irq);

	platform_set_drvdata(pdev, chip);

	ret = device_create_file(&pdev->dev, eud_attrs[0]);

	return ret;
}

static int eud_remove(struct platform_device *pdev)
{
	struct eud_chip *chip = platform_get_drvdata(pdev);

	if (chip->enable)
		disable_eud(chip);

	device_remove_file(&pdev->dev, eud_attrs[0]);
	device_init_wakeup(&pdev->dev, false);
	disable_irq_wake(chip->eud_irq);

	return 0;
}

static const struct of_device_id eud_dt_match[] = {
	{ .compatible = "qcom,usb-connector-eud" },
	{ }
};
MODULE_DEVICE_TABLE(of, eud_dt_match);

static struct platform_driver eud_driver = {
	.probe		= eud_probe,
	.remove		= eud_remove,
	.driver		= {
		.name		= "eud",
		.of_match_table = eud_dt_match,
	},
};
module_platform_driver(eud_driver);

MODULE_DESCRIPTION("QTI EUD driver");
MODULE_LICENSE("GPL v2");
