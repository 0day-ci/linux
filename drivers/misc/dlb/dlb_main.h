/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(C) 2016-2020 Intel Corporation. All rights reserved. */

#ifndef __DLB_MAIN_H
#define __DLB_MAIN_H

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/bitfield.h>

#include <uapi/linux/dlb.h>
#include "dlb_args.h"

/*
 * Hardware related #defines and data structures.
 *
 */
/* Read/write register 'reg' in the CSR BAR space */
#define DLB_CSR_REG_ADDR(a, reg)   ((a)->csr_kva + (reg))
#define DLB_CSR_RD(hw, reg)	    ioread32(DLB_CSR_REG_ADDR((hw), (reg)))
#define DLB_CSR_WR(hw, reg, value) iowrite32((value), \
					      DLB_CSR_REG_ADDR((hw), (reg)))

/* Read/write register 'reg' in the func BAR space */
#define DLB_FUNC_REG_ADDR(a, reg)   ((a)->func_kva + (reg))
#define DLB_FUNC_RD(hw, reg)	     ioread32(DLB_FUNC_REG_ADDR((hw), (reg)))
#define DLB_FUNC_WR(hw, reg, value) iowrite32((value), \
					       DLB_FUNC_REG_ADDR((hw), (reg)))

#define DLB_MAX_NUM_VDEVS			16
#define DLB_MAX_NUM_DOMAINS			32
#define DLB_MAX_NUM_LDB_QUEUES			32 /* LDB == load-balanced */
#define DLB_MAX_NUM_DIR_QUEUES			64 /* DIR == directed */
#define DLB_MAX_NUM_LDB_PORTS			64
#define DLB_MAX_NUM_DIR_PORTS			DLB_MAX_NUM_DIR_QUEUES
#define DLB_MAX_NUM_LDB_CREDITS			8192
#define DLB_MAX_NUM_DIR_CREDITS			2048
#define DLB_MAX_NUM_HIST_LIST_ENTRIES		2048
#define DLB_MAX_NUM_AQED_ENTRIES		2048
#define DLB_MAX_NUM_QIDS_PER_LDB_CQ		8
#define DLB_MAX_NUM_SEQUENCE_NUMBER_GROUPS	2
#define DLB_MAX_NUM_SEQUENCE_NUMBER_MODES	5
#define DLB_QID_PRIORITIES			8
#define DLB_NUM_ARB_WEIGHTS			8
#define DLB_MAX_WEIGHT				255
#define DLB_NUM_COS_DOMAINS			4
#define DLB_MAX_CQ_COMP_CHECK_LOOPS		409600
#define DLB_MAX_QID_EMPTY_CHECK_LOOPS		(32 * 64 * 1024 * (800 / 30))
#define DLB_HZ					800000000
#define DLB_FUNC_BAR				0
#define DLB_CSR_BAR				2

#define PCI_DEVICE_ID_INTEL_DLB_PF		0x2710

struct dlb_ldb_queue {
	struct list_head domain_list;
	struct list_head func_list;
	u32 id;
	u32 domain_id;
	u32 num_qid_inflights;
	u32 aqed_limit;
	u32 sn_group; /* sn == sequence number */
	u32 sn_slot;
	u32 num_mappings;
	u8 sn_cfg_valid;
	u8 num_pending_additions;
	u8 owned;
	u8 configured;
};

/*
 * Directed ports and queues are paired by nature, so the driver tracks them
 * with a single data structure.
 */
struct dlb_dir_pq_pair {
	struct list_head domain_list;
	struct list_head func_list;
	u32 id;
	u32 domain_id;
	u32 ref_cnt;
	u8 init_tkn_cnt;
	u8 queue_configured;
	u8 port_configured;
	u8 owned;
	u8 enabled;
};

enum dlb_qid_map_state {
	/* The slot doesn't contain a valid queue mapping */
	DLB_QUEUE_UNMAPPED,
	/* The slot contains a valid queue mapping */
	DLB_QUEUE_MAPPED,
	/* The driver is mapping a queue into this slot */
	DLB_QUEUE_MAP_IN_PROG,
	/* The driver is unmapping a queue from this slot */
	DLB_QUEUE_UNMAP_IN_PROG,
	/*
	 * The driver is unmapping a queue from this slot, and once complete
	 * will replace it with another mapping.
	 */
	DLB_QUEUE_UNMAP_IN_PROG_PENDING_MAP,
};

struct dlb_ldb_port_qid_map {
	enum dlb_qid_map_state state;
	u16 qid;
	u16 pending_qid;
	u8 priority;
	u8 pending_priority;
};

struct dlb_ldb_port {
	struct list_head domain_list;
	struct list_head func_list;
	u32 id;
	u32 domain_id;
	/* The qid_map represents the hardware QID mapping state. */
	struct dlb_ldb_port_qid_map qid_map[DLB_MAX_NUM_QIDS_PER_LDB_CQ];
	u32 hist_list_entry_base;
	u32 hist_list_entry_limit;
	u32 ref_cnt;
	u8 init_tkn_cnt;
	u8 num_pending_removals;
	u8 num_mappings;
	u8 owned;
	u8 enabled;
	u8 configured;
};

struct dlb_sn_group {
	u32 mode;
	u32 sequence_numbers_per_queue;
	u32 slot_use_bitmap;
	u32 id;
};

/*
 * Scheduling domain level resource data structure.
 *
 */
struct dlb_hw_domain {
	struct dlb_function_resources *parent_func;
	struct list_head func_list;
	struct list_head used_ldb_queues;
	struct list_head used_ldb_ports[DLB_NUM_COS_DOMAINS];
	struct list_head used_dir_pq_pairs;
	struct list_head avail_ldb_queues;
	struct list_head avail_ldb_ports[DLB_NUM_COS_DOMAINS];
	struct list_head avail_dir_pq_pairs;
	u32 total_hist_list_entries;
	u32 avail_hist_list_entries;
	u32 hist_list_entry_base;
	u32 hist_list_entry_offset;
	u32 num_ldb_credits;
	u32 num_dir_credits;
	u32 num_avail_aqed_entries;
	u32 num_used_aqed_entries;
	u32 id;
	int num_pending_removals;
	int num_pending_additions;
	u8 configured;
	u8 started;
};

/*
 * Device function (either PF or VF) level resource data structure.
 *
 */
struct dlb_function_resources {
	struct list_head avail_domains;
	struct list_head used_domains;
	struct list_head avail_ldb_queues;
	struct list_head avail_ldb_ports[DLB_NUM_COS_DOMAINS];
	struct list_head avail_dir_pq_pairs;
	struct dlb_bitmap *avail_hist_list_entries;
	u32 num_avail_domains;
	u32 num_avail_ldb_queues;
	u32 num_avail_ldb_ports[DLB_NUM_COS_DOMAINS];
	u32 num_avail_dir_pq_pairs;
	u32 num_avail_qed_entries;
	u32 num_avail_dqed_entries;
	u32 num_avail_aqed_entries;
	u8 locked; /* (VDEV only) */
};

/*
 * After initialization, each resource in dlb_hw_resources is located in one
 * of the following lists:
 * -- The PF's available resources list. These are unconfigured resources owned
 *	by the PF and not allocated to a dlb scheduling domain.
 * -- A VDEV's available resources list. These are VDEV-owned unconfigured
 *	resources not allocated to a dlb scheduling domain.
 * -- A domain's available resources list. These are domain-owned unconfigured
 *	resources.
 * -- A domain's used resources list. These are domain-owned configured
 *	resources.
 *
 * A resource moves to a new list when a VDEV or domain is created or destroyed,
 * or when the resource is configured.
 */
struct dlb_hw_resources {
	struct dlb_ldb_queue ldb_queues[DLB_MAX_NUM_LDB_QUEUES];
	struct dlb_ldb_port ldb_ports[DLB_MAX_NUM_LDB_PORTS];
	struct dlb_dir_pq_pair dir_pq_pairs[DLB_MAX_NUM_DIR_PORTS];
	struct dlb_sn_group sn_groups[DLB_MAX_NUM_SEQUENCE_NUMBER_GROUPS];
};

struct dlb_hw {
	/* BAR 0 address */
	void __iomem *csr_kva;
	unsigned long csr_phys_addr;
	/* BAR 2 address */
	void __iomem *func_kva;
	unsigned long func_phys_addr;

	/* Resource tracking */
	struct dlb_hw_resources rsrcs;
	struct dlb_function_resources pf;
	struct dlb_function_resources vdev[DLB_MAX_NUM_VDEVS];
	struct dlb_hw_domain domains[DLB_MAX_NUM_DOMAINS];
	u8 cos_reservation[DLB_NUM_COS_DOMAINS];
};

/*
 * The dlb driver uses a different minor number for each device file, of which
 * there are:
 * - 33 per device (PF or VF/VDEV): 1 for the device, 32 for scheduling domains
 * - Up to 17 devices per PF: 1 PF and up to 16 VFs/VDEVs
 * - Up to 16 PFs per system
 */
#define DLB_MAX_NUM_PFS	  16
#define DLB_NUM_FUNCS_PER_DEVICE (1 + DLB_MAX_NUM_VDEVS)
#define DLB_MAX_NUM_DEVICES	 (DLB_MAX_NUM_PFS * DLB_NUM_FUNCS_PER_DEVICE)

enum dlb_device_type {
	DLB_PF,
};

struct dlb;

int dlb_pf_map_pci_bar_space(struct dlb *dlb, struct pci_dev *pdev);
void dlb_pf_unmap_pci_bar_space(struct dlb *dlb, struct pci_dev *pdev);
int dlb_pf_init_driver_state(struct dlb *dlb);
void dlb_pf_enable_pm(struct dlb *dlb);
int dlb_pf_wait_for_device_ready(struct dlb *dlb, struct pci_dev *pdev);

extern const struct file_operations dlb_domain_fops;

struct dlb_domain {
	struct dlb *dlb;
	struct kref refcnt;
	u8 id;
};

struct dlb {
	struct pci_dev *pdev;
	struct dlb_hw hw;
	struct device *dev;
	struct dlb_domain *sched_domains[DLB_MAX_NUM_DOMAINS];
	struct file *f;
	/*
	 * The resource mutex serializes access to driver data structures and
	 * hardware registers.
	 */
	struct mutex resource_mutex;
	enum dlb_device_type type;
	int id;
	dev_t dev_number;
};

/*************************/
/*** Bitmap operations ***/
/*************************/
struct dlb_bitmap {
	unsigned long *map;
	unsigned int len;
};

/**
 * dlb_bitmap_alloc() - alloc a bitmap data structure
 * @bitmap: pointer to dlb_bitmap structure pointer.
 * @len: number of entries in the bitmap.
 *
 * This function allocates a bitmap and initializes it with length @len. All
 * entries are initially zero.
 *
 * Return:
 * Returns 0 upon success, < 0 otherwise.
 *
 * Errors:
 * EINVAL - bitmap is NULL or len is 0.
 * ENOMEM - could not allocate memory for the bitmap data structure.
 */
static inline int dlb_bitmap_alloc(struct dlb_bitmap **bitmap,
				   unsigned int len)
{
	struct dlb_bitmap *bm;

	if (!bitmap || len == 0)
		return -EINVAL;

	bm = kzalloc(sizeof(*bm), GFP_KERNEL);
	if (!bm)
		return -ENOMEM;

	bm->map = bitmap_zalloc(len, GFP_KERNEL);
	if (!bm->map) {
		kfree(bm);
		return -ENOMEM;
	}

	bm->len = len;

	*bitmap = bm;

	return 0;
}

/**
 * dlb_bitmap_free() - free a previously allocated bitmap data structure
 * @bitmap: pointer to dlb_bitmap structure.
 *
 * This function frees a bitmap that was allocated with dlb_bitmap_alloc().
 */
static inline void dlb_bitmap_free(struct dlb_bitmap *bitmap)
{
	if (!bitmap)
		return;

	bitmap_free(bitmap->map);

	kfree(bitmap);
}

/**
 * dlb_bitmap_clear_range() - clear a range of bitmap entries
 * @bitmap: pointer to dlb_bitmap structure.
 * @bit: starting bit index.
 * @len: length of the range.
 *
 * Return:
 * Returns 0 upon success, < 0 otherwise.
 *
 * Errors:
 * EINVAL - bitmap is NULL or is uninitialized, or the range exceeds the bitmap
 *	    length.
 */
static inline int dlb_bitmap_clear_range(struct dlb_bitmap *bitmap,
					 unsigned int bit,
					 unsigned int len)
{
	if (!bitmap || !bitmap->map)
		return -EINVAL;

	if (bitmap->len <= bit)
		return -EINVAL;

	bitmap_clear(bitmap->map, bit, len);

	return 0;
}

/**
 * dlb_bitmap_find_set_bit_range() - find an range of set bits
 * @bitmap: pointer to dlb_bitmap structure.
 * @len: length of the range.
 *
 * This function looks for a range of set bits of length @len.
 *
 * Return:
 * Returns the base bit index upon success, < 0 otherwise.
 *
 * Errors:
 * ENOENT - unable to find a length *len* range of set bits.
 * EINVAL - bitmap is NULL or is uninitialized, or len is invalid.
 */
static inline int dlb_bitmap_find_set_bit_range(struct dlb_bitmap *bitmap,
						unsigned int len)
{
	struct dlb_bitmap *complement_mask = NULL;
	int ret;

	if (!bitmap || !bitmap->map || len == 0)
		return -EINVAL;

	if (bitmap->len < len)
		return -ENOENT;

	ret = dlb_bitmap_alloc(&complement_mask, bitmap->len);
	if (ret)
		return ret;

	bitmap_zero(complement_mask->map, complement_mask->len);

	bitmap_complement(complement_mask->map, bitmap->map, bitmap->len);

	ret = bitmap_find_next_zero_area(complement_mask->map,
					 complement_mask->len,
					 0,
					 len,
					 0);

	dlb_bitmap_free(complement_mask);

	/* No set bit range of length len? */
	return (ret >= (int)bitmap->len) ? -ENOENT : ret;
}

/**
 * dlb_bitmap_longest_set_range() - returns longest contiguous range of set
 *				     bits
 * @bitmap: pointer to dlb_bitmap structure.
 *
 * Return:
 * Returns the bitmap's longest contiguous range of set bits upon success,
 * <0 otherwise.
 *
 * Errors:
 * EINVAL - bitmap is NULL or is uninitialized.
 */
static inline int dlb_bitmap_longest_set_range(struct dlb_bitmap *bitmap)
{
	int max_len, len;
	int start, end;

	if (!bitmap || !bitmap->map)
		return -EINVAL;

	if (bitmap_weight(bitmap->map, bitmap->len) == 0)
		return 0;

	max_len = 0;
	bitmap_for_each_set_region(bitmap->map, start, end, 0, bitmap->len) {
		len = end - start;
		if (max_len < len)
			max_len = len;
	}
	return max_len;
}

int dlb_init_domain(struct dlb *dlb, u32 domain_id);
void dlb_free_domain(struct kref *kref);

static inline struct device *hw_to_dev(struct dlb_hw *hw)
{
	struct dlb *dlb;

	dlb = container_of(hw, struct dlb, hw);
	return dlb->dev;
}

/* Prototypes for dlb_resource.c */
int dlb_resource_init(struct dlb_hw *hw);
void dlb_resource_free(struct dlb_hw *hw);
int dlb_hw_create_sched_domain(struct dlb_hw *hw,
			       struct dlb_create_sched_domain_args *args,
			       struct dlb_cmd_response *resp);
void dlb_clr_pmcsr_disable(struct dlb_hw *hw);

/* Prototypes for dlb_configfs.c */
int dlb_configfs_create_device(struct dlb *dlb);
int configfs_dlb_init(void);
void configfs_dlb_exit(void);

#endif /* __DLB_MAIN_H */
