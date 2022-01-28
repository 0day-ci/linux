/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Michael T. Kloos <michael@michaelkloos.com>
 */

#include <linux/linkage.h>
#include <asm/asm.h>

SYM_FUNC_START(__memmove)
SYM_FUNC_START_WEAK(memmove)
	/*
	 * Returns
	 *   a0 - dest
	 *
	 * Parameters
	 *   a0 - Inclusive first byte of dest
	 *   a1 - Inclusive first byte of src
	 *   a2 - Length of copy n
	 *
	 * Because the return matches the parameter register a0,
	 * we will not clobber or modify that register.
	 *
	 * Note: This currently only works on little-endian.
	 * To port to big-endian, reverse the direction of shifts
	 * in the 2 misaligned fixup copy loops.
	 */

	/* Return if nothing to do */
	beq a0, a1, 20f /* Return from memmove */
	beqz a2, 20f /* Return from memmove */

	/*
	 * Register Uses
	 *      Forward Copy: a3 - Index counter of src
	 *      Reverse Copy: a4 - Index counter of src
	 *      Forward Copy: t3 - Index counter of dest
	 *      Reverse Copy: t4 - Index counter of dest
	 *   Both Copy Modes: t5 - Inclusive first multibyte/aligned of dest
	 *   Both Copy Modes: t6 - Non-Inclusive last multibyte/aligned of dest
	 *   Both Copy Modes: t0 - Link (Once the copying begins)
	 *   Both Copy Modes: t1 - Temporary for load-store
	 *   Both Copy Modes: t2 - Temporary for load-store
	 *   Both Copy Modes: a5 - dest to src alignment offset
	 *   Both Copy Modes: a6 - Shift ammount
	 *   Both Copy Modes: a7 - Inverse Shift ammount
	 */

	/*
	 * Solve for some register values now.
	 * Byte copy does not need t5 or t6.
	 */
	add  t3, a0, zero
	add  t4, a0, a2
	add  a3, a1, zero
	add  a4, a1, a2

	/*
	 * Byte copy if copying less than SZREG bytes.
	 * This can cause problems with the bulk copy
	 * implementation and is small enough not
	 * to bother.
	 */
	andi t0, a2, -SZREG
	beqz t0, 21f

	/*
	 * Now solve for t5 and t6.
	 */
	andi t5, t3, -SZREG
	andi t6, t4, -SZREG
	/*
	 * If dest(Register t3) rounded down to the nearest naturally
	 * aligned SZREG address, does not equal dest, then add SZREG
	 * to find the low-bound of SZREG alignment in the dest memory
	 * region.  Note that this could overshoot the dest memory
	 * region if n is less than SZREG.  This is one reason why
	 * we always byte copy if n is less than SZREG.
	 * Otherwise, dest is already naturally aligned to SZREG.
	 */
	beq  t5, t3, 1f
		addi t5, t5, SZREG
	1:

	/*
	 * If the dest and src are co-aligned to SZREG, then there is
	 * no need for the full rigmarole of a full misaligned fixup copy.
	 * Instead, do a simpler co-aligned copy.
	 */
	xor  t0, a0, a1
	andi t1, t0, (SZREG - 1)
	beqz t1, 26f
	/* Fall through to misaligned fixup copy */

1: /* Misaligned fixup copy */
	bltu a1, a0, 4f /* Misaligned fixup copy: Reverse */

3: /* Misaligned fixup copy: Forward */
	jal  t0, 24f /* Byte copy until aligned: Forward */

	andi a5, a3, (SZREG - 1) /* Find the alignment offset of src (a3) */
	slli a6, a5, 3 /* Multiply by 8 to convert that to bits to shift */

	sub  a3, a3, a5 /* Align the src pointer */

	/*
	 * Compute The Inverse Shift
	 * a7 = XLEN - a6 = XLEN + -a6
	 * 2s complement negation to find the negative: -a6 = ~a6 + 1
	 * Add that to XLEN.  XLEN = SZREG * 8.
	 */
	not  a7, a6
	addi a7, a7, (SZREG * 8 + 1)

	/*
	 * Fix Misalignment Copy Loop.
	 * while (store_ptr != store_ptr_end) {
	 *   *store_ptr = (load_ptr[0] >> {a6}) | (load_ptr[1] << {a7});
	 *   load_ptr++;
	 *   store_ptr++;
	 * }
	 */
	1:
	beq   t3, t6, 2f
	REG_L t1, 0x000(a3)
	REG_L t2, SZREG(a3)
	srl   t1, t1, a6
	sll   t2, t2, a7
	or    t1, t1, t2
	REG_S t1, 0x000(t3)
	addi  a3, a3, SZREG
	addi  t3, t3, SZREG
	j 1b
	2:

	add  a3, a3, a5 /* Restore the src pointer */
	j 22f /* Byte copy: Forward */ /* Copy any remaining bytes */

4: /* Misaligned fixup copy: Reverse */
	jal  t0, 25f /* Byte copy until aligned: Reverse */

	andi a5, a4, (SZREG - 1) /* Find the alignment offset of src (a4) */
	slli a6, a5, 3 /* Multiply by 8 to convert that to bits to shift */

	sub  a4, a4, a5 /* Align the src pointer */

	/*
	 * Compute The Inverse Shift
	 * a7 = XLEN - a6 = XLEN + -a6
	 * 2s complement negation to find the negative: -a6 = ~a6 + 1
	 * Add that to XLEN.  XLEN = SZREG * 8.
	 */
	not  a7, a6
	addi a7, a7, (SZREG * 8 + 1)

	/*
	 * Fix Misalignment Copy Loop.
	 * while (store_ptr != store_ptr_end) {
	 *   load_ptr--;
	 *   store_ptr--;
	 *   *store_ptr = (load_ptr[0] >> {a6}) | (load_ptr[1] << {a7});
	 * }
	 */
	1:
	beq   t4, t5, 2f
	addi  a4, a4, -SZREG
	addi  t4, t4, -SZREG
	REG_L t1, 0x000(a4)
	REG_L t2, SZREG(a4)
	srl   t1, t1, a6
	sll   t2, t2, a7
	or    t1, t1, t2
	REG_S t1, 0x000(t4)
	j 1b
	2:

	add  a4, a4, a5 /* Restore the src pointer */
	j 23f /* Byte copy: Reverse */ /* Copy any remaining bytes */

/*
 * Simple copy loops for SZREG co-aligned memory locations.
 * These also make calls to do byte copies for any unaligned
 * data at their terminations.
 */
26: /* Co-Aligned copy */
	bltu a1, a0, 3f /* Co-Aligned copy: Reverse */

2: /* Co-Aligned copy: Forward */
	jal t0, 24f /* Byte copy until aligned: Forward */

	1:
	beq   t3, t6, 22f /* Byte copy: Forward */
	REG_L t1, (a3)
	REG_S t1, (t3)
	addi  a3, a3, SZREG
	addi  t3, t3, SZREG
	j 1b

3: /* Co-Aligned copy: Reverse */
	jal t0, 25f /* Byte copy until aligned: Reverse */

	1:
	beq   t4, t5, 23f /* Byte copy: Reverse */
	addi  a4, a4, -SZREG
	addi  t4, t4, -SZREG
	REG_L t1, (a4)
	REG_S t1, (t4)
	j 1b

/*
 * These are basically sub-functions within the function.  They
 * are used to byte copy until the dest pointer is in alignment.
 * At which point, a bulk copy method can be used by the
 * calling code.  These work on the same registers as the bulk
 * copy loops.  Therefore, the register values can be picked
 * up from where they were left and we avoid code duplication
 * without any overhead except the call in and return jumps.
 */
24: /* Byte copy until aligned: Forward */
	beq  t3, t5, 1f /* Reuse the return from the other copy loop */
	lb   t1, (a3)
	sb   t1, (t3)
	addi a3, a3, 1
	addi t3, t3, 1
	j 24b

25: /* Byte copy until aligned: Reverse */
	beq  t4, t6, 1f
	addi a4, a4, -1
	addi t4, t4, -1
	lb   t1, (a4)
	sb   t1, (t4)
	j 25b
	1: jalr zero, 0x0(t0) /* Return to multibyte copy loop */

/*
 * Simple byte copy loops.
 * These will byte copy until they reach the end of data to copy.
 * At that point, they will call to return from memmove.
 */
21: /* Byte copy */
	bltu a1, a0, 23f /* Byte copy: Reverse */

22: /* Byte copy: Forward */
	beq  t3, t4, 20f /* Return from memmove */
	lb   t1, (a3)
	sb   t1, (t3)
	addi a3, a3, 1
	addi t3, t3, 1
	j 22b

23: /* Byte copy: Reverse */
	beq  t4, t3, 20f /* Return from memmove */
	addi a4, a4, -1
	addi t4, t4, -1
	lb   t1, (a4)
	sb   t1, (t4)
	j 23b

20: /* Return from memmove */
	ret

SYM_FUNC_END(memmove)
SYM_FUNC_END(__memmove)
