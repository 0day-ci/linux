/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Code to process dynamic relocations in the kernel.
 *
 * Copyright 2008 Paul Mackerras, IBM Corp.
 */

#include <asm/ppc_asm.h>

RELA = 7
RELACOUNT = 0x6ffffff9
R_PPC64_RELATIVE = 22
R_PPC64_UADDR64 = 43

/*
 * r3 = desired final address of kernel
 */
_GLOBAL(relocate)
	mflr	r0
	bcl	20,31,$+4
0:	mflr	r12		/* r12 has runtime addr of label 0 */
	mtlr	r0
	ld	r11,(p_dyn - 0b)(r12)
	add	r11,r11,r12	/* r11 has runtime addr of .dynamic section */
	ld	r9,(p_rela - 0b)(r12)
	add	r9,r9,r12	/* r9 has runtime addr of .rela.dyn section */
	ld	r10,(p_st - 0b)(r12)
	add	r10,r10,r12	/* r10 has runtime addr of _stext */
	ld	r13,(p_sym - 0b)(r12)
	add	r13,r13,r12	/* r13 has runtime addr of .dynsym */

	/*
	 * Scan the dynamic section for the RELA and RELACOUNT entries.
	 */
	li	r7,0
	li	r8,0
1:	ld	r6,0(r11)	/* get tag */
	cmpdi	r6,0
	beq	4f		/* end of list */
	cmpdi	r6,RELA
	bne	2f
	ld	r7,8(r11)	/* get RELA pointer in r7 */
	b	3f
2:	addis	r6,r6,(-RELACOUNT)@ha
	cmpdi	r6,RELACOUNT@l
	bne	3f
	ld	r8,8(r11)	/* get RELACOUNT value in r8 */
3:	addi	r11,r11,16
	b	1b
4:	cmpdi	r7,0		/* check we have both RELA and RELACOUNT */
	cmpdi	cr1,r8,0
	beq	9f
	beq	cr1,9f

	/*
	 * Work out linktime address of _stext and hence the
	 * relocation offset to be applied.
	 * cur_offset [r7] = rela.run [r9] - rela.link [r7]
	 * _stext.link [r10] = _stext.run [r10] - cur_offset [r7]
	 * final_offset [r3] = _stext.final [r3] - _stext.link [r10]
	 */
	subf	r7,r7,r9	/* cur_offset */
	subf	r10,r7,r10
	subf	r3,r10,r3	/* final_offset */

	/*
	 * FIXME
	 * Here r8 is a number of relocations in .rela.dyn.
	 * When ld issues UADDR64 relocations, they end up at the end
	 * of the .rela.dyn section. However RELACOUNT does not include
	 * them so the loop below is going to finish after the last
	 * R_PPC64_RELATIVE as they normally go first.
	 * Work out the size of .rela.dyn at compile time.
	 */
	ld	r8,(p_rela_end - 0b)(r12)
	ld	r18,(p_rela - 0b)(r12)
	sub	r8,r8,r18
	li      r18,24		/* 24 == sizeof(elf64_rela) */
	divd	r8,r8,r18

	/*
	 * Run through the list of relocations and process the
	 * R_PPC64_RELATIVE and R_PPC64_UADDR64 ones.
	 */
	mtctr	r8
5:	lwa	r0,8(r9)	/* ELF64_R_TYPE(reloc->r_info) */
	cmpdi	r0,R_PPC64_RELATIVE
	bne	6f
	ld	r6,0(r9)	/* reloc->r_offset */
	ld	r0,16(r9)	/* reloc->r_addend */
	b	7f

6:	cmpdi	r0,R_PPC64_UADDR64
	bne	8f
	ld	r6,0(r9)
	ld	r0,16(r9)
	lwa	r14,12(r9) 	/* ELF64_R_SYM(reloc->r_info) */
	mulli	r14,r14,24	/* 24 == sizeof(elf64_sym) */
	add	r14,r14,r13	/* elf64_sym[ELF64_R_SYM] */
	ld	r14,8(r14)
	add	r0,r0,r14

7:	add	r0,r0,r3
	stdx	r0,r7,r6

8:	addi	r9,r9,24
	bdnz	5b

9:	blr

.balign 8
p_dyn:	.8byte	__dynamic_start - 0b
p_rela:	.8byte	__rela_dyn_start - 0b
p_rela_end:	.8byte __rela_dyn_end - 0b
p_sym:		.8byte __dynamic_symtab - 0b
p_st:	.8byte	_stext - 0b

