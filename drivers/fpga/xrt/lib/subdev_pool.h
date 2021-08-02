/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *	Cheng Zhen <maxz@xilinx.com>
 */

#ifndef _XRT_SUBDEV_POOL_H_
#define _XRT_SUBDEV_POOL_H_

#include <linux/device.h>
#include <linux/mutex.h>
#include "xroot.h"

/*
 * The struct xrt_subdev_pool manages a list of xrt_subdevs for root and group drivers.
 */
struct xrt_subdev_pool {
	struct list_head xsp_dev_list;
	struct device *xsp_owner;
	struct mutex xsp_lock; /* pool lock */
	bool xsp_closing;
};

/*
 * Subdev pool helper functions for root and group drivers only.
 */
void xrt_subdev_pool_init(struct device *dev,
			  struct xrt_subdev_pool *spool);
void xrt_subdev_pool_fini(struct xrt_subdev_pool *spool);
int xrt_subdev_pool_get(struct xrt_subdev_pool *spool,
			xrt_subdev_match_t match,
			void *arg, struct device *holder_dev,
			struct xrt_device **xdevp);
int xrt_subdev_pool_put(struct xrt_subdev_pool *spool,
			struct xrt_device *xdev,
			struct device *holder_dev);
int xrt_subdev_pool_add(struct xrt_subdev_pool *spool,
			enum xrt_subdev_id id, xrt_subdev_root_cb_t pcb,
			void *pcb_arg, char *dtb);
int xrt_subdev_pool_del(struct xrt_subdev_pool *spool,
			enum xrt_subdev_id id, int instance);
ssize_t xrt_subdev_pool_get_holders(struct xrt_subdev_pool *spool,
				    struct xrt_device *xdev,
				    char *buf, size_t len);

void xrt_subdev_pool_trigger_event(struct xrt_subdev_pool *spool,
				   enum xrt_events evt);
void xrt_subdev_pool_handle_event(struct xrt_subdev_pool *spool,
				  struct xrt_event *evt);

#endif	/* _XRT_SUBDEV_POOL_H_ */
