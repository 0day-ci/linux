// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *	Cheng Zhen <maxz@xilinx.com>
 *	Lizhi Hou <lizhi.hou@xilinx.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/of_device.h>
#include <linux/xrt/xdevice.h>
#include "lib-drv.h"

static int xrt_bus_ovcs_id;

#define XRT_DRVNAME(drv)		((drv)->driver.name)

static DEFINE_IDA(xrt_device_ida);

static int xrt_bus_match(struct device *dev, struct device_driver *drv)
{
	if (of_driver_match_device(dev, drv))
		return 1;

	return 0;
}

static int xrt_bus_probe(struct device *dev)
{
	struct xrt_driver *xdrv = to_xrt_drv(dev->driver);
	struct xrt_device *xdev = to_xrt_dev(dev);

	return xdrv->probe(xdev);
}

static void xrt_bus_remove(struct device *dev)
{
	struct xrt_driver *xdrv = to_xrt_drv(dev->driver);
	struct xrt_device *xdev = to_xrt_dev(dev);

	if (xdrv->remove)
		xdrv->remove(xdev);
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

static int xrt_dev_get_instance(void)
{
	int ret;

	ret = ida_alloc_range(&xrt_device_ida, 0, 0x7fffffff, GFP_KERNEL);
	if (ret < 0)
		return ret;

	return ret;
}

static void xrt_dev_put_instance(int instance)
{
	ida_free(&xrt_device_ida, instance);
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
		xrt_dev_put_instance(xdev->instance);

	if (xdev->dev.of_node)
		of_node_put(xdev->dev.of_node);

	if (xdev->dev.release == xrt_device_release)
		put_device(&xdev->dev);
}

struct xrt_device *
xrt_device_register(struct device *parent, struct device_node *dn,
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
	ret = xrt_dev_get_instance();
	if (ret < 0) {
		dev_err(parent, "failed get instance, ret %d", ret);
		goto fail;
	}

	xdev->instance = ret;
	xdev->name = dn->full_name;
	device_initialize(&xdev->dev);
	xdev->dev.release = xrt_device_release;
	xdev->dev.parent = parent;

	xdev->dev.bus = &xrt_bus_type;
	dev_set_name(&xdev->dev, "%s.%d", xdev->name, xdev->instance);

	if (res_num > 0) {
		xdev->num_resources = res_num;
		xdev->resource = kmemdup(res, sizeof(*res) * res_num, GFP_KERNEL);
		if (!xdev->resource)
			goto fail;
	}

	if (data_sz > 0) {
		xdev->sdev_data = vzalloc(data_sz);
		if (!xdev->sdev_data)
			goto fail;

		memcpy(xdev->sdev_data, pdata, data_sz);
	}

	ret = device_add(&xdev->dev);
	if (ret) {
		dev_err(parent, "failed add device, ret %d", ret);
		goto fail;
	}
	xdev->state = XRT_DEVICE_STATE_ADDED;
	xdev->dev.of_node = of_node_get(dn);

	return xdev;

fail:
	xrt_device_unregister(xdev);
	kfree(xdev);

	return NULL;
}

/*
 * Leaf driver's module init/fini callbacks. This is not a open infrastructure for dynamic
 * plugging in drivers. All drivers should be statically added.
 */
static void (*leaf_init_fini_cbs[])(bool) = {
	group_leaf_init_fini,
};

static __init int xrt_lib_init(void)
{
	int ret, i;

	ret = of_overlay_fdt_apply(__dtb_xrt_bus_begin,
				   __dtb_xrt_bus_end - __dtb_xrt_bus_begin,
				   &xrt_bus_ovcs_id);
	if (ret)
		return ret;

	ret = bus_register(&xrt_bus_type);
	if (ret) {
		of_overlay_remove(&xrt_bus_ovcs_id);
		return ret;
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

	bus_unregister(&xrt_bus_type);
	of_overlay_remove(&xrt_bus_ovcs_id);
}

module_init(xrt_lib_init);
module_exit(xrt_lib_fini);

MODULE_AUTHOR("XRT Team <runtime@xilinx.com>");
MODULE_DESCRIPTION("Xilinx Alveo IP Lib driver");
MODULE_LICENSE("GPL v2");
