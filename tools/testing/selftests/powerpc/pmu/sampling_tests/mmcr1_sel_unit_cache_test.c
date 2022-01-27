// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Athira Rajeev, IBM Corp.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../event.h"
#include "misc.h"
#include "utils.h"

extern void thirty_two_instruction_loop_with_ll_sc(u64 loops, u64 *ll_sc_target);

/* The data cache was reloaded from local core's L3 due to a demand load */
#define EventCode 0x21c040

/*
 * A perf sampling test for mmcr1
 * fields : pmcxsel, unit, cache.
 */
static int mmcr1_sel_unit_cache(void)
{
	struct event event;
	u64 *intr_regs;
	u64 dummy;

	/* Check for platform support for the test */
	SKIP_IF(check_pvr_for_sampling_tests());

	/* Init the event for the sampling test */
	event_init_sampling(&event, EventCode);
	event.attr.sample_regs_intr = platform_extended_mask;
	FAIL_IF(event_open(&event));
	event.mmap_buffer = event_sample_buf_mmap(event.fd, 1);

	event_enable(&event);

	/* workload to make the event overflow */
	thirty_two_instruction_loop_with_ll_sc(10000000, &dummy);

	event_disable(&event);

	/* Check for sample count */
	FAIL_IF(!collect_samples(event.mmap_buffer));

	intr_regs = get_intr_regs(&event, event.mmap_buffer);

	/* Check for intr_regs */
	FAIL_IF(!intr_regs);

	/*
	 * Verify that  pmcxsel, unit and cache field of MMCR1
	 * match with corresponding event code fields
	 */
	FAIL_IF(EV_CODE_EXTRACT(event.attr.config, pmcxsel) !=
			GET_MMCR_FIELD(1, get_reg_value(intr_regs, "MMCR1"), 1, pmcxsel));
	FAIL_IF(EV_CODE_EXTRACT(event.attr.config, unit) !=
			GET_MMCR_FIELD(1, get_reg_value(intr_regs, "MMCR1"), 1, unit));
	FAIL_IF(EV_CODE_EXTRACT(event.attr.config, cache) !=
			GET_MMCR_FIELD(1, get_reg_value(intr_regs, "MMCR1"), 1, cache));

	event_close(&event);
	return 0;
}

int main(void)
{
	FAIL_IF(test_harness(mmcr1_sel_unit_cache, "mmcr1_sel_unit_cache"));
}
