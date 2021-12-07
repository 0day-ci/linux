/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * IOCTLs for Open Profile for DICE character device
 */

#ifndef _UAPI_DICE_H_
#define _UAPI_DICE_H_

#include <linux/ioctl.h>

#define DICE_GET_SIZE		_IO(0xAE, 0x40)
#define DICE_SET_WIPE_ON_CLOSE	_IO(0xAE, 0x41)

#endif
