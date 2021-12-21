// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(C) 2016-2020 Intel Corporation. All rights reserved. */

#include <linux/log2.h>
#include "dlb_regs.h"
#include "dlb_main.h"

/*
 * The PF driver cannot assume that a register write will affect subsequent HCW
 * writes. To ensure a write completes, the driver must read back a CSR. This
 * function only need be called for configuration that can occur after the
 * domain has started; prior to starting, applications can't send HCWs.
 */
static inline void dlb_flush_csr(struct dlb_hw *hw)
{
	DLB_CSR_RD(hw, SYS_TOTAL_VAS);
}

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

static struct dlb_hw_domain *dlb_get_domain_from_id(struct dlb_hw *hw, u32 id)
{
	if (id >= DLB_MAX_NUM_DOMAINS)
		return NULL;

	return &hw->domains[id];
}

static struct dlb_dir_pq_pair *
dlb_get_domain_used_dir_pq(u32 id, bool vdev_req, struct dlb_hw_domain *domain)
{
	struct dlb_dir_pq_pair *port;

	if (id >= DLB_MAX_NUM_DIR_PORTS)
		return NULL;

	list_for_each_entry(port, &domain->used_dir_pq_pairs, domain_list) {
		if (!vdev_req && port->id == id)
			return port;
	}

	return NULL;
}

static struct dlb_ldb_queue *
dlb_get_domain_ldb_queue(u32 id, bool vdev_req, struct dlb_hw_domain *domain)
{
	struct dlb_ldb_queue *queue;

	if (id >= DLB_MAX_NUM_LDB_QUEUES)
		return NULL;

	list_for_each_entry(queue, &domain->used_ldb_queues, domain_list) {
		if (!vdev_req && queue->id == id)
			return queue;
	}

	return NULL;
}

static int dlb_attach_ldb_queues(struct dlb_hw *hw,
				 struct dlb_function_resources *rsrcs,
				 struct dlb_hw_domain *domain, u32 num_queues,
				 struct dlb_cmd_response *resp)
{
	unsigned int i;

	if (rsrcs->num_avail_ldb_queues < num_queues) {
		resp->status = DLB_ST_LDB_QUEUES_UNAVAILABLE;
		dev_dbg(hw_to_dev(hw), "[%s()] Internal error: %d\n", __func__,
			resp->status);
		return -EINVAL;
	}

	for (i = 0; i < num_queues; i++) {
		struct dlb_ldb_queue *queue;

		queue = list_first_entry_or_null(&rsrcs->avail_ldb_queues,
						 typeof(*queue), func_list);
		if (!queue) {
			dev_err(hw_to_dev(hw),
				"[%s()] Internal error: domain validation failed\n",
				__func__);
			return -EFAULT;
		}

		list_del(&queue->func_list);

		queue->domain_id = domain->id;
		queue->owned = true;

		list_add(&queue->domain_list, &domain->avail_ldb_queues);
	}

	rsrcs->num_avail_ldb_queues -= num_queues;

	return 0;
}

static struct dlb_ldb_port *
dlb_get_next_ldb_port(struct dlb_hw *hw, struct dlb_function_resources *rsrcs,
		      u32 domain_id, u32 cos_id)
{
	struct dlb_ldb_port *port;

	/*
	 * To reduce the odds of consecutive load-balanced ports mapping to the
	 * same queue(s), the driver attempts to allocate ports whose neighbors
	 * are owned by a different domain.
	 */
	list_for_each_entry(port, &rsrcs->avail_ldb_ports[cos_id], func_list) {
		u32 next, prev;
		u32 phys_id;

		phys_id = port->id;
		next = phys_id + 1;
		prev = phys_id - 1;

		if (phys_id == DLB_MAX_NUM_LDB_PORTS - 1)
			next = 0;
		if (phys_id == 0)
			prev = DLB_MAX_NUM_LDB_PORTS - 1;

		if (!hw->rsrcs.ldb_ports[next].owned ||
		    hw->rsrcs.ldb_ports[next].domain_id == domain_id)
			continue;

		if (!hw->rsrcs.ldb_ports[prev].owned ||
		    hw->rsrcs.ldb_ports[prev].domain_id == domain_id)
			continue;

		return port;
	}

	/*
	 * Failing that, the driver looks for a port with one neighbor owned by
	 * a different domain and the other unallocated.
	 */
	list_for_each_entry(port, &rsrcs->avail_ldb_ports[cos_id], func_list) {
		u32 next, prev;
		u32 phys_id;

		phys_id = port->id;
		next = phys_id + 1;
		prev = phys_id - 1;

		if (phys_id == DLB_MAX_NUM_LDB_PORTS - 1)
			next = 0;
		if (phys_id == 0)
			prev = DLB_MAX_NUM_LDB_PORTS - 1;

		if (!hw->rsrcs.ldb_ports[prev].owned &&
		    hw->rsrcs.ldb_ports[next].owned &&
		    hw->rsrcs.ldb_ports[next].domain_id != domain_id)
			return port;

		if (!hw->rsrcs.ldb_ports[next].owned &&
		    hw->rsrcs.ldb_ports[prev].owned &&
		    hw->rsrcs.ldb_ports[prev].domain_id != domain_id)
			return port;
	}

	/*
	 * Failing that, the driver looks for a port with both neighbors
	 * unallocated.
	 */
	list_for_each_entry(port, &rsrcs->avail_ldb_ports[cos_id], func_list) {
		u32 next, prev;
		u32 phys_id;

		phys_id = port->id;
		next = phys_id + 1;
		prev = phys_id - 1;

		if (phys_id == DLB_MAX_NUM_LDB_PORTS - 1)
			next = 0;
		if (phys_id == 0)
			prev = DLB_MAX_NUM_LDB_PORTS - 1;

		if (!hw->rsrcs.ldb_ports[prev].owned &&
		    !hw->rsrcs.ldb_ports[next].owned)
			return port;
	}

	/* If all else fails, the driver returns the next available port. */
	return list_first_entry_or_null(&rsrcs->avail_ldb_ports[cos_id],
					typeof(*port), func_list);
}

static int __dlb_attach_ldb_ports(struct dlb_hw *hw,
				  struct dlb_function_resources *rsrcs,
				  struct dlb_hw_domain *domain, u32 num_ports,
				  u32 cos_id, struct dlb_cmd_response *resp)
{
	unsigned int i;

	if (rsrcs->num_avail_ldb_ports[cos_id] < num_ports) {
		resp->status = DLB_ST_LDB_PORTS_UNAVAILABLE;
		dev_dbg(hw_to_dev(hw),
			"[%s()] Internal error: %d\n",
			__func__, resp->status);
		return -EINVAL;
	}

	for (i = 0; i < num_ports; i++) {
		struct dlb_ldb_port *port;

		port = dlb_get_next_ldb_port(hw, rsrcs,
					     domain->id, cos_id);
		if (!port) {
			dev_err(hw_to_dev(hw),
				"[%s()] Internal error: domain validation failed\n",
				__func__);
			return -EFAULT;
		}

		list_del(&port->func_list);

		port->domain_id = domain->id;
		port->owned = true;

		list_add(&port->domain_list,
			 &domain->avail_ldb_ports[cos_id]);
	}

	rsrcs->num_avail_ldb_ports[cos_id] -= num_ports;

	return 0;
}

static int dlb_attach_ldb_ports(struct dlb_hw *hw,
				struct dlb_function_resources *rsrcs,
				struct dlb_hw_domain *domain,
				struct dlb_create_sched_domain_args *args,
				struct dlb_cmd_response *resp)
{
	unsigned int i, j;
	int ret;

	/* Allocate num_ldb_ports from any class-of-service */
	for (i = 0; i < args->num_ldb_ports; i++) {
		for (j = 0; j < DLB_NUM_COS_DOMAINS; j++) {
			ret = __dlb_attach_ldb_ports(hw, rsrcs, domain, 1, j, resp);
			if (ret == 0)
				break;
		}

		if (ret)
			return ret;
	}

	return 0;
}

static int dlb_attach_dir_ports(struct dlb_hw *hw,
				struct dlb_function_resources *rsrcs,
				struct dlb_hw_domain *domain, u32 num_ports,
				struct dlb_cmd_response *resp)
{
	unsigned int i;

	if (rsrcs->num_avail_dir_pq_pairs < num_ports) {
		resp->status = DLB_ST_DIR_PORTS_UNAVAILABLE;
		dev_dbg(hw_to_dev(hw),
			"[%s()] Internal error: %d\n",
			__func__, resp->status);
		return -EINVAL;
	}

	for (i = 0; i < num_ports; i++) {
		struct dlb_dir_pq_pair *port;

		port = list_first_entry_or_null(&rsrcs->avail_dir_pq_pairs,
						typeof(*port), func_list);
		if (!port) {
			dev_err(hw_to_dev(hw),
				"[%s()] Internal error: domain validation failed\n",
				__func__);
			return -EFAULT;
		}

		list_del(&port->func_list);

		port->domain_id = domain->id;
		port->owned = true;

		list_add(&port->domain_list, &domain->avail_dir_pq_pairs);
	}

	rsrcs->num_avail_dir_pq_pairs -= num_ports;

	return 0;
}

static int dlb_attach_ldb_credits(struct dlb_function_resources *rsrcs,
				  struct dlb_hw_domain *domain, u32 num_credits,
				  struct dlb_cmd_response *resp)
{
	if (rsrcs->num_avail_qed_entries < num_credits) {
		resp->status = DLB_ST_LDB_CREDITS_UNAVAILABLE;
		return -EINVAL;
	}

	rsrcs->num_avail_qed_entries -= num_credits;
	domain->num_ldb_credits += num_credits;
	return 0;
}

static int dlb_attach_dir_credits(struct dlb_function_resources *rsrcs,
				  struct dlb_hw_domain *domain, u32 num_credits,
				  struct dlb_cmd_response *resp)
{
	if (rsrcs->num_avail_dqed_entries < num_credits) {
		resp->status = DLB_ST_DIR_CREDITS_UNAVAILABLE;
		return -EINVAL;
	}

	rsrcs->num_avail_dqed_entries -= num_credits;
	domain->num_dir_credits += num_credits;
	return 0;
}

static int dlb_attach_atomic_inflights(struct dlb_function_resources *rsrcs,
				       struct dlb_hw_domain *domain,
				       u32 num_atomic_inflights,
				       struct dlb_cmd_response *resp)
{
	if (rsrcs->num_avail_aqed_entries < num_atomic_inflights) {
		resp->status = DLB_ST_ATOMIC_INFLIGHTS_UNAVAILABLE;
		return -EINVAL;
	}

	rsrcs->num_avail_aqed_entries -= num_atomic_inflights;
	domain->num_avail_aqed_entries += num_atomic_inflights;
	return 0;
}

static int
dlb_attach_domain_hist_list_entries(struct dlb_function_resources *rsrcs,
				    struct dlb_hw_domain *domain,
				    u32 num_hist_list_entries,
				    struct dlb_cmd_response *resp)
{
	struct dlb_bitmap *bitmap;
	int base;

	if (num_hist_list_entries) {
		bitmap = rsrcs->avail_hist_list_entries;

		base = dlb_bitmap_find_set_bit_range(bitmap,
						     num_hist_list_entries);
		if (base < 0)
			goto error;

		domain->total_hist_list_entries = num_hist_list_entries;
		domain->avail_hist_list_entries = num_hist_list_entries;
		domain->hist_list_entry_base = base;
		domain->hist_list_entry_offset = 0;

		dlb_bitmap_clear_range(bitmap, base, num_hist_list_entries);
	}
	return 0;

error:
	resp->status = DLB_ST_HIST_LIST_ENTRIES_UNAVAILABLE;
	return -EINVAL;
}

static int
dlb_verify_create_sched_dom_args(struct dlb_function_resources *rsrcs,
				 struct dlb_create_sched_domain_args *args,
				 struct dlb_cmd_response *resp,
				 struct dlb_hw_domain **out_domain)
{
	u32 num_avail_ldb_ports, req_ldb_ports;
	struct dlb_bitmap *avail_hl_entries;
	unsigned int max_contig_hl_range;
	struct dlb_hw_domain *domain;
	int i;

	avail_hl_entries = rsrcs->avail_hist_list_entries;

	max_contig_hl_range = dlb_bitmap_longest_set_range(avail_hl_entries);

	num_avail_ldb_ports = 0;
	req_ldb_ports = 0;
	for (i = 0; i < DLB_NUM_COS_DOMAINS; i++)
		num_avail_ldb_ports += rsrcs->num_avail_ldb_ports[i];

	req_ldb_ports += args->num_ldb_ports;

	if (rsrcs->num_avail_domains < 1) {
		resp->status = DLB_ST_DOMAIN_UNAVAILABLE;
		return -EINVAL;
	}

	domain = list_first_entry_or_null(&rsrcs->avail_domains,
					  typeof(*domain), func_list);
	if (!domain) {
		resp->status = DLB_ST_DOMAIN_UNAVAILABLE;
		return -EFAULT;
	}

	if (rsrcs->num_avail_ldb_queues < args->num_ldb_queues) {
		resp->status = DLB_ST_LDB_QUEUES_UNAVAILABLE;
		return -EINVAL;
	}

	if (req_ldb_ports > num_avail_ldb_ports) {
		resp->status = DLB_ST_LDB_PORTS_UNAVAILABLE;
		return -EINVAL;
	}

	if (args->num_ldb_queues > 0 && req_ldb_ports == 0) {
		resp->status = DLB_ST_LDB_PORT_REQUIRED_FOR_LDB_QUEUES;
		return -EINVAL;
	}

	if (rsrcs->num_avail_dir_pq_pairs < args->num_dir_ports) {
		resp->status = DLB_ST_DIR_PORTS_UNAVAILABLE;
		return -EINVAL;
	}

	if (rsrcs->num_avail_qed_entries < args->num_ldb_credits) {
		resp->status = DLB_ST_LDB_CREDITS_UNAVAILABLE;
		return -EINVAL;
	}

	if (rsrcs->num_avail_dqed_entries < args->num_dir_credits) {
		resp->status = DLB_ST_DIR_CREDITS_UNAVAILABLE;
		return -EINVAL;
	}

	if (rsrcs->num_avail_aqed_entries < args->num_atomic_inflights) {
		resp->status = DLB_ST_ATOMIC_INFLIGHTS_UNAVAILABLE;
		return -EINVAL;
	}

	if (max_contig_hl_range < args->num_hist_list_entries) {
		resp->status = DLB_ST_HIST_LIST_ENTRIES_UNAVAILABLE;
		return -EINVAL;
	}

	*out_domain = domain;

	return 0;
}

static int
dlb_verify_create_ldb_queue_args(struct dlb_hw *hw, u32 domain_id,
				 struct dlb_create_ldb_queue_args *args,
				 struct dlb_cmd_response *resp,
				 struct dlb_hw_domain **out_domain,
				 struct dlb_ldb_queue **out_queue)
{
	struct dlb_hw_domain *domain;
	struct dlb_ldb_queue *queue;
	int i;

	domain = dlb_get_domain_from_id(hw, domain_id);

	if (!domain) {
		resp->status = DLB_ST_INVALID_DOMAIN_ID;
		return -EINVAL;
	}

	if (!domain->configured) {
		resp->status = DLB_ST_DOMAIN_NOT_CONFIGURED;
		return -EINVAL;
	}

	if (domain->started) {
		resp->status = DLB_ST_DOMAIN_STARTED;
		return -EINVAL;
	}

	queue = list_first_entry_or_null(&domain->avail_ldb_queues,
					 typeof(*queue), domain_list);
	if (!queue) {
		resp->status = DLB_ST_LDB_QUEUES_UNAVAILABLE;
		return -EINVAL;
	}

	if (args->num_sequence_numbers) {
		for (i = 0; i < DLB_MAX_NUM_SEQUENCE_NUMBER_GROUPS; i++) {
			struct dlb_sn_group *group = &hw->rsrcs.sn_groups[i];

			if (group->sequence_numbers_per_queue ==
			    args->num_sequence_numbers &&
			    !dlb_sn_group_full(group))
				break;
		}

		if (i == DLB_MAX_NUM_SEQUENCE_NUMBER_GROUPS) {
			resp->status = DLB_ST_SEQUENCE_NUMBERS_UNAVAILABLE;
			return -EINVAL;
		}
	}

	if (args->num_qid_inflights > 4096) {
		resp->status = DLB_ST_INVALID_QID_INFLIGHT_ALLOCATION;
		return -EINVAL;
	}

	/* Inflights must be <= number of sequence numbers if ordered */
	if (args->num_sequence_numbers != 0 &&
	    args->num_qid_inflights > args->num_sequence_numbers) {
		resp->status = DLB_ST_INVALID_QID_INFLIGHT_ALLOCATION;
		return -EINVAL;
	}

	if (domain->num_avail_aqed_entries < args->num_atomic_inflights) {
		resp->status = DLB_ST_ATOMIC_INFLIGHTS_UNAVAILABLE;
		return -EINVAL;
	}

	if (args->num_atomic_inflights &&
	    args->lock_id_comp_level != 0 &&
	    args->lock_id_comp_level != 64 &&
	    args->lock_id_comp_level != 128 &&
	    args->lock_id_comp_level != 256 &&
	    args->lock_id_comp_level != 512 &&
	    args->lock_id_comp_level != 1024 &&
	    args->lock_id_comp_level != 2048 &&
	    args->lock_id_comp_level != 4096 &&
	    args->lock_id_comp_level != 65536) {
		resp->status = DLB_ST_INVALID_LOCK_ID_COMP_LEVEL;
		return -EINVAL;
	}

	*out_domain = domain;
	*out_queue = queue;

	return 0;
}

static int
dlb_verify_create_dir_queue_args(struct dlb_hw *hw, u32 domain_id,
				 struct dlb_create_dir_queue_args *args,
				 struct dlb_cmd_response *resp,
				 struct dlb_hw_domain **out_domain,
				 struct dlb_dir_pq_pair **out_queue)
{
	struct dlb_hw_domain *domain;
	struct dlb_dir_pq_pair *pq;

	domain = dlb_get_domain_from_id(hw, domain_id);

	if (!domain) {
		resp->status = DLB_ST_INVALID_DOMAIN_ID;
		return -EINVAL;
	}

	if (!domain->configured) {
		resp->status = DLB_ST_DOMAIN_NOT_CONFIGURED;
		return -EINVAL;
	}

	if (domain->started) {
		resp->status = DLB_ST_DOMAIN_STARTED;
		return -EINVAL;
	}

	/*
	 * If the user claims the port is already configured, validate the port
	 * ID, its domain, and whether the port is configured.
	 */
	if (args->port_id != -1) {
		pq = dlb_get_domain_used_dir_pq(args->port_id,
						false,
						domain);

		if (!pq || pq->domain_id != domain->id ||
		    !pq->port_configured) {
			resp->status = DLB_ST_INVALID_PORT_ID;
			return -EINVAL;
		}
	} else {
		/*
		 * If the queue's port is not configured, validate that a free
		 * port-queue pair is available.
		 */
		pq = list_first_entry_or_null(&domain->avail_dir_pq_pairs,
					      typeof(*pq), domain_list);
		if (!pq) {
			resp->status = DLB_ST_DIR_QUEUES_UNAVAILABLE;
			return -EINVAL;
		}
	}

	*out_domain = domain;
	*out_queue = pq;

	return 0;
}

static void dlb_configure_ldb_queue(struct dlb_hw *hw,
				    struct dlb_hw_domain *domain,
				    struct dlb_ldb_queue *queue,
				    struct dlb_create_ldb_queue_args *args)
{
	struct dlb_sn_group *sn_group;
	unsigned int offs;
	u32 reg = 0;
	u32 alimit;
	u32 level;

	/* QID write permissions are turned on when the domain is started */
	offs = domain->id * DLB_MAX_NUM_LDB_QUEUES + queue->id;

	DLB_CSR_WR(hw, SYS_LDB_VASQID_V(offs), reg);

	/*
	 * Unordered QIDs get 4K inflights, ordered get as many as the number
	 * of sequence numbers.
	 */
	reg = FIELD_PREP(LSP_QID_LDB_INFL_LIM_LIMIT, args->num_qid_inflights);
	DLB_CSR_WR(hw, LSP_QID_LDB_INFL_LIM(queue->id), reg);

	alimit = queue->aqed_limit;

	if (alimit > DLB_MAX_NUM_AQED_ENTRIES)
		alimit = DLB_MAX_NUM_AQED_ENTRIES;

	reg = FIELD_PREP(LSP_QID_AQED_ACTIVE_LIM_LIMIT, alimit);
	DLB_CSR_WR(hw, LSP_QID_AQED_ACTIVE_LIM(queue->id), reg);

	level = args->lock_id_comp_level;
	if (level >= 64 && level <= 4096 && is_power_of_2(level)) {
		reg &= ~AQED_QID_HID_WIDTH_COMPRESS_CODE;
		reg |= FIELD_PREP(AQED_QID_HID_WIDTH_COMPRESS_CODE, ilog2(level) - 5);
	} else {
		reg = 0;
	}

	DLB_CSR_WR(hw, AQED_QID_HID_WIDTH(queue->id), reg);

	reg = 0;
	/* Don't timestamp QEs that pass through this queue */
	DLB_CSR_WR(hw, SYS_LDB_QID_ITS(queue->id), reg);

	reg = FIELD_PREP(LSP_QID_ATM_DEPTH_THRSH_THRESH, args->depth_threshold);
	DLB_CSR_WR(hw, LSP_QID_ATM_DEPTH_THRSH(queue->id), reg);

	reg = FIELD_PREP(LSP_QID_NALDB_DEPTH_THRSH_THRESH, args->depth_threshold);
	DLB_CSR_WR(hw, LSP_QID_NALDB_DEPTH_THRSH(queue->id), reg);

	/*
	 * This register limits the number of inflight flows a queue can have
	 * at one time.  It has an upper bound of 2048, but can be
	 * over-subscribed. 512 is chosen so that a single queue doesn't use
	 * the entire atomic storage, but can use a substantial portion if
	 * needed.
	 */
	reg = FIELD_PREP(AQED_QID_FID_LIM_QID_FID_LIMIT, 512);
	DLB_CSR_WR(hw, AQED_QID_FID_LIM(queue->id), reg);

	/* Configure SNs */
	sn_group = &hw->rsrcs.sn_groups[queue->sn_group];
	reg = FIELD_PREP(CHP_ORD_QID_SN_MAP_MODE, sn_group->mode);
	reg |= FIELD_PREP(CHP_ORD_QID_SN_MAP_SLOT, queue->sn_slot);
	reg |= FIELD_PREP(CHP_ORD_QID_SN_MAP_GRP, sn_group->id);

	DLB_CSR_WR(hw, CHP_ORD_QID_SN_MAP(queue->id), reg);

	reg = FIELD_PREP(SYS_LDB_QID_CFG_V_SN_CFG_V,
		  (u32)(args->num_sequence_numbers != 0));
	reg |= FIELD_PREP(SYS_LDB_QID_CFG_V_FID_CFG_V,
		  (u32)(args->num_atomic_inflights != 0));

	DLB_CSR_WR(hw, SYS_LDB_QID_CFG_V(queue->id), reg);

	reg = SYS_LDB_QID_V_QID_V;
	DLB_CSR_WR(hw, SYS_LDB_QID_V(queue->id), reg);
}

static void dlb_configure_dir_queue(struct dlb_hw *hw,
				    struct dlb_hw_domain *domain,
				    struct dlb_dir_pq_pair *queue,
				    struct dlb_create_dir_queue_args *args)
{
	unsigned int offs;
	u32 reg = 0;

	/* QID write permissions are turned on when the domain is started */
	offs = domain->id * DLB_MAX_NUM_DIR_QUEUES +
		queue->id;

	DLB_CSR_WR(hw, SYS_DIR_VASQID_V(offs), reg);

	/* Don't timestamp QEs that pass through this queue */
	DLB_CSR_WR(hw, SYS_DIR_QID_ITS(queue->id), reg);

	reg = FIELD_PREP(LSP_QID_DIR_DEPTH_THRSH_THRESH, args->depth_threshold);
	DLB_CSR_WR(hw, LSP_QID_DIR_DEPTH_THRSH(queue->id), reg);

	reg = SYS_DIR_QID_V_QID_V;
	DLB_CSR_WR(hw, SYS_DIR_QID_V(queue->id), reg);

	queue->queue_configured = true;
}

static bool
dlb_cq_depth_is_valid(u32 depth)
{
	/* Valid values for depth are
	 * 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, and 1024.
	 */
	if (!is_power_of_2(depth) || depth > 1024)
		return false;

	return true;
}

static int
dlb_verify_create_ldb_port_args(struct dlb_hw *hw, u32 domain_id,
				uintptr_t cq_dma_base,
				struct dlb_create_ldb_port_args *args,
				struct dlb_cmd_response *resp,
				struct dlb_hw_domain **out_domain,
				struct dlb_ldb_port **out_port, int *out_cos_id)
{
	struct dlb_ldb_port *port = NULL;
	struct dlb_hw_domain *domain;
	int i, id;

	domain = dlb_get_domain_from_id(hw, domain_id);

	if (!domain) {
		resp->status = DLB_ST_INVALID_DOMAIN_ID;
		return -EINVAL;
	}

	if (!domain->configured) {
		resp->status = DLB_ST_DOMAIN_NOT_CONFIGURED;
		return -EINVAL;
	}

	if (domain->started) {
		resp->status = DLB_ST_DOMAIN_STARTED;
		return -EINVAL;
	}

	for (i = 0; i < DLB_NUM_COS_DOMAINS; i++) {
		id = i % DLB_NUM_COS_DOMAINS;

		port = list_first_entry_or_null(&domain->avail_ldb_ports[id],
						typeof(*port), domain_list);
		if (port)
			break;
	}

	if (!port) {
		resp->status = DLB_ST_LDB_PORTS_UNAVAILABLE;
		return -EINVAL;
	}

	/* DLB requires 64B alignment */
	if (!IS_ALIGNED(cq_dma_base, 64)) {
		resp->status = DLB_ST_INVALID_CQ_VIRT_ADDR;
		return -EINVAL;
	}

	if (!dlb_cq_depth_is_valid(args->cq_depth)) {
		resp->status = DLB_ST_INVALID_CQ_DEPTH;
		return -EINVAL;
	}

	/* The history list size must be >= 1 */
	if (!args->cq_history_list_size) {
		resp->status = DLB_ST_INVALID_HIST_LIST_DEPTH;
		return -EINVAL;
	}

	if (args->cq_history_list_size > domain->avail_hist_list_entries) {
		resp->status = DLB_ST_HIST_LIST_ENTRIES_UNAVAILABLE;
		return -EINVAL;
	}

	*out_domain = domain;
	*out_port = port;
	*out_cos_id = id;

	return 0;
}

static int
dlb_verify_create_dir_port_args(struct dlb_hw *hw, u32 domain_id,
				uintptr_t cq_dma_base,
				struct dlb_create_dir_port_args *args,
				struct dlb_cmd_response *resp,
				struct dlb_hw_domain **out_domain,
				struct dlb_dir_pq_pair **out_port)
{
	struct dlb_hw_domain *domain;
	struct dlb_dir_pq_pair *pq;

	domain = dlb_get_domain_from_id(hw, domain_id);

	if (!domain) {
		resp->status = DLB_ST_INVALID_DOMAIN_ID;
		return -EINVAL;
	}

	if (!domain->configured) {
		resp->status = DLB_ST_DOMAIN_NOT_CONFIGURED;
		return -EINVAL;
	}

	if (domain->started) {
		resp->status = DLB_ST_DOMAIN_STARTED;
		return -EINVAL;
	}

	if (args->queue_id != -1) {
		/*
		 * If the user claims the queue is already configured, validate
		 * the queue ID, its domain, and whether the queue is
		 * configured.
		 */
		pq = dlb_get_domain_used_dir_pq(args->queue_id,
						false,
						domain);

		if (!pq || pq->domain_id != domain->id ||
		    !pq->queue_configured) {
			resp->status = DLB_ST_INVALID_DIR_QUEUE_ID;
			return -EINVAL;
		}
	} else {
		/*
		 * If the port's queue is not configured, validate that a free
		 * port-queue pair is available.
		 */
		pq = list_first_entry_or_null(&domain->avail_dir_pq_pairs,
					      typeof(*pq), domain_list);
		if (!pq) {
			resp->status = DLB_ST_DIR_PORTS_UNAVAILABLE;
			return -EINVAL;
		}
	}

	/* DLB requires 64B alignment */
	if (!IS_ALIGNED(cq_dma_base, 64)) {
		resp->status = DLB_ST_INVALID_CQ_VIRT_ADDR;
		return -EINVAL;
	}

	if (!dlb_cq_depth_is_valid(args->cq_depth)) {
		resp->status = DLB_ST_INVALID_CQ_DEPTH;
		return -EINVAL;
	}

	*out_domain = domain;
	*out_port = pq;

	return 0;
}

static void dlb_configure_domain_credits(struct dlb_hw *hw,
					 struct dlb_hw_domain *domain)
{
	u32 reg;

	reg = FIELD_PREP(CHP_CFG_LDB_VAS_CRD_COUNT, domain->num_ldb_credits);
	DLB_CSR_WR(hw, CHP_CFG_LDB_VAS_CRD(domain->id), reg);

	reg = FIELD_PREP(CHP_CFG_DIR_VAS_CRD_COUNT, domain->num_dir_credits);
	DLB_CSR_WR(hw, CHP_CFG_DIR_VAS_CRD(domain->id), reg);
}

static int
dlb_domain_attach_resources(struct dlb_hw *hw,
			    struct dlb_function_resources *rsrcs,
			    struct dlb_hw_domain *domain,
			    struct dlb_create_sched_domain_args *args,
			    struct dlb_cmd_response *resp)
{
	int ret;

	ret = dlb_attach_ldb_queues(hw, rsrcs, domain, args->num_ldb_queues, resp);
	if (ret)
		return ret;

	ret = dlb_attach_ldb_ports(hw, rsrcs, domain, args, resp);
	if (ret)
		return ret;

	ret = dlb_attach_dir_ports(hw, rsrcs, domain, args->num_dir_ports, resp);
	if (ret)
		return ret;

	ret = dlb_attach_ldb_credits(rsrcs, domain,
				     args->num_ldb_credits, resp);
	if (ret)
		return ret;

	ret = dlb_attach_dir_credits(rsrcs, domain, args->num_dir_credits, resp);
	if (ret)
		return ret;

	ret = dlb_attach_domain_hist_list_entries(rsrcs, domain,
						  args->num_hist_list_entries,
						  resp);
	if (ret)
		return ret;

	ret = dlb_attach_atomic_inflights(rsrcs, domain,
					  args->num_atomic_inflights, resp);
	if (ret)
		return ret;

	dlb_configure_domain_credits(hw, domain);

	domain->configured = true;

	domain->started = false;

	rsrcs->num_avail_domains--;

	return 0;
}

static int
dlb_ldb_queue_attach_to_sn_group(struct dlb_hw *hw,
				 struct dlb_ldb_queue *queue,
				 struct dlb_create_ldb_queue_args *args)
{
	int slot = -1;
	int i;

	queue->sn_cfg_valid = false;

	if (args->num_sequence_numbers == 0)
		return 0;

	for (i = 0; i < DLB_MAX_NUM_SEQUENCE_NUMBER_GROUPS; i++) {
		struct dlb_sn_group *group = &hw->rsrcs.sn_groups[i];

		if (group->sequence_numbers_per_queue ==
		    args->num_sequence_numbers &&
		    !dlb_sn_group_full(group)) {
			slot = dlb_sn_group_alloc_slot(group);
			if (slot >= 0)
				break;
		}
	}

	if (slot == -1) {
		dev_err(hw_to_dev(hw),
			"[%s():%d] Internal error: no sequence number slots available\n",
			__func__, __LINE__);
		return -EFAULT;
	}

	queue->sn_cfg_valid = true;
	queue->sn_group = i;
	queue->sn_slot = slot;
	return 0;
}

static int
dlb_ldb_queue_attach_resources(struct dlb_hw *hw,
			       struct dlb_hw_domain *domain,
			       struct dlb_ldb_queue *queue,
			       struct dlb_create_ldb_queue_args *args)
{
	int ret;

	ret = dlb_ldb_queue_attach_to_sn_group(hw, queue, args);
	if (ret)
		return ret;

	/* Attach QID inflights */
	queue->num_qid_inflights = args->num_qid_inflights;

	/* Attach atomic inflights */
	queue->aqed_limit = args->num_atomic_inflights;

	domain->num_avail_aqed_entries -= args->num_atomic_inflights;
	domain->num_used_aqed_entries += args->num_atomic_inflights;

	return 0;
}

static void dlb_ldb_port_cq_enable(struct dlb_hw *hw,
				   struct dlb_ldb_port *port)
{
	u32 reg = 0;

	/*
	 * Don't re-enable the port if a removal is pending. The caller should
	 * mark this port as enabled (if it isn't already), and when the
	 * removal completes the port will be enabled.
	 */
	if (port->num_pending_removals)
		return;

	DLB_CSR_WR(hw, LSP_CQ_LDB_DSBL(port->id), reg);

	dlb_flush_csr(hw);
}

static void dlb_ldb_port_cq_disable(struct dlb_hw *hw,
				    struct dlb_ldb_port *port)
{
	u32 reg = 0;

	reg |= LSP_CQ_LDB_DSBL_DISABLED;
	DLB_CSR_WR(hw, LSP_CQ_LDB_DSBL(port->id), reg);

	dlb_flush_csr(hw);
}

static void dlb_dir_port_cq_enable(struct dlb_hw *hw,
				   struct dlb_dir_pq_pair *port)
{
	u32 reg = 0;

	DLB_CSR_WR(hw, LSP_CQ_DIR_DSBL(port->id), reg);

	dlb_flush_csr(hw);
}

static void dlb_dir_port_cq_disable(struct dlb_hw *hw,
				    struct dlb_dir_pq_pair *port)
{
	u32 reg = 0;

	reg |= LSP_CQ_DIR_DSBL_DISABLED;
	DLB_CSR_WR(hw, LSP_CQ_DIR_DSBL(port->id), reg);

	dlb_flush_csr(hw);
}

static void
dlb_log_create_sched_domain_args(struct dlb_hw *hw,
				 struct dlb_create_sched_domain_args *args)
{
	dev_dbg(hw_to_dev(hw), "DLB create sched domain arguments:\n");
	dev_dbg(hw_to_dev(hw), "\tNumber of LDB queues:          %d\n",
		args->num_ldb_queues);
	dev_dbg(hw_to_dev(hw), "\tNumber of LDB ports (any CoS): %d\n",
		args->num_ldb_ports);
	dev_dbg(hw_to_dev(hw), "\tNumber of DIR ports:           %d\n",
		args->num_dir_ports);
	dev_dbg(hw_to_dev(hw), "\tNumber of ATM inflights:       %d\n",
		args->num_atomic_inflights);
	dev_dbg(hw_to_dev(hw), "\tNumber of hist list entries:   %d\n",
		args->num_hist_list_entries);
	dev_dbg(hw_to_dev(hw), "\tNumber of LDB credits:         %d\n",
		args->num_ldb_credits);
	dev_dbg(hw_to_dev(hw), "\tNumber of DIR credits:         %d\n",
		args->num_dir_credits);
}

/**
 * dlb_hw_create_sched_domain() - create a scheduling domain
 * @hw: dlb_hw handle for a particular device.
 * @args: scheduling domain creation arguments.
 * @resp: response structure.
 *
 * This function creates a scheduling domain containing the resources specified
 * in args. The individual resources (queues, ports, credits) can be configured
 * after creating a scheduling domain.
 *
 * Return:
 * Returns 0 upon success, < 0 otherwise. If an error occurs, resp->status is
 * assigned a detailed error code from enum dlb_error. If successful, resp->id
 * contains the domain ID.
 *
 * Errors:
 * EINVAL - A requested resource is unavailable, or the requested domain name
 *	    is already in use.
 * EFAULT - Internal error (resp->status not set).
 */
int dlb_hw_create_sched_domain(struct dlb_hw *hw,
			       struct dlb_create_sched_domain_args *args,
			       struct dlb_cmd_response *resp)
{
	struct dlb_function_resources *rsrcs;
	struct dlb_hw_domain *domain;
	int ret;

	rsrcs = &hw->pf;

	dlb_log_create_sched_domain_args(hw, args);

	/*
	 * Verify that hardware resources are available before attempting to
	 * satisfy the request. This simplifies the error unwinding code.
	 */
	ret = dlb_verify_create_sched_dom_args(rsrcs, args, resp, &domain);
	if (ret)
		return ret;

	dlb_init_domain_rsrc_lists(domain);

	ret = dlb_domain_attach_resources(hw, rsrcs, domain, args, resp);
	if (ret) {
		dev_err(hw_to_dev(hw),
			"[%s()] Internal error: failed to verify args.\n",
			__func__);

		return ret;
	}

	/*
	 * Configuration succeeded, so move the resource from the 'avail' to
	 * the 'used' list (if it's not already there).
	 */
	list_move(&domain->func_list, &rsrcs->used_domains);

	resp->id = domain->id;
	resp->status = 0;

	return 0;
}

static void
dlb_log_create_ldb_queue_args(struct dlb_hw *hw, u32 domain_id,
			      struct dlb_create_ldb_queue_args *args)
{
	dev_dbg(hw_to_dev(hw), "DLB create load-balanced queue arguments:\n");
	dev_dbg(hw_to_dev(hw), "\tDomain ID:                  %d\n",
		domain_id);
	dev_dbg(hw_to_dev(hw), "\tNumber of sequence numbers: %d\n",
		args->num_sequence_numbers);
	dev_dbg(hw_to_dev(hw), "\tNumber of QID inflights:    %d\n",
		args->num_qid_inflights);
	dev_dbg(hw_to_dev(hw), "\tNumber of ATM inflights:    %d\n",
		args->num_atomic_inflights);
}

/**
 * dlb_hw_create_ldb_queue() - create a load-balanced queue
 * @hw: dlb_hw handle for a particular device.
 * @domain_id: domain ID.
 * @args: queue creation arguments.
 * @resp: response structure.
 *
 * This function creates a load-balanced queue.
 *
 * Return:
 * Returns 0 upon success, < 0 otherwise. If an error occurs, resp->status is
 * assigned a detailed error code from enum dlb_error. If successful, resp->id
 * contains the queue ID.
 *
 * Errors:
 * EINVAL - A requested resource is unavailable, the domain is not configured,
 *	    the domain has already been started, or the requested queue name is
 *	    already in use.
 * EFAULT - Internal error (resp->status not set).
 */
int dlb_hw_create_ldb_queue(struct dlb_hw *hw, u32 domain_id,
			    struct dlb_create_ldb_queue_args *args,
			    struct dlb_cmd_response *resp)
{
	struct dlb_hw_domain *domain;
	struct dlb_ldb_queue *queue;
	int ret;

	dlb_log_create_ldb_queue_args(hw, domain_id, args);

	/*
	 * Verify that hardware resources are available before attempting to
	 * satisfy the request. This simplifies the error unwinding code.
	 */
	ret = dlb_verify_create_ldb_queue_args(hw, domain_id, args, resp,
					       &domain, &queue);
	if (ret)
		return ret;

	ret = dlb_ldb_queue_attach_resources(hw, domain, queue, args);
	if (ret) {
		dev_err(hw_to_dev(hw),
			"[%s():%d] Internal error: failed to attach the ldb queue resources\n",
			__func__, __LINE__);
		return ret;
	}

	dlb_configure_ldb_queue(hw, domain, queue, args);

	queue->num_mappings = 0;

	queue->configured = true;

	/*
	 * Configuration succeeded, so move the resource from the 'avail' to
	 * the 'used' list.
	 */
	list_move(&queue->domain_list, &domain->used_ldb_queues);

	resp->status = 0;
	resp->id = queue->id;

	return 0;
}

static void
dlb_log_create_dir_queue_args(struct dlb_hw *hw, u32 domain_id,
			      struct dlb_create_dir_queue_args *args)
{
	dev_dbg(hw_to_dev(hw), "DLB create directed queue arguments:\n");
	dev_dbg(hw_to_dev(hw), "\tDomain ID: %d\n", domain_id);
	dev_dbg(hw_to_dev(hw), "\tPort ID:   %d\n", args->port_id);
}

/**
 * dlb_hw_create_dir_queue() - create a directed queue
 * @hw: dlb_hw handle for a particular device.
 * @domain_id: domain ID.
 * @args: queue creation arguments.
 * @resp: response structure.
 *
 * This function creates a directed queue.
 *
 * Return:
 * Returns 0 upon success, < 0 otherwise. If an error occurs, resp->status is
 * assigned a detailed error code from enum dlb_error. If successful, resp->id
 * contains the queue ID.
 *
 * Errors:
 * EINVAL - A requested resource is unavailable, the domain is not configured,
 *	    or the domain has already been started.
 * EFAULT - Internal error (resp->status not set).
 */
int dlb_hw_create_dir_queue(struct dlb_hw *hw, u32 domain_id,
			    struct dlb_create_dir_queue_args *args,
			    struct dlb_cmd_response *resp)
{
	struct dlb_dir_pq_pair *queue;
	struct dlb_hw_domain *domain;
	int ret;

	dlb_log_create_dir_queue_args(hw, domain_id, args);

	/*
	 * Verify that hardware resources are available before attempting to
	 * satisfy the request. This simplifies the error unwinding code.
	 */
	ret = dlb_verify_create_dir_queue_args(hw, domain_id, args, resp,
					       &domain, &queue);
	if (ret)
		return ret;

	dlb_configure_dir_queue(hw, domain, queue, args);

	/*
	 * Configuration succeeded, so move the resource from the 'avail' to
	 * the 'used' list (if it's not already there).
	 */
	if (args->port_id == -1)
		list_move(&queue->domain_list, &domain->used_dir_pq_pairs);

	resp->status = 0;

	resp->id = queue->id;

	return 0;
}

static void
dlb_log_create_ldb_port_args(struct dlb_hw *hw, u32 domain_id,
			     uintptr_t cq_dma_base,
			     struct dlb_create_ldb_port_args *args)
{
	dev_dbg(hw_to_dev(hw), "DLB create load-balanced port arguments:\n");
	dev_dbg(hw_to_dev(hw), "\tDomain ID:                 %d\n",
		domain_id);
	dev_dbg(hw_to_dev(hw), "\tCQ depth:                  %d\n",
		args->cq_depth);
	dev_dbg(hw_to_dev(hw), "\tCQ hist list size:         %d\n",
		args->cq_history_list_size);
	dev_dbg(hw_to_dev(hw), "\tCQ base address:           0x%lx\n",
		cq_dma_base);
}

/**
 * dlb_hw_create_ldb_port() - create a load-balanced port
 * @hw: dlb_hw handle for a particular device.
 * @domain_id: domain ID.
 * @args: port creation arguments.
 * @cq_dma_base: base address of the CQ memory. This can be a PA or an IOVA.
 * @resp: response structure.
 *
 * This function creates a load-balanced port.
 *
 * Return:
 * Returns 0 upon success, < 0 otherwise. If an error occurs, resp->status is
 * assigned a detailed error code from enum dlb_error. If successful, resp->id
 * contains the port ID.
 *
 * Errors:
 * EINVAL - A requested resource is unavailable, a credit setting is invalid, a
 *	    pointer address is not properly aligned, the domain is not
 *	    configured, or the domain has already been started.
 * EFAULT - Internal error (resp->status not set).
 */
int dlb_hw_create_ldb_port(struct dlb_hw *hw, u32 domain_id,
			   struct dlb_create_ldb_port_args *args,
			   uintptr_t cq_dma_base,
			   struct dlb_cmd_response *resp)
{
	struct dlb_hw_domain *domain;
	struct dlb_ldb_port *port;
	int ret, cos_id;

	dlb_log_create_ldb_port_args(hw, domain_id, cq_dma_base,
				     args);

	/*
	 * Verify that hardware resources are available before attempting to
	 * satisfy the request. This simplifies the error unwinding code.
	 */
	ret = dlb_verify_create_ldb_port_args(hw, domain_id, cq_dma_base, args,
					      resp, &domain,
					      &port, &cos_id);
	if (ret)
		return ret;

	/*
	 * Configuration succeeded, so move the resource from the 'avail' to
	 * the 'used' list.
	 */
	list_move(&port->domain_list, &domain->used_ldb_ports[cos_id]);

	resp->status = 0;
	resp->id = port->id;

	return 0;
}

static void
dlb_log_create_dir_port_args(struct dlb_hw *hw,
			     u32 domain_id, uintptr_t cq_dma_base,
			     struct dlb_create_dir_port_args *args)
{
	dev_dbg(hw_to_dev(hw), "DLB create directed port arguments:\n");
	dev_dbg(hw_to_dev(hw), "\tDomain ID:                 %d\n",
		domain_id);
	dev_dbg(hw_to_dev(hw), "\tCQ depth:                  %d\n",
		args->cq_depth);
	dev_dbg(hw_to_dev(hw), "\tCQ base address:           0x%lx\n",
		cq_dma_base);
}

/**
 * dlb_hw_create_dir_port() - create a directed port
 * @hw: dlb_hw handle for a particular device.
 * @domain_id: domain ID.
 * @args: port creation arguments.
 * @cq_dma_base: base address of the CQ memory. This can be a PA or an IOVA.
 * @resp: response structure.
 *
 * This function creates a directed port.
 *
 * Return:
 * Returns 0 upon success, < 0 otherwise. If an error occurs, resp->status is
 * assigned a detailed error code from enum dlb_error. If successful, resp->id
 * contains the port ID.
 *
 * Errors:
 * EINVAL - A requested resource is unavailable, a credit setting is invalid, a
 *	    pointer address is not properly aligned, the domain is not
 *	    configured, or the domain has already been started.
 * EFAULT - Internal error (resp->status not set).
 */
int dlb_hw_create_dir_port(struct dlb_hw *hw, u32 domain_id,
			   struct dlb_create_dir_port_args *args,
			   uintptr_t cq_dma_base,
			   struct dlb_cmd_response *resp)
{
	struct dlb_dir_pq_pair *port;
	struct dlb_hw_domain *domain;
	int ret;

	dlb_log_create_dir_port_args(hw, domain_id, cq_dma_base, args);

	/*
	 * Verify that hardware resources are available before attempting to
	 * satisfy the request. This simplifies the error unwinding code.
	 */
	ret = dlb_verify_create_dir_port_args(hw, domain_id, cq_dma_base,
					      args, resp,
					      &domain, &port);
	if (ret)
		return ret;

	/*
	 * Configuration succeeded, so move the resource from the 'avail' to
	 * the 'used' list (if it's not already there).
	 */
	if (args->queue_id == -1)
		list_move(&port->domain_list, &domain->used_dir_pq_pairs);

	resp->status = 0;
	resp->id = port->id;

	return 0;
}

static u32 dlb_ldb_cq_inflight_count(struct dlb_hw *hw,
				     struct dlb_ldb_port *port)
{
	u32 cnt;

	cnt = DLB_CSR_RD(hw, LSP_CQ_LDB_INFL_CNT(port->id));

	return FIELD_GET(LSP_CQ_LDB_INFL_CNT_COUNT, cnt);
}

static u32 dlb_ldb_cq_token_count(struct dlb_hw *hw, struct dlb_ldb_port *port)
{
	u32 cnt;

	cnt = DLB_CSR_RD(hw, LSP_CQ_LDB_TKN_CNT(port->id));

	/*
	 * Account for the initial token count, which is used in order to
	 * provide a CQ with depth less than 8.
	 */

	return FIELD_GET(LSP_CQ_LDB_TKN_CNT_TOKEN_COUNT, cnt) - port->init_tkn_cnt;
}

static void __iomem *dlb_producer_port_addr(struct dlb_hw *hw, u8 port_id,
					    bool is_ldb)
{
	struct dlb *dlb = container_of(hw, struct dlb, hw);
	uintptr_t address = (uintptr_t)dlb->hw.func_kva;
	unsigned long size;

	if (is_ldb) {
		size = DLB_LDB_PP_STRIDE;
		address += DLB_DRV_LDB_PP_BASE + size * port_id;
	} else {
		size = DLB_DIR_PP_STRIDE;
		address += DLB_DRV_DIR_PP_BASE + size * port_id;
	}

	return (void __iomem *)address;
}

static void dlb_drain_ldb_cq(struct dlb_hw *hw, struct dlb_ldb_port *port)
{
	u32 infl_cnt, tkn_cnt;
	unsigned int i;

	infl_cnt = dlb_ldb_cq_inflight_count(hw, port);
	tkn_cnt = dlb_ldb_cq_token_count(hw, port);

	if (infl_cnt || tkn_cnt) {
		struct dlb_hcw hcw_mem[8], *hcw;
		void __iomem *pp_addr;

		pp_addr = dlb_producer_port_addr(hw, port->id, true);

		/* Point hcw to a 64B-aligned location */
		hcw = (struct dlb_hcw *)((uintptr_t)&hcw_mem[4] & ~0x3F);

		/*
		 * Program the first HCW for a completion and token return and
		 * the other HCWs as NOOPS
		 */

		memset(hcw, 0, 4 * sizeof(*hcw));
		hcw->qe_comp = (infl_cnt > 0);
		hcw->cq_token = (tkn_cnt > 0);
		hcw->lock_id = tkn_cnt - 1;

		/*
		 * To ensure outstanding HCWs reach the device before subsequent
		 * device accesses, fence them.
		 */
		wmb();

		/* Return tokens in the first HCW */
		iosubmit_cmds512(pp_addr, hcw, 1);

		hcw->cq_token = 0;

		/* Issue remaining completions (if any) */
		for (i = 1; i < infl_cnt; i++)
			iosubmit_cmds512(pp_addr, hcw, 1);
	}
}

/*
 * dlb_domain_reset_software_state() - returns domain's resources
 * @hw: dlb_hw handle for a particular device.
 * @domain: pointer to scheduling domain.
 *
 * This function returns the resources allocated/assigned to a domain back to
 * the device/function level resource pool. These resources include ldb/dir
 * queues,  ports, history lists, etc. It is called by the dlb_reset_domain().
 * When a domain is created/initialized, resources are moved to a domain from
 * the resource pool.
 *
 */
static int dlb_domain_reset_software_state(struct dlb_hw *hw,
					   struct dlb_hw_domain *domain)
{
	struct dlb *dlb = container_of(hw, struct dlb, hw);
	struct dlb_dir_pq_pair *tmp_dir_port;
	struct dlb_function_resources *rsrcs;
	struct dlb_ldb_queue *tmp_ldb_queue;
	struct dlb_ldb_port *tmp_ldb_port;
	struct dlb_dir_pq_pair *dir_port;
	struct dlb_ldb_queue *ldb_queue;
	struct dlb_ldb_port *ldb_port;
	int ret, i;

	lockdep_assert_held(&dlb->resource_mutex);

	rsrcs = domain->parent_func;

	/* Move the domain's ldb queues to the function's avail list */
	list_for_each_entry_safe(ldb_queue, tmp_ldb_queue,
				 &domain->used_ldb_queues, domain_list) {
		if (ldb_queue->sn_cfg_valid) {
			struct dlb_sn_group *grp;

			grp = &hw->rsrcs.sn_groups[ldb_queue->sn_group];

			dlb_sn_group_free_slot(grp, ldb_queue->sn_slot);
			ldb_queue->sn_cfg_valid = false;
		}

		ldb_queue->owned = false;
		ldb_queue->num_mappings = 0;
		ldb_queue->num_pending_additions = 0;

		list_del(&ldb_queue->domain_list);
		list_add(&ldb_queue->func_list, &rsrcs->avail_ldb_queues);
		rsrcs->num_avail_ldb_queues++;
	}

	list_for_each_entry_safe(ldb_queue, tmp_ldb_queue,
				 &domain->avail_ldb_queues, domain_list) {
		ldb_queue->owned = false;

		list_del(&ldb_queue->domain_list);
		list_add(&ldb_queue->func_list, &rsrcs->avail_ldb_queues);
		rsrcs->num_avail_ldb_queues++;
	}

	/* Move the domain's ldb ports to the function's avail list */
	for (i = 0; i < DLB_NUM_COS_DOMAINS; i++) {
		list_for_each_entry_safe(ldb_port, tmp_ldb_port,
				 &domain->used_ldb_ports[i], domain_list) {
			int j;

			ldb_port->owned = false;
			ldb_port->configured = false;
			ldb_port->num_pending_removals = 0;
			ldb_port->num_mappings = 0;
			ldb_port->init_tkn_cnt = 0;
			for (j = 0; j < DLB_MAX_NUM_QIDS_PER_LDB_CQ; j++)
				ldb_port->qid_map[j].state =
					DLB_QUEUE_UNMAPPED;

			list_del(&ldb_port->domain_list);
			list_add(&ldb_port->func_list,
				 &rsrcs->avail_ldb_ports[i]);
			rsrcs->num_avail_ldb_ports[i]++;
		}

		list_for_each_entry_safe(ldb_port, tmp_ldb_port,
				 &domain->avail_ldb_ports[i], domain_list) {
			ldb_port->owned = false;

			list_del(&ldb_port->domain_list);
			list_add(&ldb_port->func_list,
				 &rsrcs->avail_ldb_ports[i]);
			rsrcs->num_avail_ldb_ports[i]++;
		}
	}

	/* Move the domain's dir ports to the function's avail list */
	list_for_each_entry_safe(dir_port, tmp_dir_port,
				 &domain->used_dir_pq_pairs, domain_list) {
		dir_port->owned = false;
		dir_port->port_configured = false;
		dir_port->init_tkn_cnt = 0;

		list_del(&dir_port->domain_list);

		list_add(&dir_port->func_list, &rsrcs->avail_dir_pq_pairs);
		rsrcs->num_avail_dir_pq_pairs++;
	}

	list_for_each_entry_safe(dir_port, tmp_dir_port,
				 &domain->avail_dir_pq_pairs, domain_list) {
		dir_port->owned = false;

		list_del(&dir_port->domain_list);

		list_add(&dir_port->func_list, &rsrcs->avail_dir_pq_pairs);
		rsrcs->num_avail_dir_pq_pairs++;
	}

	/* Return hist list entries to the function */
	ret = dlb_bitmap_set_range(rsrcs->avail_hist_list_entries,
				   domain->hist_list_entry_base,
				   domain->total_hist_list_entries);
	if (ret) {
		dev_err(hw_to_dev(hw),
			"[%s()] Internal error: domain hist list base doesn't match the function's bitmap.\n",
			__func__);
		return ret;
	}

	domain->total_hist_list_entries = 0;
	domain->avail_hist_list_entries = 0;
	domain->hist_list_entry_base = 0;
	domain->hist_list_entry_offset = 0;

	rsrcs->num_avail_qed_entries += domain->num_ldb_credits;
	domain->num_ldb_credits = 0;

	rsrcs->num_avail_dqed_entries += domain->num_dir_credits;
	domain->num_dir_credits = 0;

	rsrcs->num_avail_aqed_entries += domain->num_avail_aqed_entries;
	rsrcs->num_avail_aqed_entries += domain->num_used_aqed_entries;
	domain->num_avail_aqed_entries = 0;
	domain->num_used_aqed_entries = 0;

	domain->num_pending_removals = 0;
	domain->num_pending_additions = 0;
	domain->configured = false;
	domain->started = false;

	/*
	 * Move the domain out of the used_domains list and back to the
	 * function's avail_domains list.
	 */
	list_move(&domain->func_list, &rsrcs->avail_domains);
	rsrcs->num_avail_domains++;

	return 0;
}

static u32 dlb_dir_queue_depth(struct dlb_hw *hw, struct dlb_dir_pq_pair *queue)
{
	u32 cnt;

	cnt = DLB_CSR_RD(hw, LSP_QID_DIR_ENQUEUE_CNT(queue->id));

	return FIELD_GET(LSP_QID_DIR_ENQUEUE_CNT_COUNT, cnt);
}

static bool dlb_dir_queue_is_empty(struct dlb_hw *hw,
				   struct dlb_dir_pq_pair *queue)
{
	return dlb_dir_queue_depth(hw, queue) == 0;
}

static void dlb_log_get_dir_queue_depth(struct dlb_hw *hw, u32 domain_id,
					u32 queue_id)
{
	dev_dbg(hw_to_dev(hw), "DLB get directed queue depth:\n");
	dev_dbg(hw_to_dev(hw), "\tDomain ID: %d\n", domain_id);
	dev_dbg(hw_to_dev(hw), "\tQueue ID: %d\n", queue_id);
}

/**
 * dlb_hw_get_dir_queue_depth() - returns the depth of a directed queue
 * @hw: dlb_hw handle for a particular device.
 * @domain_id: domain ID.
 * @args: queue depth args
 * @resp: response structure.
 *
 * This function returns the depth of a directed queue.
 *
 * Return:
 * Returns 0 upon success, < 0 otherwise. If an error occurs, resp->status is
 * assigned a detailed error code from enum dlb_error. If successful, resp->id
 * contains the depth.
 *
 * Errors:
 * EINVAL - Invalid domain ID or queue ID.
 */
int dlb_hw_get_dir_queue_depth(struct dlb_hw *hw, u32 domain_id,
			       struct dlb_get_dir_queue_depth_args *args,
			       struct dlb_cmd_response *resp)
{
	struct dlb_dir_pq_pair *queue;
	struct dlb_hw_domain *domain;
	int id;

	id = domain_id;

	dlb_log_get_dir_queue_depth(hw, domain_id, args->queue_id);

	domain = dlb_get_domain_from_id(hw, id);
	if (!domain) {
		resp->status = DLB_ST_INVALID_DOMAIN_ID;
		return -EINVAL;
	}

	id = args->queue_id;

	queue = dlb_get_domain_used_dir_pq(id, false, domain);
	if (!queue) {
		resp->status = DLB_ST_INVALID_QID;
		return -EINVAL;
	}

	resp->id = dlb_dir_queue_depth(hw, queue);

	return 0;
}

static u32 dlb_ldb_queue_depth(struct dlb_hw *hw, struct dlb_ldb_queue *queue)
{
	u32 aqed, ldb, atm;

	aqed = DLB_CSR_RD(hw, LSP_QID_AQED_ACTIVE_CNT(queue->id));
	ldb = DLB_CSR_RD(hw, LSP_QID_LDB_ENQUEUE_CNT(queue->id));
	atm = DLB_CSR_RD(hw, LSP_QID_ATM_ACTIVE(queue->id));

	return FIELD_GET(LSP_QID_AQED_ACTIVE_CNT_COUNT, aqed)
	       + FIELD_GET(LSP_QID_LDB_ENQUEUE_CNT_COUNT, ldb)
	       + FIELD_GET(LSP_QID_ATM_ACTIVE_COUNT, atm);
}

static bool dlb_ldb_queue_is_empty(struct dlb_hw *hw, struct dlb_ldb_queue *queue)
{
	return dlb_ldb_queue_depth(hw, queue) == 0;
}

static void dlb_log_get_ldb_queue_depth(struct dlb_hw *hw, u32 domain_id,
					u32 queue_id)
{
	dev_dbg(hw_to_dev(hw), "DLB get load-balanced queue depth:\n");
	dev_dbg(hw_to_dev(hw), "\tDomain ID: %d\n", domain_id);
	dev_dbg(hw_to_dev(hw), "\tQueue ID: %d\n", queue_id);
}

/**
 * dlb_hw_get_ldb_queue_depth() - returns the depth of a load-balanced queue
 * @hw: dlb_hw handle for a particular device.
 * @domain_id: domain ID.
 * @args: queue depth args
 * @resp: response structure.
 *
 * This function returns the depth of a load-balanced queue.
 *
 * Return:
 * Returns 0 upon success, < 0 otherwise. If an error occurs, resp->status is
 * assigned a detailed error code from enum dlb_error. If successful, resp->id
 * contains the depth.
 *
 * Errors:
 * EINVAL - Invalid domain ID or queue ID.
 */
int dlb_hw_get_ldb_queue_depth(struct dlb_hw *hw, u32 domain_id,
			       struct dlb_get_ldb_queue_depth_args *args,
			       struct dlb_cmd_response *resp)
{
	struct dlb_hw_domain *domain;
	struct dlb_ldb_queue *queue;

	dlb_log_get_ldb_queue_depth(hw, domain_id, args->queue_id);

	domain = dlb_get_domain_from_id(hw, domain_id);
	if (!domain) {
		resp->status = DLB_ST_INVALID_DOMAIN_ID;
		return -EINVAL;
	}

	queue = dlb_get_domain_ldb_queue(args->queue_id, false, domain);
	if (!queue) {
		resp->status = DLB_ST_INVALID_QID;
		return -EINVAL;
	}

	resp->id = dlb_ldb_queue_depth(hw, queue);

	return 0;
}

static void __dlb_domain_reset_ldb_port_registers(struct dlb_hw *hw,
						  struct dlb_ldb_port *port)
{
	DLB_CSR_WR(hw,
		   SYS_LDB_PP2VAS(port->id),
		   SYS_LDB_PP2VAS_RST);

	DLB_CSR_WR(hw,
		   CHP_LDB_CQ2VAS(port->id),
		   CHP_LDB_CQ2VAS_RST);

	DLB_CSR_WR(hw,
		   SYS_LDB_PP2VDEV(port->id),
		   SYS_LDB_PP2VDEV_RST);

	DLB_CSR_WR(hw,
		   SYS_LDB_PP_V(port->id),
		   SYS_LDB_PP_V_RST);

	DLB_CSR_WR(hw,
		   LSP_CQ_LDB_DSBL(port->id),
		   LSP_CQ_LDB_DSBL_RST);

	DLB_CSR_WR(hw,
		   CHP_LDB_CQ_DEPTH(port->id),
		   CHP_LDB_CQ_DEPTH_RST);

	DLB_CSR_WR(hw,
		   LSP_CQ_LDB_INFL_LIM(port->id),
		   LSP_CQ_LDB_INFL_LIM_RST);

	DLB_CSR_WR(hw,
		   CHP_HIST_LIST_LIM(port->id),
		   CHP_HIST_LIST_LIM_RST);

	DLB_CSR_WR(hw,
		   CHP_HIST_LIST_BASE(port->id),
		   CHP_HIST_LIST_BASE_RST);

	DLB_CSR_WR(hw,
		   CHP_HIST_LIST_POP_PTR(port->id),
		   CHP_HIST_LIST_POP_PTR_RST);

	DLB_CSR_WR(hw,
		   CHP_HIST_LIST_PUSH_PTR(port->id),
		   CHP_HIST_LIST_PUSH_PTR_RST);

	DLB_CSR_WR(hw,
		   CHP_LDB_CQ_INT_DEPTH_THRSH(port->id),
		   CHP_LDB_CQ_INT_DEPTH_THRSH_RST);

	DLB_CSR_WR(hw,
		   CHP_LDB_CQ_TMR_THRSH(port->id),
		   CHP_LDB_CQ_TMR_THRSH_RST);

	DLB_CSR_WR(hw,
		   CHP_LDB_CQ_INT_ENB(port->id),
		   CHP_LDB_CQ_INT_ENB_RST);

	DLB_CSR_WR(hw,
		   SYS_LDB_CQ_ISR(port->id),
		   SYS_LDB_CQ_ISR_RST);

	DLB_CSR_WR(hw,
		   LSP_CQ_LDB_TKN_DEPTH_SEL(port->id),
		   LSP_CQ_LDB_TKN_DEPTH_SEL_RST);

	DLB_CSR_WR(hw,
		   CHP_LDB_CQ_TKN_DEPTH_SEL(port->id),
		   CHP_LDB_CQ_TKN_DEPTH_SEL_RST);

	DLB_CSR_WR(hw,
		   CHP_LDB_CQ_WPTR(port->id),
		   CHP_LDB_CQ_WPTR_RST);

	DLB_CSR_WR(hw,
		   LSP_CQ_LDB_TKN_CNT(port->id),
		   LSP_CQ_LDB_TKN_CNT_RST);

	DLB_CSR_WR(hw,
		   SYS_LDB_CQ_ADDR_L(port->id),
		   SYS_LDB_CQ_ADDR_L_RST);

	DLB_CSR_WR(hw,
		   SYS_LDB_CQ_ADDR_U(port->id),
		   SYS_LDB_CQ_ADDR_U_RST);

	DLB_CSR_WR(hw,
		   SYS_LDB_CQ_AT(port->id),
		   SYS_LDB_CQ_AT_RST);

	DLB_CSR_WR(hw,
		   SYS_LDB_CQ_PASID(port->id),
		   SYS_LDB_CQ_PASID_RST);

	DLB_CSR_WR(hw,
		   SYS_LDB_CQ2VF_PF_RO(port->id),
		   SYS_LDB_CQ2VF_PF_RO_RST);

	DLB_CSR_WR(hw,
		   LSP_CQ_LDB_TOT_SCH_CNTL(port->id),
		   LSP_CQ_LDB_TOT_SCH_CNTL_RST);

	DLB_CSR_WR(hw,
		   LSP_CQ_LDB_TOT_SCH_CNTH(port->id),
		   LSP_CQ_LDB_TOT_SCH_CNTH_RST);

	DLB_CSR_WR(hw,
		   LSP_CQ2QID0(port->id),
		   LSP_CQ2QID0_RST);

	DLB_CSR_WR(hw,
		   LSP_CQ2QID1(port->id),
		   LSP_CQ2QID1_RST);

	DLB_CSR_WR(hw,
		   LSP_CQ2PRIOV(port->id),
		   LSP_CQ2PRIOV_RST);
}

static void dlb_domain_reset_ldb_port_registers(struct dlb_hw *hw,
						struct dlb_hw_domain *domain)
{
	struct dlb_ldb_port *port;
	int i;

	for (i = 0; i < DLB_NUM_COS_DOMAINS; i++) {
		list_for_each_entry(port, &domain->used_ldb_ports[i], domain_list)
			__dlb_domain_reset_ldb_port_registers(hw, port);
	}
}

static void
__dlb_domain_reset_dir_port_registers(struct dlb_hw *hw,
				      struct dlb_dir_pq_pair *port)
{
	DLB_CSR_WR(hw,
		   CHP_DIR_CQ2VAS(port->id),
		   CHP_DIR_CQ2VAS_RST);

	DLB_CSR_WR(hw,
		   LSP_CQ_DIR_DSBL(port->id),
		   LSP_CQ_DIR_DSBL_RST);

	DLB_CSR_WR(hw, SYS_DIR_CQ_OPT_CLR, port->id);

	DLB_CSR_WR(hw,
		   CHP_DIR_CQ_DEPTH(port->id),
		   CHP_DIR_CQ_DEPTH_RST);

	DLB_CSR_WR(hw,
		   CHP_DIR_CQ_INT_DEPTH_THRSH(port->id),
		   CHP_DIR_CQ_INT_DEPTH_THRSH_RST);

	DLB_CSR_WR(hw,
		   CHP_DIR_CQ_TMR_THRSH(port->id),
		   CHP_DIR_CQ_TMR_THRSH_RST);

	DLB_CSR_WR(hw,
		   CHP_DIR_CQ_INT_ENB(port->id),
		   CHP_DIR_CQ_INT_ENB_RST);

	DLB_CSR_WR(hw,
		   SYS_DIR_CQ_ISR(port->id),
		   SYS_DIR_CQ_ISR_RST);

	DLB_CSR_WR(hw,
		   LSP_CQ_DIR_TKN_DEPTH_SEL_DSI(port->id),
		   LSP_CQ_DIR_TKN_DEPTH_SEL_DSI_RST);

	DLB_CSR_WR(hw,
		   CHP_DIR_CQ_TKN_DEPTH_SEL(port->id),
		   CHP_DIR_CQ_TKN_DEPTH_SEL_RST);

	DLB_CSR_WR(hw,
		   CHP_DIR_CQ_WPTR(port->id),
		   CHP_DIR_CQ_WPTR_RST);

	DLB_CSR_WR(hw,
		   LSP_CQ_DIR_TKN_CNT(port->id),
		   LSP_CQ_DIR_TKN_CNT_RST);

	DLB_CSR_WR(hw,
		   SYS_DIR_CQ_ADDR_L(port->id),
		   SYS_DIR_CQ_ADDR_L_RST);

	DLB_CSR_WR(hw,
		   SYS_DIR_CQ_ADDR_U(port->id),
		   SYS_DIR_CQ_ADDR_U_RST);

	DLB_CSR_WR(hw,
		   SYS_DIR_CQ_AT(port->id),
		   SYS_DIR_CQ_AT_RST);

	DLB_CSR_WR(hw,
		   SYS_DIR_CQ_PASID(port->id),
		   SYS_DIR_CQ_PASID_RST);

	DLB_CSR_WR(hw,
		   SYS_DIR_CQ_FMT(port->id),
		   SYS_DIR_CQ_FMT_RST);

	DLB_CSR_WR(hw,
		   SYS_DIR_CQ2VF_PF_RO(port->id),
		   SYS_DIR_CQ2VF_PF_RO_RST);

	DLB_CSR_WR(hw,
		   LSP_CQ_DIR_TOT_SCH_CNTL(port->id),
		   LSP_CQ_DIR_TOT_SCH_CNTL_RST);

	DLB_CSR_WR(hw,
		   LSP_CQ_DIR_TOT_SCH_CNTH(port->id),
		   LSP_CQ_DIR_TOT_SCH_CNTH_RST);

	DLB_CSR_WR(hw,
		   SYS_DIR_PP2VAS(port->id),
		   SYS_DIR_PP2VAS_RST);

	DLB_CSR_WR(hw,
		   CHP_DIR_CQ2VAS(port->id),
		   CHP_DIR_CQ2VAS_RST);

	DLB_CSR_WR(hw,
		   SYS_DIR_PP2VDEV(port->id),
		   SYS_DIR_PP2VDEV_RST);

	DLB_CSR_WR(hw,
		   SYS_DIR_PP_V(port->id),
		   SYS_DIR_PP_V_RST);
}

static void dlb_domain_reset_dir_port_registers(struct dlb_hw *hw,
						struct dlb_hw_domain *domain)
{
	struct dlb_dir_pq_pair *port;

	list_for_each_entry(port, &domain->used_dir_pq_pairs, domain_list)
		__dlb_domain_reset_dir_port_registers(hw, port);
}

static void dlb_domain_reset_ldb_queue_registers(struct dlb_hw *hw,
						 struct dlb_hw_domain *domain)
{
	struct dlb_ldb_queue *queue;

	list_for_each_entry(queue, &domain->used_ldb_queues, domain_list) {
		unsigned int queue_id = queue->id;
		int i;

		DLB_CSR_WR(hw,
			   LSP_QID_NALDB_TOT_ENQ_CNTL(queue_id),
			   LSP_QID_NALDB_TOT_ENQ_CNTL_RST);

		DLB_CSR_WR(hw,
			   LSP_QID_NALDB_TOT_ENQ_CNTH(queue_id),
			   LSP_QID_NALDB_TOT_ENQ_CNTH_RST);

		DLB_CSR_WR(hw,
			   LSP_QID_ATM_TOT_ENQ_CNTL(queue_id),
			   LSP_QID_ATM_TOT_ENQ_CNTL_RST);

		DLB_CSR_WR(hw,
			   LSP_QID_ATM_TOT_ENQ_CNTH(queue_id),
			   LSP_QID_ATM_TOT_ENQ_CNTH_RST);

		DLB_CSR_WR(hw,
			   LSP_QID_NALDB_MAX_DEPTH(queue_id),
			   LSP_QID_NALDB_MAX_DEPTH_RST);

		DLB_CSR_WR(hw,
			   LSP_QID_LDB_INFL_LIM(queue_id),
			   LSP_QID_LDB_INFL_LIM_RST);

		DLB_CSR_WR(hw,
			   LSP_QID_AQED_ACTIVE_LIM(queue_id),
			   LSP_QID_AQED_ACTIVE_LIM_RST);

		DLB_CSR_WR(hw,
			   LSP_QID_ATM_DEPTH_THRSH(queue_id),
			   LSP_QID_ATM_DEPTH_THRSH_RST);

		DLB_CSR_WR(hw,
			   LSP_QID_NALDB_DEPTH_THRSH(queue_id),
			   LSP_QID_NALDB_DEPTH_THRSH_RST);

		DLB_CSR_WR(hw,
			   SYS_LDB_QID_ITS(queue_id),
			   SYS_LDB_QID_ITS_RST);

		DLB_CSR_WR(hw,
			   CHP_ORD_QID_SN(queue_id),
			   CHP_ORD_QID_SN_RST);

		DLB_CSR_WR(hw,
			   CHP_ORD_QID_SN_MAP(queue_id),
			   CHP_ORD_QID_SN_MAP_RST);

		DLB_CSR_WR(hw,
			   SYS_LDB_QID_V(queue_id),
			   SYS_LDB_QID_V_RST);

		DLB_CSR_WR(hw,
			   SYS_LDB_QID_CFG_V(queue_id),
			   SYS_LDB_QID_CFG_V_RST);

		if (queue->sn_cfg_valid) {
			u32 offs[2];

			offs[0] = RO_GRP_0_SLT_SHFT(queue->sn_slot);
			offs[1] = RO_GRP_1_SLT_SHFT(queue->sn_slot);

			DLB_CSR_WR(hw,
				   offs[queue->sn_group],
				   RO_GRP_0_SLT_SHFT_RST);
		}

		for (i = 0; i < LSP_QID2CQIDIX_NUM; i++) {
			DLB_CSR_WR(hw,
				   LSP_QID2CQIDIX(queue_id, i),
				   LSP_QID2CQIDIX_00_RST);

			DLB_CSR_WR(hw,
				   LSP_QID2CQIDIX2(queue_id, i),
				   LSP_QID2CQIDIX2_00_RST);

			DLB_CSR_WR(hw,
				   ATM_QID2CQIDIX(queue_id, i),
				   ATM_QID2CQIDIX_00_RST);
		}
	}
}

static void dlb_domain_reset_dir_queue_registers(struct dlb_hw *hw,
						 struct dlb_hw_domain *domain)
{
	struct dlb_dir_pq_pair *queue;

	list_for_each_entry(queue, &domain->used_dir_pq_pairs, domain_list) {
		DLB_CSR_WR(hw,
			   LSP_QID_DIR_MAX_DEPTH(queue->id),
			   LSP_QID_DIR_MAX_DEPTH_RST);

		DLB_CSR_WR(hw,
			   LSP_QID_DIR_TOT_ENQ_CNTL(queue->id),
			   LSP_QID_DIR_TOT_ENQ_CNTL_RST);

		DLB_CSR_WR(hw,
			   LSP_QID_DIR_TOT_ENQ_CNTH(queue->id),
			   LSP_QID_DIR_TOT_ENQ_CNTH_RST);

		DLB_CSR_WR(hw,
			   LSP_QID_DIR_DEPTH_THRSH(queue->id),
			   LSP_QID_DIR_DEPTH_THRSH_RST);

		DLB_CSR_WR(hw,
			   SYS_DIR_QID_ITS(queue->id),
			   SYS_DIR_QID_ITS_RST);

		DLB_CSR_WR(hw,
			   SYS_DIR_QID_V(queue->id),
			   SYS_DIR_QID_V_RST);
	}
}

static u32 dlb_dir_cq_token_count(struct dlb_hw *hw,
				  struct dlb_dir_pq_pair *port)
{
	u32 cnt;

	cnt = DLB_CSR_RD(hw, LSP_CQ_DIR_TKN_CNT(port->id));

	/*
	 * Account for the initial token count, which is used in order to
	 * provide a CQ with depth less than 8.
	 */

	return FIELD_GET(LSP_CQ_DIR_TKN_CNT_COUNT, cnt) - port->init_tkn_cnt;
}

static int dlb_domain_verify_reset_success(struct dlb_hw *hw,
					   struct dlb_hw_domain *domain)
{
	struct dlb_ldb_queue *queue;

	/*
	 * Confirm that all the domain's queue's inflight counts and AQED
	 * active counts are 0.
	 */
	list_for_each_entry(queue, &domain->used_ldb_queues, domain_list) {
		if (!dlb_ldb_queue_is_empty(hw, queue)) {
			dev_err(hw_to_dev(hw),
				"[%s()] Internal error: failed to empty ldb queue %d\n",
				__func__, queue->id);
			return -EFAULT;
		}
	}

	return 0;
}

static void dlb_domain_reset_registers(struct dlb_hw *hw,
				       struct dlb_hw_domain *domain)
{
	dlb_domain_reset_ldb_port_registers(hw, domain);

	dlb_domain_reset_dir_port_registers(hw, domain);

	dlb_domain_reset_ldb_queue_registers(hw, domain);

	dlb_domain_reset_dir_queue_registers(hw, domain);

	DLB_CSR_WR(hw,
		   CHP_CFG_LDB_VAS_CRD(domain->id),
		   CHP_CFG_LDB_VAS_CRD_RST);

	DLB_CSR_WR(hw,
		   CHP_CFG_DIR_VAS_CRD(domain->id),
		   CHP_CFG_DIR_VAS_CRD_RST);
}

static void dlb_domain_drain_ldb_cqs(struct dlb_hw *hw,
				     struct dlb_hw_domain *domain,
				     bool toggle_port)
{
	struct dlb_ldb_port *port;
	int i;

	/* If the domain hasn't been started, there's no traffic to drain */
	if (!domain->started)
		return;

	for (i = 0; i < DLB_NUM_COS_DOMAINS; i++) {
		list_for_each_entry(port, &domain->used_ldb_ports[i], domain_list) {
			if (toggle_port)
				dlb_ldb_port_cq_disable(hw, port);

			dlb_drain_ldb_cq(hw, port);

			if (toggle_port)
				dlb_ldb_port_cq_enable(hw, port);
		}
	}
}

static bool dlb_domain_mapped_queues_empty(struct dlb_hw *hw,
					   struct dlb_hw_domain *domain)
{
	struct dlb_ldb_queue *queue;

	list_for_each_entry(queue, &domain->used_ldb_queues, domain_list) {
		if (queue->num_mappings == 0)
			continue;

		if (!dlb_ldb_queue_is_empty(hw, queue))
			return false;
	}

	return true;
}

static int dlb_domain_drain_mapped_queues(struct dlb_hw *hw,
					  struct dlb_hw_domain *domain)
{
	int i;

	/* If the domain hasn't been started, there's no traffic to drain */
	if (!domain->started)
		return 0;

	if (domain->num_pending_removals > 0) {
		dev_err(hw_to_dev(hw),
			"[%s()] Internal error: failed to unmap domain queues\n",
			__func__);
		return -EFAULT;
	}

	for (i = 0; i < DLB_MAX_QID_EMPTY_CHECK_LOOPS; i++) {
		dlb_domain_drain_ldb_cqs(hw, domain, true);

		if (dlb_domain_mapped_queues_empty(hw, domain))
			break;
	}

	if (i == DLB_MAX_QID_EMPTY_CHECK_LOOPS) {
		dev_err(hw_to_dev(hw),
			"[%s()] Internal error: failed to empty queues\n",
			__func__);
		return -EFAULT;
	}

	/*
	 * Drain the CQs one more time. For the queues to go empty, they would
	 * have scheduled one or more QEs.
	 */
	dlb_domain_drain_ldb_cqs(hw, domain, true);

	return 0;
}

static int dlb_drain_dir_cq(struct dlb_hw *hw, struct dlb_dir_pq_pair *port)
{
	unsigned int port_id = port->id;
	u32 cnt;

	/* Return any outstanding tokens */
	cnt = dlb_dir_cq_token_count(hw, port);

	if (cnt != 0) {
		struct dlb_hcw hcw_mem[8], *hcw;
		void __iomem *pp_addr;

		pp_addr = dlb_producer_port_addr(hw, port_id, false);

		/* Point hcw to a 64B-aligned location */
		hcw = (struct dlb_hcw *)((uintptr_t)&hcw_mem[4] & ~0x3F);

		/*
		 * Program the first HCW for a batch token return and
		 * the rest as NOOPS
		 */
		memset(hcw, 0, 4 * sizeof(*hcw));
		hcw->cq_token = 1;
		hcw->lock_id = cnt - 1;

		/*
		 * To ensure outstanding HCWs reach the device before subsequent
		 * device accesses, fence them.
		 */
		wmb();

		iosubmit_cmds512(pp_addr, hcw, 1);
	}

	return 0;
}

static int dlb_domain_drain_dir_cqs(struct dlb_hw *hw,
				    struct dlb_hw_domain *domain,
				    bool toggle_port)
{
	struct dlb_dir_pq_pair *port;
	int ret;

	list_for_each_entry(port, &domain->used_dir_pq_pairs, domain_list) {
		/*
		 * Can't drain a port if it's not configured, and there's
		 * nothing to drain if its queue is unconfigured.
		 */
		if (!port->port_configured || !port->queue_configured)
			continue;

		if (toggle_port)
			dlb_dir_port_cq_disable(hw, port);

		ret = dlb_drain_dir_cq(hw, port);
		if (ret)
			return ret;

		if (toggle_port)
			dlb_dir_port_cq_enable(hw, port);
	}

	return 0;
}

static bool dlb_domain_dir_queues_empty(struct dlb_hw *hw,
					struct dlb_hw_domain *domain)
{
	struct dlb_dir_pq_pair *queue;

	list_for_each_entry(queue, &domain->used_dir_pq_pairs, domain_list) {
		if (!dlb_dir_queue_is_empty(hw, queue))
			return false;
	}

	return true;
}

static int dlb_domain_drain_dir_queues(struct dlb_hw *hw,
				       struct dlb_hw_domain *domain)
{
	int i, ret;

	/* If the domain hasn't been started, there's no traffic to drain */
	if (!domain->started)
		return 0;

	for (i = 0; i < DLB_MAX_QID_EMPTY_CHECK_LOOPS; i++) {
		ret = dlb_domain_drain_dir_cqs(hw, domain, true);
		if (ret)
			return ret;

		if (dlb_domain_dir_queues_empty(hw, domain))
			break;
	}

	if (i == DLB_MAX_QID_EMPTY_CHECK_LOOPS) {
		dev_err(hw_to_dev(hw),
			"[%s()] Internal error: failed to empty queues\n",
			__func__);
		return -EFAULT;
	}

	/*
	 * Drain the CQs one more time. For the queues to go empty, they would
	 * have scheduled one or more QEs.
	 */
	ret = dlb_domain_drain_dir_cqs(hw, domain, true);
	if (ret)
		return ret;

	return 0;
}

static void
dlb_domain_disable_ldb_queue_write_perms(struct dlb_hw *hw,
					 struct dlb_hw_domain *domain)
{
	int domain_offset = domain->id * DLB_MAX_NUM_LDB_QUEUES;
	struct dlb_ldb_queue *queue;

	list_for_each_entry(queue, &domain->used_ldb_queues, domain_list) {
		int idx = domain_offset + queue->id;

		DLB_CSR_WR(hw, SYS_LDB_VASQID_V(idx), 0);
	}
}

static void
dlb_domain_disable_dir_queue_write_perms(struct dlb_hw *hw,
					 struct dlb_hw_domain *domain)
{
	int domain_offset = domain->id * DLB_MAX_NUM_DIR_PORTS;
	struct dlb_dir_pq_pair *queue;

	list_for_each_entry(queue, &domain->used_dir_pq_pairs, domain_list) {
		int idx = domain_offset + queue->id;

		DLB_CSR_WR(hw, SYS_DIR_VASQID_V(idx), 0);
	}
}

static void dlb_domain_disable_dir_cqs(struct dlb_hw *hw,
				       struct dlb_hw_domain *domain)
{
	struct dlb_dir_pq_pair *port;

	list_for_each_entry(port, &domain->used_dir_pq_pairs, domain_list) {
		port->enabled = false;

		dlb_dir_port_cq_disable(hw, port);
	}
}

static void dlb_domain_disable_ldb_cqs(struct dlb_hw *hw,
				       struct dlb_hw_domain *domain)
{
	struct dlb_ldb_port *port;
	int i;

	for (i = 0; i < DLB_NUM_COS_DOMAINS; i++) {
		list_for_each_entry(port, &domain->used_ldb_ports[i], domain_list) {
			port->enabled = false;

			dlb_ldb_port_cq_disable(hw, port);
		}
	}
}

static void dlb_domain_enable_ldb_cqs(struct dlb_hw *hw,
				      struct dlb_hw_domain *domain)
{
	struct dlb_ldb_port *port;
	int i;

	for (i = 0; i < DLB_NUM_COS_DOMAINS; i++) {
		list_for_each_entry(port, &domain->used_ldb_ports[i], domain_list) {
			port->enabled = true;

			dlb_ldb_port_cq_enable(hw, port);
		}
	}
}

static void dlb_log_reset_domain(struct dlb_hw *hw, u32 domain_id)
{
	dev_dbg(hw_to_dev(hw), "DLB reset domain:\n");
	dev_dbg(hw_to_dev(hw), "\tDomain ID: %d\n", domain_id);
}

/**
 * dlb_reset_domain() - reset a scheduling domain
 * @hw: dlb_hw handle for a particular device.
 * @domain_id: domain ID.
 *
 * This function resets and frees a DLB 2.0 scheduling domain and its associated
 * resources.
 *
 * Pre-condition: the driver must ensure software has stopped sending QEs
 * through this domain's producer ports before invoking this function, or
 * undefined behavior will result.
 *
 * Return:
 * Returns 0 upon success, -1 otherwise.
 *
 * EINVAL - Invalid domain ID, or the domain is not configured.
 * EFAULT - Internal error. (Possibly caused if software is the pre-condition
 *	    is not met.)
 * ETIMEDOUT - Hardware component didn't reset in the expected time.
 */
int dlb_reset_domain(struct dlb_hw *hw, u32 domain_id)
{
	struct dlb_hw_domain *domain;
	int ret;

	dlb_log_reset_domain(hw, domain_id);

	domain = dlb_get_domain_from_id(hw, domain_id);

	if (!domain || !domain->configured)
		return -EINVAL;

	/*
	 * For each queue owned by this domain, disable its write permissions to
	 * cause any traffic sent to it to be dropped. Well-behaved software
	 * should not be sending QEs at this point.
	 */
	dlb_domain_disable_dir_queue_write_perms(hw, domain);

	dlb_domain_disable_ldb_queue_write_perms(hw, domain);

	/*
	 * Disable the LDB CQs and drain them in order to complete the map and
	 * unmap procedures, which require zero CQ inflights and zero QID
	 * inflights respectively.
	 */
	dlb_domain_disable_ldb_cqs(hw, domain);

	dlb_domain_drain_ldb_cqs(hw, domain, false);

	/* Re-enable the CQs in order to drain the mapped queues. */
	dlb_domain_enable_ldb_cqs(hw, domain);

	ret = dlb_domain_drain_mapped_queues(hw, domain);
	if (ret)
		return ret;

	/* Done draining LDB QEs, so disable the CQs. */
	dlb_domain_disable_ldb_cqs(hw, domain);

	dlb_domain_drain_dir_queues(hw, domain);

	/* Done draining DIR QEs, so disable the CQs. */
	dlb_domain_disable_dir_cqs(hw, domain);

	ret = dlb_domain_verify_reset_success(hw, domain);
	if (ret)
		return ret;

	/* Reset the QID and port state. */
	dlb_domain_reset_registers(hw, domain);

	return dlb_domain_reset_software_state(hw, domain);
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

/**
 * dlb_hw_enable_sparse_ldb_cq_mode() - enable sparse mode for load-balanced
 *      ports.
 * @hw: dlb_hw handle for a particular device.
 *
 * This function must be called prior to configuring scheduling domains.
 */
void dlb_hw_enable_sparse_ldb_cq_mode(struct dlb_hw *hw)
{
	u32 ctrl;

	ctrl = DLB_CSR_RD(hw, CHP_CFG_CHP_CSR_CTRL);

	ctrl |= CHP_CFG_CHP_CSR_CTRL_CFG_64BYTES_QE_LDB_CQ_MODE;

	DLB_CSR_WR(hw, CHP_CFG_CHP_CSR_CTRL, ctrl);
}

/**
 * dlb_hw_enable_sparse_dir_cq_mode() - enable sparse mode for directed ports.
 * @hw: dlb_hw handle for a particular device.
 *
 * This function must be called prior to configuring scheduling domains.
 */
void dlb_hw_enable_sparse_dir_cq_mode(struct dlb_hw *hw)
{
	u32 ctrl;

	ctrl = DLB_CSR_RD(hw, CHP_CFG_CHP_CSR_CTRL);

	ctrl |= CHP_CFG_CHP_CSR_CTRL_CFG_64BYTES_QE_DIR_CQ_MODE;

	DLB_CSR_WR(hw, CHP_CFG_CHP_CSR_CTRL, ctrl);
}
