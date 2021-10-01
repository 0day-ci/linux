/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Intel Software Defined Silicon: OS to hardware Interface
 * Copyright (c) 2021, Intel Corporation.
 * All rights reserved.
 *
 * Author: "David E. Box" <david.e.box@linux.intel.com>
 */

#ifndef __SDSI_IF_H
#define __SDSI_IF_H

#include <linux/ioctl.h>
#include <linux/types.h>

/**
 * struct sdsi_if_sdsi_state - Read current SDSi State Certificate
 * @size:	size of the certificate
 * @data:	SDSi State Certificate
 *
 * Used to return output of ioctl SDSI_IF_READ_STATE. This command is used to
 * read the current CPU configuration state
 */
struct sdsi_if_sdsi_state {
	__u32	size;
	__u8	data[4096];
};

/**
 * struct sdsi_if_provision_payload - Provision a certificate or activation payload
 * @size:	size of the certificate of activation payload
 * @data:	certificate or activation payload
 *
 * Used with ioctl command SDSI_IF_IOW_PROVISION_AKC and
 * SDSI_IF_IOW_PROVISION_CAP to provision a CPU with an Authentication
 * Key Certificate or Capability Activation Payload respectively.
 */
struct sdsi_if_provision_payload {
	__u32	size;
	__u8	data[4096];
};

#define SDSI_IF_MAGIC		0xDF
#define SDSI_IF_READ_STATE	_IOR(SDSI_IF_MAGIC, 0, struct sdsi_if_sdsi_state *)
#define SDSI_IF_PROVISION_AKC	_IOW(SDSI_IF_MAGIC, 1, struct sdsi_if_provision_payload *)
#define SDSI_IF_PROVISION_CAP	_IOW(SDSI_IF_MAGIC, 2, struct sdsi_if_provision_payload *)
#endif
