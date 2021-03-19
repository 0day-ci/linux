// SPDX-License-Identifier: GPL-2.0
/*
 * RISC-V performance counter support.
 *
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 *
 * This implementation is based on old RISC-V perf and ARM perf event code
 * which are in turn based on sparc64 and x86 code.
 */

#include <linux/cpumask.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/perf/riscv_pmu.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/smp.h>

#include <asm/sbi.h>

static unsigned long csr_read_num(int csr_num)
{
#define switchcase_csr_read(__csr_num, __val)		{\
	case __csr_num:					\
		__val = csr_read(__csr_num);		\
		break; }
#define switchcase_csr_read_2(__csr_num, __val)		{\
	switchcase_csr_read(__csr_num + 0, __val)	 \
	switchcase_csr_read(__csr_num + 1, __val)}
#define switchcase_csr_read_4(__csr_num, __val)		{\
	switchcase_csr_read_2(__csr_num + 0, __val)	 \
	switchcase_csr_read_2(__csr_num + 2, __val)}
#define switchcase_csr_read_8(__csr_num, __val)		{\
	switchcase_csr_read_4(__csr_num + 0, __val)	 \
	switchcase_csr_read_4(__csr_num + 4, __val)}
#define switchcase_csr_read_16(__csr_num, __val)	{\
	switchcase_csr_read_8(__csr_num + 0, __val)	 \
	switchcase_csr_read_8(__csr_num + 8, __val)}
#define switchcase_csr_read_32(__csr_num, __val)	{\
	switchcase_csr_read_16(__csr_num + 0, __val)	 \
	switchcase_csr_read_16(__csr_num + 16, __val)}

	unsigned long ret = 0;

	switch (csr_num) {
	switchcase_csr_read_32(CSR_CYCLE, ret)
	switchcase_csr_read_32(CSR_CYCLEH, ret)
	default :
		break;
	}

	return ret;
#undef switchcase_csr_read_32
#undef switchcase_csr_read_16
#undef switchcase_csr_read_8
#undef switchcase_csr_read_4
#undef switchcase_csr_read_2
#undef switchcase_csr_read
}

/*
 * Read the CSR of a corresponding counter.
 */
unsigned long riscv_pmu_read_ctr_csr(unsigned long csr)
{
	if (csr < CSR_CYCLE || csr > CSR_HPMCOUNTER31H ||
	   (csr > CSR_HPMCOUNTER31 && csr < CSR_CYCLEH)) {
		pr_err("Invalid performance counter csr %lx\n", csr);
		return -EINVAL;
	}

	return csr_read_num(csr);
}

static unsigned long riscv_pmu_get_ctr_mask(struct perf_event *event)
{
	int cwidth;
	struct riscv_pmu *rvpmu = to_riscv_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	if (!rvpmu->get_ctr_width)
	/**
	 * If the pmu driver doesn't support counter width, set it to default maximum
	 * allowed by the specification.
	 */
		cwidth = 63;
	else {
		if (hwc->idx == -1)
			/* Handle init case where idx is not initialized yet */
			cwidth = rvpmu->get_ctr_width(0);
		else
			cwidth = rvpmu->get_ctr_width(hwc->idx);
	}

	return GENMASK_ULL(cwidth, 0);
}

static u64 riscv_pmu_event_update(struct perf_event *event)
{
	struct riscv_pmu *rvpmu = to_riscv_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	u64 prev_raw_count, new_raw_count;
	unsigned long cmask;
	u64 oldval, delta;

	if (!rvpmu->read_ctr)
		return 0;

	cmask = riscv_pmu_get_ctr_mask(event);

	do {
		prev_raw_count = local64_read(&hwc->prev_count);
		new_raw_count = rvpmu->read_ctr(event);
		oldval = local64_cmpxchg(&hwc->prev_count, prev_raw_count,
					 new_raw_count);
	} while (oldval != prev_raw_count);

	delta = (new_raw_count - prev_raw_count) & cmask;
	local64_add(delta, &event->count);
	local64_sub(delta, &hwc->period_left);

	return delta;
}

static void riscv_pmu_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct riscv_pmu *rvpmu = to_riscv_pmu(event->pmu);

	WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);

	if (!(hwc->state & PERF_HES_STOPPED)) {
		riscv_pmu_event_update(event);
		if (rvpmu->stop_ctr) {
			rvpmu->stop_ctr(event);
			hwc->state |= PERF_HES_STOPPED;
		}
		hwc->state |= PERF_HES_UPTODATE;
	}
}

static int riscv_pmu_event_set_period(struct perf_event *event, u64 *init_val)
{
	struct hw_perf_event *hwc = &event->hw;
	s64 left = local64_read(&hwc->period_left);
	s64 period = hwc->sample_period;
	u64 max_period;
	int ret = 0;
	unsigned long cmask = riscv_pmu_get_ctr_mask(event);

	max_period = cmask;
	if (unlikely(left <= -period)) {
		left = period;
		local64_set(&hwc->period_left, left);
		hwc->last_period = period;
		ret = 1;
	}

	if (unlikely(left <= 0)) {
		left += period;
		local64_set(&hwc->period_left, left);
		hwc->last_period = period;
		ret = 1;
	}

	/*
	 * Limit the maximum period to prevent the counter value
	 * from overtaking the one we are about to program. In
	 * effect we are reducing max_period to account for
	 * interrupt latency (and we are being very conservative).
	 */
	if (left > (max_period >> 1))
		left = (max_period >> 1);

	local64_set(&hwc->prev_count, (u64)-left);
	*init_val = (u64)(-left) & max_period;
	perf_event_update_userpage(event);

	return ret;
}

static void riscv_pmu_start(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct riscv_pmu *rvpmu = to_riscv_pmu(event->pmu);
	u64 init_val;

	if (WARN_ON_ONCE(!(event->hw.state & PERF_HES_STOPPED)))
		return;

	if (flags & PERF_EF_RELOAD) {
		WARN_ON_ONCE(!(event->hw.state & PERF_HES_UPTODATE));

		/*
		 * Set the counter to the period to the next interrupt here,
		 * if you have any.
		 */
	}

	hwc->state = 0;
	riscv_pmu_event_set_period(event, &init_val);
	rvpmu->start_ctr(event, init_val);
	perf_event_update_userpage(event);
}

static int riscv_pmu_add(struct perf_event *event, int flags)
{
	struct riscv_pmu *rvpmu = to_riscv_pmu(event->pmu);
	struct cpu_hw_events *cpuc = this_cpu_ptr(rvpmu->hw_events);
	struct hw_perf_event *hwc = &event->hw;
	int idx;

	idx = rvpmu->get_ctr_idx(event);
	if (idx < 0)
		return idx;

	hwc->idx = idx;
	cpuc->events[idx] = event;
	cpuc->n_events++;
	hwc->state = PERF_HES_UPTODATE | PERF_HES_STOPPED;
	if (flags & PERF_EF_START)
		riscv_pmu_start(event, PERF_EF_RELOAD);

	/* Propagate our changes to the userspace mapping. */
	perf_event_update_userpage(event);

	return 0;
}

static void riscv_pmu_del(struct perf_event *event, int flags)
{
	struct riscv_pmu *rvpmu = to_riscv_pmu(event->pmu);
	struct cpu_hw_events *cpuc = this_cpu_ptr(rvpmu->hw_events);
	struct hw_perf_event *hwc = &event->hw;

	cpuc->events[hwc->idx] = NULL;
	riscv_pmu_stop(event, PERF_EF_UPDATE);
	cpuc->n_events--;
	if (rvpmu->clear_ctr_idx)
		rvpmu->clear_ctr_idx(event);
	perf_event_update_userpage(event);
	hwc->idx = -1;
}

static void riscv_pmu_read(struct perf_event *event)
{
	riscv_pmu_event_update(event);
}

static int riscv_pmu_event_init(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct riscv_pmu *rvpmu = to_riscv_pmu(event->pmu);
	int mapped_event;
	u64 event_config = 0;
	unsigned long cmask;

	hwc->flags = 0;
	mapped_event = rvpmu->map_event(event, &event_config);
	if (mapped_event < 0) {
		pr_debug("event %x:%llx not supported\n", event->attr.type,
			 event->attr.config);
		return mapped_event;
	}
	/*
	 * idx is set to -1 because the index of a general event should not be
	 * decided until binding to some counter in pmu->add().
	 * config will contain the information about counter CSR
	 * the idx will contain the counter index
	 */

	hwc->config = event_config;
	hwc->idx = -1;
	hwc->event_base = mapped_event;

	if (!is_sampling_event(event)) {
		/*
		 * For non-sampling runs, limit the sample_period to half
		 * of the counter width. That way, the new counter value
		 * is far less likely to overtake the previous one unless
		 * you have some serious IRQ latency issues.
		 */
		cmask = riscv_pmu_get_ctr_mask(event);
		hwc->sample_period  =  cmask >> 1;
		hwc->last_period    = hwc->sample_period;
		local64_set(&hwc->period_left, hwc->sample_period);
	}

	return 0;
}

static struct riscv_pmu *riscv_pmu_alloc(void)
{
	struct riscv_pmu *pmu;
	int cpuid, i;
	struct cpu_hw_events *cpuc;

	pmu = kzalloc(sizeof(*pmu), GFP_KERNEL);
	if (!pmu)
		goto out;

	pmu->hw_events = alloc_percpu_gfp(struct cpu_hw_events, GFP_KERNEL);
	if (!pmu->hw_events) {
		pr_info("failed to allocate per-cpu PMU data.\n");
		goto out_free_pmu;
	}

	for_each_possible_cpu(cpuid) {
		cpuc = per_cpu_ptr(pmu->hw_events, cpuid);
		cpuc->n_events = 0;
		for (i = 0; i < RISCV_MAX_COUNTERS; i++)
			cpuc->events[i] = NULL;
	}
	pmu->pmu = (struct pmu) {
		.event_init	= riscv_pmu_event_init,
		.add		= riscv_pmu_add,
		.del		= riscv_pmu_del,
		.start		= riscv_pmu_start,
		.stop		= riscv_pmu_stop,
		.read		= riscv_pmu_read,
	};

	return pmu;

out_free_pmu:
	kfree(pmu);
out:
	return NULL;
}

static int riscv_perf_starting_cpu(unsigned int cpu)
{
	/* Enable the access for TIME csr only from the user mode now */
	csr_write(CSR_SCOUNTEREN, 0x2);

	return 0;
}

static int riscv_perf_dying_cpu(unsigned int cpu)
{
	/* Disable all counters access for user mode now */
	csr_write(CSR_SCOUNTEREN, 0x0);

	return 0;
}

static int riscv_pmu_device_probe(struct platform_device *pdev)
{
	struct riscv_pmu *pmu = NULL;

	pmu = riscv_pmu_alloc();
	if (!pmu)
		return -ENOMEM;

	if (sbi_major_version() == 0 &&
	    sbi_minor_version() == 3 &&
	    sbi_probe_extension(SBI_EXT_PMU) > 0) {
		pr_info("SBI PMU extension detected\n");
		riscv_pmu_sbi_init(pmu);
	} else {
		pr_info("Legacy PMU is in use as SBI PMU extension is not available\n");
		riscv_pmu_legacy_init(pmu);
	}

	cpuhp_setup_state(CPUHP_AP_PERF_RISCV_STARTING,
			  "perf/riscv/pmu:starting",
			  riscv_perf_starting_cpu, riscv_perf_dying_cpu);
	perf_pmu_register(&pmu->pmu, "cpu", PERF_TYPE_RAW);

	return 0;
}

static struct platform_driver riscv_pmu_driver = {
	.probe		= riscv_pmu_device_probe,
	.driver		= {
		.name	= RISCV_PMU_PDEV_NAME,
	},
};

static int __init riscv_pmu_driver_init(void)
{
	int ret;
	struct platform_device *pdev;

	ret = platform_driver_register(&riscv_pmu_driver);
	if (ret)
		return ret;

	pdev = platform_device_register_simple(RISCV_PMU_PDEV_NAME, -1, NULL, 0);
	if (IS_ERR(pdev)) {
		platform_driver_unregister(&riscv_pmu_driver);
		return PTR_ERR(pdev);
	}

	return ret;
}
device_initcall(riscv_pmu_driver_init)
