/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _FPGA_REGION_H
#define _FPGA_REGION_H

#include <linux/device.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/fpga/fpga-bridge.h>

struct fpga_region;

/**
 * struct fpga_region_ops - ops for low level fpga region drivers
 * @get_bridges: optional function to get bridges to a list
 * @compat_id_show: optional function emit to sysfs a compatible id
 *
 * fpga_region_ops are the low level functions implemented by a specific
 * fpga region driver.  The optional ones are tested for NULL before being
 * called, so leaving them out is fine.
 */
struct fpga_region_ops {
	int (*get_bridges)(struct fpga_region *region);
	ssize_t (*compat_id_show)(struct fpga_region *region, char *buf);
};

/**
 * struct fpga_region - FPGA Region structure
 * @dev: FPGA Region device
 * @mutex: enforces exclusive reference to region
 * @bridge_list: list of FPGA bridges specified in region
 * @mgr: FPGA manager
 * @info: FPGA image info
 * @priv: private data
 * @rops: optional pointer to struct for fpga region ops
 */
struct fpga_region {
	struct device dev;
	struct mutex mutex; /* for exclusive reference to region */
	struct list_head bridge_list;
	struct fpga_manager *mgr;
	struct fpga_image_info *info;
	void *priv;
	const struct fpga_region_ops *rops;
};

#define to_fpga_region(d) container_of(d, struct fpga_region, dev)

struct fpga_region *fpga_region_class_find(
	struct device *start, const void *data,
	int (*match)(struct device *, const void *));

int fpga_region_program_fpga(struct fpga_region *region);

struct fpga_region
*fpga_region_create(struct device *dev, struct fpga_manager *mgr,
		    const struct fpga_region_ops *rops);
void fpga_region_free(struct fpga_region *region);
int fpga_region_register(struct fpga_region *region);
void fpga_region_unregister(struct fpga_region *region);

struct fpga_region
*devm_fpga_region_create(struct device *dev, struct fpga_manager *mgr,
			 const struct fpga_region_ops *rops);

#endif /* _FPGA_REGION_H */
