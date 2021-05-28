/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *  Copyright (C) 2015-2021, Xilinx Inc
 *
 */

/**
 * DOC: PCIe Kernel Driver for Management Physical Function
 * Interfaces exposed by *xrt-mgmt* driver are defined in file, *xmgmt-ioctl.h*.
 * Core functionality provided by *xrt-mgmt* driver is described in the following table:
 *
 * =========== ============================== ==================================
 * Functionality           ioctl request code           data format
 * =========== ============================== ==================================
 * 1 FPGA image download   XMGMT_IOCICAPDOWNLOAD_AXLF xmgmt_ioc_bitstream_axlf
 * =========== ============================== ==================================
 */

#ifndef _XMGMT_IOCTL_H_
#define _XMGMT_IOCTL_H_

#include <linux/ioctl.h>

#define XMGMT_IOC_MAGIC	'X'
#define XMGMT_IOC_ICAP_DOWNLOAD_AXLF 0x6

/**
 * struct xmgmt_ioc_bitstream_axlf - load xclbin (AXLF) device image
 * used with XMGMT_IOCICAPDOWNLOAD_AXLF ioctl
 *
 * @xclbin:	Pointer to user's xclbin structure in memory
 */
struct xmgmt_ioc_bitstream_axlf {
	struct axlf *xclbin;
};

#define XMGMT_IOCICAPDOWNLOAD_AXLF				\
	_IOW(XMGMT_IOC_MAGIC, XMGMT_IOC_ICAP_DOWNLOAD_AXLF, struct xmgmt_ioc_bitstream_axlf)

/*
 * The following definitions are for binary compatibility with classic XRT management driver
 */
#define XCLMGMT_IOCICAPDOWNLOAD_AXLF XMGMT_IOCICAPDOWNLOAD_AXLF
#define xclmgmt_ioc_bitstream_axlf xmgmt_ioc_bitstream_axlf

#endif
