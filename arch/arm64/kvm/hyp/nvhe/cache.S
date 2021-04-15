/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Code copied from arch/arm64/mm/cache.S.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/alternative.h>

SYM_FUNC_START_PI(__flush_dcache_area)
	dcache_by_line_op civac, sy, x0, x1, x2, x3
	ret
SYM_FUNC_END_PI(__flush_dcache_area)

/*
 *	invalidate_icache_range(start,end)
 *
 *	Ensure that the I cache is invalid within specified region.
 *
 *	- start   - virtual start address of region
 *	- end     - virtual end address of region
 */
SYM_FUNC_START(invalidate_icache_range)
SYM_FUNC_END(invalidate_icache_range)
