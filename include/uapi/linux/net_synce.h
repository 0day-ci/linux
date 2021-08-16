/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Userspace API for synchronous ethernet configuration
 *
 * Copyright (C) 2021 Intel Corporation
 * Author: Arkadiusz Kubalewski <arkadiusz.kubalewski@intel.com>
 *
 */
#ifndef _NET_SYNCE_H
#define _NET_SYNCE_H
#include <linux/types.h>

/*
 * Structure used for passing data with SIOCSSYNCE and SIOCGSYNCE ioctls
 */
struct synce_ref_clk_cfg {
	__u8 pin_id;
	_Bool enable;
};

#endif /* _NET_SYNCE_H */
