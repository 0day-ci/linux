# SPDX-License-Identifier: GPL-2.0
include ../../../../../../scripts/Kbuild.include

all: $(TEST_GEN_PROGS) mmcr0_exceptionbits_test.c mmcr0_cc56run_test.c mmcr0_pmccext_test.c \
			mmcr0_pmcjce_test.c mmcr0_fc56_pmc1ce_test.c mmcr0_fc56_pmc56_test.c \
			mmcr1_comb_test.c mmcr1_sel_unit_cache_test.c mmcr2_l2l3_test.c \
			mmcr2_fcs_fch_test.c mmcr3_src_test.c mmcra_thresh_marked_sample_test.c

noarg:
	$(MAKE) -C ../../

CFLAGS += -m64 -I../../../../../lib/

# Toolchains may build PIE by default which breaks the assembly
no-pie-option := $(call try-run, echo 'int main() { return 0; }' | \
	$(CC) -Werror $(KBUILD_CPPFLAGS) $(CC_OPTION_CFLAGS) -no-pie -x c - -o "$$TMP", -no-pie)

TEST_GEN_PROGS := mmcr0_exceptionbits_test mmcr0_cc56run_test mmcr0_pmccext_test \
		   mmcr0_pmcjce_test mmcr0_fc56_pmc1ce_test mmcr0_fc56_pmc56_test \
		   mmcr1_comb_test mmcr1_sel_unit_cache_test mmcr2_l2l3_test \
		   mmcr2_fcs_fch_test mmcr3_src_test mmcra_thresh_marked_sample_test

LDFLAGS += $(no-pie-option)

top_srcdir = ../../../../../..
include ../../../lib.mk

$(TEST_GEN_PROGS): ../../harness.c ../../utils.c ../event.c ../lib.c misc.c misc.h ../loop.S
