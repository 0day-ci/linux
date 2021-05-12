/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2019-2021 Intel Corporation */

#ifndef _NNPDRV_DEVICE_CHARDEV_H
#define _NNPDRV_DEVICE_CHARDEV_H

#include "device.h"

int nnpdev_cdev_create(struct nnp_device *nnpdev);
void nnpdev_cdev_destroy(struct nnp_device *nnpdev);
int nnpdev_cdev_class_init(void);
void nnpdev_cdev_class_cleanup(void);

#endif
