/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *  Copyright IBM Corp. 2022
 *  Author(s): Steffen Eiden <seiden@linux.ibm.com>
 */
#ifndef __S390X_ASM_UVDEVICE_H
#define __S390X_ASM_UVDEVICE_H

#include <linux/types.h>

struct uvio_ioctl_cb {
	__u32 flags;
	__u16 uv_rc;			/* UV header rc value */
	__u16 uv_rrc;			/* UV header rrc value */
	__u64 argument_addr;		/* Userspace address of uvio argument */
	__u32 argument_len;
	__u8  reserved14[0x40 - 0x14];	/* must be zero */
};

#define UVIO_QUI_MAX_LEN		0x8000

#define UVIO_DEVICE_NAME "uv"
#define UVIO_TYPE_UVC 'u'

#define UVIO_IOCTL_QUI _IOWR(UVIO_TYPE_UVC, 0x01, struct uvio_ioctl_cb)

#endif  /* __S390X_ASM_UVDEVICE_H */
