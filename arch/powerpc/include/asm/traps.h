/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_PPC_TRAPS_H
#define _ASM_PPC_TRAPS_H

#define TRAP_RESET   0x100 /* System reset */
#define TRAP_MCE     0x200 /* Machine check */
#define TRAP_DSI     0x300 /* Data storage */
#define TRAP_DSEGI   0x380 /* Data segment */
#define TRAP_ISI     0x400 /* Instruction storage */
#define TRAP_ISEGI   0x480 /* Instruction segment */
#define TRAP_ALIGN   0x600 /* Alignment */
#define TRAP_PROG    0x700 /* Program */
#define TRAP_DEC     0x900 /* Decrementer */
#define TRAP_SYSCALL 0xc00 /* System call */
#define TRAP_TRACEI  0xd00 /* Trace */
#define TRAP_FPA     0xe00 /* Floating-point Assist */
#define TRAP_PMI     0xf00 /* Performance monitor */

#endif /* _ASM_PPC_TRAPS_H */
