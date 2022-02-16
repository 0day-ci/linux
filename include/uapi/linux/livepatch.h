/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

/*
 * livepatch.h - Kernel Live Patching Core
 *
 * Copyright (C) 2016 Josh Poimboeuf <jpoimboe@redhat.com>
 */

#ifndef _UAPI_LIVEPATCH_H
#define _UAPI_LIVEPATCH_H

#include <linux/types.h>

#define KLP_RELA_PREFIX		".klp.rela."
#define KLP_SYM_PREFIX		".klp.sym."

struct klp_module_reloc {
	union {
		void *sym;
		__u64 sym64;	/* Force 64-bit width */
	};
	__u32 sympos;
} __packed;

#endif /* _UAPI_LIVEPATCH_H */
