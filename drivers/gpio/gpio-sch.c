// SPDX-License-Identifier: GPL-2.0
/*
 * GPIO interface for Intel Poulsbo SCH
 *
 *  Copyright (c) 2010 CompuLab Ltd
 *  Author: Denis Turischev <denis@compulab.co.il>
 */

#include <linux/acpi.h>
#include <linux/errno.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci_ids.h>
#include <linux/platform_device.h>

#define GEN	0x00
#define GIO	0x04
#define GLV	0x08
#define GTPE	0x0c
#define GTNE	0x10
#define GGPE	0x14
#define GTS	0x1c

struct sch_gpio {
	struct gpio_chip chip;
	struct irq_chip irqchip;
	acpi_sci_handler sci_handler;
	spinlock_t lock;
	unsigned short iobase;
	unsigned short resume_base;
};

static unsigned int sch_gpio_offset(struct sch_gpio *sch, unsigned int gpio,
				unsigned int reg)
{
	unsigned int base = 0;

	if (gpio >= sch->resume_base) {
		gpio -= sch->resume_base;
		base += 0x20;
	}

	return base + reg + gpio / 8;
}

static unsigned int sch_gpio_bit(struct sch_gpio *sch, unsigned int gpio)
{
	if (gpio >= sch->resume_base)
		gpio -= sch->resume_base;
	return gpio % 8;
}

static int sch_gpio_reg_get(struct sch_gpio *sch, unsigned int gpio, unsigned int reg)
{
	unsigned short offset, bit;
	u8 reg_val;

	offset = sch_gpio_offset(sch, gpio, reg);
	bit = sch_gpio_bit(sch, gpio);

	reg_val = !!(inb(sch->iobase + offset) & BIT(bit));

	return reg_val;
}

static void sch_gpio_reg_set(struct sch_gpio *sch, unsigned int gpio, unsigned int reg,
			     int val)
{
	unsigned short offset, bit;
	u8 reg_val;

	offset = sch_gpio_offset(sch, gpio, reg);
	bit = sch_gpio_bit(sch, gpio);

	reg_val = inb(sch->iobase + offset);

	if (val)
		outb(reg_val | BIT(bit), sch->iobase + offset);
	else
		outb((reg_val & ~BIT(bit)), sch->iobase + offset);
}

static int sch_gpio_direction_in(struct gpio_chip *gc, unsigned int gpio_num)
{
	struct sch_gpio *sch = gpiochip_get_data(gc);
	unsigned long flags;

	spin_lock_irqsave(&sch->lock, flags);
	sch_gpio_reg_set(sch, gpio_num, GIO, 1);
	spin_unlock_irqrestore(&sch->lock, flags);
	return 0;
}

static int sch_gpio_get(struct gpio_chip *gc, unsigned int gpio_num)
{
	struct sch_gpio *sch = gpiochip_get_data(gc);

	return sch_gpio_reg_get(sch, gpio_num, GLV);
}

static void sch_gpio_set(struct gpio_chip *gc, unsigned int gpio_num, int val)
{
	struct sch_gpio *sch = gpiochip_get_data(gc);
	unsigned long flags;

	spin_lock_irqsave(&sch->lock, flags);
	sch_gpio_reg_set(sch, gpio_num, GLV, val);
	spin_unlock_irqrestore(&sch->lock, flags);
}

static int sch_gpio_direction_out(struct gpio_chip *gc, unsigned int gpio_num,
				  int val)
{
	struct sch_gpio *sch = gpiochip_get_data(gc);
	unsigned long flags;

	spin_lock_irqsave(&sch->lock, flags);
	sch_gpio_reg_set(sch, gpio_num, GIO, 0);
	spin_unlock_irqrestore(&sch->lock, flags);

	/*
	 * according to the datasheet, writing to the level register has no
	 * effect when GPIO is programmed as input.
	 * Actually the the level register is read-only when configured as input.
	 * Thus presetting the output level before switching to output is _NOT_ possible.
	 * Hence we set the level after configuring the GPIO as output.
	 * But we cannot prevent a short low pulse if direction is set to high
	 * and an external pull-up is connected.
	 */
	sch_gpio_set(gc, gpio_num, val);
	return 0;
}

static int sch_gpio_get_direction(struct gpio_chip *gc, unsigned int gpio_num)
{
	struct sch_gpio *sch = gpiochip_get_data(gc);

	if (sch_gpio_reg_get(sch, gpio_num, GIO))
		return GPIO_LINE_DIRECTION_IN;

	return GPIO_LINE_DIRECTION_OUT;
}

static const struct gpio_chip sch_gpio_chip = {
	.label			= "sch_gpio",
	.owner			= THIS_MODULE,
	.direction_input	= sch_gpio_direction_in,
	.get			= sch_gpio_get,
	.direction_output	= sch_gpio_direction_out,
	.set			= sch_gpio_set,
	.get_direction		= sch_gpio_get_direction,
};

static u32 sch_gpio_sci_handler(void *context)
{
	struct sch_gpio *sch = context;
	struct gpio_chip *gc = &sch->chip;
	unsigned long core_status, resume_status;
	unsigned long pending;
	int offset;

	core_status = inl(sch->iobase + GTS + 0x00);
	resume_status = inl(sch->iobase + GTS + 0x20);

	pending = (resume_status << sch->resume_base) | core_status;

	for_each_set_bit(offset, &pending, sch->chip.ngpio)
		generic_handle_irq(irq_find_mapping(gc->irq.domain, offset));

	outl(core_status, sch->iobase + GTS + 0x00);
	outl(resume_status, sch->iobase + GTS + 0x20);

	return pending ? ACPI_INTERRUPT_HANDLED : ACPI_INTERRUPT_NOT_HANDLED;
}

static int sch_irq_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct sch_gpio *sch = gpiochip_get_data(gc);
	irq_hw_number_t gpio_num = irqd_to_hwirq(d);
	unsigned long flags;
	int rising, falling;

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_RISING:
		rising = 1;
		falling = 0;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		rising = 0;
		falling = 1;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		rising = 1;
		falling = 1;
		break;
	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&sch->lock, flags);

	sch_gpio_reg_set(sch, gpio_num, GTPE, rising);
	sch_gpio_reg_set(sch, gpio_num, GTNE, falling);

	irq_set_handler_locked(d, handle_edge_irq);

	spin_unlock_irqrestore(&sch->lock, flags);

	return 0;
}

static void sch_irq_set_enable(struct irq_data *d, int val)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct sch_gpio *sch = gpiochip_get_data(gc);
	irq_hw_number_t gpio_num = irqd_to_hwirq(d);
	unsigned long flags;

	spin_lock_irqsave(&sch->lock, flags);
	sch_gpio_reg_set(sch, gpio_num, GGPE, val);
	spin_unlock_irqrestore(&sch->lock, flags);
}

static void sch_irq_mask(struct irq_data *d)
{
	sch_irq_set_enable(d, 0);
}

static void sch_irq_unmask(struct irq_data *d)
{
	sch_irq_set_enable(d, 1);
}

static void sch_gpio_remove_sci_handler(void *data)
{
	struct sch_gpio *sch = data;
	struct device *dev = sch->chip.parent;
	acpi_status status;

	status = acpi_remove_sci_handler(sch->sci_handler);
	if (ACPI_FAILURE(status))
		dev_err(dev, "Can't remove SCI handler\n");
}

static int sch_gpio_install_sci_handler(struct sch_gpio *sch)
{
	struct device *dev = sch->chip.parent;
	acpi_status status;

	status = acpi_install_sci_handler(sch->sci_handler, sch);
	if (ACPI_SUCCESS(status))
		return devm_add_action_or_reset(dev, sch_gpio_remove_sci_handler, sch);

	/* SCI handler is optional */
	dev_warn(dev, "Can't install SCI handler, no IRQ support\n");
	return 0;
}

static int sch_gpio_probe(struct platform_device *pdev)
{
	struct sch_gpio *sch;
	struct resource *res;
	int ret;

	sch = devm_kzalloc(&pdev->dev, sizeof(*sch), GFP_KERNEL);
	if (!sch)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!res)
		return -EBUSY;

	if (!devm_request_region(&pdev->dev, res->start, resource_size(res),
				 pdev->name))
		return -EBUSY;

	spin_lock_init(&sch->lock);
	sch->iobase = res->start;
	sch->chip = sch_gpio_chip;
	sch->chip.label = dev_name(&pdev->dev);
	sch->chip.parent = &pdev->dev;

	switch (pdev->id) {
	case PCI_DEVICE_ID_INTEL_SCH_LPC:
		sch->resume_base = 10;
		sch->chip.ngpio = 14;

		/*
		 * GPIO[6:0] enabled by default
		 * GPIO7 is configured by the CMC as SLPIOVR
		 * Enable GPIO[9:8] core powered gpios explicitly
		 */
		sch_gpio_reg_set(sch, 8, GEN, 1);
		sch_gpio_reg_set(sch, 9, GEN, 1);
		/*
		 * SUS_GPIO[2:0] enabled by default
		 * Enable SUS_GPIO3 resume powered gpio explicitly
		 */
		sch_gpio_reg_set(sch, 13, GEN, 1);
		break;

	case PCI_DEVICE_ID_INTEL_ITC_LPC:
		sch->resume_base = 5;
		sch->chip.ngpio = 14;
		break;

	case PCI_DEVICE_ID_INTEL_CENTERTON_ILB:
		sch->resume_base = 21;
		sch->chip.ngpio = 30;
		break;

	case PCI_DEVICE_ID_INTEL_QUARK_X1000_ILB:
		sch->resume_base = 2;
		sch->chip.ngpio = 8;
		break;

	default:
		return -ENODEV;
	}

	platform_set_drvdata(pdev, sch);

	sch->irqchip.name = "sch_gpio";
	sch->irqchip.irq_mask = sch_irq_mask;
	sch->irqchip.irq_unmask = sch_irq_unmask;
	sch->irqchip.irq_set_type = sch_irq_type;

	sch->chip.irq.chip = &sch->irqchip;
	sch->chip.irq.num_parents = 0;
	sch->chip.irq.parents = NULL;
	sch->chip.irq.parent_handler = NULL;
	sch->chip.irq.default_type = IRQ_TYPE_NONE;
	sch->chip.irq.handler = handle_bad_irq;

	sch->sci_handler = sch_gpio_sci_handler;

	ret = sch_gpio_install_sci_handler(sch);
	if (ret)
		return ret;

	return devm_gpiochip_add_data(&pdev->dev, &sch->chip, sch);
}

static struct platform_driver sch_gpio_driver = {
	.driver = {
		.name = "sch_gpio",
	},
	.probe		= sch_gpio_probe,
};

module_platform_driver(sch_gpio_driver);

MODULE_AUTHOR("Denis Turischev <denis@compulab.co.il>");
MODULE_DESCRIPTION("GPIO interface for Intel Poulsbo SCH");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:sch_gpio");
