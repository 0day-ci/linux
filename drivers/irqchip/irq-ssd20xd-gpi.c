// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Daniel Palmer <daniel@thingy.jp>
 */

#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/interrupt.h>

#define NUM_IRQ		76
#define IRQS_PER_REG	16
#define STRIDE		4

#define REG_MASK	0x0
#define REG_ACK		0x28
#define REG_TYPE	0x40
#define REG_STATUS	0xc0

struct ssd20xd_gpi {
	struct regmap *regmap;
	struct irq_domain *domain;
};

#define REG_OFFSET(_hwirq) ((hwirq >> 4) * STRIDE)
#define BIT_OFFSET(_hwirq) (1 << (hwirq & 0xf))

static void ssd20xd_gpi_mask_irq(struct irq_data *data)
{
	irq_hw_number_t hwirq = irqd_to_hwirq(data);
	struct ssd20xd_gpi *gpi = irq_data_get_irq_chip_data(data);
	int offset_reg = REG_OFFSET(hwirq);
	int offset_bit = BIT_OFFSET(hwirq);

	regmap_update_bits(gpi->regmap, REG_MASK + offset_reg, offset_bit, offset_bit);
}

static void ssd20xd_gpi_unmask_irq(struct irq_data *data)
{
	irq_hw_number_t hwirq = irqd_to_hwirq(data);
	struct ssd20xd_gpi *gpi = irq_data_get_irq_chip_data(data);
	int offset_reg = REG_OFFSET(hwirq);
	int offset_bit = BIT_OFFSET(hwirq);

	regmap_update_bits(gpi->regmap, REG_MASK + offset_reg, offset_bit, 0);
}

static void ssd20xd_gpi_irq_eoi(struct irq_data *data)
{
	struct ssd20xd_gpi *gpi = irq_data_get_irq_chip_data(data);
	irq_hw_number_t hwirq = irqd_to_hwirq(data);
	int offset_reg = REG_OFFSET(hwirq);
	int offset_bit = BIT_OFFSET(hwirq);

	regmap_update_bits_base(gpi->regmap, REG_ACK + offset_reg,
			offset_bit, offset_bit, NULL, false, true);
}

static int ssd20xd_gpi_set_type_irq(struct irq_data *data, unsigned int flow_type)
{
	irq_hw_number_t hwirq = irqd_to_hwirq(data);
	struct ssd20xd_gpi *gpi = irq_data_get_irq_chip_data(data);
	int offset_reg = REG_OFFSET(hwirq);
	int offset_bit = BIT_OFFSET(hwirq);

	switch (flow_type) {
	case IRQ_TYPE_EDGE_FALLING:
		regmap_update_bits(gpi->regmap, REG_TYPE + offset_reg, offset_bit, offset_bit);
		break;
	case IRQ_TYPE_EDGE_RISING:
		regmap_update_bits(gpi->regmap, REG_TYPE + offset_reg, offset_bit, 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static struct irq_chip ssd20xd_gpi_chip = {
	.name		= "GPI",
	.irq_mask	= ssd20xd_gpi_mask_irq,
	.irq_unmask	= ssd20xd_gpi_unmask_irq,
	.irq_eoi	= ssd20xd_gpi_irq_eoi,
	.irq_set_type	= ssd20xd_gpi_set_type_irq,
};

static int ssd20xd_gpi_domain_alloc(struct irq_domain *domain, unsigned int virq,
				    unsigned int nr_irqs, void *arg)
{
	struct ssd20xd_gpi *intc = domain->host_data;
	struct irq_fwspec *fwspec = arg;
	int i;

	for (i = 0; i < nr_irqs; i++)
		irq_domain_set_info(domain, virq + i, fwspec->param[0] + i,
				&ssd20xd_gpi_chip, intc, handle_fasteoi_irq, NULL, NULL);

	return 0;
}

static void ssd20xd_gpi_domain_free(struct irq_domain *domain, unsigned int virq,
				    unsigned int nr_irqs)
{
	int i;

	for (i = 0; i < nr_irqs; i++) {
		struct irq_data *d = irq_domain_get_irq_data(domain, virq + i);

		irq_set_handler(virq + i, NULL);
		irq_domain_reset_irq_data(d);
	}
}

static const struct irq_domain_ops ssd20xd_gpi_domain_ops = {
	.alloc = ssd20xd_gpi_domain_alloc,
	.free = ssd20xd_gpi_domain_free,
};

static const struct regmap_config ssd20xd_gpi_regmap_config = {
	.reg_bits = 16,
	.val_bits = 16,
	.reg_stride = 4,
};

static void ssd20x_gpi_chainedhandler(struct irq_desc *desc)
{
	struct ssd20xd_gpi *intc = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int irqbit, hwirq, virq, status;
	int i;

	chained_irq_enter(chip, desc);

	for (i = 0; i < NUM_IRQ / IRQS_PER_REG; i++) {
		int offset_reg = STRIDE * i;
		int offset_irq = IRQS_PER_REG * i;

		regmap_read(intc->regmap, REG_STATUS + offset_reg, &status);

		while (status) {
			irqbit = __ffs(status);
			hwirq = offset_irq + irqbit;
			virq = irq_find_mapping(intc->domain, hwirq);
			if (virq)
				generic_handle_irq(virq);
			status &= ~BIT(irqbit);
		}
	}

	chained_irq_exit(chip, desc);
}

static int __init ssd20xd_gpi_of_init(struct device_node *node,
				      struct device_node *parent)
{
	struct ssd20xd_gpi *intc;
	void __iomem *base;
	int irq, ret;

	intc = kzalloc(sizeof(*intc), GFP_KERNEL);
	if (!intc)
		return -ENOMEM;

	base = of_iomap(node, 0);
	if (!base) {
		ret = -ENODEV;
		goto out_free;
	}

	intc->regmap = regmap_init_mmio(NULL, base, &ssd20xd_gpi_regmap_config);
	if (IS_ERR(intc->regmap)) {
		ret = PTR_ERR(intc->regmap);
		goto out_unmap;
	}

	intc->domain = irq_domain_add_linear(node, NUM_IRQ, &ssd20xd_gpi_domain_ops, intc);
	if (!intc->domain) {
		ret = -ENOMEM;
		goto out_free_regmap;
	}

	irq = of_irq_get(node, 0);
	if (irq <= 0) {
		ret = irq;
		goto out_free_domain;
	}

	irq_set_chained_handler_and_data(irq, ssd20x_gpi_chainedhandler,
			intc);

	return 0;

out_free_domain:
	irq_domain_remove(intc->domain);
out_free_regmap:
	regmap_exit(intc->regmap);
out_unmap:
	iounmap(base);
out_free:
	kfree(intc);
	return ret;
}

IRQCHIP_DECLARE(ssd20xd_gpi, "sstar,ssd20xd-gpi", ssd20xd_gpi_of_init);
