/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Header file for Firmware Upload Framework
 *
 * Copyright (C) 2019-2021 Intel Corporation, Inc.
 */
#ifndef _LINUX_FIRMWARE_UPLOAD_H
#define _LINUX_FIRMWARE_UPLOAD_H

#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/types.h>

struct fw_upload;

/**
 * struct fw_upload_ops - device specific operations
 */
struct fw_upload_ops {
};

struct fw_upload {
	struct device dev;
	const struct fw_upload_ops *ops;
	struct mutex lock;		/* protect data structure contents */
	void *priv;
};

struct fw_upload *
fw_upload_register(struct device *dev, const struct fw_upload_ops *ops,
		   void *priv);

void fw_upload_unregister(struct fw_upload *fwl);

#endif
