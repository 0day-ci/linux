/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *	Cheng Zhen <maxz@xilinx.com>
 */

#ifndef _LIB_DRV_H_
#define _LIB_DRV_H_

/* Module's init/fini routines for leaf driver in xrt-lib module */
#define XRT_LEAF_INIT_FINI_FUNC(name)					\
void name##_leaf_init_fini(bool init)					\
{									\
	if (init)							\
		xrt_register_driver(&xrt_##name##_driver);		\
	else								\
		xrt_unregister_driver(&xrt_##name##_driver);		\
}

/* Module's init/fini routines for leaf driver in xrt-lib module */
void group_leaf_init_fini(bool init);

extern u8 __dtb_xrt_bus_begin[];
extern u8 __dtb_xrt_bus_end[];

#endif	/* _LIB_DRV_H_ */
