/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Xilinx, Inc.
 *
 * Authors: Sonal.Santan@xilinx.com
 */

#ifndef _XRT_MGR_H_
#define _XRT_MGR_H_

#include <linux/fpga/fpga-mgr.h>

struct fpga_manager *xmgnt_fmgr_probe(struct xrt_device *xdev);
int xmgnt_fmgr_remove(struct fpga_manager *fmgr);

#endif /* _XRT_MGR_H_ */
