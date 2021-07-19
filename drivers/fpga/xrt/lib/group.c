// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Alveo FPGA Group Driver
 *
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *	Cheng Zhen <maxz@xilinx.com>
 */

#include <linux/mod_devicetable.h>
#include "xleaf.h"
#include "subdev_pool.h"
#include "group.h"
#include "metadata.h"
#include "lib-drv.h"

#define XRT_GRP "xrt_group"

struct xrt_group {
	struct xrt_device *xdev;
	struct xrt_subdev_pool leaves;
	bool leaves_created;
	struct mutex lock; /* lock for group */
};

static int xrt_grp_root_cb(struct device *dev, void *parg,
			   enum xrt_root_cmd cmd, void *arg)
{
	int rc;
	struct xrt_device *xdev =
		container_of(dev, struct xrt_device, dev);
	struct xrt_group *xg = (struct xrt_group *)parg;

	switch (cmd) {
	case XRT_ROOT_GET_LEAF_HOLDERS: {
		struct xrt_root_get_holders *holders =
			(struct xrt_root_get_holders *)arg;
		rc = xrt_subdev_pool_get_holders(&xg->leaves,
						 holders->xpigh_xdev,
						 holders->xpigh_holder_buf,
						 holders->xpigh_holder_buf_len);
		break;
	}
	default:
		/* Forward parent call to root. */
		rc = xrt_subdev_root_request(xdev, cmd, arg);
		break;
	}

	return rc;
}

/*
 * Cut subdev's dtb from group's dtb based on passed-in endpoint descriptor.
 * Return the subdev's dtb through dtbp, if found.
 */
static int xrt_grp_cut_subdev_dtb(struct xrt_group *xg, struct xrt_dev_endpoints *eps,
				  char *grp_dtb, char **dtbp)
{
	int ret, i, ep_count = 0;
	char *dtb = NULL;

	ret = xrt_md_create(DEV(xg->xdev), &dtb);
	if (ret)
		return ret;

	for (i = 0; eps->xse_names[i].ep_name || eps->xse_names[i].compat; i++) {
		const char *ep_name = eps->xse_names[i].ep_name;
		const char *compat = eps->xse_names[i].compat;

		if (!ep_name)
			xrt_md_get_compatible_endpoint(DEV(xg->xdev), grp_dtb, compat, &ep_name);
		if (!ep_name)
			continue;

		ret = xrt_md_copy_endpoint(DEV(xg->xdev), dtb, grp_dtb, ep_name, compat, NULL);
		if (ret)
			continue;
		xrt_md_del_endpoint(DEV(xg->xdev), grp_dtb, ep_name, compat);
		ep_count++;
	}
	/* Found enough endpoints, return the subdev's dtb. */
	if (ep_count >= eps->xse_min_ep) {
		*dtbp = dtb;
		return 0;
	}

	/* Cleanup - Restore all endpoints that has been deleted, if any. */
	if (ep_count > 0) {
		xrt_md_copy_endpoint(DEV(xg->xdev), grp_dtb, dtb,
				     XRT_MD_NODE_ENDPOINTS, NULL, NULL);
	}
	vfree(dtb);
	*dtbp = NULL;
	return 0;
}

static int xrt_grp_create_leaves(struct xrt_group *xg)
{
	struct xrt_subdev_platdata *pdata = DEV_PDATA(xg->xdev);
	struct xrt_dev_endpoints *eps = NULL;
	int ret = 0, failed = 0;
	enum xrt_subdev_id did;
	char *grp_dtb = NULL;
	unsigned long mlen;

	if (!pdata)
		return -EINVAL;

	mlen = xrt_md_size(DEV(xg->xdev), pdata->xsp_dtb);
	if (mlen == XRT_MD_INVALID_LENGTH) {
		xrt_err(xg->xdev, "invalid dtb, len %ld", mlen);
		return -EINVAL;
	}

	mutex_lock(&xg->lock);

	if (xg->leaves_created) {
		/*
		 * This is expected since caller does not keep track of the state of the group
		 * and may, in some cases, still try to create leaves after it has already been
		 * created. This special error code will let the caller know what is going on.
		 */
		mutex_unlock(&xg->lock);
		return -EEXIST;
	}

	grp_dtb = vmalloc(mlen);
	if (!grp_dtb) {
		mutex_unlock(&xg->lock);
		return -ENOMEM;
	}

	/* Create all leaves based on dtb. */
	xrt_info(xg->xdev, "bringing up leaves...");
	memcpy(grp_dtb, pdata->xsp_dtb, mlen);
	for (did = 0; did < XRT_SUBDEV_NUM; did++) {
		eps = xrt_drv_get_endpoints(did);
		while (eps && eps->xse_names) {
			char *dtb = NULL;

			ret = xrt_grp_cut_subdev_dtb(xg, eps, grp_dtb, &dtb);
			if (ret) {
				failed++;
				xrt_err(xg->xdev, "failed to cut subdev dtb for drv %s: %d",
					xrt_drv_name(did), ret);
			}
			if (!dtb) {
				/*
				 * No more dtb to cut or bad things happened for this instance,
				 * switch to the next one.
				 */
				eps++;
				continue;
			}

			/* Found a dtb for this instance, let's add it. */
			ret = xrt_subdev_pool_add(&xg->leaves, did, xrt_grp_root_cb, xg, dtb);
			if (ret < 0) {
				/*
				 * It is not a fatal error here. Some functionality is not usable
				 * due to this missing device, but the error can be handled
				 * when the functionality is used.
				 */
				failed++;
				xrt_err(xg->xdev, "failed to add %s: %d", xrt_drv_name(did), ret);
			}
			vfree(dtb);
			/* Continue searching for the same instance from grp_dtb. */
		}
	}

	xg->leaves_created = true;
	vfree(grp_dtb);
	mutex_unlock(&xg->lock);
	return failed == 0 ? 0 : -ECHILD;
}

static void xrt_grp_remove_leaves(struct xrt_group *xg)
{
	mutex_lock(&xg->lock);

	if (!xg->leaves_created) {
		mutex_unlock(&xg->lock);
		return;
	}

	xrt_info(xg->xdev, "tearing down leaves...");
	xrt_subdev_pool_fini(&xg->leaves);
	xg->leaves_created = false;

	mutex_unlock(&xg->lock);
}

static int xrt_grp_probe(struct xrt_device *xdev)
{
	struct xrt_group *xg;

	xrt_info(xdev, "probing...");

	xg = devm_kzalloc(&xdev->dev, sizeof(*xg), GFP_KERNEL);
	if (!xg)
		return -ENOMEM;

	xg->xdev = xdev;
	mutex_init(&xg->lock);
	xrt_subdev_pool_init(DEV(xdev), &xg->leaves);
	xrt_set_drvdata(xdev, xg);

	return 0;
}

static void xrt_grp_remove(struct xrt_device *xdev)
{
	struct xrt_group *xg = xrt_get_drvdata(xdev);

	xrt_info(xdev, "leaving...");
	xrt_grp_remove_leaves(xg);
}

static int xrt_grp_leaf_call(struct xrt_device *xdev, u32 cmd, void *arg)
{
	int rc = 0;
	struct xrt_group *xg = xrt_get_drvdata(xdev);

	switch (cmd) {
	case XRT_XLEAF_EVENT:
		/* Simply forward to every child. */
		xrt_subdev_pool_handle_event(&xg->leaves,
					     (struct xrt_event *)arg);
		break;
	case XRT_GROUP_GET_LEAF: {
		struct xrt_root_get_leaf *get_leaf =
			(struct xrt_root_get_leaf *)arg;

		rc = xrt_subdev_pool_get(&xg->leaves, get_leaf->xpigl_match_cb,
					 get_leaf->xpigl_match_arg,
					 DEV(get_leaf->xpigl_caller_xdev),
					 &get_leaf->xpigl_tgt_xdev);
		break;
	}
	case XRT_GROUP_PUT_LEAF: {
		struct xrt_root_put_leaf *put_leaf =
			(struct xrt_root_put_leaf *)arg;

		rc = xrt_subdev_pool_put(&xg->leaves, put_leaf->xpipl_tgt_xdev,
					 DEV(put_leaf->xpipl_caller_xdev));
		break;
	}
	case XRT_GROUP_INIT_CHILDREN:
		rc = xrt_grp_create_leaves(xg);
		break;
	case XRT_GROUP_FINI_CHILDREN:
		xrt_grp_remove_leaves(xg);
		break;
	case XRT_GROUP_TRIGGER_EVENT:
		xrt_subdev_pool_trigger_event(&xg->leaves, (enum xrt_events)(uintptr_t)arg);
		break;
	default:
		xrt_err(xdev, "unknown IOCTL cmd %d", cmd);
		rc = -EINVAL;
		break;
	}
	return rc;
}

static struct xrt_driver xrt_group_driver = {
	.driver	= {
		.name    = XRT_GRP,
	},
	.subdev_id = XRT_SUBDEV_GRP,
	.probe = xrt_grp_probe,
	.remove = xrt_grp_remove,
	.leaf_call = xrt_grp_leaf_call,
};

XRT_LEAF_INIT_FINI_FUNC(group);
