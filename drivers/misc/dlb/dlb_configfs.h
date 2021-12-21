/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(C) 2016-2020 Intel Corporation. All rights reserved. */

#ifndef __DLB_CONFIGFS_H
#define __DLB_CONFIGFS_H

#include "dlb_main.h"

struct dlb_device_configfs {
	struct config_group dev_group;
	struct dlb *dlb;
};

extern struct dlb_device_configfs dlb_dev_configfs[16];

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
	unsigned int start;

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

struct dlb_cfs_port {
	struct config_group group;
	struct config_group *domain_grp;
	unsigned int status;
	unsigned int port_id;
	unsigned int pp_fd;
	unsigned int cq_fd;
	/* Input parameters */
	unsigned int is_ldb;
	unsigned int cq_depth;
	unsigned int cq_depth_threshold;
	unsigned int cq_history_list_size;
	unsigned int create;

	/* For LDB port only */
	unsigned int queue_link[DLB_MAX_NUM_QIDS_PER_LDB_CQ];

	/* For DIR port only, default = 0xffffffff */
	unsigned int queue_id;

};

static inline
struct dlb_cfs_queue *to_dlb_cfs_queue(struct config_item *item)
{
	return container_of(to_config_group(item),
			    struct dlb_cfs_queue, group);
}

static inline
struct dlb_cfs_port *to_dlb_cfs_port(struct config_item *item)
{
	return container_of(to_config_group(item),
			    struct dlb_cfs_port, group);
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

static inline struct config_item *to_item(struct list_head *entry)
{
	return container_of(entry, struct config_item, ci_entry);
}

/*
 * Find configfs group for a port from a port_id.
 *
 */
static inline
struct dlb_cfs_port *dlb_configfs_get_port_from_id(struct dlb *dlb,
					struct dlb_domain *dlb_domain,
					int port_id)
{
	struct dlb_cfs_domain *dlb_cfs_domain = NULL;
	struct dlb_cfs_port *dlb_cfs_port = NULL;
	struct config_group *dev_grp;
	struct list_head *entry;
	int grp_found = 0;

	dev_grp = &dlb_dev_configfs[dlb->id].dev_group;

	list_for_each(entry, &dev_grp->cg_children) {
		struct config_item *item = to_item(entry);

		if (config_item_name(item))
			dev_dbg(dlb->dev,
				"%s: item = %s\n", __func__,
				config_item_name(item));

		dlb_cfs_domain = to_dlb_cfs_domain(item);

		if (dlb_cfs_domain->domain_id == dlb_domain->id) {
			grp_found = 1;
			break;
		}
	}

	if (!grp_found)
		return NULL;

	grp_found = 0;

	list_for_each(entry, &dlb_cfs_domain->group.cg_children) {
		struct config_item *item = to_item(entry);

		if (strnstr(config_item_name(item), "port", 5)) {
			dev_dbg(dlb->dev,
				"%s: item = %s\n", __func__,
				config_item_name(item));

			dlb_cfs_port = to_dlb_cfs_port(item);

			if (dlb_cfs_port->port_id == port_id) {
				grp_found = 1;
				break;
			}
		}
	}

	if (!grp_found)
		return NULL;

	return dlb_cfs_port;
}
#endif /* DLB_CONFIGFS_H */
