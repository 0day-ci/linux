// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/*
 * Copyright (C) Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <asm/exception.h>
#include <dt-bindings/interrupt-controller/sp7021-intc.h>

#define SP_INTC_HWIRQ_MIN     0
#define SP_INTC_HWIRQ_MAX     223

/* Interrupt G0/G1 offset */
#define INTR_REG_SIZE		(7 * 4)

#define G0_INTR_TYPE		(0)
#define G0_INTR_POLARITY	(G0_INTR_TYPE + INTR_REG_SIZE)
#define G0_INTR_PRIORITY	(G0_INTR_POLARITY + INTR_REG_SIZE)
#define G0_INTR_MASK		(G0_INTR_PRIORITY + INTR_REG_SIZE)

#define G1_INTR_CLR			(0)
#define G1_MASKED_EXT1		(G1_INTR_CLR + INTR_REG_SIZE)
#define G1_MASKED_EXT0		(G1_MASKED_EXT1 + INTR_REG_SIZE)
#define G1_INTR_GROUP		(31 * 4)
#define G1_INTR_MASK		(0x7F)
#define G1_EXT1_SHIFT		(0)
#define G1_EXT0_SHIFT		(8)

static struct sp_intctl {
	void __iomem *g0;
	void __iomem *g1;
	struct irq_domain *domain;
	struct device_node *node;
	raw_spinlock_t lock;
	int virq[2];
} sp_intc;

/* GPIO INT EDGE BUG WORKAROUND */
#define GPIO_INT0_HWIRQ			120
#define GPIO_INT7_HWIRQ			127
#define GPIO_INT_EDGE_ACTIVE	BIT(31)
#define IS_GPIO_INT(n)	(((n) >= GPIO_INT0_HWIRQ) && ((n) <= GPIO_INT7_HWIRQ))
/* array to hold which interrupt needs to workaround the bug
 * INT_TYPE_NONE: no need
 * INT_TYPE_EDGE_FALLING/INT_TYPE_EDGE_RISING: need to workaround
 * GPIO_INT_EDGE_ACTIVING: workaround is on going
 */
static u32 edge_trigger[SP_INTC_HWIRQ_MAX];

static struct irq_chip sp_intc_chip;

static void sp_intc_assign_bit(u32 hwirq, void __iomem *base, u32 value)
{
	u32 offset, mask;
	unsigned long flags;
	void __iomem *reg;

	offset = (hwirq / 32) * 4;
	reg = base + offset;

	raw_spin_lock_irqsave(&sp_intc.lock, flags);
	mask = readl_relaxed(reg);
	if (value)
		mask |= BIT(hwirq % 32);
	else
		mask &= ~BIT(hwirq % 32);
	writel_relaxed(mask, reg);
	raw_spin_unlock_irqrestore(&sp_intc.lock, flags);
}

static void sp_intc_ack_irq(struct irq_data *d)
{
	u32 hwirq = d->hwirq;

	if (edge_trigger[hwirq] != IRQ_TYPE_NONE) {
		sp_intc_assign_bit(hwirq, sp_intc.g0 + G0_INTR_POLARITY,
			(edge_trigger[hwirq] == IRQ_TYPE_EDGE_RISING));
		edge_trigger[hwirq] |= GPIO_INT_EDGE_ACTIVE;
	}

	sp_intc_assign_bit(hwirq, sp_intc.g1 + G1_INTR_CLR, 1);
}

static void sp_intc_mask_irq(struct irq_data *d)
{
	sp_intc_assign_bit(d->hwirq, sp_intc.g0 + G0_INTR_MASK, 0);
}

static void sp_intc_unmask_irq(struct irq_data *d)
{
	sp_intc_assign_bit(d->hwirq, sp_intc.g0 + G0_INTR_MASK, 1);
}

static void sp_intc_set_priority(u32 hwirq, u32 priority)
{
	sp_intc_assign_bit(hwirq, sp_intc.g0 + G0_INTR_PRIORITY, priority);
}

static int sp_intc_set_type(struct irq_data *d, unsigned int type)
{
	u32 intr_type;		/* 0: level			1: edge */
	u32 intr_polarity;	/* 0: high/rising	1: low/falling */
	u32 hwirq = d->hwirq;

	/* update the chip/handler */
	if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_chip_handler_name_locked(d, &sp_intc_chip,
						   handle_level_irq, NULL);
	else
		irq_set_chip_handler_name_locked(d, &sp_intc_chip,
						   handle_edge_irq, NULL);

	edge_trigger[hwirq] = IRQ_TYPE_NONE;

	if (type & IRQ_TYPE_LEVEL_MASK)
		intr_type = 0;
	else if (IS_GPIO_INT(hwirq)) {
		intr_type = 0;
		edge_trigger[hwirq] = type;
	} else
		intr_type = 1;

	intr_polarity = (type == IRQ_TYPE_LEVEL_LOW || type == IRQ_TYPE_EDGE_FALLING);

	sp_intc_assign_bit(hwirq, sp_intc.g0 + G0_INTR_TYPE, intr_type);
	sp_intc_assign_bit(hwirq, sp_intc.g0 + G0_INTR_POLARITY, intr_polarity);

	return IRQ_SET_MASK_OK;
}

static int sp_intc_get_ext_irq(int ext_num)
{
	u32 hwirq, mask;
	u32 i;

	i = readl_relaxed(sp_intc.g1 + G1_INTR_GROUP);
	if (ext_num)
		mask = (i >> G1_EXT1_SHIFT) & G1_INTR_MASK;
	else
		mask = (i >> G1_EXT0_SHIFT) & G1_INTR_MASK;
	if (mask) {
		i = fls(mask) - 1;
		if (ext_num)
			mask = readl_relaxed(sp_intc.g1 + G1_MASKED_EXT1 + i * 4);
		else
			mask = readl_relaxed(sp_intc.g1 + G1_MASKED_EXT0 + i * 4);
		if (mask) {
			hwirq = (i << 5) + fls(mask) - 1;
			return hwirq;
		}
	}
	return -1; /* No interrupt */
}

static void sp_intc_handle_ext_cascaded(struct irq_desc *desc)
{
	struct irq_chip *host_chip = irq_desc_get_chip(desc);
	int ext_num = (int)irq_desc_get_handler_data(desc);
	int hwirq;

	chained_irq_enter(host_chip, desc);

	while ((hwirq = sp_intc_get_ext_irq(ext_num)) >= 0) {
		if (edge_trigger[hwirq] & GPIO_INT_EDGE_ACTIVE) {
			edge_trigger[hwirq] &= ~GPIO_INT_EDGE_ACTIVE;
			sp_intc_assign_bit(hwirq, sp_intc.g0 + G0_INTR_POLARITY,
				(edge_trigger[hwirq] != IRQ_TYPE_EDGE_RISING));
		} else
			generic_handle_domain_irq(sp_intc.domain, hwirq);
	}

	chained_irq_exit(host_chip, desc);
}

static void __exception_irq_entry sp_intc_handle_irq(struct pt_regs *regs)
{
	int hwirq;

	while ((hwirq = sp_intc_get_ext_irq(0)) >= 0)
		generic_handle_domain_irq(sp_intc.domain, hwirq);
}

static void __init sp_intc_chip_init(void __iomem *base0, void __iomem *base1)
{
	int i;

	sp_intc.g0 = base0;
	sp_intc.g1 = base1;

	for (i = 0; i < 7; i++) {
		/* all mask */
		writel_relaxed(0, sp_intc.g0 + G0_INTR_MASK + i * 4);
		/* all edge */
		writel_relaxed(~0, sp_intc.g0 + G0_INTR_TYPE + i * 4);
		/* all high-active */
		writel_relaxed(0, sp_intc.g0 + G0_INTR_POLARITY + i * 4);
		/* all irq */
		writel_relaxed(~0, sp_intc.g0 + G0_INTR_PRIORITY + i * 4);
		/* all clear */
		writel_relaxed(~0, sp_intc.g1 + G1_INTR_CLR + i * 4);
	}
}

int sp_intc_xlate_of(struct irq_domain *d, struct device_node *node,
			  const u32 *intspec, unsigned int intsize,
			  irq_hw_number_t *out_hwirq, unsigned int *out_type)
{
	int ret;

	ret = irq_domain_xlate_twocell(d, node,
		intspec, intsize, out_hwirq, out_type);
	if (!ret) {
		/* intspec[1]: IRQ_TYPE | SP_INTC_EXT_INT
		 * SP_INTC_EXT_INT: 0-1,
		 *   to indicate route to which parent irq: EXT_INT0/EXT_INT1
		 */
		u32 ext_int = (intspec[1] & SP_INTC_EXT_INT_MASK) >> SP_INTC_EXT_INT_SHFIT;

		/* priority = 0, route to EXT_INT1
		 *    otherwise, route to EXT_INT0
		 */
		sp_intc_set_priority(*out_hwirq, 1 - ext_int);
	}

	return ret;
}

static struct irq_chip sp_intc_chip = {
	.name = "sp_intc",
	.irq_ack = sp_intc_ack_irq,
	.irq_mask = sp_intc_mask_irq,
	.irq_unmask = sp_intc_unmask_irq,
	.irq_set_type = sp_intc_set_type,
};

static int sp_intc_irq_domain_map(struct irq_domain *domain,
	unsigned int irq, irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &sp_intc_chip, handle_level_irq);
	irq_set_chip_data(irq, &sp_intc_chip);
	irq_set_noprobe(irq);

	return 0;
}

static const struct irq_domain_ops sp_intc_dm_ops = {
	.xlate = sp_intc_xlate_of,
	.map = sp_intc_irq_domain_map,
};

#ifdef CONFIG_OF
static int sp_intc_irq_map(struct device_node *node, int i)
{
	sp_intc.virq[i] = irq_of_parse_and_map(node, i);
	if (!sp_intc.virq[i]) {
		pr_err("%s: missed EXT_INT%d in DT\n", __func__, i);
		return -ENOENT;
	}

	pr_info("%s: EXT_INT%d = %d\n", __func__, i, sp_intc.virq[i]);
	irq_set_chained_handler_and_data(sp_intc.virq[i],
		sp_intc_handle_ext_cascaded, (void *)i);

	return 0;
}

int __init sp_intc_init_dt(
	struct device_node *node, struct device_node *parent)
{
	void __iomem *base0, *base1;

	base0 = of_iomap(node, 0);
	if (!base0) {
		pr_err("unable to map sp-intc base 0\n");
		return -EINVAL;
	}

	base1 = of_iomap(node, 1);
	if (!base1) {
		pr_err("unable to map sp-intc base 1\n");
		return -EINVAL;
	}

	sp_intc.node = node;

	sp_intc_chip_init(base0, base1);

	sp_intc.domain = irq_domain_add_linear(node,
			SP_INTC_HWIRQ_MAX - SP_INTC_HWIRQ_MIN,
			&sp_intc_dm_ops, &sp_intc);
	if (!sp_intc.domain) {
		pr_err("%s: unable to create linear domain\n", __func__);
		return -EINVAL;
	}

	raw_spin_lock_init(&sp_intc.lock);

	if (parent) {
		/* secondary chained controller */
		if (sp_intc_irq_map(node, 0)) // EXT_INT0
			return -ENOENT;

		if (sp_intc_irq_map(node, 1)) // EXT_INT1
			return -ENOENT;
	} else {
		/* primary controller */
		set_handle_irq(sp_intc_handle_irq);
	}

	return 0;
}
IRQCHIP_DECLARE(sp_intc, "sunplus,sp7021-intc", sp_intc_init_dt);
#endif

MODULE_AUTHOR("Qin Jian <qinjian@cqplus1.com>");
MODULE_DESCRIPTION("Sunplus SP7021 Interrupt Controller Driver");
MODULE_LICENSE("GPL v2");
