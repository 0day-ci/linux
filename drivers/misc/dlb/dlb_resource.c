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
