/* SPDX-License-Identifier: GPL-2.0
 *
 * CSKY  860  MATHEMU
 *
 * Copyright (C) 2021 Hangzhou C-SKY Microsystems co.,ltd.
 *
 *    Authors: Li Weiwei <liweiwei@iscas.ac.cn>
 *             Wang Junqiang <wangjunqiang@iscas.ac.cn>
 *
 */
#ifndef __CSKY_FP810_H__
#define __CSKY_FP810_H__

#include "math.h"
/*
 * 5 - 12 bits in SOP.
 */
#define FPUV2_FABSD 0x46
#define FPUV2_FABSM 0x86
#define FPUV2_FABSS 0x6
#define FPUV2_FADDD 0x40
#define FPUV2_FADDM 0x80
#define FPUV2_FADDS 0x0
#define FPUV2_FCMPHSD 0x4c
#define FPUV2_FCMPHSS 0xc
#define FPUV2_FCMPLTD 0x4d
#define FPUV2_FCMPLTS 0xd
#define FPUV2_FCMPNED 0x4e
#define FPUV2_FCMPNES 0xe
#define FPUV2_FCMPUOD 0x4f
#define FPUV2_FCMPUOS 0xf
#define FPUV2_FCMPZHSD 0x48
#define FPUV2_FCMPZHSS 0x8
#define FPUV2_FCMPZLSD 0x49
#define FPUV2_FCMPZLSS 0x9
#define FPUV2_FCMPZNED 0x4a
#define FPUV2_FCMPZNES 0xa
#define FPUV2_FCMPZUOD 0x4b
#define FPUV2_FCMPZUOS 0xb
#define FPUV2_FDIVD 0x58
#define FPUV2_FDIVS 0x18
#define FPUV2_FDTOS 0xd6
#define FPUV2_FDTOSI_RN 0xc8
#define FPUV2_FDTOSI_RZ 0xc9
#define FPUV2_FDTOSI_RPI 0xca
#define FPUV2_FDTOSI_RNI 0xcb
#define FPUV2_FDTOUI_RN 0xcc
#define FPUV2_FDTOUI_RZ 0xcd
#define FPUV2_FDTOUI_RPI 0xce
#define FPUV2_FDTOUI_RNI 0xcf
#define FPUV2_FMACD 0x54
#define FPUV2_FMACM 0x94
#define FPUV2_FMACS 0x14
#define FPUV2_FMFVRH 0xd8
#define FPUV2_FMFVRL 0xd9
#define FPUV2_FMOVD 0x44
#define FPUV2_FMOVM 0x84
#define FPUV2_FMOVS 0x4
#define FPUV2_FMSCD 0x55
#define FPUV2_FMSCM 0x95
#define FPUV2_FMSCS 0x15
#define FPUV2_FMTVRH 0xda
#define FPUV2_FMTVRL 0xdb
#define FPUV2_FMULD 0x50
#define FPUV2_FMULM 0x90
#define FPUV2_FMULS 0x10
#define FPUV2_FNEGD 0x47
#define FPUV2_FNEGM 0x87
#define FPUV2_FNEGS 0x7
#define FPUV2_FNMACD 0x56
#define FPUV2_FNMACM 0x96
#define FPUV2_FNMACS 0x16
#define FPUV2_FNMSCD 0x57
#define FPUV2_FNMSCM 0x97
#define FPUV2_FNMSCS 0x17
#define FPUV2_FNMULD 0x51
#define FPUV2_FNMULM 0x91
#define FPUV2_FNMULS 0x11
#define FPUV2_FRECIPD 0x59
#define FPUV2_FRECIPS 0x19
#define FPUV2_FSITOD 0xd4
#define FPUV2_FSITOS 0xd0
#define FPUV2_FSQRTD 0x5a
#define FPUV2_FSQRTS 0x1a
#define FPUV2_FSTOD 0xd7
#define FPUV2_FSTOSI_RN 0xc0
#define FPUV2_FSTOSI_RZ 0xc1
#define FPUV2_FSTOSI_RPI 0xc2
#define FPUV2_FSTOSI_RNI 0xc3
#define FPUV2_FSTOUI_RN 0xc4
#define FPUV2_FSTOUI_RZ 0xc5
#define FPUV2_FSTOUI_RPI 0xc6
#define FPUV2_FSTOUI_RNI 0xc7
#define FPUV2_FSUBD 0x41
#define FPUV2_FSUBM 0x81
#define FPUV2_FSUBS 0x1
#define FPUV2_FUITOD 0xd5
#define FPUV2_FUITOS 0xd1

/*
 * 8 - 12 bits in SOP.
 */
#define FPUV2_FLDD 0x1
#define FPUV2_FLDM 0x2
#define FPUV2_FLDMD 0x11
#define FPUV2_FLDMM 0x12
#define FPUV2_FLDMS 0x10
#define FPUV2_FLDRD 0x9
#define FPUV2_FLDRM 0xa
#define FPUV2_FLDRS 0x8
#define FPUV2_FLDS 0x0
#define FPUV2_FSTD 0x5
#define FPUV2_FSTM 0x6
#define FPUV2_FSTMD 0x15
#define FPUV2_FSTMM 0x16
#define FPUV2_FSTMS 0x14
#define FPUV2_FSTRD 0xd
#define FPUV2_FSTRM 0xe
#define FPUV2_FSTRS 0xc
#define FPUV2_FSTS 0x4

#define FPUV2_REG_MASK 0x1f
#define FPUV2_REG_SHI_RX 16

#define FPUV2_VREG_MASK 0xf
#define FPUV2_VREG_SHI_VRX 16
#define FPUV2_VREG_SHI_VRY 21
#define FPUV2_VREG_SHI_VRZ 0

#define CSKY_INSN_RX(x) ((x >> FPUV2_REG_SHI_RX) & FPUV2_REG_MASK)
#define CSKY_INSN_VRX(x) ((x >> FPUV2_VREG_SHI_VRX) & FPUV2_VREG_MASK)
#define CSKY_INSN_VRY(x) ((x >> FPUV2_VREG_SHI_VRY) & FPUV2_VREG_MASK)
#define CSKY_INSN_VRZ(x) ((x >> FPUV2_VREG_SHI_VRZ) & FPUV2_VREG_MASK)

#define FPUV2_LDST_MASK (1 << 13)
#define FPUV2_SOP_MASK (0xff)
#define FPUV2_SOP_SHIFT (0x5)
#define FPUV2_LDST_SOP_MASK (0x1f)
#define FPUV2_LDST_SOP_SHIFT (0x8)

#define FPUV2_LDST_INSN_INDEX(x)                                               \
	((x >> FPUV2_LDST_SOP_SHIFT) & FPUV2_LDST_SOP_MASK)
#define FPUV2_INSN_INDEX(x) ((x >> FPUV2_SOP_SHIFT) & FPUV2_SOP_MASK)

#define FPUV2_IMM2_MASK 0x3
#define FPUV2_IMM2 0x5
#define FPUV2_IMM4_MASK 0xf
#define FPUV2_IMM4H 0x11
#define FPUV2_IMM4L 0x4

#define FPUV2_IMM8L_MASK (FPUV2_IMM4_MASK)
#define FPUV2_IMM8H_MASK (FPUV2_IMM4_MASK << FPUV2_IMM4L)
#define FPUV2_IMM8L(x) ((x >> FPUV2_IMM4L) & FPUV2_IMM8L_MASK)
#define FPUV2_IMM8H(x) ((x >> FPUV2_IMM4H) & FPUV2_IMM8H_MASK)

#define FPUV2_LDST_R_IMM2(x) ((x >> FPUV2_IMM2) & FPUV2_IMM2_MASK)
#define FPUV2_LDST_IMM8(x) (FPUV2_IMM8H(x) | FPUV2_IMM8L(x))

#ifdef DEBUG_FP810
#define DEBUG_SP(name)                                                         \
	{                                                                      \
		unsigned int debug_ret;                                        \
		unsigned int debug_ret_l, debug_ret_h;                         \
		unsigned long long debug_x, debug_y;                           \
		debug_x = get_float64(x);                                      \
		debug_y = get_float64(y);                                      \
		__asm__ __volatile__(                                          \
			"fmtvrl vr0, %2\n "                                    \
			"fmtvrh vr0, %3\n "                                    \
			"fmtvrl vr1, %4\n"                                     \
			"fmtvrh vr1, %5\n " #name " vr2, vr0, vr1\n"           \
			"fmfvrl %0, vr2\n"                                     \
			"fmfvrh %1, vr2\n"                                     \
			: "+r"(debug_ret_l), "+r"(debug_ret_h)                 \
			: "r"((unsigned int)(debug_x & 0xffffffff)),           \
			  "r"((unsigned int)(debug_x >> 32)),                  \
			  "r"((unsigned int)(debug_y & 0xffffffff)),           \
			  "r"((unsigned int)(debug_y >> 32)));                 \
		debug_ret = debug_ret_l;                                       \
	}

#define DEBUG_DP(name)                                                         \
	{                                                                      \
		unsigned long long debug_ret;                                  \
		unsigned int debug_ret_l, debug_ret_h;                         \
		unsigned long long debug_x, debug_y;                           \
		debug_x = get_float64(x);                                      \
		debug_y = get_float64(y);                                      \
		__asm__ __volatile__(                                          \
			"fmtvrl vr0, %2\n "                                    \
			"fmtvrh vr0, %3\n "                                    \
			"fmtvrl vr1, %4\n"                                     \
			"fmtvrh vr1, %5\n " #name " vr2, vr0, vr1\n"           \
			"fmfvrl %0, vr2\n"                                     \
			"fmfvrh %1, vr2\n"                                     \
			: "+r"(debug_ret_l), "+r"(debug_ret_h)                 \
			: "r"((unsigned int)(debug_x & 0xffffffff)),           \
			  "r"((unsigned int)(debug_x >> 32)),                  \
			  "r"((unsigned int)(debug_y & 0xffffffff)),           \
			  "r"((unsigned int)(debug_y >> 32)));                 \
		debug_ret = debug_ret_l | (unsigned long long)(debug_ret_h)    \
						  << 32;                       \
	}

#define DEBUG_SP_U(name)                                                       \
	{                                                                      \
		unsigned int debug_ret;                                        \
		unsigned int debug_ret_l, debug_ret_h;                         \
		unsigned long long debug_x;                                    \
		debug_x = get_float64(x);                                      \
		__asm__ __volatile__(                                          \
			"fmtvrl vr0, %2\n "                                    \
			"fmtvrh vr0, %3\n " #name " vr2, vr0\n"                \
			"fmfvrl %0, vr2\n"                                     \
			"fmfvrh %1, vr2\n"                                     \
			: "+r"(debug_ret_l), "+r"(debug_ret_h)                 \
			: "r"((unsigned int)(debug_x & 0xffffffff)),           \
			  "r"((unsigned int)(debug_x >> 32)));                 \
		debug_ret = debug_ret_l;                                       \
	}

#define DEBUG_SP_CVT_U(name, rm)                                               \
	{                                                                      \
		unsigned int debug_ret;                                        \
		unsigned int debug_ret_l, debug_ret_h;                         \
		unsigned long long debug_x;                                    \
		debug_x = get_float64(x);                                      \
		__asm__ __volatile__(                                          \
			"fmtvrl vr0, %2\n "                                    \
			"fmtvrh vr0, %3\n " #name "." #rm " vr2, vr0\n"        \
			"fmfvrl %0, vr2\n"                                     \
			"fmfvrh %1, vr2\n"                                     \
			: "+r"(debug_ret_l), "+r"(debug_ret_h)                 \
			: "r"((unsigned int)(debug_x & 0xffffffff)),           \
			  "r"((unsigned int)(debug_x >> 32)));                 \
		debug_ret = debug_ret_l;                                       \
	}

#define DEBUG_DP_U(name)                                                       \
	{                                                                      \
		unsigned long long debug_ret;                                  \
		unsigned int debug_ret_l, debug_ret_h;                         \
		unsigned long long debug_x;                                    \
		debug_x = get_float64(x);                                      \
		__asm__ __volatile__(                                          \
			"fmtvrl vr0, %2\n "                                    \
			"fmtvrh vr0, %3\n " #name " vr2, vr0\n"                \
			"fmfvrl %0, vr2\n"                                     \
			"fmfvrh %1, vr2\n"                                     \
			: "+r"(debug_ret_l), "+r"(debug_ret_h)                 \
			: "r"((unsigned int)(debug_x & 0xffffffff)),           \
			  "r"((unsigned int)(debug_x >> 32)));                 \
		debug_ret = debug_ret_l | (unsigned long long)(debug_ret_h)    \
						  << 32;                       \
	}

#define DEBUG_SP_MAC(name)                                                     \
	{                                                                      \
		unsigned int debug_ret;                                        \
		unsigned int debug_ret_l, debug_ret_h;                         \
		unsigned long long debug_x, debug_y, debug_z;                  \
		debug_x = get_float64(x);                                      \
		debug_y = get_float64(y);                                      \
		debug_z = get_float64(z);                                      \
		debug_ret_l = (unsigned int)(debug_z & 0xffffffff);            \
		debug_ret_h = (unsigned int)(debug_z >> 32);                   \
		__asm__ __volatile__(                                          \
			"fmtvrl vr0, %2\n "                                    \
			"fmtvrh vr0, %3\n "                                    \
			"fmtvrl vr1, %4\n"                                     \
			"fmtvrh vr1, %5\n "                                    \
			"fmtvrl vr2, %0\n"                                     \
			"fmtvrh vr2, %1\n " #name " vr2, vr0, vr1\n"           \
			"fmfvrl %0, vr2\n"                                     \
			"fmfvrh %1, vr2\n"                                     \
			: "+r"(debug_ret_l), "+r"(debug_ret_h)                 \
			: "r"((unsigned int)(debug_x & 0xffffffff)),           \
			  "r"((unsigned int)(debug_x >> 32)),                  \
			  "r"((unsigned int)(debug_y & 0xffffffff)),           \
			  "r"((unsigned int)(debug_y >> 32)));                 \
		debug_ret = debug_ret_l;                                       \
	}

#define DEBUG_DP_MAC(name)                                                     \
	{                                                                      \
		unsigned long long debug_ret;                                  \
		unsigned int debug_ret_l, debug_ret_h;                         \
		unsigned long long debug_x, debug_y, debug_z;                  \
		debug_x = get_float64(x);                                      \
		debug_y = get_float64(y);                                      \
		debug_z = get_float64(z);                                      \
		debug_ret_l = (unsigned int)(debug_z & 0xffffffff);            \
		debug_ret_h = (unsigned int)(debug_z >> 32);                   \
		__asm__ __volatile__(                                          \
			"fmtvrl vr0, %2\n "                                    \
			"fmtvrh vr0, %3\n "                                    \
			"fmtvrl vr1, %4\n"                                     \
			"fmtvrh vr1, %5\n "                                    \
			"fmtvrl vr2, %0\n"                                     \
			"fmtvrh vr2, %1\n " #name " vr2, vr0, vr1\n"           \
			"fmfvrl %0, vr2\n"                                     \
			"fmfvrh %1, vr2\n"                                     \
			: "+r"(debug_ret_l), "+r"(debug_ret_h)                 \
			: "r"((unsigned int)(debug_x & 0xffffffff)),           \
			  "r"((unsigned int)(debug_x >> 32)),                  \
			  "r"((unsigned int)(debug_y & 0xffffffff)),           \
			  "r"((unsigned int)(debug_y >> 32)));                 \
		debug_ret = debug_ret_l | (unsigned long long)(debug_ret_h)    \
						  << 32;                       \
	}

#define DEBUG_SP2DP(name)                                                      \
	{                                                                      \
		unsigned long long debug_ret;                                  \
		unsigned int debug_ret_l, debug_ret_h;                         \
		unsigned long long debug_x;                                    \
		debug_x = get_float64(x);                                      \
		__asm__ __volatile__(                                          \
			"fmtvrl vr0, %2\n "                                    \
			"fmtvrh vr0, %3\n " #name " vr2, vr0\n"                \
			"fmfvrl %0, vr2\n"                                     \
			"fmfvrh %1, vr2\n"                                     \
			: "+r"(debug_ret_l), "+r"(debug_ret_h)                 \
			: "r"((unsigned int)(debug_x & 0xffffffff)),           \
			  "r"((unsigned int)(debug_x >> 32)));                 \
		debug_ret = debug_ret_l | (unsigned long long)(debug_ret_h)    \
						  << 32;                       \
	}

#define DEBUG_DP2SP(name, rm)                                                  \
	{                                                                      \
		unsigned int debug_ret;                                        \
		unsigned int debug_ret_l, debug_ret_h;                         \
		unsigned long long debug_x;                                    \
		debug_x = get_float64(x);                                      \
		__asm__ __volatile__(                                          \
			"fmtvrl vr0, %2\n "                                    \
			"fmtvrh vr0, %3\n " #name "." #rm " vr2, vr0\n"        \
			"fmfvrl %0, vr2\n"                                     \
			"fmfvrh %1, vr2\n"                                     \
			: "+r"(debug_ret_l), "+r"(debug_ret_h)                 \
			: "r"((unsigned int)(debug_x & 0xffffffff)),           \
			  "r"((unsigned int)(debug_x >> 32)));                 \
		debug_ret = debug_ret_l;                                       \
	}

#define DEBUG_CMP(name)                                                        \
	{                                                                      \
		unsigned int debug_ret;                                        \
		unsigned int debug_ret_l, debug_ret_h;                         \
		unsigned long long debug_x, debug_y;                           \
		debug_x = get_float64(x);                                      \
		debug_y = get_float64(y);                                      \
		__asm__ __volatile__(                                          \
			"fmtvrl vr0, %2\n "                                    \
			"fmtvrh vr0, %3\n "                                    \
			"fmtvrl vr1, %4\n"                                     \
			"fmtvrh vr1, %5\n " #name " vr0, vr1\n"                \
			"mfcr %0, cr<0, 0>\n"                                  \
			: "+r"(debug_ret_l), "+r"(debug_ret_h)                 \
			: "r"((unsigned int)(debug_x & 0xffffffff)),           \
			  "r"((unsigned int)(debug_x >> 32)),                  \
			  "r"((unsigned int)(debug_y & 0xffffffff)),           \
			  "r"((unsigned int)(debug_y >> 32)));                 \
		debug_ret = debug_ret_l & 1;                                   \
	}

#define DEBUG_CMP_U(name)                                                      \
	{                                                                      \
		unsigned int debug_ret;                                        \
		unsigned int debug_ret_l, debug_ret_h;                         \
		unsigned long long debug_x;                                    \
		debug_x = get_float64(x);                                      \
		__asm__ __volatile__(                                          \
			"fmtvrl vr0, %2\n "                                    \
			"fmtvrh vr0, %3\n " #name " vr0\n"                     \
			"mfcr %0, cr<0, 0>\n"                                  \
			: "+r"(debug_ret_l), "+r"(debug_ret_h)                 \
			: "r"((unsigned int)(debug_x & 0xffffffff)),           \
			  "r"((unsigned int)(debug_x >> 32)));                 \
		debug_ret = debug_ret_l & 1;                                   \
	}

#define DEBUG_SP_U_START(name) DEBUG_SP_U(name)

#define DEBUG_DP_U_START(name) DEBUG_DP_U(name)

#define DEBUG_SP_INT_START(name)                                               \
	{                                                                      \
		unsigned int debug_ret;                                        \
		unsigned long long debug_x;                                    \
		debug_x = get_float64(x);                                      \
		__asm__ __volatile__(                                          \
			"fmtvrl vr0, %1\n "                                    \
			"fmtvrh vr0, %2\n " #name " %0, vr0\n"                 \
			: "+r"(debug_ret)                                      \
			: "r"((unsigned int)(debug_x & 0xffffffff)),           \
			  "r"((unsigned int)(debug_x >> 32)));                 \
	}

#ifdef DEBUG_ADJUST
#define ADJUCT_FLAG                                                            \
	FP_CUR_EXCEPTIONS = 0;                                                 \
	current->thread.user_fp.fesr = mfcr("cr<2, 2>")

#define ADJUCT_RESULT(val) (val = debug_ret)

#else
#define ADJUCT_FLAG
#define ADJUCT_RESULT(val)
#endif

#define CHECK_EXCEPTION_FLAGS(name)                                            \
	unsigned int tmp2 = mfcr("cr<2, 2>");                                  \
	tmp2 = (tmp2 >> 8) & 0x3f;                                             \
	unsigned int tmp = ((current->thread.user_fp.fesr >> 8) & 0x3f) |      \
			   FP_CUR_EXCEPTIONS;                                  \
	if (tmp2 != tmp) {                                                     \
		pr_info("%s flags %x %x %x %x error\n", #name, tmp, tmp2,      \
			FP_CUR_EXCEPTIONS, current->thread.user_fp.fesr);      \
		ADJUCT_FLAG;                                                   \
	}

#define DEBUG_SP_END(name)                                                     \
	do {                                                                   \
		if ((unsigned int)debug_ret != (unsigned int)z_val) {          \
			pr_info("%s %x %x %x error\n", #name, z_val,           \
				debug_ret, read_fpcr());                       \
			ADJUCT_RESULT(z_val);                                  \
		}                                                              \
		CHECK_EXCEPTION_FLAGS(name);                                   \
	} while (0)

#define DEBUG_DP_END(name)                                                     \
	do {                                                                   \
		if ((unsigned long long)debug_ret !=                           \
		    (unsigned long long)z_val) {                               \
			pr_info("%s %llx %llx %x error\n", #name, z_val,       \
				debug_ret, read_fpcr());                       \
			ADJUCT_RESULT(z_val);                                  \
		}                                                              \
		CHECK_EXCEPTION_FLAGS(name);                                   \
	} while (0)

#define DEBUG_SP_U_END(name)                                                   \
	do {                                                                   \
		FP_DECL_EX;                                                    \
		if ((unsigned int)debug_ret != (unsigned int)result) {         \
			pr_info("%s %x %x %x error\n", #name, result,          \
				debug_ret, read_fpcr());                       \
			ADJUCT_RESULT(result);                                 \
		}                                                              \
		CHECK_EXCEPTION_FLAGS(name);                                   \
	} while (0)

#define DEBUG_DP_U_END(name, result)                                           \
	do {                                                                   \
		FP_DECL_EX;                                                    \
		if ((unsigned long long)debug_ret !=                           \
		    (unsigned long long)result) {                              \
			pr_info("%s %llx %llx %x error\n", #name, result,      \
				debug_ret, read_fpcr());                       \
			ADJUCT_RESULT(result);                                 \
		}                                                              \
		CHECK_EXCEPTION_FLAGS(name);                                   \
	} while (0)

#define DEBUG_SP_INT_END(name)                                                 \
	do {                                                                   \
		FP_DECL_EX;                                                    \
		if ((unsigned int)debug_ret != (unsigned int)result) {         \
			pr_info("%s %x %x %x error\n", #name, result,          \
				debug_ret, read_fpcr());                       \
			ADJUCT_RESULT(result);                                 \
		}                                                              \
		CHECK_EXCEPTION_FLAGS(name);                                   \
	} while (0)

#define DEBUG_FLAG_END(name)                                                   \
	do {                                                                   \
		if (debug_ret != result) {                                     \
			pr_info("%s %x %x error\n", #name, result, debug_ret); \
			ADJUCT_RESULT(result);                                 \
		}                                                              \
		CHECK_EXCEPTION_FLAGS(name);                                   \
	} while (0)

#else
#define DEBUG_SP(name)
#define DEBUG_DP(name)
#define DEBUG_SP_U(name)
#define DEBUG_SP_CVT_U(name, rm)
#define DEBUG_DP_U(name)
#define DEBUG_SP_MAC(name)
#define DEBUG_DP_MAC(name)
#define DEBUG_SP2DP(name)
#define DEBUG_DP2SP(name, rm)
#define DEBUG_SP_END(name)
#define DEBUG_DP_END(name)
#define DEBUG_SP_U_START(name)
#define DEBUG_DP_U_START(name)
#define DEBUG_SP_INT_START(name)
#define DEBUG_SP_U_END(name)
#define DEBUG_DP_U_END(name, result)
#define DEBUG_SP_INT_END(name)
#define DEBUG_FLAG_END(name)
#define DEBUG_CMP(name)
#define DEBUG_CMP_U(name)
#endif

#define FPU_OP_NAME(name) fp810_##name

struct instruction_op_array {
	void (*const fn)(int vrx, int vry, int vrz,
			 struct inst_data *inst_data);
};

extern struct instruction_op_array inst_op1[];
extern struct instruction_op_array inst_op2[];

#define FPUV2_OP_FUNC(insn)                                                    \
	FPU_OP_NAME(insn)(int x, int y, int z, struct inst_data *inst_data)

#endif
