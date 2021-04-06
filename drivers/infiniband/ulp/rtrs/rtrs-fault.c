// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RDMA Transport Layer
 *
 * Copyright (c) 2014 - 2018 ProfitBricks GmbH. All rights reserved.
 * Copyright (c) 2018 - 2019 1&1 IONOS Cloud GmbH. All rights reserved.
 * Copyright (c) 2019 - 2020 1&1 IONOS SE. All rights reserved.
 */

#include "rtrs-fault.h"

static DECLARE_FAULT_ATTR(fail_default_attr);

void rtrs_fault_inject_init(struct rtrs_fault_inject *fj,
			    const char *dir_name,
			    u32 err_status)
{
	struct dentry *dir, *parent;
	struct fault_attr *attr = &fj->attr;

	/* create debugfs directory and attribute */
	parent = debugfs_create_dir(dir_name, NULL);
	if (!parent) {
		pr_warn("%s: failed to create debugfs directory\n", dir_name);
		return;
	}

	*attr = fail_default_attr;
	dir = fault_create_debugfs_attr("fault_inject", parent, attr);
	if (IS_ERR(dir)) {
		pr_warn("%s: failed to create debugfs attr\n", dir_name);
		debugfs_remove_recursive(parent);
		return;
	}
	fj->parent = parent;
	fj->dir = dir;

	/* create debugfs for status code */
	fj->status = err_status;
	debugfs_create_u32("status", 0600, dir,	&fj->status);
}

void rtrs_fault_inject_final(struct rtrs_fault_inject *fj)
{
	/* remove debugfs directories */
	debugfs_remove_recursive(fj->parent);
}

void rtrs_fault_inject_add(struct dentry *dir, const char *fname, bool *value)
{
	debugfs_create_bool(fname, 0600, dir, value);
}
