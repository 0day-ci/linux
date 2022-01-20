/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Michael T. Kloos <michael@michaelkloos.com>
 */

#include <linux/linkage.h>
#include <asm/asm.h>

SYM_FUNC_START(__memmove)
SYM_FUNC_START_ALIAS(memmove)
	/*
	 * Returns
	 *   a0 - dest
	 *
	 * Parameters
	 *   a0 - Inclusive first byte of dest
	 *   a1 - Inclusive first byte of src
	 *   a2 - Length of copy
	 *
	 * Because the return matches the parameter register a0,
	 * we will not clobber or modify that register.
	 */

	/* Return if nothing to do */
	beq a0, a1, exit_memmove
	beqz a2, exit_memmove

	/*
	 * Register Uses
	 *   a3 - Inclusive first multibyte of src
	 *   a4 - Non-inclusive last multibyte of src
	 *   a5 - Non-inclusive last byte of src
	 *
	 * During the copy
	 *      Forward Copy: a1 - Index counter of src
	 *      Reverse Copy: a5 - Index counter of src
	 *   Both Copy Modes: t2 - Index counter of dest
	 *   Both Copy Modes: t1 - Temporary for load-store
	 *   Both Copy Modes: t0 - Link
	 */

	/*
	 * Solve for last byte now.  We will solve the rest when
	 * they are needed for the copy because either byte copy
	 * does not require any of the others (Wasted effort if
	 * byte copy gets used) or we do not yet have enough
	 * information to solve them.
	 */
	add  a5, a1, a2

	/*
	 * Byte copy if copying less than SZREG bytes.
	 * This can cause problems with the bulk copy
	 * implementation below and is small enough not
	 * to bother.
	 */
	andi t0, a2, -SZREG
	beqz t0, byte_copy

	/* Determine the maximum granularity of co-alignment. */
	xor  t0, a0, a1
#if   SZREG >= 8
	andi t1, t0, 0x7
	beqz t1, doubleword_copy
#endif
	andi t1, t0, 0x3
	beqz t1, word_copy
	andi t1, t0, 0x1
	beqz t1, halfword_copy
	/* Fall through to byte copy if nothing larger is found. */

byte_copy:
	bltu a1, a0, byte_copy_reverse

byte_copy_forward:
	add  t2, a0, zero
byte_copy_fw_callin:
	beq  a1, a5, exit_memmove
	lb   t1, (a1)
	sb   t1, (t2)
	addi a1, a1, 1
	addi t2, t2, 1
	j byte_copy_fw_callin

byte_copy_reverse:
	add  t2, a0, a2
byte_copy_rv_callin:
	beq  a1, a5, exit_memmove
	addi a5, a5, -1
	addi t2, t2, -1
	lb   t1, (a5)
	sb   t1, (t2)
	j byte_copy_rv_callin

exit_memmove:
	ret

copy_bytes_until_aligned_fw:
	beq  a1, a3, 1f /* Reuse the return from the other copy loop */
	lb   t1, (a1)
	sb   t1, (t2)
	addi a1, a1, 1
	addi t2, t2, 1
	j copy_bytes_until_aligned_fw

copy_bytes_until_aligned_rv:
	beq  a4, a5, 1f
	addi a5, a5, -1
	addi t2, t2, -1
	lb   t1, (a5)
	sb   t1, (t2)
	j copy_bytes_until_aligned_rv
	1: jalr zero, (t0) /* Return */

#if   SZREG >= 8
doubleword_copy:
	andi a3, a1, -8
	andi a4, a5, -8
	beq  a3, a1, 1f
	addi a3, a3, 8
	1:
	bltu a1, a0, doubleword_copy_reverse

doubleword_copy_forward:
	add  t2, a0, zero

	jal t0, copy_bytes_until_aligned_fw

	1:
	beq  a1, a4, byte_copy_fw_callin
	ld   t1, (a1)
	sd   t1, (t2)
	addi a1, a1, 8
	addi t2, t2, 8
	j 1b

doubleword_copy_reverse:
	add  t2, a0, a2

	jal t0, copy_bytes_until_aligned_rv

	1:
	beq  a3, a5, byte_copy_rv_callin
	addi a5, a5, -8
	addi t2, t2, -8
	ld   t1, (a5)
	sd   t1, (t2)
	j 1b
#endif

word_copy:
	andi a3, a1, -4
	andi a4, a5, -4
	beq  a3, a1, 1f
	addi a3, a3, 4
	1:
	bltu a1, a0, word_copy_reverse

word_copy_forward:
	add  t2, a0, zero

	jal t0, copy_bytes_until_aligned_fw

	1:
	beq  a1, a4, byte_copy_fw_callin
	lw   t1, (a1)
	sw   t1, (t2)
	addi a1, a1, 4
	addi t2, t2, 4
	j 1b

word_copy_reverse:
	add  t2, a0, a2

	jal t0, copy_bytes_until_aligned_rv

	1:
	beq  a3, a5, byte_copy_rv_callin
	addi a5, a5, -4
	addi t2, t2, -4
	lw   t1, (a5)
	sw   t1, (t2)
	j 1b

halfword_copy:
	andi a3, a1, -2
	andi a4, a5, -2
	beq  a3, a1, 1f
	addi a3, a3, 2
	1:
	bltu a1, a0, halfword_reverse

halfword_forward:
	add  t2, a0, zero

	jal t0, copy_bytes_until_aligned_fw

	1:
	beq  a1, a4, byte_copy_fw_callin
	lh   t1, (a1)
	sh   t1, (t2)
	addi a1, a1, 2
	addi t2, t2, 2
	j 1b

halfword_reverse:
	add  t2, a0, a2

	jal t0, copy_bytes_until_aligned_rv

	1:
	beq  a3, a5, byte_copy_rv_callin
	addi a5, a5, -2
	addi t2, t2, -2
	lh   t1, (a5)
	sh   t1, (t2)
	j 1b

SYM_FUNC_END_ALIAS(memmove)
SYM_FUNC_END(__memmove)
