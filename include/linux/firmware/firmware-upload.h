/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Header file for Firmware Upload Framework
 *
 * Copyright (C) 2019-2021 Intel Corporation, Inc.
 */
#ifndef _LINUX_FIRMWARE_UPLOAD_H
#define _LINUX_FIRMWARE_UPLOAD_H

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <uapi/linux/firmware-upload.h>

struct fw_upload;

/**
 * struct fw_upload_ops - device specific operations
 * @prepare:		    Required: Prepare secure update
 * @write:		    Required: The write() op receives the remaining
 *			    size to be written and must return the actual
 *			    size written or a negative error code. The write()
 *			    op will be called repeatedly until all data is
 *			    written.
 * @poll_complete:	    Required: Check for the completion of the
 *			    HW authentication/programming process.
 * @cleanup:		    Optional: Complements the prepare()
 *			    function and is called at the completion
 *			    of the update, whether success or failure,
 *			    if the prepare function succeeded.
 */
struct fw_upload_ops {
	u32 (*prepare)(struct fw_upload *fwl, const u8 *data, u32 size);
	s32 (*write)(struct fw_upload *fwl, const u8 *data,
		     u32 offset, u32 size);
	u32 (*poll_complete)(struct fw_upload *fwl);
	void (*cleanup)(struct fw_upload *fwl);
};

struct fw_upload {
	struct device dev;
	struct cdev cdev;
	const struct fw_upload_ops *ops;
	struct mutex lock;		/* protect data structure contents */
	atomic_t opened;
	struct work_struct work;
	const u8 *data;			/* pointer to update data */
	u32 remaining_size;		/* size remaining to transfer */
	u32 progress;
	u32 err_code;			/* upload error code */
	bool driver_unload;
	void *priv;
};

struct fw_upload *
fw_upload_register(struct device *dev, const struct fw_upload_ops *ops,
		   void *priv);

void fw_upload_unregister(struct fw_upload *fwl);

#endif
