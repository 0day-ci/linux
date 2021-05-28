// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *	Cheng Zhen <maxz@xilinx.com>
 *	Lizhi Hou <lizhi.hou@xilinx.com>
 */

#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include "xleaf.h"
#include "xroot.h"
#include "lib-drv.h"

#define XRT_IPLIB_MODULE_NAME		"xrt-lib"
#define XRT_IPLIB_MODULE_VERSION	"4.0.0"
#define XRT_DRVNAME(drv)		((drv)->driver.name)

#define XRT_SUBDEV_ID_SHIFT		16
#define XRT_SUBDEV_ID_MASK		((1 << XRT_SUBDEV_ID_SHIFT) - 1)

struct xrt_find_drv_data {
	enum xrt_subdev_id id;
	struct xrt_driver *xdrv;
};

struct class *xrt_class;
static DEFINE_IDA(xrt_device_ida);

static inline u32 xrt_instance_to_id(enum xrt_subdev_id id, u32 instance)
{
	return (id << XRT_SUBDEV_ID_SHIFT) | instance;
}

static inline u32 xrt_id_to_instance(u32 id)
{
	return (id & XRT_SUBDEV_ID_MASK);
}

static int xrt_bus_match(struct device *dev, struct device_driver *drv)
{
	struct xrt_device *xdev = to_xrt_dev(dev);
	struct xrt_driver *xdrv = to_xrt_drv(drv);

	if (xdev->subdev_id == xdrv->subdev_id)
		return 1;

	return 0;
}

static int xrt_bus_probe(struct device *dev)
{
	struct xrt_driver *xdrv = to_xrt_drv(dev->driver);
	struct xrt_device *xdev = to_xrt_dev(dev);

	return xdrv->probe(xdev);
}

static int xrt_bus_remove(struct device *dev)
{
	struct xrt_driver *xdrv = to_xrt_drv(dev->driver);
	struct xrt_device *xdev = to_xrt_dev(dev);

	if (xdrv->remove)
		xdrv->remove(xdev);

	return 0;
}

struct bus_type xrt_bus_type = {
	.name		= "xrt",
	.match		= xrt_bus_match,
	.probe		= xrt_bus_probe,
	.remove		= xrt_bus_remove,
};

int xrt_register_driver(struct xrt_driver *drv)
{
	const char *drvname = XRT_DRVNAME(drv);
	int rc = 0;

	/* Initialize dev_t for char dev node. */
	if (drv->file_ops.xsf_ops.open) {
		rc = alloc_chrdev_region(&drv->file_ops.xsf_dev_t, 0,
					 XRT_MAX_DEVICE_NODES, drvname);
		if (rc) {
			pr_err("failed to alloc dev minor for %s: %d\n", drvname, rc);
			return rc;
		}
	} else {
		drv->file_ops.xsf_dev_t = (dev_t)-1;
	}

	drv->driver.owner = THIS_MODULE;
	drv->driver.bus = &xrt_bus_type;

	rc = driver_register(&drv->driver);
	if (rc) {
		pr_err("register %s xrt driver failed\n", drvname);
		if (drv->file_ops.xsf_dev_t != (dev_t)-1) {
			unregister_chrdev_region(drv->file_ops.xsf_dev_t,
						 XRT_MAX_DEVICE_NODES);
		}
		return rc;
	}

	pr_info("%s registered successfully\n", drvname);

	return 0;
}
EXPORT_SYMBOL_GPL(xrt_register_driver);

void xrt_unregister_driver(struct xrt_driver *drv)
{
	driver_unregister(&drv->driver);

	if (drv->file_ops.xsf_dev_t != (dev_t)-1)
		unregister_chrdev_region(drv->file_ops.xsf_dev_t, XRT_MAX_DEVICE_NODES);

	pr_info("%s unregistered successfully\n", XRT_DRVNAME(drv));
}
EXPORT_SYMBOL_GPL(xrt_unregister_driver);

static int __find_driver(struct device_driver *drv, void *_data)
{
	struct xrt_driver *xdrv = to_xrt_drv(drv);
	struct xrt_find_drv_data *data = _data;

	if (xdrv->subdev_id == data->id) {
		data->xdrv = xdrv;
		return 1;
	}

	return 0;
}

const char *xrt_drv_name(enum xrt_subdev_id id)
{
	struct xrt_find_drv_data data = { 0 };

	data.id = id;
	bus_for_each_drv(&xrt_bus_type, NULL, &data, __find_driver);

	if (data.xdrv)
		return XRT_DRVNAME(data.xdrv);

	return NULL;
}

static int xrt_drv_get_instance(enum xrt_subdev_id id)
{
	int ret;

	ret = ida_alloc_range(&xrt_device_ida, xrt_instance_to_id(id, 0),
			      xrt_instance_to_id(id, XRT_MAX_DEVICE_NODES),
			      GFP_KERNEL);
	if (ret < 0)
		return ret;

	return xrt_id_to_instance((u32)ret);
}

static void xrt_drv_put_instance(enum xrt_subdev_id id, int instance)
{
	ida_free(&xrt_device_ida, xrt_instance_to_id(id, instance));
}

struct xrt_dev_endpoints *xrt_drv_get_endpoints(enum xrt_subdev_id id)
{
	struct xrt_find_drv_data data = { 0 };

	data.id = id;
	bus_for_each_drv(&xrt_bus_type, NULL, &data, __find_driver);

	if (data.xdrv)
		return data.xdrv->endpoints;

	return NULL;
}

static void xrt_device_release(struct device *dev)
{
	struct xrt_device *xdev = container_of(dev, struct xrt_device, dev);

	kfree(xdev);
}

void xrt_device_unregister(struct xrt_device *xdev)
{
	if (xdev->state == XRT_DEVICE_STATE_ADDED)
		device_del(&xdev->dev);

	vfree(xdev->sdev_data);
	kfree(xdev->resource);

	if (xdev->instance != XRT_INVALID_DEVICE_INST)
		xrt_drv_put_instance(xdev->subdev_id, xdev->instance);

	if (xdev->dev.release == xrt_device_release)
		put_device(&xdev->dev);
}

struct xrt_device *
xrt_device_register(struct device *parent, u32 id,
		    struct resource *res, u32 res_num,
		    void *pdata, size_t data_sz)
{
	struct xrt_device *xdev = NULL;
	int ret;

	xdev = kzalloc(sizeof(*xdev), GFP_KERNEL);
	if (!xdev)
		return NULL;
	xdev->instance = XRT_INVALID_DEVICE_INST;

	/* Obtain dev instance number. */
	ret = xrt_drv_get_instance(id);
	if (ret < 0) {
		dev_err(parent, "failed get instance, ret %d", ret);
		goto fail;
	}

	xdev->instance = ret;
	xdev->name = xrt_drv_name(id);
	xdev->subdev_id = id;
	device_initialize(&xdev->dev);
	xdev->dev.release = xrt_device_release;
	xdev->dev.parent = parent;

	xdev->dev.bus = &xrt_bus_type;
	dev_set_name(&xdev->dev, "%s.%d", xdev->name, xdev->instance);

	xdev->num_resources = res_num;
	xdev->resource = kmemdup(res, sizeof(*res) * res_num, GFP_KERNEL);
	if (!xdev->resource)
		goto fail;

	xdev->sdev_data = vzalloc(data_sz);
	if (!xdev->sdev_data)
		goto fail;

	memcpy(xdev->sdev_data, pdata, data_sz);

	ret = device_add(&xdev->dev);
	if (ret) {
		dev_err(parent, "failed add device, ret %d", ret);
		goto fail;
	}
	xdev->state = XRT_DEVICE_STATE_ADDED;

	return xdev;

fail:
	xrt_device_unregister(xdev);
	kfree(xdev);

	return NULL;
}

struct resource *xrt_get_resource(struct xrt_device *xdev, u32 type, u32 num)
{
	u32 i;

	for (i = 0; i < xdev->num_resources; i++) {
		struct resource *r = &xdev->resource[i];

		if (type == resource_type(r) && num-- == 0)
			return r;
	}
	return NULL;
}

/*
 * Leaf driver's module init/fini callbacks. This is not a open infrastructure for dynamic
 * plugging in drivers. All drivers should be statically added.
 */
static void (*leaf_init_fini_cbs[])(bool) = {
	group_leaf_init_fini,
	vsec_leaf_init_fini,
	devctl_leaf_init_fini,
	axigate_leaf_init_fini,
	icap_leaf_init_fini,
	calib_leaf_init_fini,
	clkfreq_leaf_init_fini,
	clock_leaf_init_fini,
	ucs_leaf_init_fini,
};

static __init int xrt_lib_init(void)
{
	int ret;
	int i;

	ret = bus_register(&xrt_bus_type);
	if (ret)
		return ret;

	xrt_class = class_create(THIS_MODULE, XRT_IPLIB_MODULE_NAME);
	if (IS_ERR(xrt_class)) {
		bus_unregister(&xrt_bus_type);
		return PTR_ERR(xrt_class);
	}

	for (i = 0; i < ARRAY_SIZE(leaf_init_fini_cbs); i++)
		leaf_init_fini_cbs[i](true);
	return 0;
}

static __exit void xrt_lib_fini(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(leaf_init_fini_cbs); i++)
		leaf_init_fini_cbs[i](false);

	class_destroy(xrt_class);
	bus_unregister(&xrt_bus_type);
}

module_init(xrt_lib_init);
module_exit(xrt_lib_fini);

MODULE_VERSION(XRT_IPLIB_MODULE_VERSION);
MODULE_AUTHOR("XRT Team <runtime@xilinx.com>");
MODULE_DESCRIPTION("Xilinx Alveo IP Lib driver");
MODULE_LICENSE("GPL v2");
