/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *	Cheng Zhen <maxz@xilinx.com>
 */

#ifndef _XRT_ROOT_H_
#define _XRT_ROOT_H_

#include "xdevice.h"
#include "subdev_id.h"
#include "events.h"

typedef bool (*xrt_subdev_match_t)(enum xrt_subdev_id, struct xrt_device *, void *);
#define XRT_SUBDEV_MATCH_PREV	((xrt_subdev_match_t)-1)
#define XRT_SUBDEV_MATCH_NEXT	((xrt_subdev_match_t)-2)

/*
 * Root calls.
 */
enum xrt_root_cmd {
	/* Leaf actions. */
	XRT_ROOT_GET_LEAF = 0,
	XRT_ROOT_PUT_LEAF,
	XRT_ROOT_GET_LEAF_HOLDERS,

	/* Group actions. */
	XRT_ROOT_CREATE_GROUP,
	XRT_ROOT_REMOVE_GROUP,
	XRT_ROOT_LOOKUP_GROUP,
	XRT_ROOT_WAIT_GROUP_BRINGUP,

	/* Event actions. */
	XRT_ROOT_EVENT_SYNC,
	XRT_ROOT_EVENT_ASYNC,

	/* Device info. */
	XRT_ROOT_GET_RESOURCE,
	XRT_ROOT_GET_ID,

	/* Misc. */
	XRT_ROOT_HOT_RESET,
	XRT_ROOT_HWMON,
};

struct xrt_root_get_leaf {
	struct xrt_device *xpigl_caller_xdev;
	xrt_subdev_match_t xpigl_match_cb;
	void *xpigl_match_arg;
	struct xrt_device *xpigl_tgt_xdev;
};

struct xrt_root_put_leaf {
	struct xrt_device *xpipl_caller_xdev;
	struct xrt_device *xpipl_tgt_xdev;
};

struct xrt_root_lookup_group {
	struct xrt_device *xpilp_xdev; /* caller's xdev */
	xrt_subdev_match_t xpilp_match_cb;
	void *xpilp_match_arg;
	int xpilp_grp_inst;
};

struct xrt_root_get_holders {
	struct xrt_device *xpigh_xdev; /* caller's xdev */
	char *xpigh_holder_buf;
	size_t xpigh_holder_buf_len;
};

struct xrt_root_get_res {
	u32 xpigr_region_id;
	struct resource *xpigr_res;
};

struct xrt_root_get_id {
	unsigned short  xpigi_vendor_id;
	unsigned short  xpigi_device_id;
	unsigned short  xpigi_sub_vendor_id;
	unsigned short  xpigi_sub_device_id;
};

struct xrt_root_hwmon {
	bool xpih_register;
	const char *xpih_name;
	void *xpih_drvdata;
	const struct attribute_group **xpih_groups;
	struct device *xpih_hwmon_dev;
};

/*
 * Callback for leaf to make a root request. Arguments are: parent device, parent cookie, req,
 * and arg.
 */
typedef int (*xrt_subdev_root_cb_t)(struct device *, void *, u32, void *);
int xrt_subdev_root_request(struct xrt_device *self, u32 cmd, void *arg);

/*
 * Defines physical function (MPF / UPF) specific operations
 * needed in common root driver.
 */
struct xroot_physical_function_callback {
	void (*xpc_get_id)(struct device *dev, struct xrt_root_get_id *rid);
	int (*xpc_get_resource)(struct device *dev, struct xrt_root_get_res *res);
	void (*xpc_hot_reset)(struct device *dev);
};

int xroot_probe(struct device *dev, struct xroot_physical_function_callback *cb, void **root);
void xroot_remove(void *root);
bool xroot_wait_for_bringup(void *root);
int xroot_create_group(void *xr, char *dtb);
int xroot_add_simple_node(void *root, char *dtb, const char *endpoint);
void xroot_broadcast(void *root, enum xrt_events evt);

#endif	/* _XRT_ROOT_H_ */
