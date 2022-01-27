// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022, Madhavan Srinivasan, IBM Corp.
 */

#include <stdio.h>
#include <stdlib.h>

#include "../event.h"
#include "misc.h"
#include "utils.h"

extern void thirty_two_instruction_loop(int loops);

/*
 * A perf sampling test for mmcr2
 * fields : fcs, fch.
 */
static int mmcr2_fcs_fch(void)
{
	struct event event;
	u64 *intr_regs;

	/* Check for platform support for the test */
	SKIP_IF(check_pvr_for_sampling_tests());

	/* Init the event for the sampling test */
	event_init_sampling(&event, 0x1001e);
	event.attr.sample_regs_intr = platform_extended_mask;
	event.attr.exclude_kernel = 1;
	FAIL_IF(event_open(&event));
	event.mmap_buffer = event_sample_buf_mmap(event.fd, 1);

	event_enable(&event);

	/* workload to make the event overflow */
	thirty_two_instruction_loop(10000);

	event_disable(&event);

	/* Check for sample count */
	FAIL_IF(!collect_samples(event.mmap_buffer));

	intr_regs = get_intr_regs(&event, event.mmap_buffer);

	/* Check for intr_regs */
	FAIL_IF(!intr_regs);

	/*
	 * Verify that fcs and fch field of MMCR2 match
	 * with corresponding modifier fields.
	 */
	if (is_pSeries())
		FAIL_IF(GET_ATTR_FIELD(&event, exclude_kernel) !=
			GET_MMCR_FIELD(2, get_reg_value(intr_regs, "MMCR2"), 1, fcs));
	else
		FAIL_IF(GET_ATTR_FIELD(&event, exclude_kernel) !=
			GET_MMCR_FIELD(2, get_reg_value(intr_regs, "MMCR2"), 1, fch));

	event_close(&event);
	return 0;
}

int main(void)
{
	FAIL_IF(test_harness(mmcr2_fcs_fch, "mmcr2_fcs_fch"));
}
