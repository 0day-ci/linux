// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 */

#define pr_fmt(fmt) "aclint-swi: " fmt
#include <linux/cpu.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/smp.h>

struct aclint_swi {
	void __iomem *sip_reg;
};
static DEFINE_PER_CPU(struct aclint_swi, aclint_swis);

static void aclint_swi_send_ipi(const struct cpumask *target)
{
	unsigned int cpu;
	struct aclint_swi *swi;

	for_each_cpu(cpu, target) {
		swi = per_cpu_ptr(&aclint_swis, cpu);
		if (!swi->sip_reg) {
			pr_warn("%s: CPU%d SIP register not available\n",
				__func__, cpu);
			continue;
		}

		writel(1, swi->sip_reg);
	}
}

static void aclint_swi_clear_ipi(void)
{
	struct aclint_swi *swi = this_cpu_ptr(&aclint_swis);

	if (!swi->sip_reg) {
		pr_warn("%s: CPU%d SIP register not available\n",
			__func__, smp_processor_id());
		return;
	}

	writel(0, swi->sip_reg);
}

static struct riscv_ipi_ops aclint_swi_ipi_ops = {
	.name = "ACLINT-SWI",
	.use_for_rfence = true,
	.ipi_inject = aclint_swi_send_ipi,
	.ipi_clear = aclint_swi_clear_ipi,
};

static int __init aclint_swi_init(struct device_node *node,
				  struct device_node *parent)
{
	void __iomem *base;
	struct aclint_swi *swi;
	u32 i, nr_irqs, nr_cpus = 0;

	/* Map the registers */
	base = of_iomap(node, 0);
	if (!base) {
		pr_err("%pOFP: could not map registers\n", node);
		return -ENODEV;
	}

	/* Iterarte over each target CPU connected with this ACLINT */
	nr_irqs = of_irq_count(node);
	for (i = 0; i < nr_irqs; i++) {
		struct of_phandle_args parent;
		int cpu, hartid;

		if (of_irq_parse_one(node, i, &parent)) {
			pr_err("%pOFP: failed to parse irq %d.\n",
			       node, i);
			continue;
		}

		if (parent.args[0] != RV_IRQ_SOFT) {
			pr_err("%pOFP: invalid irq %d (hwirq %d)\n",
			       node, i, parent.args[0]);
			continue;
		}

		hartid = riscv_of_parent_hartid(parent.np);
		if (hartid < 0) {
			pr_warn("failed to parse hart ID for irq %d.\n", i);
			continue;
		}

		cpu = riscv_hartid_to_cpuid(hartid);
		if (cpu < 0) {
			pr_warn("Invalid cpuid for irq %d\n", i);
			continue;
		}

		swi = per_cpu_ptr(&aclint_swis, cpu);
		swi->sip_reg = base + i * sizeof(u32);
		nr_cpus++;
	}

	/* Announce the ACLINT SWI device */
	pr_info("%pOFP: providing IPIs for %d CPUs\n", node, nr_cpus);

	/* Register the IPI operations */
	riscv_set_ipi_ops(&aclint_swi_ipi_ops);

	return 0;
}

#ifdef CONFIG_RISCV_M_MODE
IRQCHIP_DECLARE(riscv_aclint_swi, "riscv,aclint-mswi", aclint_swi_init);
#else
IRQCHIP_DECLARE(riscv_aclint_swi, "riscv,aclint-sswi", aclint_swi_init);
#endif
