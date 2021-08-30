// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 */

#define pr_fmt(fmt) "aclint-swi: " fmt
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/smp.h>

struct aclint_swi {
	void __iomem *sip_reg;
	unsigned long bits;
};

static int aclint_swi_parent_irq __ro_after_init;
static struct irq_domain *aclint_swi_domain __ro_after_init;
static DEFINE_PER_CPU(struct aclint_swi, aclint_swis);

static void aclint_swi_dummy(struct irq_data *d)
{
}

static void aclint_swi_send_mask(struct irq_data *d,
				  const struct cpumask *mask)
{
	int cpu;
	struct aclint_swi *swi;

	/* Barrier before doing atomic bit update to IPI bits */
	smp_mb__before_atomic();

	for_each_cpu(cpu, mask) {
		swi = per_cpu_ptr(&aclint_swis, cpu);
		set_bit(d->hwirq, &swi->bits);
		writel(1, swi->sip_reg);
	}

	/* Barrier after doing atomic bit update to IPI bits */
	smp_mb__after_atomic();
}

static struct irq_chip aclint_swi_chip = {
	.name = "RISC-V ACLINT SWI",
	.irq_mask	= aclint_swi_dummy,
	.irq_unmask	= aclint_swi_dummy,
	.ipi_send_mask	= aclint_swi_send_mask,
};

static int aclint_swi_domain_map(struct irq_domain *d, unsigned int irq,
				 irq_hw_number_t hwirq)
{
	irq_set_percpu_devid(irq);
	irq_domain_set_info(d, irq, hwirq, &aclint_swi_chip, d->host_data,
			    handle_percpu_devid_irq, NULL, NULL);

	return 0;
}

static int aclint_swi_domain_alloc(struct irq_domain *d, unsigned int virq,
				   unsigned int nr_irqs, void *arg)
{
	int i, ret;
	irq_hw_number_t hwirq;
	unsigned int type = IRQ_TYPE_NONE;
	struct irq_fwspec *fwspec = arg;

	ret = irq_domain_translate_onecell(d, fwspec, &hwirq, &type);
	if (ret)
		return ret;

	for (i = 0; i < nr_irqs; i++) {
		ret = aclint_swi_domain_map(d, virq + i, hwirq + i);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct irq_domain_ops aclint_swi_domain_ops = {
	.translate	= irq_domain_translate_onecell,
	.alloc		= aclint_swi_domain_alloc,
	.free		= irq_domain_free_irqs_top,
};

static void aclint_swi_handle_irq(struct irq_desc *desc)
{
	int err;
	unsigned long irqs;
	irq_hw_number_t hwirq;
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct aclint_swi *swi = this_cpu_ptr(&aclint_swis);

	chained_irq_enter(chip, desc);

	while (true) {
#ifdef CONFIG_RISCV_M_MODE
		writel(0, swi->sip_reg);
#else
		csr_clear(CSR_IP, IE_SIE);
#endif

		/* Order bit clearing and data access. */
		mb();

		irqs = xchg(&swi->bits, 0);
		if (!irqs)
			goto done;

		for_each_set_bit(hwirq, &irqs, BITS_PER_LONG) {
			err = generic_handle_domain_irq(aclint_swi_domain,
							hwirq);
			if (unlikely(err))
				pr_warn_ratelimited(
					"can't find mapping for hwirq %lu\n",
					hwirq);
		}
	}

done:
	chained_irq_exit(chip, desc);
}

static int aclint_swi_dying_cpu(unsigned int cpu)
{
	disable_percpu_irq(aclint_swi_parent_irq);
	return 0;
}

static int aclint_swi_starting_cpu(unsigned int cpu)
{
	enable_percpu_irq(aclint_swi_parent_irq,
			  irq_get_trigger_type(aclint_swi_parent_irq));
	return 0;
}

static int __init aclint_swi_domain_init(struct device_node *node)
{
	int virq;
	struct irq_fwspec ipi;

	/*
	 * We can have multiple ACLINT SWI devices but we only need
	 * one IRQ domain for providing per-HART (or per-CPU) IPIs.
	 */
	if (aclint_swi_domain)
		return 0;

	aclint_swi_domain = irq_domain_add_linear(node, BITS_PER_LONG,
						&aclint_swi_domain_ops, NULL);
	if (!aclint_swi_domain) {
		pr_err("unable to add ACLINT SWI IRQ domain\n");
		return -ENOMEM;
	}

	ipi.fwnode		= aclint_swi_domain->fwnode;
	ipi.param_count	= 1;
	ipi.param[0]		= 0;
	virq = __irq_domain_alloc_irqs(aclint_swi_domain, -1, BITS_PER_LONG,
				       NUMA_NO_NODE, &ipi,
				       false, NULL);
	if (virq <= 0) {
		pr_err("unable to alloc IRQs from ACLINT SWI IRQ domain\n");
		return -ENOMEM;
	}

	riscv_ipi_set_virq_range(virq, BITS_PER_LONG, true);

	return 0;
}

static int __init aclint_swi_init(struct device_node *node,
				  struct device_node *parent)
{
	int rc;
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

		/* Find parent domain and register chained handler */
		if (!aclint_swi_parent_irq && irq_find_host(parent.np)) {
			aclint_swi_parent_irq = irq_of_parse_and_map(node, i);
			if (aclint_swi_parent_irq) {
				irq_set_chained_handler(aclint_swi_parent_irq,
							aclint_swi_handle_irq);
				cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
					"irqchip/riscv/aclint-swi:starting",
					aclint_swi_starting_cpu,
					aclint_swi_dying_cpu);
			}
		}

		swi = per_cpu_ptr(&aclint_swis, cpu);
		swi->sip_reg = base + i * sizeof(u32);
		writel(0, swi->sip_reg);

		nr_cpus++;
	}

	/* Create the IPI domain for ACLINT SWI device */
	rc = aclint_swi_domain_init(node);
	if (rc)
		return rc;

	/* Announce the ACLINT SWI device */
	pr_info("%pOFP: providing IPIs for %d CPUs\n", node, nr_cpus);

	return 0;
}

#ifdef CONFIG_RISCV_M_MODE
IRQCHIP_DECLARE(riscv_aclint_swi, "riscv,clint0", aclint_swi_init);
IRQCHIP_DECLARE(riscv_aclint_swi1, "sifive,clint0", aclint_swi_init);
IRQCHIP_DECLARE(riscv_aclint_swi2, "riscv,aclint-mswi", aclint_swi_init);
#else
IRQCHIP_DECLARE(riscv_aclint_swi, "riscv,aclint-sswi", aclint_swi_init);
#endif
