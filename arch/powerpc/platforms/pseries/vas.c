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

static struct vas_all_caps caps_all;
static bool copypaste_feat;

static struct vas_caps vascaps[VAS_MAX_FEAT_TYPE];

static DEFINE_MUTEX(vas_pseries_mutex);

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
EXPORT_SYMBOL_GPL(plpar_vas_query_capabilities);

/*
 * HCALL to get fault CRB from pHyp.
 */
static int plpar_get_nx_fault(u32 winid, u64 buffer)
{
	int64_t rc;

	rc = plpar_hcall_norets(H_GET_NX_FAULT, winid, buffer);

	switch (rc) {
	case H_SUCCESS:
		return 0;
	case H_PARAMETER:
		pr_err("HCALL(%x): Invalid window ID %u\n", H_GET_NX_FAULT,
		       winid);
		return -EINVAL;
	case H_STATE:
		pr_err("HCALL(%x): No outstanding faults for window ID %u\n",
		       H_GET_NX_FAULT, winid);
		return -EINVAL;
	case H_PRIVILEGE:
		pr_err("HCALL(%x): Window(%u): Invalid fault buffer 0x%llx\n",
		       H_GET_NX_FAULT, winid, buffer);
		return -EACCES;
	default:
		pr_err("HCALL(%x): Unexpected error %lld for window(%u)\n",
		       H_GET_NX_FAULT, rc, winid);
		return -EIO;
	}
}

/*
 * Handle the fault interrupt.
 * When the fault interrupt is received for each window, query pHyp to get
 * the fault CRB on the specific fault. Then process the CRB by updating
 * CSB or send signal if the user space CSB is invalid.
 * Note: pHyp forwards an interrupt for each fault request. So one fault
 *	CRB to process for each H_GET_NX_FAULT HCALL.
 */
irqreturn_t pseries_vas_fault_thread_fn(int irq, void *data)
{
	struct vas_window *txwin = data;
	struct coprocessor_request_block crb;
	struct vas_user_win_ref *tsk_ref;
	int rc;

	rc = plpar_get_nx_fault(txwin->winid, (u64)virt_to_phys(&crb));
	if (!rc) {
		tsk_ref = &txwin->task_ref;
		vas_dump_crb(&crb);
		vas_update_csb(&crb, tsk_ref);
	}

	return IRQ_HANDLED;
}

/*
 * Allocate window and setup IRQ mapping.
 */
static int allocate_setup_window(struct vas_window *txwin,
				 u64 *domain, u8 wintype)
{
	int rc;

	rc = plpar_vas_allocate_window(txwin, domain, wintype, DEF_WIN_CREDS);
	if (rc)
		return rc;
	/*
	 * On powerVM, pHyp setup and forwards the fault interrupt per
	 * window. So the IRQ setup and fault handling will be done for
	 * each open window separately.
	 */
	txwin->lpar.fault_virq = irq_create_mapping(NULL,
						    txwin->lpar.fault_irq);
	if (!txwin->lpar.fault_virq) {
		pr_err("Failed irq mapping %d\n", txwin->lpar.fault_irq);
		rc = -EINVAL;
		goto out_win;
	}

	txwin->lpar.name = kasprintf(GFP_KERNEL, "vas-win-%d", txwin->winid);
	if (!txwin->lpar.name) {
		rc = -ENOMEM;
		goto out_irq;
	}

	rc = request_threaded_irq(txwin->lpar.fault_virq, NULL,
				  pseries_vas_fault_thread_fn, IRQF_ONESHOT,
				  txwin->lpar.name, txwin);
	if (rc) {
		pr_err("VAS-Window[%d]: Request IRQ(%u) failed with %d\n",
		       txwin->winid, txwin->lpar.fault_virq, rc);
		goto out_free;
	}

	txwin->wcreds_max = DEF_WIN_CREDS;

	return 0;
out_free:
	kfree(txwin->lpar.name);
out_irq:
	irq_dispose_mapping(txwin->lpar.fault_virq);
out_win:
	plpar_vas_deallocate_window(txwin->winid);
	return rc;
}

static inline void free_irq_setup(struct vas_window *txwin)
{
	free_irq(txwin->lpar.fault_virq, txwin);
	irq_dispose_mapping(txwin->lpar.fault_virq);
	kfree(txwin->lpar.name);
}

static struct vas_window *vas_allocate_window(struct vas_tx_win_open_attr *uattr,
					      enum vas_cop_type cop_type)
{
	long domain[PLPAR_HCALL9_BUFSIZE] = {VAS_DEFAULT_DOMAIN_ID};
	struct vas_ct_caps *ct_caps;
	struct vas_caps *caps;
	struct vas_window *txwin;
	int rc;

	txwin = kzalloc(sizeof(*txwin), GFP_KERNEL);
	if (!txwin)
		return ERR_PTR(-ENOMEM);

	/*
	 * A VAS window can have many credits which means that many
	 * requests can be issued simultaneously. But phyp restricts
	 * one credit per window.
	 * phyp introduces 2 different types of credits:
	 * Default credit type (Uses normal priority FIFO):
	 *	A limited number of credits are assigned to partitions
	 *	based on processor entitlement. But these credits may be
	 *	over-committed on a system depends on whether the CPUs
	 *	are in shared or dedicated modes - that is, more requests
	 *	may be issued across the system than NX can service at
	 *	once which can result in paste command failure (RMA_busy).
	 *	Then the process has to resend requests or fall-back to
	 *	SW compression.
	 * Quality of Service (QoS) credit type (Uses high priority FIFO):
	 *	To avoid NX HW contention, the system admins can assign
	 *	QoS credits for each LPAR so that this partition is
	 *	guaranteed access to NX resources. These credits are
	 *	assigned to partitions via the HMC.
	 *	Refer PAPR for more information.
	 *
	 * Allocate window with QoS credits if user requested. Otherwise
	 * default credits are used.
	 */
	if (uattr->flags & VAS_TX_WIN_FLAG_QOS_CREDIT)
		caps = &vascaps[VAS_GZIP_QOS_FEAT_TYPE];
	else
		caps = &vascaps[VAS_GZIP_DEF_FEAT_TYPE];

	ct_caps = &caps->caps;

	if (atomic_inc_return(&ct_caps->used_lpar_creds) >
			atomic_read(&ct_caps->target_lpar_creds)) {
		pr_err("Credits are not available to allocate window\n");
		rc = -EINVAL;
		goto out;
	}

	/*
	 * The user space is requesting to allocate a window on a VAS
	 * instance (or chip) where the process is executing.
	 * On powerVM, domain values are passed to pHyp to select chip /
	 * VAS instance. Useful if the process is affinity to NUMA node.
	 * pHyp selects VAS instance if VAS_DEFAULT_DOMAIN_ID (-1) is
	 * passed for domain values.
	 */
	if (uattr->vas_id == -1) {
		/*
		 * To allocate VAS window, pass same domain values returned
		 * from this HCALL.
		 */
		rc = plpar_hcall9(H_HOME_NODE_ASSOCIATIVITY, domain,
				  VPHN_FLAG_VCPU, smp_processor_id());
		if (rc != H_SUCCESS) {
			pr_err("HCALL(%x): failed with ret(%d)\n",
			       H_HOME_NODE_ASSOCIATIVITY, rc);
			goto out;
		}
	}

	/*
	 * Allocate / Deallocate window HCALLs and setup / free IRQs
	 * have to be protected with mutex.
	 * Open VAS window: Allocate window HCALL and setup IRQ
	 * Close VAS window: Deallocate window HCALL and free IRQ
	 *	The hypervisor waits until all NX requests are
	 *	completed before closing the window. So expects OS
	 *	to handle NX faults, means IRQ can be freed only
	 *	after the deallocate window HCALL is returned.
	 * So once the window is closed with deallocate HCALL before
	 * the IRQ is freed, it can be assigned to new allocate
	 * HCALL with the same fault IRQ by the hypervisor. It can
	 * result in setup IRQ fail for the new window since the
	 * same fault IRQ is not freed by the OS.
	 */
	mutex_lock(&vas_pseries_mutex);
	rc = allocate_setup_window(txwin, (u64 *)&domain[0],
				   ct_caps->win_type);
	mutex_unlock(&vas_pseries_mutex);
	if (rc)
		goto out;

	/*
	 * Modify window and it is ready to use.
	 */
	rc = plpar_vas_modify_window(txwin);
	if (!rc)
		rc = vas_reference_pid_mm(&txwin->task_ref);
	if (rc)
		goto out_free;

	txwin->lpar.win_type = ct_caps->win_type;
	mutex_lock(&vas_pseries_mutex);
	list_add(&txwin->lpar.win_list, &caps->list);
	mutex_unlock(&vas_pseries_mutex);

	return txwin;

out_free:
	/*
	 * Window is not operational. Free IRQ before closing
	 * window so that do not have to hold mutex.
	 */
	free_irq_setup(txwin);
	plpar_vas_deallocate_window(txwin->winid);
out:
	atomic_dec(&ct_caps->used_lpar_creds);
	kfree(txwin);
	return ERR_PTR(rc);
}

static u64 vas_paste_address(void *addr)
{
	struct vas_window *win = addr;

	return win->lpar.win_addr;
}

static int deallocate_free_window(struct vas_window *win)
{
	int rc = 0;

	/*
	 * Free IRQ after executing H_DEALLOCATE_VAS_WINDOW HCALL
	 * to close the window. pHyp waits for all requests including
	 * faults are processed before closing the window - Means all
	 * credits are returned. In the case of fault request, credit
	 * is returned after OS issues H_GET_NX_FAULT HCALL.
	 */
	rc = plpar_vas_deallocate_window(win->winid);
	if (!rc)
		free_irq_setup(win);

	return rc;
}

static int vas_deallocate_window(void *addr)
{
	struct vas_window *win = (struct vas_window *)addr;
	struct vas_ct_caps *caps;
	int rc = 0;

	if (!win)
		return -EINVAL;

	/* Should not happen */
	if (win->lpar.win_type >= VAS_MAX_FEAT_TYPE) {
		pr_err("Window (%u): Invalid window type %u\n",
				win->winid, win->lpar.win_type);
		return -EINVAL;
	}

	caps = &vascaps[win->lpar.win_type].caps;
	mutex_lock(&vas_pseries_mutex);
	rc = deallocate_free_window(win);
	if (rc) {
		mutex_unlock(&vas_pseries_mutex);
		return rc;
	}

	list_del(&win->lpar.win_list);
	atomic_dec(&caps->used_lpar_creds);
	mutex_unlock(&vas_pseries_mutex);

	vas_drop_reference_pid_mm(&win->task_ref);

	kfree(win);
	return 0;
}

static struct vas_user_win_ops vops_pseries = {
	.open_win	= vas_allocate_window,	/* Open and configure window */
	.paste_addr	= vas_paste_address,	/* To do copy/paste */
	.close_win	= vas_deallocate_window, /* Close window */
};

/*
 * Supporting only nx-gzip coprocessor type now, but this API code
 * extended to other coprocessor types later.
 */
int vas_register_api_pseries(struct module *mod, enum vas_cop_type cop_type,
			     const char *name)
{
	int rc;

	if (!copypaste_feat)
		return -ENOTSUPP;

	rc = vas_register_coproc_api(mod, cop_type, name, &vops_pseries);

	return rc;
}
EXPORT_SYMBOL_GPL(vas_register_api_pseries);

void vas_unregister_api_pseries(void)
{
	vas_unregister_coproc_api();
}
EXPORT_SYMBOL_GPL(vas_unregister_api_pseries);

/*
 * Get the specific capabilities based on the feature type.
 * Right now supports GZIP default and GZIP QoS capabilities.
 */
static int get_vas_capabilities(u8 feat, enum vas_cop_feat_type type,
				struct hv_vas_ct_caps *hv_caps)
{
	struct vas_ct_caps *caps;
	struct vas_caps *vcaps;
	int rc = 0;

	vcaps = &vascaps[type];
	memset(vcaps, 0, sizeof(*vcaps));
	INIT_LIST_HEAD(&vcaps->list);

	caps = &vcaps->caps;

	rc = plpar_vas_query_capabilities(H_QUERY_VAS_CAPABILITIES, feat,
					  (u64)virt_to_phys(hv_caps));
	if (rc)
		return rc;

	caps->user_mode = hv_caps->user_mode;
	if (!(caps->user_mode & VAS_COPY_PASTE_USER_MODE)) {
		pr_err("User space COPY/PASTE is not supported\n");
		return -ENOTSUPP;
	}

	snprintf(caps->name, VAS_DESCR_LEN + 1, "%.8s",
		 (char *)&hv_caps->descriptor);
	caps->descriptor = be64_to_cpu(hv_caps->descriptor);
	caps->win_type = hv_caps->win_type;
	if (caps->win_type >= VAS_MAX_FEAT_TYPE) {
		pr_err("Unsupported window type %u\n", caps->win_type);
		return -EINVAL;
	}
	caps->max_lpar_creds = be16_to_cpu(hv_caps->max_lpar_creds);
	caps->max_win_creds = be16_to_cpu(hv_caps->max_win_creds);
	atomic_set(&caps->target_lpar_creds,
		   be16_to_cpu(hv_caps->target_lpar_creds));
	if (feat == VAS_GZIP_DEF_FEAT) {
		caps->def_lpar_creds = be16_to_cpu(hv_caps->def_lpar_creds);

		if (caps->max_win_creds < DEF_WIN_CREDS) {
			pr_err("Window creds(%u) > max allowed window creds(%u)\n",
			       DEF_WIN_CREDS, caps->max_win_creds);
			return -EINVAL;
		}
	}

	copypaste_feat = true;

	return 0;
}

static int __init pseries_vas_init(void)
{
	struct hv_vas_ct_caps *hv_ct_caps;
	struct hv_vas_all_caps *hv_caps;
	int rc;

	/*
	 * Linux supports user space COPY/PASTE only with Radix
	 */
	if (!radix_enabled()) {
		pr_err("API is supported only with radix page tables\n");
		return -ENOTSUPP;
	}

	hv_caps = kmalloc(sizeof(*hv_caps), GFP_KERNEL);
	if (!hv_caps)
		return -ENOMEM;
	/*
	 * Get VAS overall capabilities by passing 0 to feature type.
	 */
	rc = plpar_vas_query_capabilities(H_QUERY_VAS_CAPABILITIES, 0,
					  (u64)virt_to_phys(hv_caps));
	if (rc)
		goto out;

	snprintf(caps_all.name, VAS_DESCR_LEN, "%.7s",
		 (char *)&hv_caps->descriptor);
	caps_all.descriptor = be64_to_cpu(hv_caps->descriptor);
	caps_all.feat_type = be64_to_cpu(hv_caps->feat_type);

	hv_ct_caps = kmalloc(sizeof(*hv_ct_caps), GFP_KERNEL);
	if (!hv_ct_caps) {
		rc = -ENOMEM;
		goto out;
	}
	/*
	 * QOS capabilities available
	 */
	if (caps_all.feat_type & VAS_GZIP_QOS_FEAT_BIT) {
		rc = get_vas_capabilities(VAS_GZIP_QOS_FEAT,
					  VAS_GZIP_QOS_FEAT_TYPE, hv_ct_caps);

		if (rc)
			goto out_ct;
	}
	/*
	 * Default capabilities available
	 */
	if (caps_all.feat_type & VAS_GZIP_DEF_FEAT_BIT) {
		rc = get_vas_capabilities(VAS_GZIP_DEF_FEAT,
					  VAS_GZIP_DEF_FEAT_TYPE, hv_ct_caps);
		if (rc)
			goto out_ct;
	}

	pr_info("GZIP feature is available\n");

out_ct:
	kfree(hv_ct_caps);
out:
	kfree(hv_caps);
	return rc;
}
machine_device_initcall(pseries, pseries_vas_init);
