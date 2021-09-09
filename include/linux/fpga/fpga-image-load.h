/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Header file for FPGA Image Load Driver
 *
 * Copyright (C) 2019-2021 Intel Corporation, Inc.
 */
#ifndef _LINUX_FPGA_IMAGE_LOAD_H
#define _LINUX_FPGA_IMAGE_LOAD_H

#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/types.h>

struct fpga_image_load;

/**
 * struct fpga_image_load_ops - device specific operations
 */
struct fpga_image_load_ops {
};

struct fpga_image_load {
	struct device dev;
	const struct fpga_image_load_ops *lops;
	struct mutex lock;		/* protect data structure contents */
	void *priv;
};

struct fpga_image_load *
fpga_image_load_register(struct device *dev,
			 const struct fpga_image_load_ops *lops, void *priv);

void fpga_image_load_unregister(struct fpga_image_load *imgld);

#endif
