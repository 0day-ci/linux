/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Header file for FPGA Image Load Driver
 *
 * Copyright (C) 2019-2021 Intel Corporation, Inc.
 */
#ifndef _LINUX_FPGA_IMAGE_LOAD_H
#define _LINUX_FPGA_IMAGE_LOAD_H

#include <linux/cdev.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <uapi/linux/fpga-image-load.h>

struct fpga_image_load;

/**
 * struct fpga_image_load_ops - device specific operations
 * @prepare:		    Required: Prepare secure update
 * @write_blk:		    Required: Write a block of data
 * @poll_complete:	    Required: Check for the completion of the
 *			    HW authentication/programming process. This
 *			    function should check for imgld->driver_unload
 *			    and abort with FPGA_IMAGE_ERR_CANCELED when true.
 * @cancel:		    Required: Signal HW to cancel update
 * @cleanup:		    Optional: Complements the prepare()
 *			    function and is called at the completion
 *			    of the update, whether success or failure,
 *			    if the prepare function succeeded.
 */
struct fpga_image_load_ops {
	enum fpga_image_err (*prepare)(struct fpga_image_load *imgld);
	enum fpga_image_err (*write_blk)(struct fpga_image_load *imgld, u32 offset);
	enum fpga_image_err (*poll_complete)(struct fpga_image_load *imgld);
	enum fpga_image_err (*cancel)(struct fpga_image_load *imgld);
	void (*cleanup)(struct fpga_image_load *imgld);
};

struct fpga_image_load {
	struct device dev;
	struct cdev cdev;
	const struct fpga_image_load_ops *lops;
	struct mutex lock;		/* protect data structure contents */
	unsigned long opened;
	struct work_struct work;
	struct completion update_done;
	const u8 *data;				/* pointer to update data */
	u32 remaining_size;			/* size remaining to transfer */
	enum fpga_image_prog progress;
	enum fpga_image_prog err_progress;	/* progress at time of failure */
	enum fpga_image_err err_code;		/* image load error code */
	bool driver_unload;
	void *priv;
};

struct fpga_image_load *
fpga_image_load_register(struct device *dev,
			 const struct fpga_image_load_ops *lops, void *priv);

void fpga_image_load_unregister(struct fpga_image_load *imgld);

#endif
