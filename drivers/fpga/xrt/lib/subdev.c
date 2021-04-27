// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *	Cheng Zhen <maxz@xilinx.com>
 */

#include <linux/vmalloc.h>
#include <linux/slab.h>
#include "xleaf.h"
#include "subdev_pool.h"
#include "lib-drv.h"
#include "metadata.h"

extern struct bus_type xrt_bus_type;

#define IS_ROOT_DEV(dev)	((dev)->bus != &xrt_bus_type)
#define XRT_HOLDER_BUF_SZ	1024

static inline struct device *find_root(struct xrt_device *xdev)
{
	struct device *d = DEV(xdev);

	while (!IS_ROOT_DEV(d))
		d = d->parent;
	return d;
}

/*
 * It represents a holder of a subdev. One holder can repeatedly hold a subdev
 * as long as there is a unhold corresponding to a hold.
 */
struct xrt_subdev_holder {
	struct list_head xsh_holder_list;
	struct device *xsh_holder;
	int xsh_count;
	struct kref xsh_kref;
};

/*
 * It represents a specific instance of platform driver for a subdev, which
 * provides services to its clients (another subdev driver or root driver).
 */
struct xrt_subdev {
	struct list_head xs_dev_list;
	struct list_head xs_holder_list;
	enum xrt_subdev_id xs_id;		/* type of subdev */
	struct xrt_device *xs_xdev;
	struct completion xs_holder_comp;
};

static struct xrt_subdev *xrt_subdev_alloc(void)
{
	struct xrt_subdev *sdev = kzalloc(sizeof(*sdev), GFP_KERNEL);

	if (!sdev)
		return NULL;

	INIT_LIST_HEAD(&sdev->xs_dev_list);
	INIT_LIST_HEAD(&sdev->xs_holder_list);
	init_completion(&sdev->xs_holder_comp);
	return sdev;
}

int xrt_subdev_root_request(struct xrt_device *self, u32 cmd, void *arg)
{
	struct device *dev = DEV(self);
	struct xrt_subdev_platdata *pdata = DEV_PDATA(self);

	WARN_ON(!pdata->xsp_root_cb);
	return (*pdata->xsp_root_cb)(dev->parent, pdata->xsp_root_cb_arg, cmd, arg);
}

/*
 * Subdev common sysfs nodes.
 */
static ssize_t holders_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len;
	struct xrt_device *xdev = to_xrt_dev(dev);
	struct xrt_root_get_holders holders = { xdev, buf, XRT_HOLDER_BUF_SZ };

	len = xrt_subdev_root_request(xdev, XRT_ROOT_GET_LEAF_HOLDERS, &holders);
	if (len >= holders.xpigh_holder_buf_len)
		return len;
	buf[len] = '\n';
	return len + 1;
}
static DEVICE_ATTR_RO(holders);

static struct attribute *xrt_subdev_attrs[] = {
	&dev_attr_holders.attr,
	NULL,
};

static ssize_t metadata_output(struct file *filp, struct kobject *kobj,
			       struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct xrt_device *xdev = to_xrt_dev(dev);
	struct xrt_subdev_platdata *pdata = DEV_PDATA(xdev);
	unsigned char *blob;
	unsigned long  size;
	ssize_t ret = 0;

	blob = pdata->xsp_dtb;
	size = xrt_md_size(dev, blob);
	if (size == XRT_MD_INVALID_LENGTH) {
		ret = -EINVAL;
		goto failed;
	}

	if (off >= size) {
		dev_dbg(dev, "offset (%lld) beyond total size: %ld\n", off, size);
		goto failed;
	}

	if (off + count > size) {
		dev_dbg(dev, "count (%ld) beyond left bytes: %lld\n", count, size - off);
		count = size - off;
	}
	memcpy(buf, blob + off, count);

	ret = count;
failed:
	return ret;
}

static struct bin_attribute meta_data_attr = {
	.attr = {
		.name = "metadata",
		.mode = 0400
	},
	.read = metadata_output,
	.size = 0
};

static struct bin_attribute  *xrt_subdev_bin_attrs[] = {
	&meta_data_attr,
	NULL,
};

static const struct attribute_group xrt_subdev_attrgroup = {
	.attrs = xrt_subdev_attrs,
	.bin_attrs = xrt_subdev_bin_attrs,
};

/*
 * Given the device metadata, parse it to get IO ranges and construct
 * resource array.
 */
static int
xrt_subdev_getres(struct device *parent, enum xrt_subdev_id id,
		  char *dtb, struct resource **res, int *res_num)
{
	struct xrt_subdev_platdata *pdata;
	struct resource *pci_res = NULL;
	const u64 *bar_range;
	const u32 *bar_idx;
	char *ep_name = NULL, *compat = NULL;
	uint bar;
	int count1 = 0, count2 = 0, ret;

	if (!dtb)
		return -EINVAL;

	pdata = DEV_PDATA(to_xrt_dev(parent));

	/* go through metadata and count endpoints in it */
	xrt_md_get_next_endpoint(parent, dtb, NULL, NULL, &ep_name, &compat);
	while (ep_name) {
		ret = xrt_md_get_prop(parent, dtb, ep_name, compat,
				      XRT_MD_PROP_IO_OFFSET, (const void **)&bar_range, NULL);
		if (!ret)
			count1++;
		xrt_md_get_next_endpoint(parent, dtb, ep_name, compat, &ep_name, &compat);
	}
	if (!count1)
		return 0;

	/* allocate resource array for all endpoints been found in metadata */
	*res = vzalloc(sizeof(**res) * count1);

	/* go through all endpoints again and get IO range for each endpoint */
	ep_name = NULL;
	xrt_md_get_next_endpoint(parent, dtb, NULL, NULL, &ep_name, &compat);
	while (ep_name) {
		ret = xrt_md_get_prop(parent, dtb, ep_name, compat,
				      XRT_MD_PROP_IO_OFFSET, (const void **)&bar_range, NULL);
		if (ret)
			continue;
		xrt_md_get_prop(parent, dtb, ep_name, compat,
				XRT_MD_PROP_BAR_IDX, (const void **)&bar_idx, NULL);
		bar = bar_idx ? be32_to_cpu(*bar_idx) : 0;
		xleaf_get_root_res(to_xrt_dev(parent), bar, &pci_res);
		(*res)[count2].start = pci_res->start + be64_to_cpu(bar_range[0]);
		(*res)[count2].end = pci_res->start + be64_to_cpu(bar_range[0]) +
			be64_to_cpu(bar_range[1]) - 1;
		(*res)[count2].flags = IORESOURCE_MEM;
		/* check if there is conflicted resource */
		ret = request_resource(pci_res, *res + count2);
		if (ret) {
			dev_err(parent, "Conflict resource %pR\n", *res + count2);
			vfree(*res);
			*res_num = 0;
			*res = NULL;
			return ret;
		}
		release_resource(*res + count2);

		(*res)[count2].parent = pci_res;

		xrt_md_find_endpoint(parent, pdata->xsp_dtb, ep_name,
				     compat, &(*res)[count2].name);

		count2++;
		xrt_md_get_next_endpoint(parent, dtb, ep_name, compat, &ep_name, &compat);
	}

	WARN_ON(count1 != count2);
	*res_num = count2;

	return 0;
}

static inline enum xrt_dev_file_mode
xleaf_devnode_mode(struct xrt_device *xdev)
{
	return DEV_FILE_OPS(xdev)->xsf_mode;
}

static bool xrt_subdev_cdev_auto_creation(struct xrt_device *xdev)
{
	enum xrt_dev_file_mode mode = xleaf_devnode_mode(xdev);

	if (!xleaf_devnode_enabled(xdev))
		return false;

	return (mode == XRT_DEV_FILE_DEFAULT || mode == XRT_DEV_FILE_MULTI_INST);
}

static struct xrt_subdev *
xrt_subdev_create(struct device *parent, enum xrt_subdev_id id,
		  xrt_subdev_root_cb_t pcb, void *pcb_arg, char *dtb)
{
	struct xrt_subdev_platdata *pdata = NULL;
	struct xrt_subdev *sdev = NULL;
	struct xrt_device *xdev = NULL;
	struct resource *res = NULL;
	unsigned long dtb_len = 0;
	bool dtb_alloced = false;
	int res_num = 0;
	size_t pdata_sz;
	int ret;

	sdev = xrt_subdev_alloc();
	if (!sdev) {
		dev_err(parent, "failed to alloc subdev for ID %d", id);
		return NULL;
	}
	sdev->xs_id = id;

	if (!dtb) {
		ret = xrt_md_create(parent, &dtb);
		if (ret) {
			dev_err(parent, "can't create empty dtb: %d", ret);
			goto fail;
		}
		dtb_alloced = true;
	}
	xrt_md_pack(parent, dtb);
	dtb_len = xrt_md_size(parent, dtb);
	if (dtb_len == XRT_MD_INVALID_LENGTH) {
		dev_err(parent, "invalid metadata len %ld", dtb_len);
		goto fail1;
	}
	pdata_sz = sizeof(struct xrt_subdev_platdata) + dtb_len;

	/* Prepare platform data passed to subdev. */
	pdata = vzalloc(pdata_sz);
	if (!pdata)
		goto fail1;

	pdata->xsp_root_cb = pcb;
	pdata->xsp_root_cb_arg = pcb_arg;
	memcpy(pdata->xsp_dtb, dtb, dtb_len);
	if (id == XRT_SUBDEV_GRP) {
		/* Group can only be created by root driver. */
		pdata->xsp_root_name = dev_name(parent);
	} else {
		struct xrt_device *grp = to_xrt_dev(parent);

		/* Leaf can only be created by group driver. */
		WARN_ON(to_xrt_drv(parent->driver)->subdev_id != XRT_SUBDEV_GRP);
		pdata->xsp_root_name = DEV_PDATA(grp)->xsp_root_name;
	}

	/* Create subdev. */
	if (id != XRT_SUBDEV_GRP) {
		int rc = xrt_subdev_getres(parent, id, dtb, &res, &res_num);

		if (rc) {
			dev_err(parent, "failed to get resource for %s: %d",
				xrt_drv_name(id), rc);
			goto fail2;
		}
	}
	xdev = xrt_device_register(parent, id, res, res_num, pdata, pdata_sz);
	vfree(res);
	if (!xdev) {
		dev_err(parent, "failed to create subdev for %s", xrt_drv_name(id));
		goto fail2;
	}
	sdev->xs_xdev = xdev;

	if (device_attach(DEV(xdev)) != 1) {
		xrt_err(xdev, "failed to attach");
		goto fail3;
	}

	if (sysfs_create_group(&DEV(xdev)->kobj, &xrt_subdev_attrgroup))
		xrt_err(xdev, "failed to create sysfs group");

	/*
	 * Create sysfs sym link under root for leaves
	 * under random groups for easy access to them.
	 */
	if (id != XRT_SUBDEV_GRP) {
		if (sysfs_create_link(&find_root(xdev)->kobj,
				      &DEV(xdev)->kobj, dev_name(DEV(xdev)))) {
			xrt_err(xdev, "failed to create sysfs link");
		}
	}

	/* All done, ready to handle req thru cdev. */
	if (xrt_subdev_cdev_auto_creation(xdev))
		xleaf_devnode_create(xdev, DEV_FILE_OPS(xdev)->xsf_dev_name, NULL);

	vfree(pdata);
	return sdev;

fail3:
	xrt_device_unregister(sdev->xs_xdev);
fail2:
	vfree(pdata);
fail1:
	if (dtb_alloced)
		vfree(dtb);
fail:
	kfree(sdev);
	return NULL;
}

static void xrt_subdev_destroy(struct xrt_subdev *sdev)
{
	struct xrt_device *xdev = sdev->xs_xdev;
	struct device *dev = DEV(xdev);

	/* Take down the device node */
	if (xrt_subdev_cdev_auto_creation(xdev))
		xleaf_devnode_destroy(xdev);
	if (sdev->xs_id != XRT_SUBDEV_GRP)
		sysfs_remove_link(&find_root(xdev)->kobj, dev_name(dev));
	sysfs_remove_group(&dev->kobj, &xrt_subdev_attrgroup);
	xrt_device_unregister(xdev);
	kfree(sdev);
}

struct xrt_device *
xleaf_get_leaf(struct xrt_device *xdev, xrt_subdev_match_t match_cb, void *match_arg)
{
	int rc;
	struct xrt_root_get_leaf get_leaf = {
		xdev, match_cb, match_arg, };

	rc = xrt_subdev_root_request(xdev, XRT_ROOT_GET_LEAF, &get_leaf);
	if (rc)
		return NULL;
	return get_leaf.xpigl_tgt_xdev;
}
EXPORT_SYMBOL_GPL(xleaf_get_leaf);

bool xleaf_has_endpoint(struct xrt_device *xdev, const char *endpoint_name)
{
	struct resource	*res;
	int i = 0;

	do {
		res = xrt_get_resource(xdev, IORESOURCE_MEM, i);
		if (res && !strncmp(res->name, endpoint_name, strlen(res->name) + 1))
			return true;
		++i;
	} while (res);

	return false;
}
EXPORT_SYMBOL_GPL(xleaf_has_endpoint);

int xleaf_put_leaf(struct xrt_device *xdev, struct xrt_device *leaf)
{
	struct xrt_root_put_leaf put_leaf = { xdev, leaf };

	return xrt_subdev_root_request(xdev, XRT_ROOT_PUT_LEAF, &put_leaf);
}
EXPORT_SYMBOL_GPL(xleaf_put_leaf);

int xleaf_create_group(struct xrt_device *xdev, char *dtb)
{
	return xrt_subdev_root_request(xdev, XRT_ROOT_CREATE_GROUP, dtb);
}
EXPORT_SYMBOL_GPL(xleaf_create_group);

int xleaf_destroy_group(struct xrt_device *xdev, int instance)
{
	return xrt_subdev_root_request(xdev, XRT_ROOT_REMOVE_GROUP, (void *)(uintptr_t)instance);
}
EXPORT_SYMBOL_GPL(xleaf_destroy_group);

int xleaf_wait_for_group_bringup(struct xrt_device *xdev)
{
	return xrt_subdev_root_request(xdev, XRT_ROOT_WAIT_GROUP_BRINGUP, NULL);
}
EXPORT_SYMBOL_GPL(xleaf_wait_for_group_bringup);

static ssize_t
xrt_subdev_get_holders(struct xrt_subdev *sdev, char *buf, size_t len)
{
	const struct list_head *ptr;
	struct xrt_subdev_holder *h;
	ssize_t n = 0;

	list_for_each(ptr, &sdev->xs_holder_list) {
		h = list_entry(ptr, struct xrt_subdev_holder, xsh_holder_list);
		n += snprintf(buf + n, len - n, "%s:%d ",
			      dev_name(h->xsh_holder), kref_read(&h->xsh_kref));
		/* Truncation is fine here. Buffer content is only for debugging. */
		if (n >= (len - 1))
			break;
	}
	return n;
}

void xrt_subdev_pool_init(struct device *dev, struct xrt_subdev_pool *spool)
{
	INIT_LIST_HEAD(&spool->xsp_dev_list);
	spool->xsp_owner = dev;
	mutex_init(&spool->xsp_lock);
	spool->xsp_closing = false;
}

static void xrt_subdev_free_holder(struct xrt_subdev_holder *holder)
{
	list_del(&holder->xsh_holder_list);
	vfree(holder);
}

static void xrt_subdev_pool_wait_for_holders(struct xrt_subdev_pool *spool, struct xrt_subdev *sdev)
{
	const struct list_head *ptr, *next;
	char holders[128];
	struct xrt_subdev_holder *holder;
	struct mutex *lk = &spool->xsp_lock;

	while (!list_empty(&sdev->xs_holder_list)) {
		int rc;

		/* It's most likely a bug if we ever enters this loop. */
		xrt_subdev_get_holders(sdev, holders, sizeof(holders));
		xrt_err(sdev->xs_xdev, "awaits holders: %s", holders);
		mutex_unlock(lk);
		rc = wait_for_completion_killable(&sdev->xs_holder_comp);
		mutex_lock(lk);
		if (rc == -ERESTARTSYS) {
			xrt_err(sdev->xs_xdev, "give up on waiting for holders, clean up now");
			list_for_each_safe(ptr, next, &sdev->xs_holder_list) {
				holder = list_entry(ptr, struct xrt_subdev_holder, xsh_holder_list);
				xrt_subdev_free_holder(holder);
			}
		}
	}
}

void xrt_subdev_pool_fini(struct xrt_subdev_pool *spool)
{
	struct list_head *dl = &spool->xsp_dev_list;
	struct mutex *lk = &spool->xsp_lock;

	mutex_lock(lk);
	if (spool->xsp_closing) {
		mutex_unlock(lk);
		return;
	}
	spool->xsp_closing = true;
	mutex_unlock(lk);

	/* Remove subdev in the reverse order of added. */
	while (!list_empty(dl)) {
		struct xrt_subdev *sdev = list_first_entry(dl, struct xrt_subdev, xs_dev_list);

		xrt_subdev_pool_wait_for_holders(spool, sdev);
		list_del(&sdev->xs_dev_list);
		xrt_subdev_destroy(sdev);
	}
}

static struct xrt_subdev_holder *xrt_subdev_find_holder(struct xrt_subdev *sdev,
							struct device *holder_dev)
{
	struct list_head *hl = &sdev->xs_holder_list;
	struct xrt_subdev_holder *holder;
	const struct list_head *ptr;

	list_for_each(ptr, hl) {
		holder = list_entry(ptr, struct xrt_subdev_holder, xsh_holder_list);
		if (holder->xsh_holder == holder_dev)
			return holder;
	}
	return NULL;
}

static int xrt_subdev_hold(struct xrt_subdev *sdev, struct device *holder_dev)
{
	struct xrt_subdev_holder *holder = xrt_subdev_find_holder(sdev, holder_dev);
	struct list_head *hl = &sdev->xs_holder_list;

	if (!holder) {
		holder = vzalloc(sizeof(*holder));
		if (!holder)
			return -ENOMEM;
		holder->xsh_holder = holder_dev;
		kref_init(&holder->xsh_kref);
		list_add_tail(&holder->xsh_holder_list, hl);
	} else {
		kref_get(&holder->xsh_kref);
	}

	return 0;
}

static void xrt_subdev_free_holder_kref(struct kref *kref)
{
	struct xrt_subdev_holder *holder = container_of(kref, struct xrt_subdev_holder, xsh_kref);

	xrt_subdev_free_holder(holder);
}

static int
xrt_subdev_release(struct xrt_subdev *sdev, struct device *holder_dev)
{
	struct xrt_subdev_holder *holder = xrt_subdev_find_holder(sdev, holder_dev);
	struct list_head *hl = &sdev->xs_holder_list;

	if (!holder) {
		dev_err(holder_dev, "can't release, %s did not hold %s",
			dev_name(holder_dev), dev_name(DEV(sdev->xs_xdev)));
		return -EINVAL;
	}
	kref_put(&holder->xsh_kref, xrt_subdev_free_holder_kref);

	/* kref_put above may remove holder from list. */
	if (list_empty(hl))
		complete(&sdev->xs_holder_comp);
	return 0;
}

int xrt_subdev_pool_add(struct xrt_subdev_pool *spool, enum xrt_subdev_id id,
			xrt_subdev_root_cb_t pcb, void *pcb_arg, char *dtb)
{
	struct mutex *lk = &spool->xsp_lock;
	struct list_head *dl = &spool->xsp_dev_list;
	struct xrt_subdev *sdev;
	int ret = 0;

	sdev = xrt_subdev_create(spool->xsp_owner, id, pcb, pcb_arg, dtb);
	if (sdev) {
		mutex_lock(lk);
		if (spool->xsp_closing) {
			/* No new subdev when pool is going away. */
			xrt_err(sdev->xs_xdev, "pool is closing");
			ret = -ENODEV;
		} else {
			list_add(&sdev->xs_dev_list, dl);
		}
		mutex_unlock(lk);
		if (ret)
			xrt_subdev_destroy(sdev);
	} else {
		ret = -EINVAL;
	}

	ret = ret ? ret : sdev->xs_xdev->instance;
	return ret;
}

int xrt_subdev_pool_del(struct xrt_subdev_pool *spool, enum xrt_subdev_id id, int instance)
{
	const struct list_head *ptr;
	struct mutex *lk = &spool->xsp_lock;
	struct list_head *dl = &spool->xsp_dev_list;
	struct xrt_subdev *sdev;
	int ret = -ENOENT;

	mutex_lock(lk);
	if (spool->xsp_closing) {
		/* Pool is going away, all subdevs will be gone. */
		mutex_unlock(lk);
		return 0;
	}
	list_for_each(ptr, dl) {
		sdev = list_entry(ptr, struct xrt_subdev, xs_dev_list);
		if (sdev->xs_id != id || sdev->xs_xdev->instance != instance)
			continue;
		xrt_subdev_pool_wait_for_holders(spool, sdev);
		list_del(&sdev->xs_dev_list);
		ret = 0;
		break;
	}
	mutex_unlock(lk);
	if (ret)
		return ret;

	xrt_subdev_destroy(sdev);
	return 0;
}

static int xrt_subdev_pool_get_impl(struct xrt_subdev_pool *spool, xrt_subdev_match_t match,
				    void *arg, struct device *holder_dev, struct xrt_subdev **sdevp)
{
	struct xrt_device *xdev = (struct xrt_device *)arg;
	struct list_head *dl = &spool->xsp_dev_list;
	struct mutex *lk = &spool->xsp_lock;
	struct xrt_subdev *sdev = NULL;
	const struct list_head *ptr;
	struct xrt_subdev *d = NULL;
	int ret = -ENOENT;

	mutex_lock(lk);

	if (!xdev) {
		if (match == XRT_SUBDEV_MATCH_PREV) {
			sdev = list_empty(dl) ? NULL :
				list_last_entry(dl, struct xrt_subdev, xs_dev_list);
		} else if (match == XRT_SUBDEV_MATCH_NEXT) {
			sdev = list_first_entry_or_null(dl, struct xrt_subdev, xs_dev_list);
		}
	}

	list_for_each(ptr, dl) {
		d = list_entry(ptr, struct xrt_subdev, xs_dev_list);
		if (match == XRT_SUBDEV_MATCH_PREV || match == XRT_SUBDEV_MATCH_NEXT) {
			if (d->xs_xdev != xdev)
				continue;
		} else {
			if (!match(d->xs_id, d->xs_xdev, arg))
				continue;
		}

		if (match == XRT_SUBDEV_MATCH_PREV)
			sdev = !list_is_first(ptr, dl) ? list_prev_entry(d, xs_dev_list) : NULL;
		else if (match == XRT_SUBDEV_MATCH_NEXT)
			sdev = !list_is_last(ptr, dl) ? list_next_entry(d, xs_dev_list) : NULL;
		else
			sdev = d;
	}

	if (sdev)
		ret = xrt_subdev_hold(sdev, holder_dev);

	mutex_unlock(lk);

	if (!ret)
		*sdevp = sdev;
	return ret;
}

int xrt_subdev_pool_get(struct xrt_subdev_pool *spool, xrt_subdev_match_t match, void *arg,
			struct device *holder_dev, struct xrt_device **xdevp)
{
	int rc;
	struct xrt_subdev *sdev;

	rc = xrt_subdev_pool_get_impl(spool, match, arg, holder_dev, &sdev);
	if (rc) {
		if (rc != -ENOENT)
			dev_err(holder_dev, "failed to hold device: %d", rc);
		return rc;
	}

	if (!IS_ROOT_DEV(holder_dev)) {
		xrt_dbg(to_xrt_dev(holder_dev), "%s <<==== %s",
			dev_name(holder_dev), dev_name(DEV(sdev->xs_xdev)));
	}

	*xdevp = sdev->xs_xdev;
	return 0;
}

static int xrt_subdev_pool_put_impl(struct xrt_subdev_pool *spool, struct xrt_device *xdev,
				    struct device *holder_dev)
{
	const struct list_head *ptr;
	struct mutex *lk = &spool->xsp_lock;
	struct list_head *dl = &spool->xsp_dev_list;
	struct xrt_subdev *sdev;
	int ret = -ENOENT;

	mutex_lock(lk);
	list_for_each(ptr, dl) {
		sdev = list_entry(ptr, struct xrt_subdev, xs_dev_list);
		if (sdev->xs_xdev != xdev)
			continue;
		ret = xrt_subdev_release(sdev, holder_dev);
		break;
	}
	mutex_unlock(lk);

	return ret;
}

int xrt_subdev_pool_put(struct xrt_subdev_pool *spool, struct xrt_device *xdev,
			struct device *holder_dev)
{
	int ret = xrt_subdev_pool_put_impl(spool, xdev, holder_dev);

	if (ret)
		return ret;

	if (!IS_ROOT_DEV(holder_dev)) {
		xrt_dbg(to_xrt_dev(holder_dev), "%s <<==X== %s",
			dev_name(holder_dev), dev_name(DEV(xdev)));
	}
	return 0;
}

void xrt_subdev_pool_trigger_event(struct xrt_subdev_pool *spool, enum xrt_events e)
{
	struct xrt_device *tgt = NULL;
	struct xrt_subdev *sdev = NULL;
	struct xrt_event evt;

	while (!xrt_subdev_pool_get_impl(spool, XRT_SUBDEV_MATCH_NEXT,
					 tgt, spool->xsp_owner, &sdev)) {
		tgt = sdev->xs_xdev;
		evt.xe_evt = e;
		evt.xe_subdev.xevt_subdev_id = sdev->xs_id;
		evt.xe_subdev.xevt_subdev_instance = tgt->instance;
		xrt_subdev_root_request(tgt, XRT_ROOT_EVENT_SYNC, &evt);
		xrt_subdev_pool_put_impl(spool, tgt, spool->xsp_owner);
	}
}

void xrt_subdev_pool_handle_event(struct xrt_subdev_pool *spool, struct xrt_event *evt)
{
	struct xrt_device *tgt = NULL;
	struct xrt_subdev *sdev = NULL;

	while (!xrt_subdev_pool_get_impl(spool, XRT_SUBDEV_MATCH_NEXT,
					 tgt, spool->xsp_owner, &sdev)) {
		tgt = sdev->xs_xdev;
		xleaf_call(tgt, XRT_XLEAF_EVENT, evt);
		xrt_subdev_pool_put_impl(spool, tgt, spool->xsp_owner);
	}
}

ssize_t xrt_subdev_pool_get_holders(struct xrt_subdev_pool *spool,
				    struct xrt_device *xdev, char *buf, size_t len)
{
	const struct list_head *ptr;
	struct mutex *lk = &spool->xsp_lock;
	struct list_head *dl = &spool->xsp_dev_list;
	struct xrt_subdev *sdev;
	ssize_t ret = 0;

	mutex_lock(lk);
	list_for_each(ptr, dl) {
		sdev = list_entry(ptr, struct xrt_subdev, xs_dev_list);
		if (sdev->xs_xdev != xdev)
			continue;
		ret = xrt_subdev_get_holders(sdev, buf, len);
		break;
	}
	mutex_unlock(lk);

	return ret;
}
EXPORT_SYMBOL_GPL(xrt_subdev_pool_get_holders);

int xleaf_broadcast_event(struct xrt_device *xdev, enum xrt_events evt, bool async)
{
	struct xrt_event e = { evt, };
	enum xrt_root_cmd cmd = async ? XRT_ROOT_EVENT_ASYNC : XRT_ROOT_EVENT_SYNC;

	WARN_ON(evt == XRT_EVENT_POST_CREATION || evt == XRT_EVENT_PRE_REMOVAL);
	return xrt_subdev_root_request(xdev, cmd, &e);
}
EXPORT_SYMBOL_GPL(xleaf_broadcast_event);

void xleaf_hot_reset(struct xrt_device *xdev)
{
	xrt_subdev_root_request(xdev, XRT_ROOT_HOT_RESET, NULL);
}
EXPORT_SYMBOL_GPL(xleaf_hot_reset);

void xleaf_get_root_res(struct xrt_device *xdev, u32 region_id, struct resource **res)
{
	struct xrt_root_get_res arg = { 0 };

	arg.xpigr_region_id = region_id;
	xrt_subdev_root_request(xdev, XRT_ROOT_GET_RESOURCE, &arg);
	*res = arg.xpigr_res;
}

void xleaf_get_root_id(struct xrt_device *xdev, unsigned short *vendor, unsigned short *device,
		       unsigned short *subvendor, unsigned short *subdevice)
{
	struct xrt_root_get_id id = { 0 };

	WARN_ON(!vendor && !device && !subvendor && !subdevice);

	xrt_subdev_root_request(xdev, XRT_ROOT_GET_ID, (void *)&id);
	if (vendor)
		*vendor = id.xpigi_vendor_id;
	if (device)
		*device = id.xpigi_device_id;
	if (subvendor)
		*subvendor = id.xpigi_sub_vendor_id;
	if (subdevice)
		*subdevice = id.xpigi_sub_device_id;
}

struct device *xleaf_register_hwmon(struct xrt_device *xdev, const char *name, void *drvdata,
				    const struct attribute_group **grps)
{
	struct xrt_root_hwmon hm = { true, name, drvdata, grps, };

	xrt_subdev_root_request(xdev, XRT_ROOT_HWMON, (void *)&hm);
	return hm.xpih_hwmon_dev;
}

void xleaf_unregister_hwmon(struct xrt_device *xdev, struct device *hwmon)
{
	struct xrt_root_hwmon hm = { false, };

	hm.xpih_hwmon_dev = hwmon;
	xrt_subdev_root_request(xdev, XRT_ROOT_HWMON, (void *)&hm);
}
