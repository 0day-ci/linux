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

#ifdef __KERNEL__
#include <linux/uio.h>
#else
#include <sys/uio.h>
#endif

#define USER_EVENTS_SYSTEM "user_events"
#define USER_EVENTS_PREFIX "u:"

/* Bits 0-6 are for known probe types, Bit 7 is for unknown probes */
#define EVENT_BIT_FTRACE 0
#define EVENT_BIT_PERF 1
#define EVENT_BIT_OTHER 7

#define EVENT_STATUS_FTRACE (1 << EVENT_BIT_FTRACE)
#define EVENT_STATUS_PERF (1 << EVENT_BIT_PERF)
#define EVENT_STATUS_OTHER (1 << EVENT_BIT_OTHER)

/* Use raw iterator for attached BPF program(s), no affect on ftrace/perf */
#define FLAG_BPF_ITER (1 << 0)

struct user_reg {
	__u32 size;
	__u64 name_args;
	__u32 status_index;
	__u32 write_index;
};

#define DIAG_IOC_MAGIC '*'
#define DIAG_IOCSREG _IOWR(DIAG_IOC_MAGIC, 0, struct user_reg*)
#define DIAG_IOCSDEL _IOW(DIAG_IOC_MAGIC, 1, char*)

enum {
	USER_BPF_DATA_KERNEL,
	USER_BPF_DATA_USER,
	USER_BPF_DATA_ITER,
};

struct user_bpf_iter {
	__u32 iov_offset;
	__u32 nr_segs;
	const struct iovec *iov;
};

struct user_bpf_context {
	__u32 data_type;
	__u32 data_len;
	union {
		void *kdata;
		void *udata;
		struct user_bpf_iter *iter;
	};
};

#endif /* _UAPI_LINUX_USER_EVENTS_H */
