/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2019-2021 NXP.
 */

#ifndef __LINUX_IMX_RPMSG_H__
#define __LINUX_IMX_RPMSG_H__

#include <linux/types.h>

/*
 * Global header file for iMX RPMSG
 */

/* Category define */
#define IMX_RMPSG_LIFECYCLE     1
#define IMX_RPMSG_PMIC          2
#define IMX_RPMSG_AUDIO         3
#define IMX_RPMSG_KEY           4
#define IMX_RPMSG_GPIO          5
#define IMX_RPMSG_RTC           6
#define IMX_RPMSG_SENSOR        7

/* rpmsg version */
#define IMX_RMPSG_MAJOR         1
#define IMX_RMPSG_MINOR         0

struct imx_rpmsg_head {
	u8 cate;
	u8 major;
	u8 minor;
	u8 type;
	u8 cmd;
	u8 reserved[5];
} __packed;

#endif /* __LINUX_IMX_RPMSG_H__ */
