/* SPDX-License-Identifier: GPL-2.0 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <x86intrin.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "../kselftest.h"

#ifdef __i386__
#define XSAVE _xsave
#else
#define XSAVE _xsave64
#endif

#ifdef __i386__
#define XRSTOR _xrstor
#else
#define XRSTOR _xrstor64
#endif

#define XSAVE_MASK_FP (1 << XFEATURE_FP)
#define XSAVE_MASK_YMM (1 << XFEATURE_YMM)
#define XSAVE_MASK_OPMASK (1 << XFEATURE_OPMASK)
#define XSAVE_MASK_PKRU (1 << XFEATURE_PKRU)
#define XSAVE_TEST_MASK (XSAVE_MASK_FP | XSAVE_MASK_YMM | XSAVE_MASK_OPMASK \
			| XSAVE_MASK_PKRU)
#define RESULT_PASS 0
#define RESULT_FAIL 1
#define RESULT_ERROR 3
#define CHANGE 10
#define NO_CHANGE 11

/* Intel-defined CPU features, CPUID level 0x00000007:0 (EBX), word 9 */
#define X86_FEATURE_AVX2		(1U << 5) /* AVX2 instructions */
#define X86_FEATURE_AVX512F		(1U << 16) /* AVX-512 Foundation */

/* Intel-defined CPU features, CPUID level 0x00000007:0 (ecx) */
#define X86_FEATURE_PKU        (1U << 3) /* Protection Keys for Userspace */
#define X86_FEATURE_OSPKE      (1U << 4) /* OS Protection Keys Enable */

#define XSTATE_CPUID           (0xd)

/*
 * List of XSAVE features Linux knows about:
 */
enum xfeature {
	XFEATURE_FP,
	XFEATURE_SSE,
	/*
	 * Values above here are "legacy states".
	 * Those below are "extended states".
	 */
	XFEATURE_YMM,
	XFEATURE_BNDREGS,
	XFEATURE_BNDCSR,
	XFEATURE_OPMASK,
	XFEATURE_ZMM_Hi256,
	XFEATURE_Hi16_ZMM,
	XFEATURE_PT_UNIMPLEMENTED_SO_FAR,
	XFEATURE_PKRU,
	XFEATURE_PASID,
	XFEATURE_RSRVD_COMP_11,
	XFEATURE_RSRVD_COMP_12,
	XFEATURE_RSRVD_COMP_13,
	XFEATURE_RSRVD_COMP_14,
	XFEATURE_LBR,

	XFEATURE_MAX,
};

/* Copied from Linux kernel */
static inline void native_cpuid(unsigned int *eax, unsigned int *ebx,
				unsigned int *ecx, unsigned int *edx)
{
	/* ecx is often an input as well as an output. */
	asm volatile("cpuid"
		: "=a" (*eax),
		  "=b" (*ebx),
		  "=c" (*ecx),
		  "=d" (*edx)
		: "0" (*eax), "2" (*ecx)
		: "memory");
}

static inline int cpu_has_avx2(void)
{
	unsigned int eax, ebx, ecx, edx;

	eax = 0x7;
	ecx = 0x0;
	native_cpuid(&eax, &ebx, &ecx, &edx);

	/* Is avx2(Advanced Vector Extensions-2) enabled? */
	return (ebx & X86_FEATURE_AVX2);
}

static inline int cpu_has_avx512f(void)
{
	unsigned int eax, ebx, ecx, edx;

	eax = 0x7;
	ecx = 0x0;
	native_cpuid(&eax, &ebx, &ecx, &edx);

	/* Is avx512f(Advanced Vector Extensions-512 Foundation) enabled? */
	return (ebx & X86_FEATURE_AVX512F);
}

static inline int cpu_has_pkeys(void)
{
	unsigned int eax, ebx, ecx, edx;

	eax = 0x7;
	ecx = 0x0;
	native_cpuid(&eax, &ebx, &ecx, &edx);

	/* check if the cpu supports protection keys for user-mode pages */
	if (!(ecx & X86_FEATURE_PKU)) {
		printf("cpu does not have PKU\n");
		return 0;
	}
	/* check if the cpu supports RDPKRU/WRPKRU instructions */
	if (!(ecx & X86_FEATURE_OSPKE)) {
		printf("cpu does not have OSPKE\n");
		return 0;
	}

	return 1;
}

void execution_failed(char *reason, ...)
{
	ksft_test_result_xfail("%s", reason);
	ksft_exit_fail();
}

int get_xsave_size(void)
{
	unsigned int eax, ebx, ecx, edx;

	eax = 0x0d;
	ebx = 0;
	ecx = 0;
	edx = 0;
	native_cpuid(&eax, &ebx, &ecx, &edx);

	return (int)ecx;
}

void dump_buffer(unsigned char *buf, int size)
{
	int i, j;

	printf("xsave size = %d (%03xh)\n", size, size);

	for (i = 0; i < size; i += 16) {
		printf("%04x: ", i);

		for (j = i; ((j < i + 16) && (j < size)); j++)
			printf("%02x ", buf[j]);
		printf("\n");
	}
}

void show_part_buf(unsigned char *buf0, unsigned char *buf1, int start,
		int size)
{
	int c;

	printf("%04x: ", start);
	for (c = start; ((c < start + 16) && (c < size)); c++)
		printf("%02x ", buf0[c]);
	printf(" ->  ");
	for (c = start; ((c < start + 16) && (c < size)); c++)
		printf("%02x ", buf1[c]);
	printf("\n");
}

int show_buf_diff(unsigned char *buf0, unsigned char *buf1, int size)
{
	int a, b, result_buf = NO_CHANGE;

	for (a = 0; a < size; a += 16) {
		for (b = a; ((b < a + 16) && (b < size)); b++) {
			if (buf0[b] != buf1[b]) {
				show_part_buf(buf0, buf1, a, size);
				result_buf = CHANGE;
				break;
			}
		}
	}

	return result_buf;
}

int check_xsave_reserved_header(unsigned char *buf0,
			unsigned char *buf1, int size, const char *test_name)
{
	int a, b, result_resv_header = RESULT_PASS;

	/* SDM: Software should ensure that bytes 63:16 of the XSAVE header are 0 */
	for (a = 528; a < 576 ; a += 16) {
		for (b = a; ((b < a + 16) && (b < size)); b++) {
			if ((buf0[b] != 0) || (buf1[b] != 0)) {
				ksft_print_msg("%s FAIL: buf0[%d]:%d or buf1[%d]:%d not 0\n",
					test_name, b, buf0[b], b, buf1[b]);
				show_part_buf(buf0, buf1, a, size);
				result_resv_header = RESULT_FAIL;
				break;
			}
		}
	}

	return result_resv_header;
}

int compare_xsave_buf(unsigned char *buf0, unsigned char *buf1,
	int size, const char *test_name, int change)
{
	int result_buf = RESULT_ERROR, result_resv_header = RESULT_ERROR;

	switch (change) {
	case CHANGE:
		if (show_buf_diff(buf0, buf1, size) == CHANGE)
			result_buf = RESULT_PASS;
		else {
			ksft_print_msg("%s FAIL: xsave content was same\n", test_name);
			result_buf = RESULT_FAIL;
		}
		break;
	case NO_CHANGE:
		if (show_buf_diff(buf0, buf1, size) == CHANGE) {
			ksft_print_msg("%s FAIL: xsave content changed\n", test_name);
			show_buf_diff(buf0, buf1, size);
			result_buf = RESULT_FAIL;
		} else
			result_buf = RESULT_PASS;
		break;
	default:
		ksft_test_result_error("%s ERROR: invalid change:%d\n", test_name,
			change);
		break;
	}

	result_resv_header = check_xsave_reserved_header(buf0, buf1, size,
		test_name);

	return (result_buf || result_resv_header);
}

int verify_xsave_buf(unsigned char *xsave_buf, int offset, int size,
		unsigned char *verify_buf, const char *test_name)
{
	int check_byte, result = RESULT_PASS;

	printf("Verify offset:0x%x, size:0x%x\n%x: ", offset, size, offset);
	for (check_byte = 0; check_byte < size; check_byte++) {
		if (xsave_buf[offset + check_byte] == verify_buf[check_byte]) {
			printf("%02x ", xsave_buf[offset + check_byte]);
			continue;
		} else {
			ksft_print_msg("%s FAIL: xsave offset(0x%x+0x%x):%x, expect:%x\n",
				test_name, offset, check_byte, xsave_buf[offset + check_byte],
				verify_buf[check_byte]);
			result = RESULT_FAIL;
			break;
		}
	}
	if (result == RESULT_PASS)
		printf("xsave content check pass\n");

	return result;
}

void check_result(int result, const char *test_name)
{
	switch (result) {
	case RESULT_PASS:
		ksft_test_result_pass("%s PASS\n", test_name);
		break;
	case RESULT_FAIL:
		ksft_test_result_fail("%s FAIL\n", test_name);
		break;
	case RESULT_ERROR:
		ksft_test_result_fail("%s ERROR\n", test_name);
		break;
	default:
		ksft_test_result_error("%s ERROR: invalid result:%c\n",
			test_name, result);
		break;
	}
}

static int set_avx2_ymm(void)
{
	uint32_t ui32_ymm_value = 0x1F2F3F4F;

	if (!cpu_has_avx2()) {
		printf("[SKIP] AVX2 disabled, will not set ymm registers.\n");
		return 2;
	}

	/* populate ymm0 to ymm15 with 0x1F2F3F4F in 64bit kernel */
	asm volatile ("vbroadcastss %0, %%ymm0" : : "m" (ui32_ymm_value));
	asm volatile ("vbroadcastss %0, %%ymm1" : : "m" (ui32_ymm_value));
	asm volatile ("vbroadcastss %0, %%ymm2" : : "m" (ui32_ymm_value));
	asm volatile ("vbroadcastss %0, %%ymm3" : : "m" (ui32_ymm_value));
	asm volatile ("vbroadcastss %0, %%ymm4" : : "m" (ui32_ymm_value));
	asm volatile ("vbroadcastss %0, %%ymm5" : : "m" (ui32_ymm_value));
	asm volatile ("vbroadcastss %0, %%ymm6" : : "m" (ui32_ymm_value));
	asm volatile ("vbroadcastss %0, %%ymm7" : : "m" (ui32_ymm_value));
	#ifndef __i386__
	asm volatile ("vbroadcastss %0, %%ymm8" : : "m" (ui32_ymm_value));
	asm volatile ("vbroadcastss %0, %%ymm9" : : "m" (ui32_ymm_value));
	asm volatile ("vbroadcastss %0, %%ymm10" : : "m" (ui32_ymm_value));
	asm volatile ("vbroadcastss %0, %%ymm11" : : "m" (ui32_ymm_value));
	asm volatile ("vbroadcastss %0, %%ymm12" : : "m" (ui32_ymm_value));
	asm volatile ("vbroadcastss %0, %%ymm13" : : "m" (ui32_ymm_value));
	asm volatile ("vbroadcastss %0, %%ymm14" : : "m" (ui32_ymm_value));
	asm volatile ("vbroadcastss %0, %%ymm15" : : "m" (ui32_ymm_value));
	#endif

	return 0;
}

static inline unsigned int read_pkey_reg(void)
{
	unsigned int eax, edx, ecx = 0;

	asm volatile(".byte 0x0f, 0x01, 0xee\n\t"
		     : "=a" (eax), "=d" (edx)
		     : "c" (ecx));

	return eax;
}

static inline int write_pkey_reg(unsigned int pkey_reg)
{
	unsigned int eax = pkey_reg, ecx = 0, edx = 0, actual;
	int ret;

	asm volatile(".byte 0x0f, 0x01, 0xef\n\t"
		     : : "a" (eax), "c" (ecx), "d" (edx));
	actual = read_pkey_reg();
	ret = (pkey_reg == actual);
	if (!ret)
		printf("[ERROR] Write pkey with error, expected:0x%x, actual:0x%x\n",
			pkey_reg, actual);

	return ret;
}

int set_avx512_opmask(void)
{
	if (!cpu_has_avx512f()) {
		printf("[SKIP] avx512f disabled, skip avx512f part xstate.\n");
		return 2;
	}

	/* populate avx512 opmask k0 to k7 with all ff */
	asm volatile("kxnorq %%k0, %%k0, %%k0;" : : : "rcx");
	asm volatile("kxnorq %%k1, %%k1, %%k1;" : : : "rcx");
	asm volatile("kxnorq %%k2, %%k2, %%k2;" : : : "rcx");
	asm volatile("kxnorq %%k3, %%k3, %%k3;" : : : "rcx");
	asm volatile("kxnorq %%k4, %%k4, %%k4;" : : : "rcx");
	asm volatile("kxnorq %%k5, %%k5, %%k5;" : : : "rcx");
	asm volatile("kxnorq %%k6, %%k6, %%k6;" : : : "rcx");
	asm volatile("kxnorq %%k7, %%k7, %%k7;" : : : "rcx");

	return 0;
}

int set_pkru_xstate(void)
{
	unsigned int pkey_change = 0xfffffffc;

	if (!cpu_has_pkeys()) {
		printf("[SKIP] pkeys disabled, skip pkru part xstate.\n");
		return 2;
	}

	/*  populate PKRU xstate with 0xfffffffc */
	if (!write_pkey_reg(pkey_change)) {
		execution_failed("Write pkey %x FAIL\n", pkey_change);
		return RESULT_FAIL;
	}

	return 0;
}

void set_fpu_reg(void)
{
	uint32_t ui32;
	uint64_t ui64;
	float a = 0.12, b = 0.23;

	ui32 = 1;
	ui64 = 0xBAB00500FAB7;

	/* Initialize FPU and push different values onto FPU register stack */
	asm volatile("finit");
	asm volatile("fldl %0" : : "m" (ui64));
	asm volatile("flds %0" : : "m" (ui32));
	ui64 += 0x93ABE13;
	asm volatile("fldl %0" : : "m" (ui64));
	ui64 += 0x93;
	asm volatile("fldl %0" : : "m" (ui64));
	asm volatile("flds %0" : : "m" (ui32));
	asm volatile("fldl %0" : : "m" (ui64));
	ui64 -= 0x21;
	asm volatile("fldl %0" : : "m" (ui64));
	asm volatile("flds %0" : : "m" (ui32));
	asm volatile("fldl %0" : : "m" (ui64));
	a = a * b;
}

void populate_xstate_regs(void)
{
	set_fpu_reg();
	set_avx2_ymm();
	set_avx512_opmask();
	set_pkru_xstate();
}
