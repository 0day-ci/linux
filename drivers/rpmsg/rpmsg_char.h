/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) STMicroelectronics 2021.
 */

#ifndef __RPMSG_CHRDEV_H__
#define __RPMSG_CHRDEV_H__

#if IS_REACHABLE(CONFIG_RPMSG_CHAR)
/**
 * rpmsg_chrdev_create_eptdev() - register char device based on an endpoint
 * @rpdev:  prepared rpdev to be used for creating endpoints
 * @parent: parent device
 * @chinfo: assiated endpoint channel information.
 *
 * This function create a new rpmsg char endpoint device to instantiate a new
 * endpoint based on chinfo information.
 */
int rpmsg_chrdev_create_eptdev(struct rpmsg_device *rpdev, struct device *parent,
			       struct rpmsg_channel_info chinfo, struct class *rpmsg_class);

/**
 * rpmsg_chrdev_destroy_eptdev() - destroy created char device endpoint.
 * @data: private data associated to the endpoint device
 *
 * This function destroys a rpmsg char endpoint device created by the RPMSG_DESTROY_EPT_IOCTL
 * control.
 */
int rpmsg_chrdev_destroy_eptdev(struct device *dev, void *data);

#else  /*IS_REACHABLE(CONFIG_RPMSG_CHAR) */

static inline int rpmsg_chrdev_create_eptdev(struct rpmsg_device *rpdev, struct device *parent,
					     struct rpmsg_channel_info chinfo,
					     struct class *rpmsg_class)
{
	return -EINVAL;
}

static inline int rpmsg_chrdev_destroy_eptdev(struct device *dev, void *data)
{
	/* This shouldn't be possible */
	WARN_ON(1);

	return 0;
}

#endif /*IS_REACHABLE(CONFIG_RPMSG_CHAR) */

#endif /*__RPMSG_CHRDEV_H__ */
