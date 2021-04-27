// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Alveo FPGA device node helper functions.
 *
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *	Cheng Zhen <maxz@xilinx.com>
 */

#include "xleaf.h"

extern struct class *xrt_class;

#define XRT_CDEV_DIR		"xrt"
#define INODE2PDATA(inode)	\
	container_of((inode)->i_cdev, struct xrt_subdev_platdata, xsp_cdev)
#define INODE2PDEV(inode)	\
	to_xrt_dev(kobj_to_dev((inode)->i_cdev->kobj.parent))
#define CDEV_NAME(sysdev)	(strchr((sysdev)->kobj.name, '!') + 1)

/* Allow it to be accessed from cdev. */
static void xleaf_devnode_allowed(struct xrt_device *xdev)
{
	struct xrt_subdev_platdata *pdata = DEV_PDATA(xdev);

	/* Allow new opens. */
	mutex_lock(&pdata->xsp_devnode_lock);
	pdata->xsp_devnode_online = true;
	mutex_unlock(&pdata->xsp_devnode_lock);
}

/* Turn off access from cdev and wait for all existing user to go away. */
static void xleaf_devnode_disallowed(struct xrt_device *xdev)
{
	struct xrt_subdev_platdata *pdata = DEV_PDATA(xdev);

	mutex_lock(&pdata->xsp_devnode_lock);

	/* Prevent new opens. */
	pdata->xsp_devnode_online = false;
	/* Wait for existing user to close. */
	while (pdata->xsp_devnode_ref) {
		mutex_unlock(&pdata->xsp_devnode_lock);
		wait_for_completion(&pdata->xsp_devnode_comp);
		mutex_lock(&pdata->xsp_devnode_lock);
	}

	mutex_unlock(&pdata->xsp_devnode_lock);
}

static struct xrt_device *
__xleaf_devnode_open(struct inode *inode, bool excl)
{
	struct xrt_subdev_platdata *pdata = INODE2PDATA(inode);
	struct xrt_device *xdev = INODE2PDEV(inode);
	bool opened = false;

	mutex_lock(&pdata->xsp_devnode_lock);

	if (pdata->xsp_devnode_online) {
		if (excl && pdata->xsp_devnode_ref) {
			xrt_err(xdev, "%s has already been opened exclusively",
				CDEV_NAME(pdata->xsp_sysdev));
		} else if (!excl && pdata->xsp_devnode_excl) {
			xrt_err(xdev, "%s has been opened exclusively",
				CDEV_NAME(pdata->xsp_sysdev));
		} else {
			pdata->xsp_devnode_ref++;
			pdata->xsp_devnode_excl = excl;
			opened = true;
			xrt_info(xdev, "opened %s, ref=%d",
				 CDEV_NAME(pdata->xsp_sysdev),
				 pdata->xsp_devnode_ref);
		}
	} else {
		xrt_err(xdev, "%s is offline", CDEV_NAME(pdata->xsp_sysdev));
	}

	mutex_unlock(&pdata->xsp_devnode_lock);

	xdev = opened ? xdev : NULL;
	return xdev;
}

struct xrt_device *
xleaf_devnode_open_excl(struct inode *inode)
{
	return __xleaf_devnode_open(inode, true);
}

struct xrt_device *
xleaf_devnode_open(struct inode *inode)
{
	return __xleaf_devnode_open(inode, false);
}
EXPORT_SYMBOL_GPL(xleaf_devnode_open);

void xleaf_devnode_close(struct inode *inode)
{
	struct xrt_subdev_platdata *pdata = INODE2PDATA(inode);
	struct xrt_device *xdev = INODE2PDEV(inode);
	bool notify = false;

	mutex_lock(&pdata->xsp_devnode_lock);

	WARN_ON(pdata->xsp_devnode_ref == 0);
	pdata->xsp_devnode_ref--;
	if (pdata->xsp_devnode_ref == 0) {
		pdata->xsp_devnode_excl = false;
		notify = true;
	}
	if (notify)
		xrt_info(xdev, "closed %s", CDEV_NAME(pdata->xsp_sysdev));
	else
		xrt_info(xdev, "closed %s, notifying waiter", CDEV_NAME(pdata->xsp_sysdev));

	mutex_unlock(&pdata->xsp_devnode_lock);

	if (notify)
		complete(&pdata->xsp_devnode_comp);
}
EXPORT_SYMBOL_GPL(xleaf_devnode_close);

static inline enum xrt_dev_file_mode
devnode_mode(struct xrt_device *xdev)
{
	return DEV_FILE_OPS(xdev)->xsf_mode;
}

int xleaf_devnode_create(struct xrt_device *xdev, const char *file_name,
			 const char *inst_name)
{
	struct xrt_subdev_platdata *pdata = DEV_PDATA(xdev);
	struct xrt_dev_file_ops *fops = DEV_FILE_OPS(xdev);
	struct cdev *cdevp;
	struct device *sysdev;
	int ret = 0;
	char fname[256];

	mutex_init(&pdata->xsp_devnode_lock);
	init_completion(&pdata->xsp_devnode_comp);

	cdevp = &DEV_PDATA(xdev)->xsp_cdev;
	cdev_init(cdevp, &fops->xsf_ops);
	cdevp->owner = fops->xsf_ops.owner;
	cdevp->dev = MKDEV(MAJOR(fops->xsf_dev_t), xdev->instance);

	/*
	 * Set xdev as parent of cdev so that when xdev (and its platform
	 * data) will not be freed when cdev is not freed.
	 */
	cdev_set_parent(cdevp, &DEV(xdev)->kobj);

	ret = cdev_add(cdevp, cdevp->dev, 1);
	if (ret) {
		xrt_err(xdev, "failed to add cdev: %d", ret);
		goto failed;
	}
	if (!file_name)
		file_name = xdev->name;
	if (!inst_name) {
		if (devnode_mode(xdev) == XRT_DEV_FILE_MULTI_INST) {
			snprintf(fname, sizeof(fname), "%s/%s/%s.%u",
				 XRT_CDEV_DIR, DEV_PDATA(xdev)->xsp_root_name,
				 file_name, xdev->instance);
		} else {
			snprintf(fname, sizeof(fname), "%s/%s/%s",
				 XRT_CDEV_DIR, DEV_PDATA(xdev)->xsp_root_name,
				 file_name);
		}
	} else {
		snprintf(fname, sizeof(fname), "%s/%s/%s.%s", XRT_CDEV_DIR,
			 DEV_PDATA(xdev)->xsp_root_name, file_name, inst_name);
	}
	sysdev = device_create(xrt_class, NULL, cdevp->dev, NULL, "%s", fname);
	if (IS_ERR(sysdev)) {
		ret = PTR_ERR(sysdev);
		xrt_err(xdev, "failed to create device node: %d", ret);
		goto failed_cdev_add;
	}
	pdata->xsp_sysdev = sysdev;

	xleaf_devnode_allowed(xdev);

	xrt_info(xdev, "created (%d, %d): /dev/%s",
		 MAJOR(cdevp->dev), xdev->instance, fname);
	return 0;

failed_cdev_add:
	cdev_del(cdevp);
failed:
	cdevp->owner = NULL;
	return ret;
}

void xleaf_devnode_destroy(struct xrt_device *xdev)
{
	struct xrt_subdev_platdata *pdata = DEV_PDATA(xdev);
	struct cdev *cdevp = &pdata->xsp_cdev;
	dev_t dev = cdevp->dev;

	xleaf_devnode_disallowed(xdev);

	xrt_info(xdev, "removed (%d, %d): /dev/%s/%s", MAJOR(dev), MINOR(dev),
		 XRT_CDEV_DIR, CDEV_NAME(pdata->xsp_sysdev));
	device_destroy(xrt_class, cdevp->dev);
	pdata->xsp_sysdev = NULL;
	cdev_del(cdevp);
}
