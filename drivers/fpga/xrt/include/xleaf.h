/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *    Cheng Zhen <maxz@xilinx.com>
 *    Sonal Santan <sonal.santan@xilinx.com>
 */

#ifndef _XRT_XLEAF_H_
#define _XRT_XLEAF_H_

#include <linux/mod_devicetable.h>
#include "xdevice.h"
#include "subdev_id.h"
#include "xroot.h"
#include "events.h"

/* All subdev drivers should use below common routines to print out msg. */
#define DEV(xdev)	(&(xdev)->dev)
#define DEV_PDATA(xdev)					\
	((struct xrt_subdev_platdata *)xrt_get_xdev_data(DEV(xdev)))
#define DEV_FILE_OPS(xdev)				\
	(&(to_xrt_drv((xdev)->dev.driver))->file_ops)
#define FMT_PRT(prt_fn, xdev, fmt, args...)		\
	({typeof(xdev) (_xdev) = (xdev);		\
	prt_fn(DEV(_xdev), "%s %s: " fmt,		\
	DEV_PDATA(_xdev)->xsp_root_name, __func__, ##args); })
#define xrt_err(xdev, fmt, args...) FMT_PRT(dev_err, xdev, fmt, ##args)
#define xrt_warn(xdev, fmt, args...) FMT_PRT(dev_warn, xdev, fmt, ##args)
#define xrt_info(xdev, fmt, args...) FMT_PRT(dev_info, xdev, fmt, ##args)
#define xrt_dbg(xdev, fmt, args...) FMT_PRT(dev_dbg, xdev, fmt, ##args)

#define XRT_DEFINE_REGMAP_CONFIG(config_name)				\
	static const struct regmap_config config_name = {		\
		.reg_bits = 32,						\
		.val_bits = 32,						\
		.reg_stride = 4,					\
		.max_register = 0x1000,					\
	}

enum {
	/* Starting cmd for common leaf cmd implemented by all leaves. */
	XRT_XLEAF_COMMON_BASE = 0,
	/* Starting cmd for leaves' specific leaf cmds. */
	XRT_XLEAF_CUSTOM_BASE = 64,
};

enum xrt_xleaf_common_leaf_cmd {
	XRT_XLEAF_EVENT = XRT_XLEAF_COMMON_BASE,
};

/*
 * Partially initialized by the parent driver, then, passed in as subdev driver's
 * platform data when creating subdev driver instance by calling platform
 * device register API (xrt_device_register_data() or the likes).
 *
 * Once device register API returns, platform driver framework makes a copy of
 * this buffer and maintains its life cycle. The content of the buffer is
 * completely owned by subdev driver.
 *
 * Thus, parent driver should be very careful when it touches this buffer
 * again once it's handed over to subdev driver. And the data structure
 * should not contain pointers pointing to buffers that is managed by
 * other or parent drivers since it could have been freed before platform
 * data buffer is freed by platform driver framework.
 */
struct xrt_subdev_platdata {
	/*
	 * Per driver instance callback. The xdev points to the instance.
	 * Should always be defined for subdev driver to get service from root.
	 */
	xrt_subdev_root_cb_t xsp_root_cb;
	void *xsp_root_cb_arg;

	/* Something to associate w/ root for msg printing. */
	const char *xsp_root_name;

	/*
	 * Char dev support for this subdev instance.
	 * Initialized by subdev driver.
	 */
	struct cdev xsp_cdev;
	struct device *xsp_sysdev;
	struct mutex xsp_devnode_lock; /* devnode lock */
	struct completion xsp_devnode_comp;
	int xsp_devnode_ref;
	bool xsp_devnode_online;
	bool xsp_devnode_excl;

	/*
	 * Subdev driver specific init data. The buffer should be embedded
	 * in this data structure buffer after dtb, so that it can be freed
	 * together with platform data.
	 */
	loff_t xsp_priv_off; /* Offset into this platform data buffer. */
	size_t xsp_priv_len;

	/*
	 * Populated by parent driver to describe the device tree for
	 * the subdev driver to handle. Should always be last one since it's
	 * of variable length.
	 */
	bool xsp_dtb_valid;
	char xsp_dtb[0];
};

struct subdev_match_arg {
	enum xrt_subdev_id id;
	int instance;
};

bool xleaf_has_endpoint(struct xrt_device *xdev, const char *endpoint_name);
struct xrt_device *xleaf_get_leaf(struct xrt_device *xdev,
				  xrt_subdev_match_t cb, void *arg);

static inline bool subdev_match(enum xrt_subdev_id id, struct xrt_device *xdev, void *arg)
{
	const struct subdev_match_arg *a = (struct subdev_match_arg *)arg;
	int instance = a->instance;

	if (id != a->id)
		return false;
	if (instance != xdev->instance && instance != XRT_INVALID_DEVICE_INST)
		return false;
	return true;
}

static inline bool xrt_subdev_match_epname(enum xrt_subdev_id id,
					   struct xrt_device *xdev, void *arg)
{
	return xleaf_has_endpoint(xdev, arg);
}

static inline struct xrt_device *
xleaf_get_leaf_by_id(struct xrt_device *xdev,
		     enum xrt_subdev_id id, int instance)
{
	struct subdev_match_arg arg = { id, instance };

	return xleaf_get_leaf(xdev, subdev_match, &arg);
}

static inline struct xrt_device *
xleaf_get_leaf_by_epname(struct xrt_device *xdev, const char *name)
{
	return xleaf_get_leaf(xdev, xrt_subdev_match_epname, (void *)name);
}

static inline int xleaf_call(struct xrt_device *tgt, u32 cmd, void *arg)
{
	return (to_xrt_drv(tgt->dev.driver)->leaf_call)(tgt, cmd, arg);
}

int xleaf_broadcast_event(struct xrt_device *xdev, enum xrt_events evt, bool async);
int xleaf_create_group(struct xrt_device *xdev, char *dtb);
int xleaf_destroy_group(struct xrt_device *xdev, int instance);
void xleaf_get_root_res(struct xrt_device *xdev, u32 region_id, struct resource **res);
void xleaf_get_root_id(struct xrt_device *xdev, unsigned short *vendor, unsigned short *device,
		       unsigned short *subvendor, unsigned short *subdevice);
void xleaf_hot_reset(struct xrt_device *xdev);
int xleaf_put_leaf(struct xrt_device *xdev, struct xrt_device *leaf);
struct device *xleaf_register_hwmon(struct xrt_device *xdev, const char *name, void *drvdata,
				    const struct attribute_group **grps);
void xleaf_unregister_hwmon(struct xrt_device *xdev, struct device *hwmon);
int xleaf_wait_for_group_bringup(struct xrt_device *xdev);

/*
 * Character device helper APIs for use by leaf drivers
 */
static inline bool xleaf_devnode_enabled(struct xrt_device *xdev)
{
	return DEV_FILE_OPS(xdev)->xsf_ops.open;
}

int xleaf_devnode_create(struct xrt_device *xdev,
			 const char *file_name, const char *inst_name);
void xleaf_devnode_destroy(struct xrt_device *xdev);

struct xrt_device *xleaf_devnode_open_excl(struct inode *inode);
struct xrt_device *xleaf_devnode_open(struct inode *inode);
void xleaf_devnode_close(struct inode *inode);

/* Module's init/fini routines for leaf driver in xrt-lib module */
#define XRT_LEAF_INIT_FINI_FUNC(name)					\
void name##_leaf_init_fini(bool init)					\
{									\
	if (init)							\
		xrt_register_driver(&xrt_##name##_driver);		\
	else								\
		xrt_unregister_driver(&xrt_##name##_driver);		\
}

/* Module's init/fini routines for leaf driver in xrt-lib module */
void group_leaf_init_fini(bool init);
void vsec_leaf_init_fini(bool init);
void devctl_leaf_init_fini(bool init);
void axigate_leaf_init_fini(bool init);
void icap_leaf_init_fini(bool init);
void calib_leaf_init_fini(bool init);
void clkfreq_leaf_init_fini(bool init);
void clock_leaf_init_fini(bool init);
void ucs_leaf_init_fini(bool init);

#endif	/* _XRT_LEAF_H_ */
