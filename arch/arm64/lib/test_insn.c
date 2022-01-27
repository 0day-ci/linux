// SPDX-License-Identifier: GPL-2.0-only
/*
 * Test cases for the aarch64 insn encoder.
 *
 * Copyright (C) 2021 ARM Limited.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>

#include <asm/debug-monitors.h>
#include <asm/insn.h>

#include "../../../tools/testing/selftests/kselftest_module.h"

struct bitmask_test_case {
	/* input */
	u64 imm;

	/* expected output */
	u64 n, immr, imms;
};
struct bitmask_test_case aarch64_logic_imm_test[] = {
#include <asm/test_logic_imm_generated.h>
};

KSTM_MODULE_GLOBALS();

static void __init test_logic_imm(void)
{
	int i;
	u8 rd, rn;
	u32 insn;

	for (i = 0; i < ARRAY_SIZE(aarch64_logic_imm_test); i++) {
		total_tests++;

		rd = i % 30;
		rn = (i + 1) % 30;

		insn = aarch64_insn_gen_logical_immediate(AARCH64_INSN_LOGIC_AND,
							  AARCH64_INSN_VARIANT_64BIT,
							  rn, rd, aarch64_logic_imm_test[i].imm);

		if (!aarch64_insn_is_and_imm(insn) ||
		    rd != aarch64_insn_decode_register(AARCH64_INSN_REGTYPE_RD, insn) ||
		    rn != aarch64_insn_decode_register(AARCH64_INSN_REGTYPE_RN, insn) ||
		    aarch64_logic_imm_test[i].imms != aarch64_insn_decode_immediate(AARCH64_INSN_IMM_S, insn) ||
		    aarch64_logic_imm_test[i].immr != aarch64_insn_decode_immediate(AARCH64_INSN_IMM_R, insn) ||
		    aarch64_logic_imm_test[i].n != aarch64_insn_decode_immediate(AARCH64_INSN_IMM_N, insn)) {
			failed_tests++;
			pr_warn_once("[%s:%u] Failed to encode immediate 0x%llx (got insn 0x%x))\n",
				     __FILE__, __LINE__, aarch64_logic_imm_test[i].imm, insn);
			continue;
		}
	}
}

static void __init do_test_bad_logic_imm(u64 imm, enum aarch64_insn_variant var)
{
	u32 insn;

	total_tests++;
	insn = aarch64_insn_gen_logical_immediate(AARCH64_INSN_LOGIC_AND,
						  var, 0, 0, imm);
	if (insn != AARCH64_BREAK_FAULT)
		failed_tests++;
}

static void __init test_bad_logic_imm(void)
{
	do_test_bad_logic_imm(0, AARCH64_INSN_VARIANT_64BIT);
	do_test_bad_logic_imm(0x1234, AARCH64_INSN_VARIANT_64BIT);
	do_test_bad_logic_imm(0xffffffffffffffff, AARCH64_INSN_VARIANT_64BIT);
	do_test_bad_logic_imm((1ULL<<32), AARCH64_INSN_VARIANT_32BIT);
}

static void __init selftest(void)
{
	test_logic_imm();
	test_bad_logic_imm();
}

KSTM_MODULE_LOADERS(test_insn);
MODULE_AUTHOR("James Morse <james.morse@arm.com>");
MODULE_LICENSE("GPL");
