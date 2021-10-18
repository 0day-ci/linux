/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2021, Microsoft Corporation.
 *
 * Authors:
 *   Beau Belgrave <beaub@linux.microsoft.com>
 */
#ifndef _UAPI_LINUX_USER_EVENTS_H
#define _UAPI_LINUX_USER_EVENTS_H

#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/uio.h>

#define USER_EVENTS_SYSTEM "user_events"
#define USER_EVENTS_PREFIX "u:"

/* Bits 0-6 are for known probe types, Bit 7 is for unknown probes */
#define EVENT_BIT_FTRACE 0
#define EVENT_BIT_PERF 1
#define EVENT_BIT_OTHER 7

#define EVENT_STATUS_FTRACE (1 << EVENT_BIT_FTRACE)
#define EVENT_STATUS_PERF (1 << EVENT_BIT_PERF)
#define EVENT_STATUS_OTHER (1 << EVENT_BIT_OTHER)

#define DIAG_IOC_MAGIC '*'
#define DIAG_IOCSREG _IOW(DIAG_IOC_MAGIC, 0, char*)
#define DIAG_IOCSDEL _IOW(DIAG_IOC_MAGIC, 1, char*)

#define INDEX_WRITE(index) (index & 0xFFFF)
#define INDEX_STATUS(index) ((index >> 16) & 0xFFFF)
#define INDEX_COMBINE(write, status) (status << 16 | write)

enum {
	USER_BPF_DATA_KERNEL,
	USER_BPF_DATA_USER,
	USER_BPF_DATA_ITER,
};

struct user_bpf_iter {
	size_t iov_offset;
	const struct iovec *iov;
	unsigned long nr_segs;
};

struct user_bpf_context {
	int data_type;
	int data_len;
	union {
		void *kdata;
		void *udata;
		struct user_bpf_iter *iter;
	};
};

#endif /* _UAPI_LINUX_USER_EVENTS_H */
