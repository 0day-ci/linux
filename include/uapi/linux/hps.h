/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 *  Copyright 2022 Google LLC.
 */

#ifndef _UAPI_HPS_H
#define _UAPI_HPS_H

#include <linux/types.h>

#define HPS_IOC_TRANSFER	_IOWR('h', 0x01, struct hps_transfer_ioctl_data)

struct hps_transfer_ioctl_data {
	__u32 isize;			/* Number of bytes to send */
	unsigned char __user *ibuf;	/* Input buffer */
	__u32 osize;			/* Number of bytes to receive */
	unsigned char __user *obuf;	/* Output buffer */
};

#endif /* _UAPI_HPS_H */
