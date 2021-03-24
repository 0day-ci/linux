/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2017-2021 Intel Corporation */

#ifndef __GNA_DRIVER_H__
#define __GNA_DRIVER_H__

#include <linux/mutex.h>
#include <linux/list.h>

#define GNA_DV_NAME	"intel_gna"

struct gna_private;
struct file;

struct gna_driver_private {
	int recovery_timeout_jiffies;
};

struct gna_file_private {
	struct file *fd;
	struct gna_private *gna_priv;

	struct list_head memory_list;
	struct mutex memlist_lock;

	struct list_head flist;
};

extern struct gna_driver_private gna_drv_priv;

#endif /* __GNA_DRIVER_H__ */
