/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2016-17 IBM Corp.
 */

#ifndef _ASM_POWERPC_VAS_H
#define _ASM_POWERPC_VAS_H
#include <linux/sched/mm.h>
#include <linux/mmu_context.h>
#include <asm/icswx.h>
#include <uapi/asm/vas-api.h>

/*
 * Min and max FIFO sizes are based on Version 1.05 Section 3.1.4.25
 * (Local FIFO Size Register) of the VAS workbook.
 */
#define VAS_RX_FIFO_SIZE_MIN	(1 << 10)	/* 1KB */
#define VAS_RX_FIFO_SIZE_MAX	(8 << 20)	/* 8MB */

/*
 * Threshold Control Mode: Have paste operation fail if the number of
 * requests in receive FIFO exceeds a threshold.
 *
 * NOTE: No special error code yet if paste is rejected because of these
 *	 limits. So users can't distinguish between this and other errors.
 */
#define VAS_THRESH_DISABLED		0
#define VAS_THRESH_FIFO_GT_HALF_FULL	1
#define VAS_THRESH_FIFO_GT_QTR_FULL	2
#define VAS_THRESH_FIFO_GT_EIGHTH_FULL	3

/*
 * Get/Set bit fields
 */
#define GET_FIELD(m, v)                (((v) & (m)) >> MASK_LSH(m))
#define MASK_LSH(m)            (__builtin_ffsl(m) - 1)
#define SET_FIELD(m, v, val)   \
		(((v) & ~(m)) | ((((typeof(v))(val)) << MASK_LSH(m)) & (m)))

/*
 * Co-processor Engine type.
 */
enum vas_cop_type {
	VAS_COP_TYPE_FAULT,
	VAS_COP_TYPE_842,
	VAS_COP_TYPE_842_HIPRI,
	VAS_COP_TYPE_GZIP,
	VAS_COP_TYPE_GZIP_HIPRI,
	VAS_COP_TYPE_FTW,
	VAS_COP_TYPE_MAX,
};

/*
 * User space VAS windows are opened by tasks and take references
 * to pid and mm until windows are closed.
 * Stores pid, mm, and tgid for each window.
 */
struct vas_user_win_ref {
	struct pid *pid;	/* PID of owner */
	struct pid *tgid;	/* Thread group ID of owner */
	struct mm_struct *mm;	/* Linux process mm_struct */
};

/*
 * In-kernel state a VAS window. One per window.
 * powerVM: Used only for Tx windows.
 * powerNV: Used for both Tx and Rx windows.
 */
struct vas_window {
	u32 winid;
	u32 wcreds_max;	/* Window credits */
	enum vas_cop_type cop;
	struct vas_user_win_ref task_ref;
	char *dbgname;
	struct dentry *dbgdir;
	union {
		/* powerNV specific data */
		struct {
			void *vinst;	/* points to VAS instance */
			bool tx_win;	/* True if send window */
			bool nx_win;	/* True if NX window */
			bool user_win;	/* True if user space window */
			void *hvwc_map;	/* HV window context */
			void *uwc_map;	/* OS/User window context */

			/* Fields applicable only to send windows */
			void *paste_kaddr;
			char *paste_addr_name;
			struct vas_window *rxwin;

			atomic_t num_txwins;	/* Only for receive windows */
		} pnv;
		struct {
			u64 win_addr;	/* Physical paste address */
			u8 win_type;	/* QoS or Default window */
			u8 status;
			u32 complete_irq;	/* Completion interrupt */
			u32 fault_irq;	/* Fault interrupt */
			u64 domain[6];	/* Associativity domain Ids */
					/* this window is allocated */
			u64 util;

			/* List of windows opened which is used for LPM */
			struct list_head win_list;
			u64 flags;
			char *name;
			int fault_virq;
		} lpar;
	};
};

/*
 * User space window operations used for powernv and powerVM
 */
struct vas_user_win_ops {
	struct vas_window * (*open_win)(struct vas_tx_win_open_attr *,
				enum vas_cop_type);
	u64 (*paste_addr)(void *);
	int (*close_win)(void *);
};

static inline void vas_drop_reference_pid_mm(struct vas_user_win_ref *ref)
{
	/* Drop references to pid and mm */
	put_pid(ref->pid);
	if (ref->mm) {
		mm_context_remove_vas_window(ref->mm);
		mmdrop(ref->mm);
	}
}

/*
 * Receive window attributes specified by the (in-kernel) owner of window.
 */
struct vas_rx_win_attr {
	void *rx_fifo;
	int rx_fifo_size;
	int wcreds_max;

	bool pin_win;
	bool rej_no_credit;
	bool tx_wcred_mode;
	bool rx_wcred_mode;
	bool tx_win_ord_mode;
	bool rx_win_ord_mode;
	bool data_stamp;
	bool nx_win;
	bool fault_win;
	bool user_win;
	bool notify_disable;
	bool intr_disable;
	bool notify_early;

	int lnotify_lpid;
	int lnotify_pid;
	int lnotify_tid;
	u32 pswid;

	int tc_mode;
};

/*
 * Window attributes specified by the in-kernel owner of a send window.
 */
struct vas_tx_win_attr {
	enum vas_cop_type cop;
	int wcreds_max;
	int lpid;
	int pidr;		/* hardware PID (from SPRN_PID) */
	int pswid;
	int rsvd_txbuf_count;
	int tc_mode;

	bool user_win;
	bool pin_win;
	bool rej_no_credit;
	bool rsvd_txbuf_enable;
	bool tx_wcred_mode;
	bool rx_wcred_mode;
	bool tx_win_ord_mode;
	bool rx_win_ord_mode;
};

#ifdef CONFIG_PPC_POWERNV
/*
 * Helper to map a chip id to VAS id.
 * For POWER9, this is a 1:1 mapping. In the future this maybe a 1:N
 * mapping in which case, we will need to update this helper.
 *
 * Return the VAS id or -1 if no matching vasid is found.
 */
int chip_to_vas_id(int chipid);

/*
 * Helper to initialize receive window attributes to defaults for an
 * NX window.
 */
void vas_init_rx_win_attr(struct vas_rx_win_attr *rxattr, enum vas_cop_type cop);

/*
 * Open a VAS receive window for the instance of VAS identified by @vasid
 * Use @attr to initialize the attributes of the window.
 *
 * Return a handle to the window or ERR_PTR() on error.
 */
struct vas_window *vas_rx_win_open(int vasid, enum vas_cop_type cop,
				   struct vas_rx_win_attr *attr);

/*
 * Helper to initialize send window attributes to defaults for an NX window.
 */
extern void vas_init_tx_win_attr(struct vas_tx_win_attr *txattr,
			enum vas_cop_type cop);

/*
 * Open a VAS send window for the instance of VAS identified by @vasid
 * and the co-processor type @cop. Use @attr to initialize attributes
 * of the window.
 *
 * Note: The instance of VAS must already have an open receive window for
 * the coprocessor type @cop.
 *
 * Return a handle to the send window or ERR_PTR() on error.
 */
struct vas_window *vas_tx_win_open(int vasid, enum vas_cop_type cop,
			struct vas_tx_win_attr *attr);

/*
 * Close the send or receive window identified by @win. For receive windows
 * return -EAGAIN if there are active send windows attached to this receive
 * window.
 */
int vas_win_close(struct vas_window *win);

/*
 * Copy the co-processor request block (CRB) @crb into the local L2 cache.
 */
int vas_copy_crb(void *crb, int offset);

/*
 * Paste a previously copied CRB (see vas_copy_crb()) from the L2 cache to
 * the hardware address associated with the window @win. @re is expected/
 * assumed to be true for NX windows.
 */
int vas_paste_crb(struct vas_window *win, int offset, bool re);

void vas_win_paste_addr(struct vas_window *window, u64 *addr,
			int *len);
int vas_register_api_powernv(struct module *mod, enum vas_cop_type cop_type,
			     const char *name);
void vas_unregister_api_powernv(void);
#endif

#ifdef CONFIG_PPC_PSERIES

/* VAS Capabilities */
#define VAS_GZIP_QOS_FEAT	0x1
#define VAS_GZIP_DEF_FEAT	0x2
#define VAS_GZIP_QOS_FEAT_BIT	PPC_BIT(VAS_GZIP_QOS_FEAT) /* Bit 1 */
#define VAS_GZIP_DEF_FEAT_BIT	PPC_BIT(VAS_GZIP_DEF_FEAT) /* Bit 2 */

/* NX Capabilities */
#define VAS_NX_GZIP_FEAT	0x1
#define VAS_NX_GZIP_FEAT_BIT	PPC_BIT(VAS_NX_GZIP_FEAT) /* Bit 1 */
#define VAS_DESCR_LEN		8

/*
 * These structs are used to retrieve overall VAS capabilities that
 * the hypervisor provides.
 */
struct hv_vas_all_caps {
	__be64  descriptor;
	__be64  feat_type;
} __packed __aligned(0x1000);

struct vas_all_caps {
	char	name[VAS_DESCR_LEN + 1];
	u64     descriptor;
	u64     feat_type;
};

int plpar_vas_query_capabilities(const u64 hcall, u8 query_type,
				 u64 result);
int vas_register_api_pseries(struct module *mod,
			     enum vas_cop_type cop_type, const char *name);
void vas_unregister_api_pseries(void);
#endif

/*
 * Register / unregister coprocessor type to VAS API which will be exported
 * to user space. Applications can use this API to open / close window
 * which can be used to send / receive requests directly to cooprcessor.
 *
 * Only NX GZIP coprocessor type is supported now, but this API can be
 * used for others in future.
 */
int vas_register_coproc_api(struct module *mod, enum vas_cop_type cop_type,
			    const char *name,
			    struct vas_user_win_ops *vops);
void vas_unregister_coproc_api(void);

int vas_reference_pid_mm(struct vas_user_win_ref *task_ref);
void vas_update_csb(struct coprocessor_request_block *crb,
		    struct vas_user_win_ref *task_ref);
void vas_dump_crb(struct coprocessor_request_block *crb);
#endif /* __ASM_POWERPC_VAS_H */
