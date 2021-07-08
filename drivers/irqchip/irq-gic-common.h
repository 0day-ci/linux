/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2002 ARM Limited, All Rights Reserved.
 */

#ifndef _IRQ_GIC_COMMON_H
#define _IRQ_GIC_COMMON_H

#include <linux/of.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/arm-gic-common.h>

struct gic_quirk {
	const char *desc;
	const char *compatible;
	bool (*init)(void *data);
	u32 iidr;
	u32 mask;
};

int gic_configure_irq(unsigned int irq, unsigned int type,
                       void __iomem *base, void (*sync_access)(void));
void gic_dist_config(void __iomem *base, int gic_irqs,
		     void (*sync_access)(void));
void gic_cpu_config(void __iomem *base, int nr, void (*sync_access)(void));
void gic_enable_quirks(u32 iidr, const struct gic_quirk *quirks,
		void *data);
void gic_enable_of_quirks(const struct device_node *np,
			  const struct gic_quirk *quirks, void *data);

void gic_set_kvm_info(const struct gic_kvm_info *info);

/* LPI related functionality */
/*
 * TODO: Ideally, I think these should be moved to a different file
 *	 such as irq-gic-v3-lpi-common.h/.c. But, keeping it here
 *	 for now to get comments from the RFC.
 */
DECLARE_PER_CPU(struct cpu_lpi_count, cpu_lpi_count);

__maybe_unused u32 its_read_lpi_count(struct irq_data *d, int cpu);
void its_inc_lpi_count(struct irq_data *d, int cpu);
void its_dec_lpi_count(struct irq_data *d, int cpu);
unsigned int cpumask_pick_least_loaded(struct irq_data *d,
				       const struct cpumask *cpu_mask);
int its_irq_gic_domain_alloc(struct irq_domain *domain,
			     unsigned int virq,
			     irq_hw_number_t hwirq);
unsigned long *its_lpi_alloc(int nr_irqs, u32 *base, int *nr_ids);
void its_lpi_free(unsigned long *bitmap, u32 base, u32 nr_ids);

struct rdists;
int direct_lpi_init(struct irq_domain *parent, struct rdists *rdists);

#endif /* _IRQ_GIC_COMMON_H */
