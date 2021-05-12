/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2019-2021 Intel Corporation */

#ifndef _NNPDRV_BOOTIMAGE_H
#define _NNPDRV_BOOTIMAGE_H

#include <linux/mutex.h>
#include <linux/workqueue.h>

struct nnp_device;

enum image_state {
	IMAGE_NONE = 0,
	IMAGE_REQUESTED,
	IMAGE_LOAD_FAILED,
	IMAGE_AVAILABLE
};

/**
 * struct image_info - describes a boot image object
 * @work: handle for placing the image load in a workqueue
 * @state: state indicating whether it is loaded or load failed
 * @mutex: protects accesses to @state and @hostres
 * @load_fail_err: zero or error code if @state is IMAGE_LOAD_FAILED.
 * @hostres: host resource object allocated for the image content
 * @hostres_map: mapping object of host resource to device
 *
 * This structure describe a request to load boot image from disk,
 * there is one such structure for each device.
 */
struct image_info {
	struct work_struct           work;
	enum image_state             state;
	struct mutex                 mutex;
	struct host_resource         *hostres;
	struct nnpdev_mapping        *hostres_map;
};

void nnpdev_boot_image_init(struct image_info *boot_image);
int nnpdev_load_boot_image(struct nnp_device *nnpdev);
int nnpdev_unload_boot_image(struct nnp_device *nnpdev);

#endif /* _NNPDRV_BOOTIMAGE_H */
