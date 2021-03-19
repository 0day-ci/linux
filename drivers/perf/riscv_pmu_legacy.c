// SPDX-License-Identifier: GPL-2.0
/*
 * RISC-V performance counter support.
 *
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 *
 * This implementation is based on old RISC-V perf and ARM perf event code
 * which are in turn based on sparc64 and x86 code.
 */

#include <linux/perf/riscv_pmu.h>

#define RISCV_PMU_LEGACY_CYCLE		0
#define RISCV_PMU_LEGACY_INSTRET	1
#define RISCV_PMU_LEGACY_NUM_CTR	2

static int pmu_legacy_get_ctr_idx(struct perf_event *event)
{
	struct perf_event_attr *attr = &event->attr;

	if (event->attr.type != PERF_TYPE_HARDWARE)
		return -EOPNOTSUPP;
	if (attr->config == PERF_COUNT_HW_CPU_CYCLES)
		return RISCV_PMU_LEGACY_CYCLE;
	else if (attr->config == PERF_COUNT_HW_INSTRUCTIONS)
		return RISCV_PMU_LEGACY_INSTRET;
	else
		return -EOPNOTSUPP;
}

/* For legacy config & counter index are same */
static int pmu_legacy_map_event(struct perf_event *event, u64 *config)
{
	return pmu_legacy_get_ctr_idx(event);
}

static u64 pmu_legacy_read_ctr(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	int idx = hwc->idx;
	u64 val;

	if (idx == RISCV_PMU_LEGACY_CYCLE) {
		val = riscv_pmu_read_ctr_csr(CSR_CYCLE);
		if (IS_ENABLED(CONFIG_32BIT))
			val = (u64)riscv_pmu_read_ctr_csr(CSR_CYCLEH) << 32 | val;
	} else if (idx == RISCV_PMU_LEGACY_INSTRET) {
		val = riscv_pmu_read_ctr_csr(CSR_INSTRET);
		if (IS_ENABLED(CONFIG_32BIT))
			val = ((u64)riscv_pmu_read_ctr_csr(CSR_INSTRETH)) << 32 | val;
	} else
		return 0;

	return val;
}

static void pmu_legacy_start_ctr(struct perf_event *event, u64 ival)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 initial_val = pmu_legacy_read_ctr(event);

	/**
	 * The legacy method doesn't really have a start/stop method.
	 * It also can not update the counter with a initial value.
	 * But we still need to set the prev_count so that read() can compute
	 * the delta. Just use the current counter value to set the prev_count.
	 */
	local64_set(&hwc->prev_count, initial_val);
}

/**
 * This is just a simple implementation to allow legacy implementations
 * compatible with new RISC-V PMU driver framework.
 * This driver only allows reading two counters i.e CYCLE & INSTRET.
 * However, it can not start or stop the counter. Thus, it is not very useful
 * will be removed in future.
 */
void riscv_pmu_legacy_init(struct riscv_pmu *pmu)
{
	pmu->num_counters = RISCV_PMU_LEGACY_NUM_CTR;
	pmu->start_ctr = pmu_legacy_start_ctr;
	pmu->stop_ctr = NULL;
	pmu->map_event = pmu_legacy_map_event;
	pmu->get_ctr_idx = pmu_legacy_get_ctr_idx;
	pmu->get_ctr_width = NULL;
	pmu->clear_ctr_idx = NULL;
	pmu->read_ctr = pmu_legacy_read_ctr;
}
