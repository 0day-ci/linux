// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2020-21 IBM Corp.
 */

#define pr_fmt(fmt) "vas: " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/irqdomain.h>
#include <linux/interrupt.h>
#include <linux/sched/mm.h>
#include <linux/mmu_context.h>
#include <asm/hvcall.h>
#include <asm/hvconsole.h>
#include <asm/machdep.h>
#include <asm/plpar_wrappers.h>
#include <asm/vas.h>
#include "vas.h"

#define	VAS_INVALID_WIN_ADDRESS	0xFFFFFFFFFFFFFFFFul
#define	VAS_DEFAULT_DOMAIN_ID	0xFFFFFFFFFFFFFFFFul
/* Authority Mask Register (AMR) value is not supported in */
/* linux implementation. So pass '0' to modify window HCALL */
#define	VAS_AMR_VALUE	0
/* phyp allows one credit per window right now */
#define DEF_WIN_CREDS		1

static int64_t hcall_return_busy_check(int64_t rc)
{
	/* Check if we are stalled for some time */
	if (H_IS_LONG_BUSY(rc)) {
		msleep(get_longbusy_msecs(rc));
		rc = H_BUSY;
	} else if (rc == H_BUSY) {
		cond_resched();
	}

	return rc;
}

/*
 * Allocate VAS window HCALL
 */
static int plpar_vas_allocate_window(struct vas_window *win, u64 *domain,
				     u8 wintype, u16 credits)
{
	long retbuf[PLPAR_HCALL9_BUFSIZE] = {0};
	int64_t rc;

	do {
		rc = plpar_hcall9(H_ALLOCATE_VAS_WINDOW, retbuf, wintype,
				  credits, domain[0], domain[1], domain[2],
				  domain[3], domain[4], domain[5]);

		rc = hcall_return_busy_check(rc);
	} while (rc == H_BUSY);

	switch (rc) {
	case H_SUCCESS:
		win->winid = retbuf[0];
		win->lpar.win_addr = retbuf[1];
		win->lpar.complete_irq = retbuf[2];
		win->lpar.fault_irq = retbuf[3];
		if (win->lpar.win_addr == VAS_INVALID_WIN_ADDRESS) {
			pr_err("HCALL(%x): COPY/PASTE is not supported\n",
				H_ALLOCATE_VAS_WINDOW);
			return -ENOTSUPP;
		}
		return 0;
	case H_PARAMETER:
		pr_err("HCALL(%x): Invalid window type (%u)\n",
			H_ALLOCATE_VAS_WINDOW, wintype);
		return -EINVAL;
	case H_P2:
		pr_err("HCALL(%x): Credits(%u) exceed maximum window credits\n",
			H_ALLOCATE_VAS_WINDOW, credits);
		return -EINVAL;
	case H_COP_HW:
		pr_err("HCALL(%x): User-mode COPY/PASTE is not supported\n",
			H_ALLOCATE_VAS_WINDOW);
		return -ENOTSUPP;
	case H_RESOURCE:
		pr_err("HCALL(%x): LPAR credit limit exceeds window limit\n",
			H_ALLOCATE_VAS_WINDOW);
		return -EPERM;
	case H_CONSTRAINED:
		pr_err("HCALL(%x): Credits (%u) are not available\n",
			H_ALLOCATE_VAS_WINDOW, credits);
		return -EPERM;
	default:
		pr_err("HCALL(%x): Unexpected error %lld\n",
			H_ALLOCATE_VAS_WINDOW, rc);
		return -EIO;
	}
}

/*
 * Deallocate VAS window HCALL.
 */
static int plpar_vas_deallocate_window(u64 winid)
{
	int64_t rc;

	do {
		rc = plpar_hcall_norets(H_DEALLOCATE_VAS_WINDOW, winid);

		rc = hcall_return_busy_check(rc);
	} while (rc == H_BUSY);

	switch (rc) {
	case H_SUCCESS:
		return 0;
	case H_PARAMETER:
		pr_err("HCALL(%x): Invalid window ID %llu\n",
			H_DEALLOCATE_VAS_WINDOW, winid);
		return -EINVAL;
	case H_STATE:
		pr_err("HCALL(%x): Window(%llu): Invalid page table entries\n",
			H_DEALLOCATE_VAS_WINDOW, winid);
		return -EPERM;
	default:
		pr_err("HCALL(%x): Unexpected error %lld for window(%llu)\n",
			H_DEALLOCATE_VAS_WINDOW, rc, winid);
		return -EIO;
	}
}

/*
 * Modify VAS window.
 * After the window is opened with allocate window HCALL, configure it
 * with flags and LPAR PID before using.
 */
static int plpar_vas_modify_window(struct vas_window *win)
{
	int64_t rc;
	u32 lpid = mfspr(SPRN_PID);

	/*
	 * AMR value is not supported in Linux implementation
	 * phyp ignores it if 0 is passed.
	 */
	do {
		rc = plpar_hcall_norets(H_MODIFY_VAS_WINDOW, win->winid,
					lpid, 0, VAS_MOD_WIN_FLAGS,
					VAS_AMR_VALUE);

		rc = hcall_return_busy_check(rc);
	} while (rc == H_BUSY);

	switch (rc) {
	case H_SUCCESS:
		return 0;
	case H_PARAMETER:
		pr_err("HCALL(%x): Invalid window ID %u\n",
			H_MODIFY_VAS_WINDOW, win->winid);
		return -EINVAL;
	case H_P2:
		pr_err("HCALL(%x): Window(%d): Invalid LPAR Process ID %u\n",
			H_MODIFY_VAS_WINDOW, lpid, win->winid);
		return -EINVAL;
	case H_P3:
		/* LPAR thread ID is deprecated on P10 */
		pr_err("HCALL(%x): Invalid LPAR Thread ID for window(%u)\n",
			H_MODIFY_VAS_WINDOW, win->winid);
		return -EINVAL;
	case H_STATE:
		pr_err("HCALL(%x): Jobs in progress, Can't modify window(%u)\n",
			H_MODIFY_VAS_WINDOW, win->winid);
		return -EBUSY;
	default:
		pr_err("HCALL(%x): Unexpected error %lld for window(%u)\n",
			H_MODIFY_VAS_WINDOW, rc, win->winid);
		return -EIO;
	}
}

/*
 * This HCALL is used to determine the capabilities that pHyp provides.
 * @hcall: H_QUERY_VAS_CAPABILITIES or H_QUERY_NX_CAPABILITIES
 * @query_type: If 0 is passed, phyp returns the overall capabilities
 *		which provides all feature(s) that are available. Then
 *		query phyp to get the corresponding capabilities for
 *		the specific feature.
 *		Example: H_QUERY_VAS_CAPABILITIES provides VAS GZIP QoS
 *			and VAS GZIP Default capabilities.
 *			H_QUERY_NX_CAPABILITIES provides NX GZIP
 *			capabilities.
 * @result: Return buffer to save capabilities.
 */
int plpar_vas_query_capabilities(const u64 hcall, u8 query_type,
					u64 result)
{
	int64_t rc;

	rc = plpar_hcall_norets(hcall, query_type, result);

	switch (rc) {
	case H_SUCCESS:
		return 0;
	case H_PARAMETER:
		pr_err("HCALL(%llx): Invalid query type %u\n", hcall,
			query_type);
		return -EINVAL;
	case H_PRIVILEGE:
		pr_err("HCALL(%llx): Invalid result buffer 0x%llx\n",
			hcall, result);
		return -EACCES;
	default:
		pr_err("HCALL(%llx): Unexpected error %lld\n", hcall, rc);
		return -EIO;
	}
}
