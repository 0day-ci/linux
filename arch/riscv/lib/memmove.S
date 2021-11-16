/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/linkage.h>
#include <asm/asm.h>

ENTRY(__memmove)
WEAK(memmove)
        bltu    a0, a1, __memcpy

        add     t1, a1, a2
        bleu    t1, a0, __memcpy

        beq     a0, a1, exit_memcpy
        beqz    a2, exit_memcpy

        /* reverse src and dst */
        add     t0, a0, a2
        /* check low-order bits match */
        andi    a3, t0, SZREG-1
        andi    a4, t1, SZREG-1
        bne     a3, a4, copy_tail_loop
        beqz    a4, copy_body

copy_head:
        /* head misalignment */
        addi    t1, t1, -1
        lb      t3, 0(t1)
        addi    t0, t0, -1
        addi    a4, a4, -1
        sb      t3, 0(t0)
        bnez    a4, copy_head
        sub     a2, a2, a3

copy_body:
        andi    a4, a2, ~(SZREG-1)
        andi    a2, a2, (SZREG-1)
        beqz    a4, copy_tail
copy_body_loop:
        addi    t1, t1, -SZREG
        REG_L   t3, 0(t1)
        addi    t0, t0, -SZREG
        addi    a4, a4, -SZREG
        REG_S   t3, 0(t0)
        bnez    a4, copy_body_loop

copy_tail:
        /* tail misalignment */
        beqz    a2, exit_memcpy
copy_tail_loop:
        addi    t1, t1, -1
        lb      t3, 0(t1)
        addi    t0, t0, -1
        addi    a2, a2, -1
        sb      t3, 0(t0)
        bnez    a2, copy_tail_loop

exit_memcpy:
        ret
END(__memmove)
