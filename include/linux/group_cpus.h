/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 Thomas Gleixner.
 * Copyright (C) 2016-2017 Christoph Hellwig.
 */

#ifndef __LINUX_GROUP_CPUS_H
#define __LINUX_GROUP_CPUS_H
#include <linux/kernel.h>
#include <linux/cpu.h>

#ifdef CONFIG_SMP
struct cpumask *group_cpus_evenly(unsigned int numgrps);
#else
static inline struct cpumask *group_cpus_evenly(unsigned int numgrps)
{
	struct cpumask *masks = kcalloc(numgrps, sizeof(*masks), GFP_KERNEL);

	if (!masks)
		return NULL;

	/* assign all CPUs(cpu 0) to the 1st group only */
	cpumask_copy(&masks[0], cpu_possible_mask);
	return masks;
}
#endif

#endif
