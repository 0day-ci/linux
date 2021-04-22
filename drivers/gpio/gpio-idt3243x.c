// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for IDT/Renesas 79RC3243x Interrupt Controller.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#define IDT_PIC_IRQ_PEND	0x00
#define IDT_PIC_IRQ_MASK	0x08

#define IDT_GPIO_DIR		0x00
#define IDT_GPIO_DATA		0x04
#define IDT_GPIO_ILEVEL		0x08
#define IDT_GPIO_ISTAT		0x0C

struct idt_gpio_ctrl {
	struct gpio_chip gc;
	void __iomem *pic;
	void __iomem *gpio;
	u32 mask_cache;
};

static void idt_gpio_dispatch(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct idt_gpio_ctrl *ctrl = gpiochip_get_data(gc);
	struct irq_chip *host_chip = irq_desc_get_chip(desc);
	unsigned int bit, virq;
	unsigned long pending;

	chained_irq_enter(host_chip, desc);

	pending = readl(ctrl->pic + IDT_PIC_IRQ_PEND);
	pending &= ~ctrl->mask_cache;
	for_each_set_bit(bit, &pending, gc->ngpio) {
		virq = irq_linear_revmap(gc->irq.domain, bit);
		if (virq)
			generic_handle_irq(virq);
	}

	chained_irq_exit(host_chip, desc);
}

static int idt_gpio_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct idt_gpio_ctrl *ctrl = gpiochip_get_data(gc);
	unsigned int sense = flow_type & IRQ_TYPE_SENSE_MASK;
	u32 ilevel;

	if (sense & ~(IRQ_TYPE_LEVEL_HIGH | IRQ_TYPE_LEVEL_LOW))
		return -EINVAL;

	ilevel = readl(ctrl->gpio + IDT_GPIO_ILEVEL);
	if (sense & IRQ_TYPE_LEVEL_HIGH)
		ilevel |= BIT(d->hwirq);
	else if (sense & IRQ_TYPE_LEVEL_LOW)
		ilevel &= ~BIT(d->hwirq);
	else
		return -EINVAL;

	writel(ilevel, ctrl->gpio + IDT_GPIO_ILEVEL);
	return 0;
}

static void idt_gpio_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct idt_gpio_ctrl *ctrl = gpiochip_get_data(gc);

	writel(~BIT(d->hwirq), ctrl->gpio + IDT_GPIO_ISTAT);
}

static void idt_gpio_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct idt_gpio_ctrl *ctrl = gpiochip_get_data(gc);

	ctrl->mask_cache |= BIT(d->hwirq);
	writel(ctrl->mask_cache, ctrl->pic + IDT_PIC_IRQ_MASK);
}

static void idt_gpio_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct idt_gpio_ctrl *ctrl = gpiochip_get_data(gc);

	ctrl->mask_cache &= ~BIT(d->hwirq);
	writel(ctrl->mask_cache, ctrl->pic + IDT_PIC_IRQ_MASK);
}

static struct irq_chip idt_gpio_irqchip = {
	.name = "IDTGPIO",
	.irq_mask = idt_gpio_mask,
	.irq_ack = idt_gpio_ack,
	.irq_unmask = idt_gpio_unmask,
	.irq_set_type = idt_gpio_irq_set_type
};

static int idt_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct gpio_irq_chip *girq;
	struct idt_gpio_ctrl *ctrl;
	unsigned int parent_irq;
	int ngpios;
	int ret;

	ret = device_property_read_u32(dev, "ngpios", &ngpios);
	if (ret) {
		dev_err(dev, "ngpios property is not valid\n");
		return ret;
	}

	ctrl = devm_kzalloc(dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	ctrl->gpio = devm_platform_ioremap_resource_byname(pdev, "gpio");
	if (!ctrl->gpio)
		return -ENOMEM;

	ctrl->gc.parent = dev;

	ret = bgpio_init(&ctrl->gc, &pdev->dev, 4, ctrl->gpio + IDT_GPIO_DATA,
			 NULL, NULL, ctrl->gpio + IDT_GPIO_DIR, NULL, 0);
	if (ret) {
		dev_err(dev, "bgpio_init failed\n");
		return ret;
	}
	ctrl->gc.ngpio = ngpios;

	ctrl->pic = devm_platform_ioremap_resource_byname(pdev, "pic");
	if (!ctrl->pic)
		return -ENOMEM;

	parent_irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (!parent_irq) {
		dev_err(&pdev->dev, "Failed to map parent IRQ!\n");
		return -EINVAL;
	}

	/* Mask interrupts. */
	ctrl->mask_cache = 0xffffffff;
	writel(ctrl->mask_cache, ctrl->pic + IDT_PIC_IRQ_MASK);

	girq = &ctrl->gc.irq;
	girq->chip = &idt_gpio_irqchip;
	girq->parent_handler = idt_gpio_dispatch;
	girq->num_parents = 1;
	girq->parents = devm_kcalloc(dev, 1, sizeof(*girq->parents),
				     GFP_KERNEL);
	if (!girq->parents) {
		ret = -ENOMEM;
		goto out_unmap_irq;
	}
	girq->parents[0] = parent_irq;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_level_irq;

	ret = devm_gpiochip_add_data(&pdev->dev, &ctrl->gc, ctrl);
	if (ret)
		goto out_unmap_irq;

	return 0;

out_unmap_irq:
	irq_dispose_mapping(parent_irq);
	return ret;
}

static const struct of_device_id idt_gpio_of_match[] = {
	{ .compatible = "idt,32434-gpio" },
	{ }
};
MODULE_DEVICE_TABLE(of, idt_gpio_of_match);

static struct platform_driver idt_gpio_driver = {
	.probe = idt_gpio_probe,
	.driver = {
		.name = "idt3243x-gpio",
		.of_match_table = idt_gpio_of_match,
	},
};
module_platform_driver(idt_gpio_driver);

MODULE_DESCRIPTION("IDT 79RC3243x GPIO/PIC Driver");
MODULE_AUTHOR("Thomas Bogendoerfer <tsbogend@alpha.franken.de>");
MODULE_LICENSE("GPL");
