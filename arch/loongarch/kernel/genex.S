/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 *
 * Derived from MIPS:
 * Copyright (C) 1994 - 2000, 2001, 2003 Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2002, 2007  Maciej W. Rozycki
 * Copyright (C) 2001, 2012 MIPS Technologies, Inc.  All rights reserved.
 */
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/loongarch.h>
#include <asm/regdef.h>
#include <asm/fpregdef.h>
#include <asm/stackframe.h>
#include <asm/thread_info.h>

	.align	5
SYM_FUNC_START(__arch_cpu_idle)
	/* start of rollback region */
	LONG_L	t0, tp, TI_FLAGS
	nop
	andi	t0, t0, _TIF_NEED_RESCHED
	bnez	t0, 1f
	nop
	nop
	nop
	idle	0
	/* end of rollback region */
1:	jirl	zero, ra, 0
SYM_FUNC_END(__arch_cpu_idle)

SYM_FUNC_START(handle_vint)
	BACKUP_T0T1
	SAVE_ALL
	la.abs	t1, __arch_cpu_idle
	LONG_L  t0, sp, PT_EPC
	/* 32 byte rollback region */
	ori	t0, t0, 0x1f
	xori	t0, t0, 0x1f
	bne	t0, t1, 1f
	LONG_S  t0, sp, PT_EPC
1:
	KMODE

	move	a0, sp
	move	a1, sp
	la.abs	t0, do_vint
	jirl    ra, t0, 0

	RESTORE_ALL_AND_RET
SYM_FUNC_END(handle_vint)

SYM_FUNC_START(except_vec_cex)
	b	cache_parity_error
	nop
SYM_FUNC_END(except_vec_cex)

	.macro	__build_prep_badv
	csrrd	t0, LOONGARCH_CSR_BADV
	PTR_S	t0, sp, PT_BVADDR
	KMODE
	.endm

	.macro	__build_prep_fcsr
	movfcsr2gr	a1, fcsr0
	KMODE
	.endm

	.macro	__build_prep_none
	KMODE
	.endm

	.macro	__BUILD_silent exception
	.endm

	.macro	__BUILD_verbose nexception
	LONG_L	a1, sp, PT_EPC
	ASM_PRINT("Got \nexception at %016lx\012")
	.endm

	.macro	BUILD_HANDLER exception handler prep verbose
	.align	5
	SYM_FUNC_START(handle_\exception)
	BACKUP_T0T1
	SAVE_ALL

	__build_prep_\prep
	__BUILD_\verbose \exception
	move	a0, sp
	la.abs	t0, do_\handler
	jirl    ra, t0, 0

	RESTORE_ALL_AND_RET
	SYM_FUNC_END(handle_\exception)
	.endm

	BUILD_HANDLER ade ade badv silent
	BUILD_HANDLER ale ale badv silent
	BUILD_HANDLER bp bp none silent
	BUILD_HANDLER fpe fpe fcsr silent
	BUILD_HANDLER fpu fpu none silent
	BUILD_HANDLER lsx lsx none silent
	BUILD_HANDLER lasx lasx none silent
	BUILD_HANDLER lbt lbt none silent
	BUILD_HANDLER ri ri none silent
	BUILD_HANDLER watch watch none silent
	BUILD_HANDLER reserved reserved none verbose	/* others */

SYM_FUNC_START(handle_sys)
	la.abs	t0, handle_syscall
	jirl    zero, t0, 0
SYM_FUNC_END(handle_sys)
