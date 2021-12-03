// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016-2018 IBM Corporation
 * Author:
 *   Yuqiong Sun <suny@us.ibm.com>
 *   Stefan Berger <stefanb@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 */

#include <linux/export.h>
#include <linux/user_namespace.h>
#include <linux/ima.h>
#include <linux/proc_ns.h>

int ima_init_namespace(struct ima_namespace *ns)
{
	return 0;
}

int __init ima_ns_init(void)
{
	return ima_init_namespace(&init_ima_ns);
}

struct ima_namespace init_ima_ns = {
	.kref = KREF_INIT(1),
	.user_ns = &init_user_ns,
};
EXPORT_SYMBOL(init_ima_ns);
