/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __LINUX_USB_PDDEV_H
#define __LINUX_USB_PDDEV_H

#include <uapi/linux/usb/pd_dev.h>

struct pd_dev;

struct pd_ops {
	int (*configure)(const struct pd_dev *dev, u32 flags);
	int (*get_message)(const struct pd_dev *dev, struct pd_message *msg);
	int (*set_message)(const struct pd_dev *dev, struct pd_message *msg);
	int (*submit)(const struct pd_dev *dev, struct pd_message *msg);
};

struct pd_dev {
	const struct pd_info *info;
	const struct pd_ops *ops;
};

#endif /* __LINUX_USB_PDDEV_H */
