// SPDX-License-Identifier: GPL-2.0-only
/*
 * Multiplex several IPIs over a single HW IPI.
 *
 * Copyright (c) 2022 Ventana Micro Systems Inc.
 */

#define pr_fmt(fmt) "riscv-ipi-mux: " fmt
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/smp.h>
#include <asm/ipi-mux.h>

struct ipi_mux {
	struct irq_domain *domain;
	int parent_virq;
	void (*clear_ipi)(void);
	void (*send_ipi)(const struct cpumask *mask);
};

static struct ipi_mux ipi_mux_priv;
static DEFINE_PER_CPU(unsigned long, ipi_mux_bits);

static void ipi_mux_dummy(struct irq_data *d)
{
}

static void ipi_mux_send_mask(struct irq_data *d, const struct cpumask *mask)
{
	int cpu;

	/* Barrier before doing atomic bit update to IPI bits */
	smp_mb__before_atomic();

	for_each_cpu(cpu, mask)
		set_bit(d->hwirq, per_cpu_ptr(&ipi_mux_bits, cpu));

	/* Barrier after doing atomic bit update to IPI bits */
	smp_mb__after_atomic();

	if (ipi_mux_priv.send_ipi)
		ipi_mux_priv.send_ipi(mask);
}

static struct irq_chip ipi_mux_chip = {
	.name		= "RISC-V IPI Mux",
	.irq_mask	= ipi_mux_dummy,
	.irq_unmask	= ipi_mux_dummy,
	.ipi_send_mask	= ipi_mux_send_mask,
};

static int ipi_mux_domain_map(struct irq_domain *d, unsigned int irq,
			      irq_hw_number_t hwirq)
{
	irq_set_percpu_devid(irq);
	irq_domain_set_info(d, irq, hwirq, &ipi_mux_chip, d->host_data,
			    handle_percpu_devid_irq, NULL, NULL);

	return 0;
}

static int ipi_mux_domain_alloc(struct irq_domain *d, unsigned int virq,
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
		ret = ipi_mux_domain_map(d, virq + i, hwirq + i);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct irq_domain_ops ipi_mux_domain_ops = {
	.translate	= irq_domain_translate_onecell,
	.alloc		= ipi_mux_domain_alloc,
	.free		= irq_domain_free_irqs_top,
};

void riscv_ipi_mux_handle_irq(void)
{
	int err;
	unsigned long irqs, *bits = this_cpu_ptr(&ipi_mux_bits);
	irq_hw_number_t hwirq;

	while (true) {
		if (ipi_mux_priv.clear_ipi)
			ipi_mux_priv.clear_ipi();

		/* Order bit clearing and data access. */
		mb();

		irqs = xchg(bits, 0);
		if (!irqs)
			break;

		for_each_set_bit(hwirq, &irqs, BITS_PER_LONG) {
			err = generic_handle_domain_irq(ipi_mux_priv.domain,
							hwirq);
			if (unlikely(err))
				pr_warn_ratelimited(
					"can't find mapping for hwirq %lu\n",
					hwirq);
		}
	}
}

static void ipi_mux_handle_irq(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);

	chained_irq_enter(chip, desc);
	riscv_ipi_mux_handle_irq();
	chained_irq_exit(chip, desc);
}

static int ipi_mux_dying_cpu(unsigned int cpu)
{
	if (ipi_mux_priv.parent_virq)
		disable_percpu_irq(ipi_mux_priv.parent_virq);
	return 0;
}

static int ipi_mux_starting_cpu(unsigned int cpu)
{
	if (ipi_mux_priv.parent_virq)
		enable_percpu_irq(ipi_mux_priv.parent_virq,
			irq_get_trigger_type(ipi_mux_priv.parent_virq));
	return 0;
}

struct irq_domain *riscv_ipi_mux_create(bool use_soft_irq,
			bool use_for_rfence,
			void (*clear_ipi)(void),
			void (*send_ipi)(const struct cpumask *mask))
{
	int virq, parent_virq = 0;
	struct irq_domain *domain;
	struct irq_fwspec ipi;

	if (ipi_mux_priv.domain || riscv_ipi_have_virq_range())
		return NULL;

	if (use_soft_irq) {
		domain = irq_find_matching_fwnode(riscv_intc_fwnode(),
						  DOMAIN_BUS_ANY);
		if (!domain) {
			pr_err("unable to find INTC IRQ domain\n");
			return NULL;
		}

		parent_virq = irq_create_mapping(domain, RV_IRQ_SOFT);
		if (!parent_virq) {
			pr_err("unable to create INTC IRQ mapping\n");
			return NULL;
		}
	}

	domain = irq_domain_add_linear(NULL, BITS_PER_LONG,
				       &ipi_mux_domain_ops, NULL);
	if (!domain) {
		pr_err("unable to add IPI Mux domain\n");
		goto fail_dispose_mapping;
	}

	ipi.fwnode = domain->fwnode;
	ipi.param_count = 1;
	ipi.param[0] = 0;
	virq = __irq_domain_alloc_irqs(domain, -1, BITS_PER_LONG,
				       NUMA_NO_NODE, &ipi, false, NULL);
	if (virq <= 0) {
		pr_err("unable to alloc IRQs from IPI Mux domain\n");
		goto fail_domain_remove;
	}

	ipi_mux_priv.domain = domain;
	ipi_mux_priv.parent_virq = parent_virq;
	ipi_mux_priv.clear_ipi = clear_ipi;
	ipi_mux_priv.send_ipi = send_ipi;

	if (parent_virq)
		irq_set_chained_handler(parent_virq, ipi_mux_handle_irq);

	cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
			  "irqchip/riscv/ipi-mux:starting",
			  ipi_mux_starting_cpu, ipi_mux_dying_cpu);

	riscv_ipi_set_virq_range(virq, BITS_PER_LONG, use_for_rfence);

	return ipi_mux_priv.domain;

fail_domain_remove:
	irq_domain_remove(domain);
fail_dispose_mapping:
	if (parent_virq)
		irq_dispose_mapping(parent_virq);
	return NULL;
}

void riscv_ipi_mux_destroy(struct irq_domain *d)
{
	if (!d || ipi_mux_priv.domain != d)
		return;

	irq_domain_remove(ipi_mux_priv.domain);
	if (ipi_mux_priv.parent_virq)
		irq_dispose_mapping(ipi_mux_priv.parent_virq);
	memset(&ipi_mux_priv, 0, sizeof(ipi_mux_priv));
}
