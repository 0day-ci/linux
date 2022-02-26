/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Author: Hanlu Li <lihanlu@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _UAPI_ASM_SIGCONTEXT_H
#define _UAPI_ASM_SIGCONTEXT_H

#include <linux/types.h>
#include <linux/posix_types.h>

/* FP context was used */
#define USED_FP			(1 << 0)
/* Load/Store access flags for address error */
#define ADRERR_RD		(1 << 30)
#define ADRERR_WR		(1 << 31)

struct sigcontext {
	__u64	sc_pc;
	__u64	sc_regs[32];
	__u32	sc_flags;
	__u64	sc_extcontext[0] __attribute__((__aligned__(16)));
};

#define CONTEXT_INFO_ALIGN	16
struct context_info {
	__u32	magic;
	__u32	size;
	__u64	padding;	/* padding to 16 bytes */
};

/* FPU context */
#define FPU_CTX_MAGIC		0x46505501
#define FPU_CTX_ALIGN		8
struct fpu_context {
	__u64	regs[32];
	__u64	fcc;
	__u32	fcsr;
};

/* LSX context */
#define LSX_CTX_MAGIC		0x53580001
#define LSX_CTX_ALIGN		16
struct lsx_context {
	__u64	regs[2*32];
	__u64	fcc;
	__u32	fcsr;
	__u32	vcsr;
};

/* LASX context */
#define LASX_CTX_MAGIC		0x41535801
#define LASX_CTX_ALIGN		32
struct lasx_context {
	__u64	regs[4*32];
	__u64	fcc;
	__u32	fcsr;
	__u32	vcsr;
};

#endif /* _UAPI_ASM_SIGCONTEXT_H */
