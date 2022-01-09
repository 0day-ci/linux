/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * IMA user API
 *
 */
#ifndef _UAPI_LINUX_IMA_H
#define _UAPI_LINUX_IMA_H

#include <linux/types.h>

/*
 * The hash format of fs-verity's file digest and other file metadata
 * to be signed.  The resulting signature is stored as a security.ima
 * xattr.
 *
 * "type" is defined as IMA_VERITY_DIGSIG
 * "algo" is the hash_algo enum of fs-verity's file digest
 * (e.g. HASH_ALGO_SHA256, HASH_ALGO_SHA512).
 */
struct ima_tbs_hash {
	__u8 type;        /* xattr type [enum evm_ima_xattr_type] */
	__u8 algo;        /* Digest algorithm [enum hash_algo] */
	__u8 digest[];    /* fs-verity digest */
};

#endif /* _UAPI_LINUX_IMA_H */
