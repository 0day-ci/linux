/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_UACCESS_BUFFER_H
#define _UAPI_LINUX_UACCESS_BUFFER_H

/* Location of the uaccess log. */
struct uaccess_descriptor {
	/* Address of the uaccess_buffer_entry array. */
	__u64 addr;
	/* Size of the uaccess_buffer_entry array in number of elements. */
	__u64 size;
};

/* Format of the entries in the uaccess log. */
struct uaccess_buffer_entry {
	/* Address being accessed. */
	__u64 addr;
	/* Number of bytes that were accessed. */
	__u64 size;
	/* UACCESS_BUFFER_* flags. */
	__u64 flags;
};

#define UACCESS_BUFFER_FLAG_WRITE	1 /* access was a write */

#endif /* _UAPI_LINUX_UACCESS_BUFFER_H */
