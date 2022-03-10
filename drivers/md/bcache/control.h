/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BCACHE_CONTROL_H__
#define __BCACHE_CONTROL_H__

#include "ioctl_codes.h"

int __init bch_ctrl_device_init(void);
void bch_ctrl_device_deinit(void);

ssize_t register_bcache_ioctl(struct bch_register_device *brd);

#endif
