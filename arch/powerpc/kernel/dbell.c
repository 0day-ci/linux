// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Author: Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2009 Freescale Semiconductor Inc.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/threads.h>
#include <linux/hardirq.h>

#include <asm/dbell.h>
#include <asm/interrupt.h>
#include <asm/irq_regs.h>
#include <asm/kvm_ppc.h>
#include <asm/trace.h>

#ifdef CONFIG_SMP

DEFINE_INTERRUPT_HANDLER_ASYNC(doorbell_exception)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	trace_doorbell_entry(regs);

	ppc_msgsync();

	may_hard_irq_enable();

	kvmppc_clear_host_ipi(smp_processor_id());
	__this_cpu_inc(irq_stat.doorbell_irqs);

	smp_ipi_demux_relaxed(); /* already performed the barrier */

	trace_doorbell_exit(regs);

	set_irq_regs(old_regs);
}

bool doorbell_disabled;

static int __init doorbell_cmdline(char *arg)
{
	if (!arg)
		return -EINVAL;

	if (strncmp(arg, "off", 3) == 0) {
		pr_info("Doorbell disabled on kernel command line\n");
		doorbell_disabled = true;
	}

	return 0;
}
__setup("doorbell=", doorbell_cmdline);

#else /* CONFIG_SMP */
DEFINE_INTERRUPT_HANDLER_ASYNC(doorbell_exception)
{
	printk(KERN_WARNING "Received doorbell on non-smp system\n");
}
#endif /* CONFIG_SMP */
