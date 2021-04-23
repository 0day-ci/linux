/* SPDX-License-Identifier: GPL-2.0 */
.macro save_registers, el
	stp	x28, x29, [sp, #-16]!
	stp	x26, x27, [sp, #-16]!
	stp	x24, x25, [sp, #-16]!
	stp	x22, x23, [sp, #-16]!
	stp	x20, x21, [sp, #-16]!
	stp	x18, x19, [sp, #-16]!
	stp	x16, x17, [sp, #-16]!
	stp	x14, x15, [sp, #-16]!
	stp	x12, x13, [sp, #-16]!
	stp	x10, x11, [sp, #-16]!
	stp	x8, x9, [sp, #-16]!
	stp	x6, x7, [sp, #-16]!
	stp	x4, x5, [sp, #-16]!
	stp	x2, x3, [sp, #-16]!
	stp	x0, x1, [sp, #-16]!

	.if \el == 0
	mrs	x1, sp_el0
	.else
	mov	x1, sp
	.endif
	stp	x1, lr, [sp, #-16]! /* SP, LR */

	mrs	x1, elr_el1
	mrs	x2, spsr_el1
	stp	x1, x2, [sp, #-16]! /* PC, PSTATE */
.endm

.macro restore_registers, el
	ldp	x1, x2, [sp], #16 /* PC, PSTATE */
	msr	elr_el1, x1
	msr	spsr_el1, x2

	ldp	x1, lr, [sp], #16 /* SP, LR */
	.if \el == 0
	msr	sp_el0, x1
	.endif

	ldp	x0, x1, [sp], #16
	ldp	x2, x3, [sp], #16
	ldp	x4, x5, [sp], #16
	ldp	x6, x7, [sp], #16
	ldp	x8, x9, [sp], #16
	ldp	x10, x11, [sp], #16
	ldp	x12, x13, [sp], #16
	ldp	x14, x15, [sp], #16
	ldp	x16, x17, [sp], #16
	ldp	x18, x19, [sp], #16
	ldp	x20, x21, [sp], #16
	ldp	x22, x23, [sp], #16
	ldp	x24, x25, [sp], #16
	ldp	x26, x27, [sp], #16
	ldp	x28, x29, [sp], #16

	eret
.endm

.pushsection ".entry.text", "ax"
.balign 0x800
.global vectors
vectors:
.popsection

/*
 * Build an exception handler for vector and append a jump to it into
 * vectors (while making sure that it's 0x80 aligned).
 */
.macro HANDLER, el, label, vector
handler\()\vector:
	save_registers \el
	mov	x0, sp
	mov	x1, \vector
	bl	route_exception
	restore_registers \el

.pushsection ".entry.text", "ax"
.balign 0x80
	b	handler\()\vector
.popsection
.endm

.global ex_handler_code
ex_handler_code:
	HANDLER	1, sync, 0			// Synchronous EL1t
	HANDLER	1, irq, 1			// IRQ EL1t
	HANDLER	1, fiq, 2			// FIQ EL1t
	HANDLER	1, error, 3			// Error EL1t

	HANDLER	1, sync, 4			// Synchronous EL1h
	HANDLER	1, irq, 5			// IRQ EL1h
	HANDLER	1, fiq, 6			// FIQ EL1h
	HANDLER	1, error, 7			// Error EL1h

	HANDLER	0, sync, 8			// Synchronous 64-bit EL0
	HANDLER	0, irq, 9			// IRQ 64-bit EL0
	HANDLER	0, fiq, 10			// FIQ 64-bit EL0
	HANDLER	0, error, 11			// Error 64-bit EL0

	HANDLER	0, sync, 12			// Synchronous 32-bit EL0
	HANDLER	0, irq, 13			// IRQ 32-bit EL0
	HANDLER	0, fiq, 14			// FIQ 32-bit EL0
	HANDLER	0, error, 15			// Error 32-bit EL0
