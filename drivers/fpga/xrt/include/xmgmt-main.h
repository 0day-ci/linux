/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *	Cheng Zhen <maxz@xilinx.com>
 */

#ifndef _XMGMT_MAIN_H_
#define _XMGMT_MAIN_H_

#include <linux/xrt/xclbin.h>
#include "xleaf.h"

enum xrt_mgmt_main_leaf_cmd {
	XRT_MGMT_MAIN_GET_AXLF_SECTION = XRT_XLEAF_CUSTOM_BASE, /* See comments in xleaf.h */
	XRT_MGMT_MAIN_GET_VBNV,
};

/* There are three kind of partitions. Each of them is programmed independently. */
enum provider_kind {
	XMGMT_BLP, /* Base Logic Partition */
	XMGMT_PLP, /* Provider Logic Partition */
	XMGMT_ULP, /* User Logic Partition */
};

struct xrt_mgmt_main_get_axlf_section {
	enum provider_kind xmmigas_axlf_kind;
	enum axlf_section_kind xmmigas_section_kind;
	void *xmmigas_section;
	u64 xmmigas_section_size;
};

#endif	/* _XMGMT_MAIN_H_ */
