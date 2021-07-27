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

#define SAVE_MASK 0xffffffffffffffff
#define RESULT_PASS 0
#define RESULT_FAIL 1
#define RESULT_ERROR 3
#define CHANGE 10
#define NO_CHANGE 11

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

void execution_failed(char *reason)
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
	int a, b, result_buf = RESULT_PASS;

	for (a = 0; a < size; a += 16) {
		/* SDM "XSAVE Area": XSAVE feature set does not use bytes 511:416 */
		if ((a >= 416) && (a <= 511))
			continue;

		for (b = a; ((b < a + 16) && (b < size)); b++) {
			if (buf0[b] != buf1[b]) {
				show_part_buf(buf0, buf1, a, size);
				result_buf = RESULT_FAIL;
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

	/* SDM "Form of XRSTOR": Bytes 63:16 of the XSAVE header should 0 */
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

int check_xsave_buf(unsigned char *buf0, unsigned char *buf1,
	int size, const char *test_name, int change)
{
	int result_buf = RESULT_ERROR, result_resv_header = RESULT_ERROR;

	switch (change) {
	case CHANGE:
		if (show_buf_diff(buf0, buf1, size))
			result_buf = RESULT_PASS;
		else {
			ksft_print_msg("%s FAIL: xsave content was same\n", test_name);
			result_buf = RESULT_FAIL;
		}
		break;
	case NO_CHANGE:
		if (show_buf_diff(buf0, buf1, size)) {
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

void populate_fpu_regs(void)
{
	uint32_t ui32;
	uint64_t ui64;

	ui32 = 1;
	ui64 = 0xBAB00500FAB7;

	/* Initialize FPU and push different values onto FPU register stack: */
	asm volatile ("finit");
	asm volatile ("fldl %0" : : "m" (ui64));
	asm volatile ("flds %0" : : "m" (ui32));
	ui64 += 0x93ABE13;
	asm volatile ("fldl %0" : : "m" (ui64));
	ui64 += 0x93;
	asm volatile ("fldl %0" : : "m" (ui64));
	asm volatile ("flds %0" : : "m" (ui32));
	asm volatile ("fldl %0" : : "m" (ui64));
	ui64 -= 0x21;
	asm volatile ("fldl %0" : : "m" (ui64));
	asm volatile ("flds %0" : : "m" (ui32));
	asm volatile ("fldl %0" : : "m" (ui64));

	/* Fill each remaining YMM register with a different value: */
	asm volatile ("vbroadcastss %0, %%ymm0" : : "m" (ui32));
	ui32 = 0xFAFBABAF;
	asm volatile ("vbroadcastss %0, %%ymm1" : : "m" (ui32));
	ui32 -= 0xA;
	asm volatile ("vbroadcastss %0, %%ymm2" : : "m" (ui32));
	ui32 -= 0xB;
	asm volatile ("vbroadcastss %0, %%ymm3" : : "m" (ui32));
	ui32 -= 0x3;
	asm volatile ("vbroadcastss %0, %%ymm4" : : "m" (ui32));
	ui32 += 0xA;
	asm volatile ("vbroadcastss %0, %%ymm5" : : "m" (ui32));
	ui32 -= 0x7;
	asm volatile ("vbroadcastss %0, %%ymm6" : : "m" (ui32));
	ui32 -= 0xABABA;
	asm volatile ("vbroadcastss %0, %%ymm7" : : "m" (ui32));

	#ifndef __i386__
	ui32 += 0xF7;
	asm volatile ("vbroadcastss %0, %%ymm8" : : "m" (ui32));
	ui32 -= 0x7;
	asm volatile ("vbroadcastss %0, %%ymm9" : : "m" (ui32));
	ui32 += 0x2;
	asm volatile ("vbroadcastss %0, %%ymm10" : : "m" (ui32));
	ui32 += 0xD;
	asm volatile ("vbroadcastss %0, %%ymm11" : : "m" (ui32));
	ui32 -= 0x4;
	asm volatile ("vbroadcastss %0, %%ymm12" : : "m" (ui32));
	ui32 -= 0xDD;
	asm volatile ("vbroadcastss %0, %%ymm13" : : "m" (ui32));
	ui32 -= 0xABD;
	asm volatile ("vbroadcastss %0, %%ymm14" : : "m" (ui32));
	ui32 += 0xBEBABF456;
	asm volatile ("vbroadcastss %0, %%ymm15" : : "m" (ui32));
	#endif
}
