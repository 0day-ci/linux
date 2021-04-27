/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *    Lizhi Hou <lizhi.hou@xilinx.com>
 */

#ifndef _XRT_DEVICE_H_
#define _XRT_DEVICE_H_

#include <linux/fs.h>
#include <linux/cdev.h>

#define XRT_MAX_DEVICE_NODES		128
#define XRT_INVALID_DEVICE_INST		(XRT_MAX_DEVICE_NODES + 1)

enum {
	XRT_DEVICE_STATE_NONE = 0,
	XRT_DEVICE_STATE_ADDED
};

/*
 * struct xrt_device - represent an xrt device on xrt bus
 *
 * dev: generic device interface.
 * id: id of the xrt device.
 */
struct xrt_device {
	struct device dev;
	u32 subdev_id;
	const char *name;
	u32 instance;
	u32 state;
	u32 num_resources;
	struct resource *resource;
	void *sdev_data;
};

/*
 * If populated by xrt device driver, infra will handle the mechanics of
 * char device (un)registration.
 */
enum xrt_dev_file_mode {
	/* Infra create cdev, default file name */
	XRT_DEV_FILE_DEFAULT = 0,
	/* Infra create cdev, need to encode inst num in file name */
	XRT_DEV_FILE_MULTI_INST,
	/* No auto creation of cdev by infra, leaf handles it by itself */
	XRT_DEV_FILE_NO_AUTO,
};

struct xrt_dev_file_ops {
	const struct file_operations xsf_ops;
	dev_t xsf_dev_t;
	const char *xsf_dev_name;
	enum xrt_dev_file_mode xsf_mode;
};

/*
 * this struct define the endpoints belong to the same xrt device
 */
struct xrt_dev_ep_names {
	const char *ep_name;
	const char *compat;
};

struct xrt_dev_endpoints {
	struct xrt_dev_ep_names *xse_names;
	/* minimum number of endpoints to support the subdevice */
	u32 xse_min_ep;
};

/*
 * struct xrt_driver - represent a xrt device driver
 *
 * drv: driver model structure.
 * id_table: pointer to table of device IDs the driver is interested in.
 *           { } member terminated.
 * probe: mandatory callback for device binding.
 * remove: callback for device unbinding.
 */
struct xrt_driver {
	struct device_driver driver;
	u32 subdev_id;
	struct xrt_dev_file_ops file_ops;
	struct xrt_dev_endpoints *endpoints;

	/*
	 * Subdev driver callbacks populated by subdev driver.
	 */
	int (*probe)(struct xrt_device *xrt_dev);
	void (*remove)(struct xrt_device *xrt_dev);
	/*
	 * If leaf_call is defined, these are called by other leaf drivers.
	 * Note that root driver may call into leaf_call of a group driver.
	 */
	int (*leaf_call)(struct xrt_device *xrt_dev, u32 cmd, void *arg);
};

#define to_xrt_dev(d) container_of(d, struct xrt_device, dev)
#define to_xrt_drv(d) container_of(d, struct xrt_driver, driver)

static inline void *xrt_get_drvdata(const struct xrt_device *xdev)
{
	return dev_get_drvdata(&xdev->dev);
}

static inline void xrt_set_drvdata(struct xrt_device *xdev, void *data)
{
	dev_set_drvdata(&xdev->dev, data);
}

static inline void *xrt_get_xdev_data(struct device *dev)
{
	struct xrt_device *xdev = to_xrt_dev(dev);

	return xdev->sdev_data;
}

struct xrt_device *
xrt_device_register(struct device *parent, u32 id,
		    struct resource *res, u32 res_num,
		    void *pdata, size_t data_sz);
void xrt_device_unregister(struct xrt_device *xdev);
int xrt_register_driver(struct xrt_driver *drv);
void xrt_unregister_driver(struct xrt_driver *drv);
void *xrt_get_xdev_data(struct device *dev);
struct resource *xrt_get_resource(struct xrt_device *xdev, u32 type, u32 num);

#endif /* _XRT_DEVICE_H_ */
