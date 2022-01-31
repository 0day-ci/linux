/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 Emil Renner Berthing
 */
#ifndef __ASM_RISCV_INSN_H
#define __ASM_RISCV_INSN_H

#include <linux/const.h>

#define RISCV_INSN_LD		_AC(0x00003003, U)
#define RISCV_INSN_ADDI		_AC(0x00000013, U)
#define RISCV_INSN_NOP		RISCV_INSN_ADDI
#define RISCV_INSN_AUIPC	_AC(0x00000017, U)
#define RISCV_INSN_LUI		_AC(0x00000037, U)
#define RISCV_INSN_JALR		_AC(0x00000067, U)
#define RISCV_INSN_JAL		_AC(0x0000006f, U)

#define RISCV_INSN_RA		_AC(0x1, U)
#define RISCV_INSN_T0		_AC(0x5, U)
#define RISCV_INSN_T1		_AC(0x6, U)

#define RISCV_INSN_RD_POS	7
#define RISCV_INSN_RD_RA	(RISCV_INSN_RA << RISCV_INSN_RD_POS)
#define RISCV_INSN_RD_T0	(RISCV_INSN_T0 << RISCV_INSN_RD_POS)
#define RISCV_INSN_RD_T1	(RISCV_INSN_T1 << RISCV_INSN_RD_POS)

#define RISCV_INSN_RS1_POS	15
#define RISCV_INSN_RS1_RA	(RISCV_INSN_RA << RISCV_INSN_RS1_POS)
#define RISCV_INSN_RS1_T0	(RISCV_INSN_T0 << RISCV_INSN_RS1_POS)
#define RISCV_INSN_RS1_T1	(RISCV_INSN_T1 << RISCV_INSN_RS1_POS)

#define RISCV_INSN_I_IMM_MASK	_AC(0xfff00000, U)
#define RISCV_INSN_S_IMM_MASK	_AC(0xfe000f80, U)
#define RISCV_INSN_B_IMM_MASK	_AC(0xfe000f80, U)
#define RISCV_INSN_U_IMM_MASK	_AC(0xfffff000, U)
#define RISCV_INSN_J_IMM_MASK	_AC(0xfffff000, U)

#define RISCV_INSN_CI_IMM_MASK	_AC(0x107c, U)
#define RISCV_INSN_CSS_IMM_MASK	_AC(0x1f80, U)
#define RISCV_INSN_CIW_IMM_MASK	_AC(0x1fe0, U)
#define RISCV_INSN_CL_IMM_MASK	_AC(0x1c60, U)
#define RISCV_INSN_CS_IMM_MASK	_AC(0x1c60, U)
#define RISCV_INSN_CB_IMM_MASK	_AC(0x1c7c, U)
#define RISCV_INSN_CJ_IMM_MASK	_AC(0x1ffc, U)

#ifndef __ASSEMBLY__
#include <linux/bits.h>
#include <asm/types.h>

static inline bool riscv_insn_valid_20bit_offset(ptrdiff_t val)
{
	return !(val & 1) && -(1L << 19) <= val && val < (1L << 19);
}

static inline bool riscv_insn_valid_32bit_offset(ptrdiff_t val)
{
	if (IS_ENABLED(CONFIG_32BIT))
		return true;

	/*
	 * auipc+jalr can reach any PC-relative offset in the range
	 * [-2^31 - 2^11, 2^31 - 2^11)
	 */
	return (-(1L << 31) - (1L << 11)) <= val && val < ((1L << 31) - (1L << 11));
}

static inline u32 riscv_insn_i_imm(u32 imm)
{
	return (imm & GENMASK(11, 0)) << 20;
}

static inline u32 riscv_insn_s_imm(u32 imm)
{
	return (imm & GENMASK( 4, 0)) << ( 7 - 0) |
	       (imm & GENMASK(11, 5)) << (25 - 5);
}

static inline u32 riscv_insn_b_imm(u32 imm)
{
	return (imm & GENMASK(11, 11)) >> (11 -  7) |
	       (imm & GENMASK( 4,  1)) << ( 8 -  1) |
	       (imm & GENMASK(10,  5)) << (25 -  5) |
	       (imm & GENMASK(12, 12)) << (31 - 12);
}

static inline u32 riscv_insn_u_imm(u32 imm)
{
	return imm & GENMASK(31, 12);
}

static inline u32 riscv_insn_j_imm(u32 imm)
{
	return (imm & GENMASK(19, 12)) << (12 - 12) |
	       (imm & GENMASK(11, 11)) << (20 - 11) |
	       (imm & GENMASK(10,  1)) << (21 -  1) |
	       (imm & GENMASK(20, 20)) << (31 - 20);
}

static inline u16 riscv_insn_rvc_branch_imm(u16 imm)
{
	return (imm & GENMASK(5, 5)) >> ( 5 - 2) |
	       (imm & GENMASK(2, 1)) << ( 3 - 1) |
	       (imm & GENMASK(7, 6)) >> ( 6 - 5) |
	       (imm & GENMASK(4, 3)) << (10 - 3) |
	       (imm & GENMASK(8, 8)) << (12 - 8);
}

static inline u16 riscv_insn_rvc_jump_imm(u16 imm)
{
	return (imm & GENMASK( 5,  5)) >> ( 5 -  2) |
	       (imm & GENMASK( 3,  1)) << ( 3 -  1) |
	       (imm & GENMASK( 7,  7)) >> ( 7 -  6) |
	       (imm & GENMASK( 6,  6)) << ( 7 -  6) |
	       (imm & GENMASK(10, 10)) >> (10 -  8) |
	       (imm & GENMASK( 9,  8)) << ( 9 -  8) |
	       (imm & GENMASK( 4,  4)) << (11 -  4) |
	       (imm & GENMASK(11, 11)) << (12 - 11);
}

#endif
#endif
