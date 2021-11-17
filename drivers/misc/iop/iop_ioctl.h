/* SPDX-License-Identifier: GPL-2.0-or-later*/
/*
 *
 *iop_ioctl.h
 *
 */

#ifndef _IOP_IOCTL_H
#define _IOP_IOCTL_H

#include <linux/ioctl.h>

struct ioctl_cmd {
		unsigned int reg;
		unsigned int offset;
		unsigned int val;
};

#define IOC_MAGIC 'i'

#define IOCTL_VALSET _IOW(IOC_MAGIC, 1, struct ioctl_cmd)
#define IOCTL_VALGET _IOR(IOC_MAGIC, 2, struct ioctl_cmd)

#endif
