/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, Microsoft Corporation.
 *
 * Authors:
 *   Long Li <longli@microsoft.com>
 */
#ifndef _XS_FASTPATH_H
#define _XS_FASTPATH_H

#include <linux/hyperv.h>
#include <linux/miscdevice.h>
#include <linux/hashtable.h>
#include <linux/uio.h>

struct xs_fastpath_device {
	struct hv_device *device;
	bool removing;

	struct list_head vsp_pending_list;
	spinlock_t vsp_pending_lock;
	wait_queue_head_t wait_vsp;

	wait_queue_head_t wait_files;
	atomic_t file_count;
};

/* user-mode sync request sent through ioctl */
struct xs_fastpath_request_sync_response {
	__u32 status;
	__u32 response_len;
};

struct xs_fastpath_request_sync {
	guid_t guid;
	__u32 timeout;
	__u32 request_len;
	__u32 response_len;
	__u32 data_len;
	__aligned_u64 request_buffer;
	__aligned_u64 response_buffer;
	__aligned_u64 data_buffer;
	struct xs_fastpath_request_sync_response response;
};

/* VSP messages */
enum xs_fastpath_vsp_request_type {
	XS_FASTPATH_DRIVER_REQUEST_FIRST     = 0x100,
	XS_FASTPATH_DRIVER_USER_REQUEST      = 0x100,
	XS_FASTPATH_DRIVER_REGISTER_BUFFER   = 0x101,
	XS_FASTPATH_DRIVER_DEREGISTER_BUFFER = 0x102,
	XS_FASTPATH_DRIVER_REQUEST_MAX       = 0x103
};

/* VSC->VSP request */
struct xs_fastpath_vsp_request {
	u32 version;
	u32 timeout_ms;
	u32 data_buffer_offset;
	u32 data_buffer_length;
	u32 data_buffer_valid;
	u32 operation_type;
	u32 request_buffer_offset;
	u32 request_buffer_length;
	u32 response_buffer_offset;
	u32 response_buffer_length;
	guid_t transaction_id;
} __packed;

/* VSP->VSC response */
struct xs_fastpath_vsp_response {
	u32 length;
	u32 error;
	u32 response_len;
} __packed;

#define IOCTL_XS_FASTPATH_DRIVER_USER_REQUEST \
		_IOWR('R', 10, struct xs_fastpath_request_sync)

#define XS_FASTPATH_MAX_PAGES 8192

#endif /* define _XS_FASTPATH_H */
