// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Alveo FPGA Root Functions
 *
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *	Cheng Zhen <maxz@xilinx.com>
 */

#include <linux/module.h>
#include <linux/hwmon.h>
#include "xroot.h"
#include "subdev_pool.h"
#include "group.h"
#include "metadata.h"

#define xroot_err(xr, fmt, args...) dev_err((xr)->dev, "%s: " fmt, __func__, ##args)
#define xroot_warn(xr, fmt, args...) dev_warn((xr)->dev, "%s: " fmt, __func__, ##args)
#define xroot_info(xr, fmt, args...) dev_info((xr)->dev, "%s: " fmt, __func__, ##args)
#define xroot_dbg(xr, fmt, args...) dev_dbg((xr)->dev, "%s: " fmt, __func__, ##args)

#define XROOT_GROUP_FIRST		(-1)
#define XROOT_GROUP_LAST		(-2)

static int xroot_root_cb(struct device *, void *, u32, void *);

struct xroot_evt {
	struct list_head list;
	struct xrt_event evt;
	struct completion comp;
	bool async;
};

struct xroot_events {
	struct mutex evt_lock; /* event lock */
	struct list_head evt_list;
	struct work_struct evt_work;
};

struct xroot_groups {
	struct xrt_subdev_pool pool;
	struct work_struct bringup_work;
	atomic_t bringup_pending_cnt;
	atomic_t bringup_failed_cnt;
	struct completion bringup_comp;
};

struct xroot {
	struct device *dev;
	struct xroot_events events;
	struct xroot_groups groups;
	struct xroot_physical_function_callback pf_cb;
};

struct xroot_group_match_arg {
	enum xrt_subdev_id id;
	int instance;
};

static bool xroot_group_match(enum xrt_subdev_id id, struct xrt_device *xdev, void *arg)
{
	struct xroot_group_match_arg *a = (struct xroot_group_match_arg *)arg;

	/* xdev->instance is the instance of the subdev. */
	return id == a->id && xdev->instance == a->instance;
}

static int xroot_get_group(struct xroot *xr, int instance, struct xrt_device **grpp)
{
	int rc = 0;
	struct xrt_subdev_pool *grps = &xr->groups.pool;
	struct device *dev = xr->dev;
	struct xroot_group_match_arg arg = { XRT_SUBDEV_GRP, instance };

	if (instance == XROOT_GROUP_LAST) {
		rc = xrt_subdev_pool_get(grps, XRT_SUBDEV_MATCH_NEXT,
					 *grpp, dev, grpp);
	} else if (instance == XROOT_GROUP_FIRST) {
		rc = xrt_subdev_pool_get(grps, XRT_SUBDEV_MATCH_PREV,
					 *grpp, dev, grpp);
	} else {
		rc = xrt_subdev_pool_get(grps, xroot_group_match,
					 &arg, dev, grpp);
	}

	if (rc && rc != -ENOENT)
		xroot_err(xr, "failed to hold group %d: %d", instance, rc);
	return rc;
}

static void xroot_put_group(struct xroot *xr, struct xrt_device *grp)
{
	int inst = grp->instance;
	int rc = xrt_subdev_pool_put(&xr->groups.pool, grp, xr->dev);

	if (rc)
		xroot_err(xr, "failed to release group %d: %d", inst, rc);
}

static int xroot_trigger_event(struct xroot *xr, struct xrt_event *e, bool async)
{
	struct xroot_evt *enew = vzalloc(sizeof(*enew));

	if (!enew)
		return -ENOMEM;

	enew->evt = *e;
	enew->async = async;
	init_completion(&enew->comp);

	mutex_lock(&xr->events.evt_lock);
	list_add(&enew->list, &xr->events.evt_list);
	mutex_unlock(&xr->events.evt_lock);

	schedule_work(&xr->events.evt_work);

	if (async)
		return 0;

	wait_for_completion(&enew->comp);
	vfree(enew);
	return 0;
}

static void
xroot_group_trigger_event(struct xroot *xr, int inst, enum xrt_events e)
{
	int ret;
	struct xrt_device *xdev = NULL;
	struct xrt_event evt = { 0 };

	WARN_ON(inst < 0);
	/* Only triggers subdev specific events. */
	if (e != XRT_EVENT_POST_CREATION && e != XRT_EVENT_PRE_REMOVAL) {
		xroot_err(xr, "invalid event %d", e);
		return;
	}

	ret = xroot_get_group(xr, inst, &xdev);
	if (ret)
		return;

	/* Triggers event for children, first. */
	xleaf_call(xdev, XRT_GROUP_TRIGGER_EVENT, (void *)(uintptr_t)e);

	/* Triggers event for itself. */
	evt.xe_evt = e;
	evt.xe_subdev.xevt_subdev_id = XRT_SUBDEV_GRP;
	evt.xe_subdev.xevt_subdev_instance = inst;
	xroot_trigger_event(xr, &evt, false);

	xroot_put_group(xr, xdev);
}

int xroot_create_group(void *root, char *dtb)
{
	struct xroot *xr = (struct xroot *)root;
	int ret;

	atomic_inc(&xr->groups.bringup_pending_cnt);
	ret = xrt_subdev_pool_add(&xr->groups.pool, XRT_SUBDEV_GRP, xroot_root_cb, xr, dtb);
	if (ret >= 0) {
		schedule_work(&xr->groups.bringup_work);
	} else {
		atomic_dec(&xr->groups.bringup_pending_cnt);
		atomic_inc(&xr->groups.bringup_failed_cnt);
		xroot_err(xr, "failed to create group: %d", ret);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(xroot_create_group);

static int xroot_destroy_single_group(struct xroot *xr, int instance)
{
	struct xrt_device *xdev = NULL;
	int ret;

	WARN_ON(instance < 0);
	ret = xroot_get_group(xr, instance, &xdev);
	if (ret)
		return ret;

	xroot_group_trigger_event(xr, instance, XRT_EVENT_PRE_REMOVAL);

	/* Now tear down all children in this group. */
	ret = xleaf_call(xdev, XRT_GROUP_FINI_CHILDREN, NULL);
	xroot_put_group(xr, xdev);
	if (!ret)
		ret = xrt_subdev_pool_del(&xr->groups.pool, XRT_SUBDEV_GRP, instance);

	return ret;
}

static int xroot_destroy_group(struct xroot *xr, int instance)
{
	struct xrt_device *target = NULL;
	struct xrt_device *deps = NULL;
	int ret;

	WARN_ON(instance < 0);
	/*
	 * Make sure target group exists and can't go away before
	 * we remove it's dependents
	 */
	ret = xroot_get_group(xr, instance, &target);
	if (ret)
		return ret;

	/*
	 * Remove all groups depend on target one.
	 * Assuming subdevs in higher group ID can depend on ones in
	 * lower ID groups, we remove them in the reservse order.
	 */
	while (xroot_get_group(xr, XROOT_GROUP_LAST, &deps) != -ENOENT) {
		int inst = deps->instance;

		xroot_put_group(xr, deps);
		/* Reached the target group instance, stop here. */
		if (instance == inst)
			break;
		xroot_destroy_single_group(xr, inst);
		deps = NULL;
	}

	/* Now we can remove the target group. */
	xroot_put_group(xr, target);
	return xroot_destroy_single_group(xr, instance);
}

static int xroot_lookup_group(struct xroot *xr,
			      struct xrt_root_lookup_group *arg)
{
	int rc = -ENOENT;
	struct xrt_device *grp = NULL;

	while (rc < 0 && xroot_get_group(xr, XROOT_GROUP_LAST, &grp) != -ENOENT) {
		if (arg->xpilp_match_cb(XRT_SUBDEV_GRP, grp, arg->xpilp_match_arg))
			rc = grp->instance;
		xroot_put_group(xr, grp);
	}
	return rc;
}

static void xroot_event_work(struct work_struct *work)
{
	struct xroot_evt *tmp;
	struct xroot *xr = container_of(work, struct xroot, events.evt_work);

	mutex_lock(&xr->events.evt_lock);
	while (!list_empty(&xr->events.evt_list)) {
		tmp = list_first_entry(&xr->events.evt_list, struct xroot_evt, list);
		list_del(&tmp->list);
		mutex_unlock(&xr->events.evt_lock);

		xrt_subdev_pool_handle_event(&xr->groups.pool, &tmp->evt);

		if (tmp->async)
			vfree(tmp);
		else
			complete(&tmp->comp);

		mutex_lock(&xr->events.evt_lock);
	}
	mutex_unlock(&xr->events.evt_lock);
}

static void xroot_event_init(struct xroot *xr)
{
	INIT_LIST_HEAD(&xr->events.evt_list);
	mutex_init(&xr->events.evt_lock);
	INIT_WORK(&xr->events.evt_work, xroot_event_work);
}

static void xroot_event_fini(struct xroot *xr)
{
	flush_scheduled_work();
	WARN_ON(!list_empty(&xr->events.evt_list));
}

static int xroot_get_leaf(struct xroot *xr, struct xrt_root_get_leaf *arg)
{
	int rc = -ENOENT;
	struct xrt_device *grp = NULL;

	while (rc && xroot_get_group(xr, XROOT_GROUP_LAST, &grp) != -ENOENT) {
		rc = xleaf_call(grp, XRT_GROUP_GET_LEAF, arg);
		xroot_put_group(xr, grp);
	}
	return rc;
}

static int xroot_put_leaf(struct xroot *xr, struct xrt_root_put_leaf *arg)
{
	int rc = -ENOENT;
	struct xrt_device *grp = NULL;

	while (rc && xroot_get_group(xr, XROOT_GROUP_LAST, &grp) != -ENOENT) {
		rc = xleaf_call(grp, XRT_GROUP_PUT_LEAF, arg);
		xroot_put_group(xr, grp);
	}
	return rc;
}

static int xroot_root_cb(struct device *dev, void *parg, enum xrt_root_cmd cmd, void *arg)
{
	struct xroot *xr = (struct xroot *)parg;
	int rc = 0;

	switch (cmd) {
	/* Leaf actions. */
	case XRT_ROOT_GET_LEAF: {
		struct xrt_root_get_leaf *getleaf = (struct xrt_root_get_leaf *)arg;

		rc = xroot_get_leaf(xr, getleaf);
		break;
	}
	case XRT_ROOT_PUT_LEAF: {
		struct xrt_root_put_leaf *putleaf = (struct xrt_root_put_leaf *)arg;

		rc = xroot_put_leaf(xr, putleaf);
		break;
	}
	case XRT_ROOT_GET_LEAF_HOLDERS: {
		struct xrt_root_get_holders *holders = (struct xrt_root_get_holders *)arg;

		rc = xrt_subdev_pool_get_holders(&xr->groups.pool,
						 holders->xpigh_xdev,
						 holders->xpigh_holder_buf,
						 holders->xpigh_holder_buf_len);
		break;
	}

	/* Group actions. */
	case XRT_ROOT_CREATE_GROUP:
		rc = xroot_create_group(xr, (char *)arg);
		break;
	case XRT_ROOT_REMOVE_GROUP:
		rc = xroot_destroy_group(xr, (int)(uintptr_t)arg);
		break;
	case XRT_ROOT_LOOKUP_GROUP: {
		struct xrt_root_lookup_group *getgrp = (struct xrt_root_lookup_group *)arg;

		rc = xroot_lookup_group(xr, getgrp);
		break;
	}
	case XRT_ROOT_WAIT_GROUP_BRINGUP:
		rc = xroot_wait_for_bringup(xr) ? 0 : -EINVAL;
		break;

	/* Event actions. */
	case XRT_ROOT_EVENT_SYNC:
	case XRT_ROOT_EVENT_ASYNC: {
		bool async = (cmd == XRT_ROOT_EVENT_ASYNC);
		struct xrt_event *evt = (struct xrt_event *)arg;

		rc = xroot_trigger_event(xr, evt, async);
		break;
	}

	/* Device info. */
	case XRT_ROOT_GET_RESOURCE: {
		struct xrt_root_get_res *res = (struct xrt_root_get_res *)arg;

		if (xr->pf_cb.xpc_get_resource) {
			rc = xr->pf_cb.xpc_get_resource(xr->dev, res);
		} else {
			xroot_err(xr, "get resource is not supported");
			rc = -EOPNOTSUPP;
		}
		break;
	}
	case XRT_ROOT_GET_ID: {
		struct xrt_root_get_id *id = (struct xrt_root_get_id *)arg;

		if (xr->pf_cb.xpc_get_id)
			xr->pf_cb.xpc_get_id(xr->dev, id);
		else
			memset(id, 0, sizeof(*id));
		break;
	}

	/* MISC generic root driver functions. */
	case XRT_ROOT_HOT_RESET: {
		if (xr->pf_cb.xpc_hot_reset) {
			xr->pf_cb.xpc_hot_reset(xr->dev);
		} else {
			xroot_err(xr, "hot reset is not supported");
			rc = -EOPNOTSUPP;
		}
		break;
	}
	case XRT_ROOT_HWMON: {
		struct xrt_root_hwmon *hwmon = (struct xrt_root_hwmon *)arg;

		if (hwmon->xpih_register) {
			hwmon->xpih_hwmon_dev =
				hwmon_device_register_with_info(xr->dev,
								hwmon->xpih_name,
								hwmon->xpih_drvdata,
								NULL,
								hwmon->xpih_groups);
		} else {
			hwmon_device_unregister(hwmon->xpih_hwmon_dev);
		}
		break;
	}

	default:
		xroot_err(xr, "unknown IOCTL cmd %d", cmd);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static void xroot_bringup_group_work(struct work_struct *work)
{
	struct xrt_device *xdev = NULL;
	struct xroot *xr = container_of(work, struct xroot, groups.bringup_work);

	while (xroot_get_group(xr, XROOT_GROUP_FIRST, &xdev) != -ENOENT) {
		int r, i;

		i = xdev->instance;
		r = xleaf_call(xdev, XRT_GROUP_INIT_CHILDREN, NULL);
		xroot_put_group(xr, xdev);
		if (r == -EEXIST)
			continue; /* Already brough up, nothing to do. */
		if (r)
			atomic_inc(&xr->groups.bringup_failed_cnt);

		xroot_group_trigger_event(xr, i, XRT_EVENT_POST_CREATION);

		if (atomic_dec_and_test(&xr->groups.bringup_pending_cnt))
			complete(&xr->groups.bringup_comp);
	}
}

static void xroot_groups_init(struct xroot *xr)
{
	xrt_subdev_pool_init(xr->dev, &xr->groups.pool);
	INIT_WORK(&xr->groups.bringup_work, xroot_bringup_group_work);
	atomic_set(&xr->groups.bringup_pending_cnt, 0);
	atomic_set(&xr->groups.bringup_failed_cnt, 0);
	init_completion(&xr->groups.bringup_comp);
}

static void xroot_groups_fini(struct xroot *xr)
{
	flush_scheduled_work();
	xrt_subdev_pool_fini(&xr->groups.pool);
}

int xroot_add_simple_node(void *root, char *dtb, const char *endpoint)
{
	struct xroot *xr = (struct xroot *)root;
	struct device *dev = xr->dev;
	struct xrt_md_endpoint ep = { 0 };
	int ret = 0;

	ep.ep_name = endpoint;
	ret = xrt_md_add_endpoint(dev, dtb, &ep);
	if (ret)
		xroot_err(xr, "add %s failed, ret %d", endpoint, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(xroot_add_simple_node);

bool xroot_wait_for_bringup(void *root)
{
	struct xroot *xr = (struct xroot *)root;

	wait_for_completion(&xr->groups.bringup_comp);
	return atomic_read(&xr->groups.bringup_failed_cnt) == 0;
}
EXPORT_SYMBOL_GPL(xroot_wait_for_bringup);

int xroot_probe(struct device *dev, struct xroot_physical_function_callback *cb, void **root)
{
	struct xroot *xr = NULL;

	dev_info(dev, "%s: probing...", __func__);

	xr = devm_kzalloc(dev, sizeof(*xr), GFP_KERNEL);
	if (!xr)
		return -ENOMEM;

	xr->dev = dev;
	xr->pf_cb = *cb;
	xroot_groups_init(xr);
	xroot_event_init(xr);

	*root = xr;
	return 0;
}
EXPORT_SYMBOL_GPL(xroot_probe);

void xroot_remove(void *root)
{
	struct xroot *xr = (struct xroot *)root;
	struct xrt_device *grp = NULL;

	xroot_info(xr, "leaving...");

	if (xroot_get_group(xr, XROOT_GROUP_FIRST, &grp) == 0) {
		int instance = grp->instance;

		xroot_put_group(xr, grp);
		xroot_destroy_group(xr, instance);
	}

	xroot_event_fini(xr);
	xroot_groups_fini(xr);
}
EXPORT_SYMBOL_GPL(xroot_remove);

void xroot_broadcast(void *root, enum xrt_events evt)
{
	struct xroot *xr = (struct xroot *)root;
	struct xrt_event e = { 0 };

	/* Root pf driver only broadcasts below two events. */
	if (evt != XRT_EVENT_POST_CREATION && evt != XRT_EVENT_PRE_REMOVAL) {
		xroot_info(xr, "invalid event %d", evt);
		return;
	}

	e.xe_evt = evt;
	e.xe_subdev.xevt_subdev_id = XRT_ROOT;
	e.xe_subdev.xevt_subdev_instance = 0;
	xroot_trigger_event(xr, &e, false);
}
EXPORT_SYMBOL_GPL(xroot_broadcast);
