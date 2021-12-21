/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(C) 2016-2020 Intel Corporation. All rights reserved. */

#ifndef __DLB_CONFIGFS_H
#define __DLB_CONFIGFS_H

#include "dlb_main.h"

struct dlb_device_configfs {
	struct config_group dev_group;
	struct dlb *dlb;
};

struct dlb_cfs_domain {
	struct config_group group;
	struct config_group *dev_grp;
	unsigned int status;
	unsigned int domain_id;
	/* Input parameters */
	unsigned int domain_fd;
	unsigned int num_ldb_queues;
	unsigned int num_ldb_ports;
	unsigned int num_dir_ports;
	unsigned int num_atomic_inflights;
	unsigned int num_hist_list_entries;
	unsigned int num_ldb_credits;
	unsigned int num_dir_credits;
	unsigned int create;

};

struct dlb_cfs_queue {
	struct config_group group;
	struct config_group *domain_grp;
	unsigned int status;
	unsigned int queue_id;
	/* Input parameters */
	unsigned int is_ldb;
	unsigned int queue_depth;
	unsigned int depth_threshold;
	unsigned int create;

	/* For LDB queue only */
	unsigned int num_sequence_numbers;
	unsigned int num_qid_inflights;
	unsigned int num_atomic_inflights;
	unsigned int lock_id_comp_level;

	/* For DIR queue only, default = 0xffffffff */
	unsigned int port_id;

};

static inline
struct dlb_cfs_queue *to_dlb_cfs_queue(struct config_item *item)
{
	return container_of(to_config_group(item),
			    struct dlb_cfs_queue, group);
}

static inline
struct dlb_cfs_domain *to_dlb_cfs_domain(struct config_item *item)
{
	return container_of(to_config_group(item),
			    struct dlb_cfs_domain, group);
}

/*
 * Get the dlb and dlb_domain pointers from the domain configfs group
 * in the dlb_cfs_domain structure.
 */
static
int dlb_configfs_get_dlb_domain(struct config_group *domain_grp,
				struct dlb **dlb,
				struct dlb_domain **dlb_domain)
{
	struct dlb_device_configfs *dlb_dev_configfs;
	struct dlb_cfs_domain *dlb_cfs_domain;

	dlb_cfs_domain = container_of(domain_grp, struct dlb_cfs_domain, group);

	dlb_dev_configfs = container_of(dlb_cfs_domain->dev_grp,
					struct dlb_device_configfs,
					dev_group);
	*dlb = dlb_dev_configfs->dlb;

	if (!*dlb)
		return -EINVAL;

	*dlb_domain = (*dlb)->sched_domains[dlb_cfs_domain->domain_id];

	if (!*dlb_domain)
		return -EINVAL;

	return 0;
}
#endif /* DLB_CONFIGFS_H */
