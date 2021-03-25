/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * RDMA Transport Layer
 *
 * Copyright (c) 2014 - 2018 ProfitBricks GmbH. All rights reserved.
 * Copyright (c) 2018 - 2019 1&1 IONOS Cloud GmbH. All rights reserved.
 * Copyright (c) 2019 - 2020 1&1 IONOS SE. All rights reserved.
 */

#ifndef RTRS_FAULT_H
#define RTRS_FAULT_H

#include <linux/fault-inject.h>

struct rtrs_fault_inject {
#ifdef CONFIG_FAULT_INJECTION_DEBUG_FS
	struct fault_attr attr;
	struct dentry *parent;
	struct dentry *dir;
	u32 status;
#endif
};

void rtrs_fault_inject_init(struct rtrs_fault_inject *fj,
			    const char *dev_name, u32 err_status);
void rtrs_fault_inject_add(struct dentry *dir, const char *fname, bool *value);
void rtrs_fault_inject_final(struct rtrs_fault_inject *fj);
#endif /* RTRS_FAULT_H */
