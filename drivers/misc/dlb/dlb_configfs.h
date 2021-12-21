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

static inline
struct dlb_cfs_domain *to_dlb_cfs_domain(struct config_item *item)
{
	return container_of(to_config_group(item),
			    struct dlb_cfs_domain, group);
}

#endif /* DLB_CONFIGFS_H */
