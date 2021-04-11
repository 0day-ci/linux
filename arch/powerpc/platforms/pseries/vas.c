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

static struct vas_all_capabs capabs_all;
static int copypaste_feat;

struct vas_capabs vcapabs[VAS_MAX_FEAT_TYPE];

DEFINE_MUTEX(vas_pseries_mutex);

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

/*
 * Get the specific capabilities based on the feature type.
 * Right now supports GZIP default and GZIP QoS capabilities.
 */
static int get_vas_capabilities(u8 feat, enum vas_cop_feat_type type,
				struct vas_ct_capabs_be *capab_be)
{
	struct vas_ct_capabs *capab;
	struct vas_capabs *vcapab;
	int rc = 0;

	vcapab = &vcapabs[type];
	memset(vcapab, 0, sizeof(*vcapab));
	INIT_LIST_HEAD(&vcapab->list);

	capab = &vcapab->capab;

	rc = plpar_vas_query_capabilities(H_QUERY_VAS_CAPABILITIES, feat,
					  (u64)virt_to_phys(capab_be));
	if (rc)
		return rc;

	capab->user_mode = capab_be->user_mode;
	if (!(capab->user_mode & VAS_COPY_PASTE_USER_MODE)) {
		pr_err("User space COPY/PASTE is not supported\n");
		return -ENOTSUPP;
	}

	snprintf(capab->name, VAS_DESCR_LEN + 1, "%.8s",
		 (char *)&capab_be->descriptor);
	capab->descriptor = be64_to_cpu(capab_be->descriptor);
	capab->win_type = capab_be->win_type;
	if (capab->win_type >= VAS_MAX_FEAT_TYPE) {
		pr_err("Unsupported window type %u\n", capab->win_type);
		return -EINVAL;
	}
	capab->max_lpar_creds = be16_to_cpu(capab_be->max_lpar_creds);
	capab->max_win_creds = be16_to_cpu(capab_be->max_win_creds);
	atomic_set(&capab->target_lpar_creds,
		   be16_to_cpu(capab_be->target_lpar_creds));
	if (feat == VAS_GZIP_DEF_FEAT) {
		capab->def_lpar_creds = be16_to_cpu(capab_be->def_lpar_creds);

		if (capab->max_win_creds < DEF_WIN_CREDS) {
			pr_err("Window creds(%u) > max allowed window creds(%u)\n",
			       DEF_WIN_CREDS, capab->max_win_creds);
			return -EINVAL;
		}
	}

	copypaste_feat = 1;

	return 0;
}

static int __init pseries_vas_init(void)
{
	struct vas_ct_capabs_be *ct_capabs_be;
	struct vas_all_capabs_be *capabs_be;
	int rc;

	/*
	 * Linux supports user space COPY/PASTE only with Radix
	 */
	if (!radix_enabled()) {
		pr_err("API is supported only with radix page tables\n");
		return -ENOTSUPP;
	}

	capabs_be = kmalloc(sizeof(*capabs_be), GFP_KERNEL);
	if (!capabs_be)
		return -ENOMEM;
	/*
	 * Get VAS overall capabilities by passing 0 to feature type.
	 */
	rc = plpar_vas_query_capabilities(H_QUERY_VAS_CAPABILITIES, 0,
					  (u64)virt_to_phys(capabs_be));
	if (rc)
		goto out;

	snprintf(capabs_all.name, VAS_DESCR_LEN, "%.7s",
		 (char *)&capabs_be->descriptor);
	capabs_all.descriptor = be64_to_cpu(capabs_be->descriptor);
	capabs_all.feat_type = be64_to_cpu(capabs_be->feat_type);

	ct_capabs_be = kmalloc(sizeof(*ct_capabs_be), GFP_KERNEL);
	if (!ct_capabs_be) {
		rc = -ENOMEM;
		goto out;
	}
	/*
	 * QOS capabilities available
	 */
	if (capabs_all.feat_type & VAS_GZIP_QOS_FEAT_BIT) {
		rc = get_vas_capabilities(VAS_GZIP_QOS_FEAT,
					  VAS_GZIP_QOS_FEAT_TYPE, ct_capabs_be);

		if (rc)
			goto out_ct;
	}
	/*
	 * Default capabilities available
	 */
	if (capabs_all.feat_type & VAS_GZIP_DEF_FEAT_BIT) {
		rc = get_vas_capabilities(VAS_GZIP_DEF_FEAT,
					  VAS_GZIP_DEF_FEAT_TYPE, ct_capabs_be);
		if (rc)
			goto out_ct;
	}

	if (!copypaste_feat)
		pr_err("GZIP feature is not supported\n");

	pr_info("GZIP feature is available\n");

out_ct:
	kfree(ct_capabs_be);
out:
	kfree(capabs_be);
	return rc;
}
machine_device_initcall(pseries, pseries_vas_init);
