/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Split from entry_32.S
 */

#include <linux/magic.h>
#include <asm/reg.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/ftrace.h>
#include <asm/export.h>
#include <asm/ptrace.h>
#include <asm/bug.h>

_GLOBAL(mcount)
_GLOBAL(_mcount)
	/*
	 * It is required that _mcount on PPC32 must preserve the
	 * link register. But we have r12 to play with. We use r12
	 * to push the return address back to the caller of mcount
	 * into the ctr register, restore the link register and
	 * then jump back using the ctr register.
	 */
	mflr	r12
	mtctr	r12
	mtlr	r0
	bctr
EXPORT_SYMBOL(_mcount)

_GLOBAL(ftrace_caller)
	MCOUNT_SAVE_FRAME
	/* r3 ends up with link register */
	subi	r3, r3, MCOUNT_INSN_SIZE
	lis	r5,function_trace_op@ha
	lwz	r5,function_trace_op@l(r5)
	li	r6, 0
.globl ftrace_call
ftrace_call:
	bl	ftrace_stub
	nop
	MCOUNT_RESTORE_FRAME
ftrace_caller_common:
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
.globl ftrace_graph_call
ftrace_graph_call:
	b	ftrace_graph_stub
_GLOBAL(ftrace_graph_stub)
#endif
	/* old link register ends up in ctr reg */
	bctr


_GLOBAL(ftrace_stub)
	blr

_GLOBAL(ftrace_regs_caller)
	/* Save the original return address in A's stack frame */
	stw	r0,LRSAVE(r1)

	/* Create our stack frame + pt_regs */
	stwu	r1,-INT_FRAME_SIZE(r1)

	/* Save all gprs to pt_regs */
	stw	r0, GPR0(r1)
	stmw	r2, GPR2(r1)

	/* Save previous stack pointer (r1) */
	addi	r8, r1, INT_FRAME_SIZE
	stw	r8, GPR1(r1)

	/* Load special regs for save below */
	mfmsr   r8
	mfctr   r9
	mfxer   r10
	mfcr	r11

	/* Get the _mcount() call site out of LR */
	mflr	r7
	/* Save it as pt_regs->nip */
	stw     r7, _NIP(r1)
	/* Save the read LR in pt_regs->link */
	stw     r0, _LINK(r1)

	lis	r3,function_trace_op@ha
	lwz	r5,function_trace_op@l(r3)

#ifdef CONFIG_LIVEPATCH
	mr	r14,r7		/* remember old NIP */
#endif
	/* Calculate ip from nip-4 into r3 for call below */
	subi    r3, r7, MCOUNT_INSN_SIZE

	/* Put the original return address in r4 as parent_ip */
	mr	r4, r0

	/* Save special regs */
	stw     r8, _MSR(r1)
	stw     r9, _CTR(r1)
	stw     r10, _XER(r1)
	stw     r11, _CCR(r1)

	/* Load &pt_regs in r6 for call below */
	addi    r6, r1, STACK_FRAME_OVERHEAD

	/* ftrace_call(r3, r4, r5, r6) */
.globl ftrace_regs_call
ftrace_regs_call:
	bl	ftrace_stub
	nop

	/* Load ctr with the possibly modified NIP */
	lwz	r3, _NIP(r1)
	mtctr	r3
#ifdef CONFIG_LIVEPATCH
	cmpw	r14, r3		/* has NIP been altered? */
#endif

	/* Restore gprs */
	lmw	r2, GPR2(r1)

	/* Restore possibly modified LR */
	lwz	r0, _LINK(r1)
	mtlr	r0

	/* Pop our stack frame */
	addi r1, r1, INT_FRAME_SIZE

#ifdef CONFIG_LIVEPATCH
        /* Based on the cmpw above, if the NIP was altered handle livepatch */
	bne-	livepatch_handler
#endif
	b	ftrace_caller_common

#ifdef CONFIG_LIVEPATCH
	/*
	 * This function runs in the mcount context, between two functions. As
	 * such it can only clobber registers which are volatile and used in
	 * function linkage.
	 *
	 * We get here when a function A, calls another function B, but B has
	 * been live patched with a new function C.
	 *
	 * On entry:
	 *  - we have no stack frame and can not allocate one
	 *  - LR points back to the original caller (in A)
	 *  - CTR holds the new NIP in C
	 *  - r0, r11 & r12 are free
	 */
livepatch_handler:
	/* Allocate 2 x 8 bytes */
	lwz	r11, TI_livepatch_sp+THREAD(r2)
	addi	r11, r11, 16
	stw	r11, TI_livepatch_sp+THREAD(r2)

	/* Save real LR on livepatch stack */
	mflr	r12
	stw	r12, -16(r11)

	/* Store stack end marker */
	lis     r12, STACK_END_MAGIC@h
	ori     r12, r12, STACK_END_MAGIC@l
	stw	r12, -4(r11)

	/* Branch to ctr */
	bctrl

	/*
	 * Now we are returning from the patched function to the original
	 * caller A. We are free to use r11 and r12.
	 */

	lwz	r11, TI_livepatch_sp+THREAD(r2)

	/* Check stack marker hasn't been trashed */
	lwz	r12, -4(r11)
	subis	r12, r12, STACK_END_MAGIC@h
1:	twnei	r12, STACK_END_MAGIC@l
	EMIT_BUG_ENTRY 1b, __FILE__, __LINE__ - 1, 0

	/* Restore LR from livepatch stack */
	lwz	r12, -16(r11)
	mtlr	r12

	/* Pop livepatch stack frame */
	subi	r11, r11, 16
	stw	r11, TI_livepatch_sp+THREAD(r2)

	/* Return to original caller of live patched function */
	blr
#endif /* CONFIG_LIVEPATCH */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
_GLOBAL(ftrace_graph_caller)
	stwu	r1,-48(r1)
	stw	r3, 12(r1)
	stw	r4, 16(r1)
	stw	r5, 20(r1)
	stw	r6, 24(r1)
	stw	r7, 28(r1)
	stw	r8, 32(r1)
	stw	r9, 36(r1)
	stw	r10,40(r1)

	addi	r5, r1, 48
	mfctr	r4		/* ftrace_caller has moved local addr here */
	stw	r4, 44(r1)
	mflr	r3		/* ftrace_caller has restored LR from stack */
	subi	r4, r4, MCOUNT_INSN_SIZE

	bl	prepare_ftrace_return
	nop

        /*
         * prepare_ftrace_return gives us the address we divert to.
         * Change the LR in the callers stack frame to this.
         */
	stw	r3,52(r1)
	mtlr	r3
	lwz	r0,44(r1)
	mtctr	r0

	lwz	r3, 12(r1)
	lwz	r4, 16(r1)
	lwz	r5, 20(r1)
	lwz	r6, 24(r1)
	lwz	r7, 28(r1)
	lwz	r8, 32(r1)
	lwz	r9, 36(r1)
	lwz	r10,40(r1)

	addi	r1, r1, 48

	bctr

_GLOBAL(return_to_handler)
	/* need to save return values */
	stwu	r1, -32(r1)
	stw	r3, 20(r1)
	stw	r4, 16(r1)
	stw	r31, 12(r1)
	mr	r31, r1

	bl	ftrace_return_to_handler
	nop

	/* return value has real return address */
	mtlr	r3

	lwz	r3, 20(r1)
	lwz	r4, 16(r1)
	lwz	r31,12(r1)
	lwz	r1, 0(r1)

	/* Jump back to real return address */
	blr
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
