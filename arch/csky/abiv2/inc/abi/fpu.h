/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_FPU_H
#define __ASM_CSKY_FPU_H

#include <asm/sigcontext.h>
#include <asm/ptrace.h>


#ifdef CONFIG_CPU_HAS_MATHEMU
#define FPU_INIT		mtcr("cr<1, 2>", 0x3f)
#define MFCR_FCR        current->thread.emul_fp.user_fcr
#define MFCR_FESR       current->thread.emul_fp.user_fesr
#define MTCR_FCR(regx)		\
		{	\
			mtcr("cr<1, 2>", regx | 0x3f);	\
			current->thread.emul_fp.user_fcr = regx;	\
		}
#define MTCR_FESR(regx)		\
		{	\
			mtcr("cr<2, 2>", regx); \
			current->thread.emul_fp.user_fesr = regx;	\
		}
#define CR_NUM          15

inline unsigned int get_fpu_insn(struct pt_regs *regs);
inline int do_fpu_insn(unsigned int inst, struct pt_regs *regs);
#else
#define FPU_INIT		mtcr("cr<1, 2>", 0)
#define MFCR_FCR mfcr("cr<1, 2>")
#define MFCR_FESR mfcr("cr<2, 2>")
#define MTCR_FCR(regx)		{ mtcr("cr<1, 2>", regx); }
#define MTCR_FESR(regx)		{ mtcr("cr<2, 2>", regx); }
#define CR_NUM    2
#endif

int fpu_libc_helper(struct pt_regs *regs);
void fpu_fpe(struct pt_regs *regs);

static inline void init_fpu(void)
{
	FPU_INIT;
}

void save_to_user_fp(struct user_fp *user_fp);
void restore_from_user_fp(struct user_fp *user_fp);
/*
 * Define the fesr bit for fpe handle.
 */
#define  FPE_ILLE  (1 << 16)    /* Illegal instruction  */
#define  FPE_FEC   (1 << 7)     /* Input float-point arithmetic exception */
#define  FPE_IDC   (1 << 5)     /* Input denormalized exception */
#define  FPE_IXC   (1 << 4)     /* Inexact exception */
#define  FPE_UFC   (1 << 3)     /* Underflow exception */
#define  FPE_OFC   (1 << 2)     /* Overflow exception */
#define  FPE_DZC   (1 << 1)     /* Divide by zero exception */
#define  FPE_IOC   (1 << 0)     /* Invalid operation exception */
#define  FPE_REGULAR_EXCEPTION (FPE_IXC | FPE_UFC | FPE_OFC | FPE_DZC | FPE_IOC)

#ifdef CONFIG_OPEN_FPU_IDE
#define IDE_STAT   (1 << 5)
#else
#define IDE_STAT   0
#endif

#ifdef CONFIG_OPEN_FPU_IXE
#define IXE_STAT   (1 << 4)
#else
#define IXE_STAT   0
#endif

#ifdef CONFIG_OPEN_FPU_UFE
#define UFE_STAT   (1 << 3)
#else
#define UFE_STAT   0
#endif

#ifdef CONFIG_OPEN_FPU_OFE
#define OFE_STAT   (1 << 2)
#else
#define OFE_STAT   0
#endif

#ifdef CONFIG_OPEN_FPU_DZE
#define DZE_STAT   (1 << 1)
#else
#define DZE_STAT   0
#endif

#ifdef CONFIG_OPEN_FPU_IOE
#define IOE_STAT   (1 << 0)
#else
#define IOE_STAT   0
#endif

#endif /* __ASM_CSKY_FPU_H */
