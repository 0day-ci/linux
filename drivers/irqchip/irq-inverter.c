// SPDX-License-Identifier: GPL-2.0

#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/module.h>
#include <linux/platform_device.h>

struct irq_inverter {
	int parent_irq;
	int child_irq;
	unsigned int inverted_type;
};

static irqreturn_t irq_inverter_parent_irq(int irq, void *data)
{
	struct irq_inverter *inv = data;
	unsigned long flags;

	raw_local_irq_save(flags);
	generic_handle_irq(inv->child_irq);
	raw_local_irq_restore(flags);

	return IRQ_HANDLED;
}

static void irq_inverter_enable(struct irq_data *data)
{
	struct irq_inverter *inv = data->chip_data;

	enable_irq(inv->parent_irq);
}

static void irq_inverter_disable(struct irq_data *data)
{
	struct irq_inverter *inv = data->chip_data;

	disable_irq_nosync(inv->parent_irq);
}

static int irq_inverter_set_type(struct irq_data *data, unsigned int type)
{
	struct irq_inverter *inv = data->chip_data;

	return type == inv->inverted_type ? 0 : -EINVAL;
}

static struct irq_chip irq_inverter_chip = {
	.name = KBUILD_MODNAME,
	.irq_enable = irq_inverter_enable,
	.irq_disable = irq_inverter_disable,
	.irq_set_type = irq_inverter_set_type,
};

static int irq_inverter_xlate(struct irq_domain *d, struct device_node *node,
		const u32 *intspec, unsigned int intsize,
		unsigned long *out_hwirq, unsigned int *out_type)
{
	struct irq_inverter *inv = d->host_data;

	if (intsize != 0)
		return -EINVAL;

	*out_hwirq = 0;
	*out_type = inv->inverted_type;
	return 0;
}

static int irq_inverter_map(struct irq_domain *d, unsigned int virq,
		irq_hw_number_t hw)
{
	struct irq_inverter *inv = d->host_data;

	inv->child_irq = virq;
	irq_set_chip_and_handler(virq, &irq_inverter_chip, handle_simple_irq);
	irq_set_chip_data(virq, inv);
	return 0;
}

static const struct irq_domain_ops irq_inverter_domain_ops = {
	.xlate = irq_inverter_xlate,
	.map = irq_inverter_map,
};

static int irq_inverter_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct irq_inverter *inv;
	struct irq_domain *domain;
	unsigned int parent_type;
	int ret;

	inv = kzalloc(sizeof(*inv), GFP_KERNEL);
	if (!inv)
		return -ENOMEM;

	ret = of_irq_get(node, 0);
	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "could not get parent irq\n");
		goto err_free_inv;
	}
	inv->parent_irq = ret;

	parent_type = irq_get_trigger_type(inv->parent_irq);
	if (!parent_type) {
		dev_err(&pdev->dev, "parent irq trigger type is not defined\n");
		ret = -EINVAL;
		goto err_free_inv;
	}
	if (parent_type & IRQ_TYPE_EDGE_RISING)
		inv->inverted_type |= IRQ_TYPE_EDGE_FALLING;
	if (parent_type & IRQ_TYPE_EDGE_FALLING)
		inv->inverted_type |= IRQ_TYPE_EDGE_RISING;
	if (parent_type & IRQ_TYPE_LEVEL_HIGH)
		inv->inverted_type |= IRQ_TYPE_LEVEL_LOW;
	if (parent_type & IRQ_TYPE_LEVEL_LOW)
		inv->inverted_type |= IRQ_TYPE_LEVEL_HIGH;

	ret = request_irq(inv->parent_irq, irq_inverter_parent_irq,
			parent_type | IRQF_NO_AUTOEN, KBUILD_MODNAME, inv);
	if (ret < 0) {
		dev_err(&pdev->dev, "could not request parent irq\n");
		goto err_free_inv;
	}

	domain = irq_domain_add_linear(node, 1, &irq_inverter_domain_ops, inv);
	if (!domain) {
		ret = -ENOMEM;
		goto err_free_irq;
	}

	return 0;

err_free_irq:
	free_irq(inv->parent_irq, inv);
err_free_inv:
	kfree(inv);
	return ret;
}

static const struct of_device_id irq_inverter_match[] = {
	{ .compatible = "linux,irq-inverter" },
	{}
};

static struct platform_driver irq_inverter_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = irq_inverter_match,
	},
	.probe = irq_inverter_probe,
};

static int __init irq_inverter_init(void)
{
	return platform_driver_register(&irq_inverter_driver);
}

subsys_initcall(irq_inverter_init);
