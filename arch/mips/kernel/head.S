/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995 Waldorf Electronics
 * Written by Ralf Baechle and Andreas Busse
 * Copyright (C) 1994 - 99, 2003, 06 Ralf Baechle
 * Copyright (C) 1996 Paul M. Antoine
 * Modified for DECStation and hence R3000 support by Paul M. Antoine
 * Further modifications by David S. Miller and Harald Koerfgen
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Kevin Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 */

#include <linux/init.h>
#include <linux/threads.h>

#include <asm/addrspace.h>
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/irqflags.h>
#include <asm/regdef.h>
#include <asm/mipsregs.h>
#include <asm/stackframe.h>

#include <kernel-entry-init.h>

	.macro	do_relocate_kernel
	.set	push
#ifdef CONFIG_RELOCATABLE
#define R_MIPS_32	2
#define R_MIPS_26	4
#define R_MIPS_HI16	5
#define R_MIPS_64	18
	/* Save args */
	move s0, a0
	move s1, a1
	move s2, a2
	move s3, a3

	/* Whether the PC meets expectation */
	bal	1f
1:	move	a0, ra
	PTR_LA	a1, 1b
	PTR_SUB	a0, a0, a1
	beqz	a0, 999f

	/* Whether offset 64KB aligned */
	and		t0, a0, 0xffff
	bnez	t0, 999f

	/* Whether current _text and _end are in the PC region */
	PTR_LA	t0, _end
	PTR_ADDU	t1, t0, a0
	PTR_SRL	t1, 28
	PTR_SRL	t0, 28
	bne	t0, t1, 999f

	PTR_LA	t0, _text
	PTR_ADDU	t1, t0, a0
	move	a3, t1
	PTR_SRL	t1, 28
	PTR_SRL	t0, 28
	bne	t0, t1, 999f

	/* get current __start/stop___ex_table address */
	PTR_LA	a1, __start___ex_table
	PTR_ADDU	a1, a0
	PTR_LA	a2, __stop___ex_table
	PTR_ADDU	a2, a0

	/*
	 * a0: offset
	 * a1: current __start___ex_table
	 * a2: current __stop___ex_table
	 */
1:	beq	a1, a2, 998f
	PTR_L	t0, 0(a1)
	PTR_ADDU	t0, a0
	PTR_S	t0, 0(a1)
	LONG_ADDIU	a1, PTRSIZE
	b	1b

998:
	/* get current _relocation_start address */
	PTR_LA	a1, _relocation_start
	PTR_ADDU	a1, a0

	/*
	 * a0: offset
	 * a1: _relocation_start[]
	 * a2: offset >> 16, to relocate R_MIPS_HI16
	 * a3: current _text
	 * t0: to load _relocation_start[]
	 * t1: relocation type
	 * t2: current relocate position
	 * t3: temporarily
	 * t8: 0x00ffffff, mask, to get relocate position
	 * t9: offset >> 2, to relocate R_MIPS_26
	 */
	li	t8, 0x00ffffff
	LONG_SRL t9, a0, 2
	LONG_SRL a2, a0, 16
1:	lw	t0, 0(a1)
	beqz	t0, 900f
	LONG_SRL	t1, t0, 24
	and	t1, 0xff
	and	t2, t0, t8
	LONG_SLL	t2, 2
	PTR_ADDU	t2, a3

	li	t3, R_MIPS_64
	beq	t1, t3, 964f
	li	t3, R_MIPS_32
	beq	t1, t3, 932f
	li	t3, R_MIPS_26
	beq	t1, t3, 926f
	li	t3, R_MIPS_HI16
	beq	t1, t3, 916f
	b	999f

964:
#ifdef CONFIG_64BIT
	ld	t3, 0(t2)
	LONG_ADDU	t3, a0
	sd	t3, 0(t2)
#endif
	LONG_ADDIU	a1, 4
	b	1b

932:
	lw	t3, 0(t2)
	LONG_ADDU	t3, a0
	sw	t3, 0(t2)
	LONG_ADDIU	a1, 4
	b	1b

926:
	lw	t3, 0(t2)
	LONG_ADDU	t3, t9
	sw	t3, 0(t2)
	LONG_ADDIU	a1, 4
	b	1b

916:
	lw	t3, 0(t2)
	LONG_ADDU	t3, a2
	sw	t3, 0(t2)
	LONG_ADDIU	a1, 4
	b	1b

900:
	/* Complete! And flush i-cache */
	PTR_LA	a0, _text
	PTR_LA	a1,	_end
	synci	0(a0)
	rdhwr	t0, $1
	beqz	t0, 999f
	PTR_ADDU	a0, t0
	PTR_SUBU	t0, a1, a0
	bgtz	t0, 900b
	sync

999:
	/* Restore args */
	move a0, s0
	move a1, s1
	move a2, s2
	move a3, s3
#endif /* CONFIG_RELOCATABLE */
	.set	pop
	.endm

	/*
	 * For the moment disable interrupts, mark the kernel mode and
	 * set ST0_KX so that the CPU does not spit fire when using
	 * 64-bit addresses.  A full initialization of the CPU's status
	 * register is done later in per_cpu_trap_init().
	 */
	.macro	setup_c0_status set clr
	.set	push
	mfc0	t0, CP0_STATUS
	or	t0, ST0_KERNEL_CUMASK|\set|0x1f|\clr
	xor	t0, 0x1f|\clr
	mtc0	t0, CP0_STATUS
	.set	noreorder
	sll	zero,3				# ehb
	.set	pop
	.endm

	.macro	setup_c0_status_pri
#ifdef CONFIG_64BIT
	setup_c0_status ST0_KX 0
#else
	setup_c0_status 0 0
#endif
	.endm

	.macro	setup_c0_status_sec
#ifdef CONFIG_64BIT
	setup_c0_status ST0_KX ST0_BEV
#else
	setup_c0_status 0 ST0_BEV
#endif
	.endm

#ifndef CONFIG_NO_EXCEPT_FILL
	/*
	 * Reserved space for exception handlers.
	 * Necessary for machines which link their kernels at KSEG0.
	 */
	.fill	0x400
#endif

EXPORT(_stext)

#ifdef CONFIG_BOOT_RAW
	/*
	 * Give us a fighting chance of running if execution beings at the
	 * kernel load address.	 This is needed because this platform does
	 * not have a ELF loader yet.
	 */
FEXPORT(__kernel_entry)
	j	kernel_entry
#endif /* CONFIG_BOOT_RAW */

	__REF

NESTED(kernel_entry, 16, sp)			# kernel entry point

	do_relocate_kernel

	kernel_entry_setup			# cpu specific setup

	setup_c0_status_pri

	/* We might not get launched at the address the kernel is linked to,
	   so we jump there.  */
	PTR_LA	t0, 0f
	jr	t0
0:

	PTR_LA		t0, __bss_start		# clear .bss
	LONG_S		zero, (t0)
	PTR_LA		t1, __bss_stop - LONGSIZE
1:
	PTR_ADDIU	t0, LONGSIZE
	LONG_S		zero, (t0)
	bne		t0, t1, 1b

	LONG_S		a0, fw_arg0		# firmware arguments
	LONG_S		a1, fw_arg1
	LONG_S		a2, fw_arg2
	LONG_S		a3, fw_arg3

	MTC0		zero, CP0_CONTEXT	# clear context register
#ifdef CONFIG_64BIT
	MTC0		zero, CP0_XCONTEXT
#endif
	PTR_LA		$28, init_thread_union
	/* Set the SP after an empty pt_regs.  */
	PTR_LI		sp, _THREAD_SIZE - 32 - PT_SIZE
	PTR_ADDU	sp, $28
	back_to_back_c0_hazard
	set_saved_sp	sp, t0, t1
	PTR_SUBU	sp, 4 * SZREG		# init stack pointer

#ifdef CONFIG_RANDOMIZE_BASE
	/* Copy kernel and apply the relocations */
	jal		relocate_kernel

	/* Repoint the sp into the new kernel image */
	PTR_LI		sp, _THREAD_SIZE - 32 - PT_SIZE
	PTR_ADDU	sp, $28
	set_saved_sp	sp, t0, t1
	PTR_SUBU	sp, 4 * SZREG		# init stack pointer

	/*
	 * relocate_kernel returns the entry point either
	 * in the relocated kernel or the original if for
	 * some reason relocation failed - jump there now
	 * with instruction hazard barrier because of the
	 * newly sync'd icache.
	 */
	jr.hb		v0
#else  /* !CONFIG_RANDOMIZE_BASE */
	j		start_kernel
#endif /* !CONFIG_RANDOMIZE_BASE */
	END(kernel_entry)

#ifdef CONFIG_SMP
/*
 * SMP slave cpus entry point.	Board specific code for bootstrap calls this
 * function after setting up the stack and gp registers.
 */
NESTED(smp_bootstrap, 16, sp)
	smp_slave_setup
	setup_c0_status_sec
	j	start_secondary
	END(smp_bootstrap)
#endif /* CONFIG_SMP */
