// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(C) 2016-2020 Intel Corporation. All rights reserved. */

#include "dlb_regs.h"
#include "dlb_main.h"

static void dlb_init_fn_rsrc_lists(struct dlb_function_resources *rsrc)
{
	int i;

	INIT_LIST_HEAD(&rsrc->avail_domains);
	INIT_LIST_HEAD(&rsrc->used_domains);
	INIT_LIST_HEAD(&rsrc->avail_ldb_queues);
	INIT_LIST_HEAD(&rsrc->avail_dir_pq_pairs);

	for (i = 0; i < DLB_NUM_COS_DOMAINS; i++)
		INIT_LIST_HEAD(&rsrc->avail_ldb_ports[i]);
}

static void dlb_init_domain_rsrc_lists(struct dlb_hw_domain *domain)
{
	int i;

	INIT_LIST_HEAD(&domain->used_ldb_queues);
	INIT_LIST_HEAD(&domain->used_dir_pq_pairs);
	INIT_LIST_HEAD(&domain->avail_ldb_queues);
	INIT_LIST_HEAD(&domain->avail_dir_pq_pairs);

	for (i = 0; i < DLB_NUM_COS_DOMAINS; i++)
		INIT_LIST_HEAD(&domain->used_ldb_ports[i]);
	for (i = 0; i < DLB_NUM_COS_DOMAINS; i++)
		INIT_LIST_HEAD(&domain->avail_ldb_ports[i]);
}

/**
 * dlb_resource_free() - free device state memory
 * @hw: dlb_hw handle for a particular device.
 *
 * This function frees software state pointed to by dlb_hw. This function
 * should be called when resetting the device or unloading the driver.
 */
void dlb_resource_free(struct dlb_hw *hw)
{
	int i;

	if (hw->pf.avail_hist_list_entries)
		dlb_bitmap_free(hw->pf.avail_hist_list_entries);

	for (i = 0; i < DLB_MAX_NUM_VDEVS; i++) {
		if (hw->vdev[i].avail_hist_list_entries)
			dlb_bitmap_free(hw->vdev[i].avail_hist_list_entries);
	}
}

/**
 * dlb_resource_init() - initialize the device
 * @hw: pointer to struct dlb_hw.
 *
 * This function initializes the device's software state (pointed to by the hw
 * argument) and programs global scheduling QoS registers. This function should
 * be called during driver initialization, and the dlb_hw structure should
 * be zero-initialized before calling the function.
 *
 * The dlb_hw struct must be unique per DLB 2.0 device and persist until the
 * device is reset.
 *
 * Return:
 * Returns 0 upon success, <0 otherwise.
 */
int dlb_resource_init(struct dlb_hw *hw)
{
	struct dlb_bitmap *map;
	struct list_head *list;
	unsigned int i;
	int ret;

	/*
	 * For optimal load-balancing, ports that map to one or more QIDs in
	 * common should not be in numerical sequence. The port->QID mapping is
	 * application dependent, but the driver interleaves port IDs as much
	 * as possible to reduce the likelihood of sequential ports mapping to
	 * the same QID(s). This initial allocation of port IDs maximizes the
	 * average distance between an ID and its immediate neighbors (i.e.
	 * the distance from 1 to 0 and to 2, the distance from 2 to 1 and to
	 * 3, etc.).
	 */
	const u8 init_ldb_port_allocation[DLB_MAX_NUM_LDB_PORTS] = {
		0,  7,  14,  5, 12,  3, 10,  1,  8, 15,  6, 13,  4, 11,  2,  9,
		16, 23, 30, 21, 28, 19, 26, 17, 24, 31, 22, 29, 20, 27, 18, 25,
		32, 39, 46, 37, 44, 35, 42, 33, 40, 47, 38, 45, 36, 43, 34, 41,
		48, 55, 62, 53, 60, 51, 58, 49, 56, 63, 54, 61, 52, 59, 50, 57,
	};

	dlb_init_fn_rsrc_lists(&hw->pf);

	for (i = 0; i < DLB_MAX_NUM_VDEVS; i++)
		dlb_init_fn_rsrc_lists(&hw->vdev[i]);

	for (i = 0; i < DLB_MAX_NUM_DOMAINS; i++) {
		dlb_init_domain_rsrc_lists(&hw->domains[i]);
		hw->domains[i].parent_func = &hw->pf;
	}

	/* Give all resources to the PF driver */
	hw->pf.num_avail_domains = DLB_MAX_NUM_DOMAINS;
	for (i = 0; i < hw->pf.num_avail_domains; i++) {
		list = &hw->domains[i].func_list;

		list_add(list, &hw->pf.avail_domains);
	}

	hw->pf.num_avail_ldb_queues = DLB_MAX_NUM_LDB_QUEUES;
	for (i = 0; i < hw->pf.num_avail_ldb_queues; i++) {
		list = &hw->rsrcs.ldb_queues[i].func_list;

		list_add(list, &hw->pf.avail_ldb_queues);
	}

	for (i = 0; i < DLB_NUM_COS_DOMAINS; i++)
		hw->pf.num_avail_ldb_ports[i] =
			DLB_MAX_NUM_LDB_PORTS / DLB_NUM_COS_DOMAINS;

	for (i = 0; i < DLB_MAX_NUM_LDB_PORTS; i++) {
		int cos_id = i >> DLB_NUM_COS_DOMAINS;
		struct dlb_ldb_port *port;

		port = &hw->rsrcs.ldb_ports[init_ldb_port_allocation[i]];

		list_add(&port->func_list, &hw->pf.avail_ldb_ports[cos_id]);
	}

	hw->pf.num_avail_dir_pq_pairs = DLB_MAX_NUM_DIR_PORTS;
	for (i = 0; i < hw->pf.num_avail_dir_pq_pairs; i++) {
		list = &hw->rsrcs.dir_pq_pairs[i].func_list;

		list_add(list, &hw->pf.avail_dir_pq_pairs);
	}

	hw->pf.num_avail_qed_entries = DLB_MAX_NUM_LDB_CREDITS;
	hw->pf.num_avail_dqed_entries = DLB_MAX_NUM_DIR_CREDITS;
	hw->pf.num_avail_aqed_entries = DLB_MAX_NUM_AQED_ENTRIES;

	ret = dlb_bitmap_alloc(&hw->pf.avail_hist_list_entries,
			       DLB_MAX_NUM_HIST_LIST_ENTRIES);
	if (ret)
		goto unwind;

	map = hw->pf.avail_hist_list_entries;
	bitmap_fill(map->map, map->len);

	for (i = 0; i < DLB_MAX_NUM_VDEVS; i++) {
		ret = dlb_bitmap_alloc(&hw->vdev[i].avail_hist_list_entries,
				       DLB_MAX_NUM_HIST_LIST_ENTRIES);
		if (ret)
			goto unwind;

		map = hw->vdev[i].avail_hist_list_entries;
		bitmap_zero(map->map, map->len);
	}

	/* Initialize the hardware resource IDs */
	for (i = 0; i < DLB_MAX_NUM_DOMAINS; i++)
		hw->domains[i].id = i;

	for (i = 0; i < DLB_MAX_NUM_LDB_QUEUES; i++)
		hw->rsrcs.ldb_queues[i].id = i;

	for (i = 0; i < DLB_MAX_NUM_LDB_PORTS; i++)
		hw->rsrcs.ldb_ports[i].id = i;

	for (i = 0; i < DLB_MAX_NUM_DIR_PORTS; i++)
		hw->rsrcs.dir_pq_pairs[i].id = i;

	for (i = 0; i < DLB_MAX_NUM_SEQUENCE_NUMBER_GROUPS; i++) {
		hw->rsrcs.sn_groups[i].id = i;
		/* Default mode (0) is 64 sequence numbers per queue */
		hw->rsrcs.sn_groups[i].mode = 0;
		hw->rsrcs.sn_groups[i].sequence_numbers_per_queue = 64;
		hw->rsrcs.sn_groups[i].slot_use_bitmap = 0;
	}

	for (i = 0; i < DLB_NUM_COS_DOMAINS; i++)
		hw->cos_reservation[i] = 100 / DLB_NUM_COS_DOMAINS;

	return 0;

unwind:
	dlb_resource_free(hw);

	return ret;
}

/**
 * dlb_clr_pmcsr_disable() - power on bulk of DLB 2.0 logic
 * @hw: dlb_hw handle for a particular device.
 *
 * Clearing the PMCSR must be done at initialization to make the device fully
 * operational.
 */
void dlb_clr_pmcsr_disable(struct dlb_hw *hw)
{
	u32 pmcsr_dis;

	pmcsr_dis = DLB_CSR_RD(hw, CM_CFG_PM_PMCSR_DISABLE);

	/* Clear register bits */
	pmcsr_dis &= ~CM_CFG_PM_PMCSR_DISABLE_DISABLE;

	DLB_CSR_WR(hw, CM_CFG_PM_PMCSR_DISABLE, pmcsr_dis);
}
