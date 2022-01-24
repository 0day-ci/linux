/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Branch Record Buffer Extension Helpers.
 *
 * Copyright (C) 2021 ARM Limited
 *
 * Author: Anshuman Khandual <anshuman.khandual@arm.com>
 */
#define pr_fmt(fmt) "brbe: " fmt

#include <linux/perf/arm_pmu.h>

/*
 * BRBE Instructions
 *
 * BRB_IALL : Invalidate the entire buffer
 * BRB_INJ  : Inject latest branch record derived from [BRBSRCINJ, BRBTGTINJ, BRBINFINJ]
 */
#define BRB_IALL __emit_inst(0xD5000000 | sys_insn(1, 1, 7, 2, 4) | (0x1f))
#define BRB_INJ  __emit_inst(0xD5000000 | sys_insn(1, 1, 7, 2, 5) | (0x1f))

/*
 * BRBE Buffer Organization
 *
 * BRBE buffer is arranged as multiple banks of 32 branch record
 * entries each. An indivdial branch record in a given bank could
 * be accessedi, after selecting the bank in BRBFCR_EL1.BANK and
 * accessing the registers i.e [BRBSRC, BRBTGT, BRBINF] set with
 * indices [0..31].
 *
 * Bank 0
 *
 *	---------------------------------	------
 *	| 00 | BRBSRC | BRBTGT | BRBINF |	| 00 |
 *	---------------------------------	------
 *	| 01 | BRBSRC | BRBTGT | BRBINF |	| 01 |
 *	---------------------------------	------
 *	| .. | BRBSRC | BRBTGT | BRBINF |	| .. |
 *	---------------------------------	------
 *	| 31 | BRBSRC | BRBTGT | BRBINF |	| 31 |
 *	---------------------------------	------
 *
 * Bank 1
 *
 *	---------------------------------	------
 *	| 32 | BRBSRC | BRBTGT | BRBINF |	| 00 |
 *	---------------------------------	------
 *	| 33 | BRBSRC | BRBTGT | BRBINF |	| 01 |
 *	---------------------------------	------
 *	| .. | BRBSRC | BRBTGT | BRBINF |	| .. |
 *	---------------------------------	------
 *	| 63 | BRBSRC | BRBTGT | BRBINF |	| 31 |
 *	---------------------------------	------
 */
#define BRBE_BANK0_IDX_MIN 0
#define BRBE_BANK0_IDX_MAX 31
#define BRBE_BANK1_IDX_MIN 32
#define BRBE_BANK1_IDX_MAX 63

#define RETURN_READ_BRBSRCN(n) \
	read_sysreg_s(SYS_BRBSRC##n##_EL1)

#define RETURN_READ_BRBTGTN(n) \
	read_sysreg_s(SYS_BRBTGT##n##_EL1)

#define RETURN_READ_BRBINFN(n) \
	read_sysreg_s(SYS_BRBINF##n##_EL1)

#define BRBE_REGN_CASE(n, case_macro) \
	case n: return case_macro(n); break

#define BRBE_REGN_SWITCH(x, case_macro)				\
	do {							\
		switch (x) {					\
		BRBE_REGN_CASE(0, case_macro);			\
		BRBE_REGN_CASE(1, case_macro);			\
		BRBE_REGN_CASE(2, case_macro);			\
		BRBE_REGN_CASE(3, case_macro);			\
		BRBE_REGN_CASE(4, case_macro);			\
		BRBE_REGN_CASE(5, case_macro);			\
		BRBE_REGN_CASE(6, case_macro);			\
		BRBE_REGN_CASE(7, case_macro);			\
		BRBE_REGN_CASE(8, case_macro);			\
		BRBE_REGN_CASE(9, case_macro);			\
		BRBE_REGN_CASE(10, case_macro);			\
		BRBE_REGN_CASE(11, case_macro);			\
		BRBE_REGN_CASE(12, case_macro);			\
		BRBE_REGN_CASE(13, case_macro);			\
		BRBE_REGN_CASE(14, case_macro);			\
		BRBE_REGN_CASE(15, case_macro);			\
		BRBE_REGN_CASE(16, case_macro);			\
		BRBE_REGN_CASE(17, case_macro);			\
		BRBE_REGN_CASE(18, case_macro);			\
		BRBE_REGN_CASE(19, case_macro);			\
		BRBE_REGN_CASE(20, case_macro);			\
		BRBE_REGN_CASE(21, case_macro);			\
		BRBE_REGN_CASE(22, case_macro);			\
		BRBE_REGN_CASE(23, case_macro);			\
		BRBE_REGN_CASE(24, case_macro);			\
		BRBE_REGN_CASE(25, case_macro);			\
		BRBE_REGN_CASE(26, case_macro);			\
		BRBE_REGN_CASE(27, case_macro);			\
		BRBE_REGN_CASE(28, case_macro);			\
		BRBE_REGN_CASE(29, case_macro);			\
		BRBE_REGN_CASE(30, case_macro);			\
		BRBE_REGN_CASE(31, case_macro);			\
		default:					\
			pr_warn("unknown register index\n");	\
			return -1;				\
		}						\
	} while (0)

static inline int buffer_to_brbe_idx(int buffer_idx)
{
	return buffer_idx % 32;
}

static inline u64 get_brbsrc_reg(int buffer_idx)
{
	int brbe_idx = buffer_to_brbe_idx(buffer_idx);

	BRBE_REGN_SWITCH(brbe_idx, RETURN_READ_BRBSRCN);
}

static inline u64 get_brbtgt_reg(int buffer_idx)
{
	int brbe_idx = buffer_to_brbe_idx(buffer_idx);

	BRBE_REGN_SWITCH(brbe_idx, RETURN_READ_BRBTGTN);
}

static inline u64 get_brbinf_reg(int buffer_idx)
{
	int brbe_idx = buffer_to_brbe_idx(buffer_idx);

	BRBE_REGN_SWITCH(brbe_idx, RETURN_READ_BRBINFN);
}

static inline u64 brbe_record_valid(u64 brbinf)
{
	return brbinf & (BRBINF_VALID_MASK << BRBINF_VALID_SHIFT);
}

static inline bool brbe_invalid(u64 brbinf)
{
	return brbe_record_valid(brbinf) == BRBINF_VALID_INVALID;
}

static inline bool brbe_valid(u64 brbinf)
{
	return brbe_record_valid(brbinf) == BRBINF_VALID_ALL;
}

static inline bool brbe_source(u64 brbinf)
{
	return brbe_record_valid(brbinf) == BRBINF_VALID_SOURCE;
}

static inline bool brbe_target(u64 brbinf)
{
	return brbe_record_valid(brbinf) == BRBINF_VALID_TARGET;
}

static inline int brbe_fetch_cycles(u64 brbinf)
{
	/*
	 * Captured cycle count is unknown and hence
	 * should not be passed on the user space.
	 */
	if (brbinf & BRBINF_CCU)
		return 0;

	return (brbinf >> BRBINF_CC_SHIFT) & BRBINF_CC_MASK;
}

static inline int brbe_fetch_type(u64 brbinf)
{
	return (brbinf >> BRBINF_TYPE_SHIFT) & BRBINF_TYPE_MASK;
}

static inline int brbe_fetch_el(u64 brbinf)
{
	return (brbinf >> BRBINF_EL_SHIFT) & BRBINF_EL_MASK;
}

static inline int brbe_fetch_numrec(u64 brbidr)
{
	return (brbidr >> BRBIDR0_NUMREC_SHIFT) & BRBIDR0_NUMREC_MASK;
}

static inline int brbe_fetch_format(u64 brbidr)
{
	return (brbidr >> BRBIDR0_FORMAT_SHIFT) & BRBIDR0_FORMAT_MASK;
}

static inline int brbe_fetch_cc_bits(u64 brbidr)
{
	return (brbidr >> BRBIDR0_CC_SHIFT) & BRBIDR0_CC_MASK;
}

static inline void select_brbe_bank(int bank)
{
	static int brbe_current_bank = -1;
	u64 brbfcr;

	if (brbe_current_bank == bank)
		return;

	WARN_ON(bank > 1);
	brbfcr = read_sysreg_s(SYS_BRBFCR_EL1);
	brbfcr &= ~(BRBFCR_BANK_MASK << BRBFCR_BANK_SHIFT);
	brbfcr |= ((bank & BRBFCR_BANK_MASK) << BRBFCR_BANK_SHIFT);
	write_sysreg_s(brbfcr, SYS_BRBFCR_EL1);
	isb();
	brbe_current_bank = bank;
}

static inline void select_brbe_bank_index(int buffer_idx)
{
	switch (buffer_idx) {
	case BRBE_BANK0_IDX_MIN ... BRBE_BANK0_IDX_MAX:
		select_brbe_bank(0);
		break;
	case BRBE_BANK1_IDX_MIN ... BRBE_BANK1_IDX_MAX:
		select_brbe_bank(1);
		break;
	default:
		pr_warn("unsupported BRBE index\n");
	}
}

static inline bool valid_brbe_nr(int brbe_nr)
{
	switch (brbe_nr) {
	case BRBIDR0_NUMREC_8:
	case BRBIDR0_NUMREC_16:
	case BRBIDR0_NUMREC_32:
	case BRBIDR0_NUMREC_64:
		return true;
	default:
		pr_warn("unsupported BRBE entries\n");
		return false;
	}
}

static inline bool brbe_paused(void)
{
	u64 brbfcr = read_sysreg_s(SYS_BRBFCR_EL1);

	return brbfcr & BRBFCR_PAUSED;
}

static inline void set_brbe_paused(void)
{
	u64 brbfcr = read_sysreg_s(SYS_BRBFCR_EL1);

	write_sysreg_s(brbfcr | BRBFCR_PAUSED, SYS_BRBFCR_EL1);
	isb();
}
