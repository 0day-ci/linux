/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only WITH Linux-syscall-note */
/* Copyright (c) Microsoft Corporation. */

#ifndef _AZ_BLOB_H
#define _AZ_BLOB_H

#include <linux/kernel.h>
#include <linux/uuid.h>

/* user-mode sync request sent through ioctl */
struct az_blob_request_sync_response {
	__u32 status;
	__u32 response_len;
};

struct az_blob_request_sync {
	guid_t guid;
	__u32 timeout;
	__u32 request_len;
	__u32 response_len;
	__u32 data_len;
	__u32 data_valid;
	__aligned_u64 request_buffer;
	__aligned_u64 response_buffer;
	__aligned_u64 data_buffer;
	struct az_blob_request_sync_response response;
};

#define AZ_BLOB_MAGIC_NUMBER	'R'
#define IOCTL_AZ_BLOB_DRIVER_USER_REQUEST \
		_IOWR(AZ_BLOB_MAGIC_NUMBER, 0xf0, \
			struct az_blob_request_sync)

#endif /* define _AZ_BLOB_H */
