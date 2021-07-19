/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors:
 *	Lizhi Hou <Lizhi.Hou@xilinx.com>
 */

#ifndef _XRT_DEVCTL_H_
#define _XRT_DEVCTL_H_

#include "xleaf.h"

/*
 * DEVCTL driver leaf calls.
 */
enum xrt_devctl_leaf_cmd {
	XRT_DEVCTL_READ = XRT_XLEAF_CUSTOM_BASE, /* See comments in xleaf.h */
};

enum xrt_devctl_id {
	XRT_DEVCTL_ROM_UUID = 0,
	XRT_DEVCTL_DDR_CALIB,
	XRT_DEVCTL_GOLDEN_VER,
	XRT_DEVCTL_MAX
};

struct xrt_devctl_rw {
	u32	xdr_id;
	void	*xdr_buf;
	u32	xdr_len;
	u32	xdr_offset;
};

struct xrt_devctl_intf_uuid {
	u32	uuid_num;
	uuid_t	*uuids;
};

#endif	/* _XRT_DEVCTL_H_ */
