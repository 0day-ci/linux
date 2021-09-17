// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */
#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/kallsyms.h>
#include <linux/uaccess.h>

#include <asm/irq.h>
#include <asm/loongson.h>
#include <asm/setup.h>

DEFINE_PER_CPU(unsigned long, irq_stack);

struct acpi_madt_lio_pic *acpi_liointc;
struct acpi_madt_eio_pic *acpi_eiointc[MAX_IO_PICS];

struct acpi_madt_ht_pic *acpi_htintc;
struct acpi_madt_lpc_pic *acpi_pchlpc;
struct acpi_madt_msi_pic *acpi_pchmsi[MAX_IO_PICS];
struct acpi_madt_bio_pic *acpi_pchpic[MAX_IO_PICS];

struct irq_domain *cpu_domain;
struct irq_domain *liointc_domain;
struct irq_domain *pch_msi_domain[MAX_IO_PICS];
struct irq_domain *pch_pic_domain[MAX_IO_PICS];

int find_pch_pic(u32 gsi)
{
	int i, start, end;

	/* Find the PCH_PIC that manages this GSI. */
	for (i = 0; i < loongson_sysconf.nr_io_pics; i++) {
		struct acpi_madt_bio_pic *irq_cfg = acpi_pchpic[i];

		start = irq_cfg->gsi_base;
		end   = irq_cfg->gsi_base + irq_cfg->size;
		if (gsi >= start && gsi < end)
			return i;
	}

	pr_err("ERROR: Unable to locate PCH_PIC for GSI %d\n", gsi);
	return -1;
}

#ifdef CONFIG_HOTPLUG_CPU
static void handle_irq_affinity(void)
{
	struct irq_desc *desc;
	struct irq_chip *chip;
	unsigned int irq;
	unsigned long flags;
	struct cpumask *affinity;

	for_each_active_irq(irq) {
		desc = irq_to_desc(irq);
		if (!desc)
			continue;

		raw_spin_lock_irqsave(&desc->lock, flags);

		affinity = desc->irq_data.common->affinity;
		if (!cpumask_intersects(affinity, cpu_online_mask))
			cpumask_copy(affinity, cpu_online_mask);

		chip = irq_data_get_irq_chip(&desc->irq_data);
		if (chip && chip->irq_set_affinity)
			chip->irq_set_affinity(&desc->irq_data, desc->irq_data.common->affinity, true);
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}
}

void fixup_irqs(void)
{
	handle_irq_affinity();
	irq_cpu_offline();
	clear_csr_ecfg(ECFG0_IM);
}
#endif

/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves.
 */
void ack_bad_irq(unsigned int irq)
{
	pr_warn("Unexpected IRQ # %d\n", irq);
}

atomic_t irq_err_count;

asmlinkage void spurious_interrupt(void)
{
	atomic_inc(&irq_err_count);
}

int arch_show_interrupts(struct seq_file *p, int prec)
{
	show_ipi_list(p, prec);
	seq_printf(p, "%*s: %10u\n", prec, "ERR", atomic_read(&irq_err_count));
	return 0;
}

void __init setup_IRQ(void)
{
	int i;
	struct irq_domain *parent_domain;

	if (!acpi_eiointc[0])
		cpu_data[0].options &= ~LOONGARCH_CPU_EXTIOI;

	cpu_domain = loongarch_cpu_irq_init();
	liointc_domain = liointc_acpi_init(cpu_domain, acpi_liointc);

	if (cpu_has_extioi) {
		pr_info("Using EIOINTC interrupt mode\n");
		for (i = 0; i < loongson_sysconf.nr_io_pics; i++) {
			parent_domain = eiointc_acpi_init(cpu_domain, acpi_eiointc[i]);
			pch_pic_domain[i] = pch_pic_acpi_init(parent_domain, acpi_pchpic[i]);
			pch_msi_domain[i] = pch_msi_acpi_init(parent_domain, acpi_pchmsi[i]);
		}
	} else {
		pr_info("Using HTVECINTC interrupt mode\n");
		parent_domain = htvec_acpi_init(liointc_domain, acpi_htintc);
		pch_pic_domain[0] = pch_pic_acpi_init(parent_domain, acpi_pchpic[0]);
		pch_msi_domain[0] = pch_msi_acpi_init(parent_domain, acpi_pchmsi[0]);
	}

	irq_set_default_host(pch_pic_domain[0]);
	pch_lpc_acpi_init(pch_pic_domain[0], acpi_pchlpc);
}

void __init init_IRQ(void)
{
	int i, r, ipi_irq;
	static int ipi_dummy_dev;
	unsigned int order = get_order(IRQ_STACK_SIZE);

	clear_csr_ecfg(ECFG0_IM);
	clear_csr_estat(ESTATF_IP);

	setup_IRQ();
#ifdef CONFIG_SMP
	ipi_irq = get_ipi_irq();
	irq_set_percpu_devid(ipi_irq);
	r = request_percpu_irq(ipi_irq, loongson3_ipi_interrupt, "IPI", &ipi_dummy_dev);
	if (r < 0)
		panic("IPI IRQ request failed\n");
#endif

	for (i = 0; i < NR_IRQS; i++)
		irq_set_noprobe(i);

	for_each_possible_cpu(i) {
		void *s = (void *)__get_free_pages(GFP_KERNEL, order);

		per_cpu(irq_stack, i) = (unsigned long)s;
		pr_debug("CPU%d IRQ stack at 0x%lx - 0x%lx\n", i,
			per_cpu(irq_stack, i), per_cpu(irq_stack, i) + IRQ_STACK_SIZE);
	}

	set_csr_ecfg(ECFGF_IP0 | ECFGF_IP1 | ECFGF_IP2 | ECFGF_IPI | ECFGF_PMC);
}
