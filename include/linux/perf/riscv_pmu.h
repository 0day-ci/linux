/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 SiFive
 * Copyright (C) 2018 Andes Technology Corporation
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 *
 */

#ifndef _ASM_RISCV_PERF_EVENT_H
#define _ASM_RISCV_PERF_EVENT_H

#include <linux/perf_event.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>

#ifdef CONFIG_RISCV_PMU

/*
 * The RISCV_MAX_COUNTERS parameter should be specified.
 */

#define RISCV_MAX_COUNTERS	128
#define RISCV_OP_UNSUPP		(-EOPNOTSUPP)
#define RISCV_PMU_PDEV_NAME	"riscv-pmu"

struct cpu_hw_events {
	/* currently enabled events */
	int			n_events;
	/* currently enabled events */
	struct perf_event	*events[RISCV_MAX_COUNTERS];
	/* currently enabled counters */
	DECLARE_BITMAP(used_event_ctrs, RISCV_MAX_COUNTERS);
};

struct riscv_pmu {
	struct pmu	pmu;
	char		*name;

	irqreturn_t	(*handle_irq)(int irq_num, void *dev);
	int		irq;

	int		num_counters;
	u64		(*read_ctr)(struct perf_event *event);
	int		(*get_ctr_idx)(struct perf_event *event);
	int		(*get_ctr_width)(int idx);
	void		(*clear_ctr_idx)(struct perf_event *event);
	void		(*start_ctr)(struct perf_event *event, u64 init_val);
	void		(*stop_ctr)(struct perf_event *event);
	int		(*map_event)(struct perf_event *event, u64 *config);

	struct cpu_hw_events	__percpu *hw_events;
};

#define to_riscv_pmu(p) (container_of(p, struct riscv_pmu, pmu))
unsigned long riscv_pmu_read_ctr_csr(unsigned long csr);

#endif /* CONFIG_RISCV_PMU */

#endif /* _ASM_RISCV_PERF_EVENT_H */
