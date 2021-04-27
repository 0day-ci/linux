/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *  Copyright (C) 2015-2021, Xilinx Inc
 *
 */

/**
 * DOC: PCIe Kernel Driver for Management Physical Function
 * Interfaces exposed by *xrt-mgnt* driver are defined in file, *xmgnt-ioctl.h*.
 * Core functionality provided by *xrt-mgnt* driver is described in the following table:
 *
 * =========== ============================== ==================================
 * Functionality           ioctl request code           data format
 * =========== ============================== ==================================
 * 1 FPGA image download   XMGNT_IOCICAPDOWNLOAD_AXLF xmgnt_ioc_bitstream_axlf
 * =========== ============================== ==================================
 */

#ifndef _XMGNT_IOCTL_H_
#define _XMGNT_IOCTL_H_

#include <linux/ioctl.h>

#define XMGNT_IOC_MAGIC	'X'
#define XMGNT_IOC_ICAP_DOWNLOAD_AXLF 0x6

/**
 * struct xmgnt_ioc_bitstream_axlf - load xclbin (AXLF) device image
 * used with XMGNT_IOCICAPDOWNLOAD_AXLF ioctl
 *
 * @xclbin:	Pointer to user's xclbin structure in memory
 */
struct xmgnt_ioc_bitstream_axlf {
	struct axlf *xclbin;
};

#define XMGNT_IOCICAPDOWNLOAD_AXLF				\
	_IOW(XMGNT_IOC_MAGIC, XMGNT_IOC_ICAP_DOWNLOAD_AXLF, struct xmgnt_ioc_bitstream_axlf)

/*
 * The following definitions are for binary compatibility with classic XRT management driver
 */
#define XCLMGNT_IOCICAPDOWNLOAD_AXLF XMGNT_IOCICAPDOWNLOAD_AXLF
#define xclmgnt_ioc_bitstream_axlf xmgnt_ioc_bitstream_axlf

#endif
