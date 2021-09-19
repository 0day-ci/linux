/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * IOMMUFD API definition
 *
 * Copyright (C) 2021 Intel Corporation
 *
 * Author: Liu Yi L <yi.l.liu@intel.com>
 */
#ifndef __LINUX_IOMMUFD_H
#define __LINUX_IOMMUFD_H

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/pci.h>

#define IOMMUFD_IOASID_MAX	((unsigned int)(0x7FFFFFFF))
#define IOMMUFD_IOASID_MIN	0
#define IOMMUFD_INVALID_IOASID	-1

#define IOMMUFD_DEVID_MAX	((unsigned int)(0x7FFFFFFF))
#define IOMMUFD_DEVID_MIN	0

struct iommufd_device;

#if IS_ENABLED(CONFIG_IOMMUFD)
struct iommufd_device *
iommufd_bind_device(int fd, struct device *dev, u64 dev_cookie);
void iommufd_unbind_device(struct iommufd_device *idev);

int iommufd_device_attach_ioasid(struct iommufd_device *idev, int ioasid);
void iommufd_device_detach_ioasid(struct iommufd_device *idev, int ioasid);

static inline int
__pci_iommufd_device_attach_ioasid(struct pci_dev *pdev,
				   struct iommufd_device *idev, int ioasid)
{
	return iommufd_device_attach_ioasid(idev, ioasid);
}

#else /* !CONFIG_IOMMUFD */
static inline struct iommufd_device *
iommufd_bind_device(int fd, struct device *dev, u64 dev_cookie)
{
	return ERR_PTR(-ENODEV);
}

static inline void iommufd_unbind_device(struct iommufd_device *idev)
{
}

static inline int iommufd_device_attach_ioasid(struct iommufd_device *idev,
					       int ioasid)
{
	return -ENODEV;
}

static inline void iommufd_device_detach_ioasid(struct iommufd_device *idev,
						int ioasid)
{
}

static inline int
__pci_iommufd_device_attach_ioasid(struct pci_dev *pdev,
				   struct iommufd_device *idev, int ioasid)
{
	return -ENODEV;
}
#endif /* CONFIG_IOMMUFD */
#endif /* __LINUX_IOMMUFD_H */
