// SPDX-License-Identifier: GPL-2.0
/*
 * CSKY  860  MATHEMU
 *
 * Copyright (C) 2021 Hangzhou C-SKY Microsystems co.,ltd.
 *
 *    Authors: Li Weiwei <liweiwei@iscas.ac.cn>
 *             Wang Junqiang <wangjunqiang@iscas.ac.cn>
 *
 */
#include <linux/uaccess.h>
#include "sfp-util.h"
#include <math-emu/soft-fp.h>
#include "sfp-fixs.h"
#include <math-emu/single.h>
#include <math-emu/double.h>
#include "fp810.h"

/*
 * z = |x|
 */
void
FPUV2_OP_FUNC(fabsd)
{
	union float64_components u;

	DEBUG_DP_U_START(fabsd);
	u.f64 = get_float64(x);
#ifdef __CSKYBE__
	u.i[0] &= 0x7fffffff;
#else
	u.i[1] &= 0x7fffffff;
#endif
	set_float64(u.f64, z);
	DEBUG_DP_U_END(fabsd, u.f64);
}

void
FPUV2_OP_FUNC(fabsm)
{
	union float64_components u;

	DEBUG_DP_U_START(fabsm);
	u.f64 = get_float64(x);
	u.i[0] &= 0x7fffffff;
	u.i[1] &= 0x7fffffff;
	set_float64(u.f64, z);
	DEBUG_DP_U_END(fabsm, u.f64);
}

void
FPUV2_OP_FUNC(fabss)
{
	unsigned int result;

	DEBUG_SP_U_START(fabss);
	result = get_float32(x) & 0x7fffffff;
	set_float32(result, z);
	DEBUG_SP_U_END(fabss);
}

/*
 * z = x + y
 */
void
FPUV2_OP_FUNC(faddd)
{
	FPU_INSN_START(DR, DR, DI);
	DEBUG_DP(faddd);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);

	FP_ADD_D(R, A, B);

	FP_PACK_DP(vrz, R);

	DEBUG_DP_END(faddd);
	FPU_INSN_DP_END;
}

void
FPUV2_OP_FUNC(faddm)
{
	FPU_INSN_START(DR, DR, DI);
	DEBUG_DP(faddm);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);

	FP_ADD_S(R, A, B);
	FP_PACK_SP(vrz, R);

	FP_UNPACK_SP(A, vrx + 4);
	FP_UNPACK_SP(B, vry + 4);

	FP_ADD_S(R, A, B);

	FP_PACK_SP(vrz + 4, R);

	DEBUG_DP_END(faddm);
	FPU_INSN_DP_END;
}

void
FPUV2_OP_FUNC(fadds)
{
	FPU_INSN_START(SR, SR, SI);
	DEBUG_SP(fadds);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);

	FP_ADD_S(R, A, B);

	FP_PACK_SP(vrz, R);

	DEBUG_SP_END(fadds);
	FPU_INSN_SP_END;
}

/*
 * fpsr.c = (x >= y) ? 1 : 0
 */
void
FPUV2_OP_FUNC(fcmphsd)
{
	int result;

	FPU_INSN_START(DR, DR, DN);
	DEBUG_CMP(fcmphsd);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);

	FP_CMP_D(result, A, B, 3);
	if  ((result == 3) && ((A_c == FP_CLS_NAN) || (B_c == FP_CLS_NAN))) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
	    result = 0;
	} else
		result = ((result == 0) || (result == 1)) ? 1 : 0;

	DEBUG_FLAG_END(fcmphsd);
	SET_FLAG_END;
}

/*
 * fpsr.c = (x >= y) ? 1 : 0
 */
void
FPUV2_OP_FUNC(fcmphss)
{
	int result;

	FPU_INSN_START(SR, SR, SN);
	DEBUG_CMP(fcmphss);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);

	FP_CMP_S(result, A, B, 3);
	if  ((result == 3) && ((A_c == FP_CLS_NAN) || (B_c == FP_CLS_NAN))) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
	    result = 0;
	} else
		result = ((result == 0) || (result == 1)) ? 1 : 0;

	DEBUG_FLAG_END(fcmphss);
	SET_FLAG_END;
}

/*
 * fpsr.c = (x < y) ? 1 : 0
 */
void
FPUV2_OP_FUNC(fcmpltd)
{
	int result;

	FPU_INSN_START(DR, DR, DN);
	DEBUG_CMP(fcmpltd);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);

	FP_CMP_D(result, A, B, 3);
	if  ((result == 3) && (((A_c == FP_CLS_NAN) || (B_c == FP_CLS_NAN)))) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
	    result = 0;
	} else
		result = (result == -1) ? 1 : 0;

	DEBUG_FLAG_END(fcmpltd);
	SET_FLAG_END;
}

/*
 * fpsr.c = (x < y) ? 1 : 0
 */
void
FPUV2_OP_FUNC(fcmplts)
{
	int result;

	FPU_INSN_START(SR, SR, SN);
	DEBUG_CMP(fcmplts);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);

	FP_CMP_S(result, A, B, 3);
	if  ((result == 3) && ((A_c == FP_CLS_NAN) || (B_c == FP_CLS_NAN))) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
	    result = 0;
	} else
		result = (result == -1) ? 1 : 0;

	DEBUG_FLAG_END(fcmplts);
	SET_FLAG_END;
}

/*
 * fpsr.c = (x == y) ? 0 : 1
 */
void
FPUV2_OP_FUNC(fcmpned)
{
	int result;

	FPU_INSN_START(DR, DR, DN);
	DEBUG_CMP(fcmpned);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);

	FP_CMP_D(result, A, B, 3);
	if  ((result == 3) && (FP_ISSIGNAN_D(A) || FP_ISSIGNAN_D(B))) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
	    result = 1;
	} else
		result = (result != 0) ? 1 : 0;

	DEBUG_FLAG_END(fcmpned);
	SET_FLAG_END;
}

/*
 * fpsr.c = (x == y) ? 0 : 1
 */
void
FPUV2_OP_FUNC(fcmpnes)
{
	int result;

	FPU_INSN_START(SR, SR, SN);
	DEBUG_CMP(fcmpnes);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);

	FP_CMP_S(result, A, B, 3);
	if ((result == 3) && (FP_ISSIGNAN_S(A) || FP_ISSIGNAN_S(B))) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
	    result = 1;
	} else
		result = (result != 0) ? 1 : 0;

	DEBUG_FLAG_END(fcmpnes);
	SET_FLAG_END;
}

/*
 * fpsr.c = (x == NaN || y == NaN) ? 1 : 0
 */
void
FPUV2_OP_FUNC(fcmpuod)
{
	int result;

	FPU_INSN_START(DR, DR, DN);
	DEBUG_CMP(fcmpuod);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);

	result = (A_c == FP_CLS_NAN) || (B_c == FP_CLS_NAN) ? 1 : 0;

	DEBUG_FLAG_END(fcmpuod);
	SET_FLAG_END;
}

/*
 * fpsr.c = (x == NaN || y == NaN) ? 1 : 0
 */
void
FPUV2_OP_FUNC(fcmpuos)
{
	int result;

	FPU_INSN_START(SR, SR, SN);
	DEBUG_CMP(fcmpuos);

	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);

	result = (A_c == FP_CLS_NAN) || (B_c == FP_CLS_NAN) ? 1 : 0;

	DEBUG_FLAG_END(fcmpuos);
	SET_FLAG_END;
}

/*
 * fpsr.c = (x >= 0) ? 1 : 0
 */
void
FPUV2_OP_FUNC(fcmpzhsd)
{
	int result;
	void *constant;

	FPU_INSN_START(DR, DN, DN);
	DP_CONST_DATA(constant, 0);
	DEBUG_CMP_U(fcmpzhsd);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, constant);

	FP_CMP_D(result, A, B, 3);
	if ((result == 3) && (A_c == FP_CLS_NAN)) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
	    result = 0;
	} else
		result = ((result == 0) || (result == 1)) ? 1 : 0;

	DEBUG_FLAG_END(fcmpzhsd);
	SET_FLAG_END;
}

/*
 * fpsr.c = (x >= 0) ? 1 : 0
 */
void
FPUV2_OP_FUNC(fcmpzhss)
{
	int result;
	void *constant;

	FPU_INSN_START(SR, SN, SN);
	SP_CONST_DATA(constant, 0);
	DEBUG_CMP_U(fcmpzhss);

	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, constant);

	FP_CMP_S(result, A, B, 3);
	if  ((result == 3) && (A_c == FP_CLS_NAN)) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
	    result = 0;
	} else
		result = ((result == 0) || (result == 1)) ? 1 : 0;

	DEBUG_FLAG_END(fcmpzhss);
	SET_FLAG_END;
}

/*
 * fpsr.c = (x <= 0) ? 1 : 0
 */
void
FPUV2_OP_FUNC(fcmpzlsd)
{
	int result;
	void *constant;

	FPU_INSN_START(DR, DN, DN);
	DP_CONST_DATA(constant, 0);
	DEBUG_CMP_U(fcmpzlsd);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, constant);

	FP_CMP_D(result, A, B, 3);
	if  ((result == 3) && (FP_ISSIGNAN_D(A))) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
	    result = 0;
	} else
		result = ((result == 0) || (result == -1)) ? 1 : 0;

	DEBUG_FLAG_END(fcmpzlsd);
	SET_FLAG_END;
}

/*
 * fpsr.c = (x <= 0) ? 1 : 0
 */
void
FPUV2_OP_FUNC(fcmpzlss)
{
	int result;
	void *constant;

	FPU_INSN_START(SR, SN, SN);
	SP_CONST_DATA(constant, 0);
	DEBUG_CMP_U(fcmpzlss);

	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, constant);

	FP_CMP_S(result, A, B, 3);
	if  ((result == 3) && (A_c == FP_CLS_NAN)) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
	    result = 0;
	} else
		result = ((result == 0) || (result == -1)) ? 1 : 0;

	DEBUG_FLAG_END(fcmpzlss);
	SET_FLAG_END;
}

/*
 * fpsr.c = (x != 0) ? 1 : 0
 */
void
FPUV2_OP_FUNC(fcmpzned)
{
	int result;
	void *constant;

	FPU_INSN_START(DR, DN, DN);
	DP_CONST_DATA(constant, 0);
	DEBUG_CMP_U(fcmpzned);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, constant);

	FP_CMP_D(result, A, B, 3);
	if  ((result == 3) && (FP_ISSIGNAN_D(A))) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
	    result = 1;
	} else
		result = (result != 0) ? 1 : 0;

	DEBUG_FLAG_END(fcmpzned);
	SET_FLAG_END;
}

/*
 * fpsr.c = (x != 0) ? 1 : 0
 */
void
FPUV2_OP_FUNC(fcmpznes)
{
	int result;
	void *constant;

	FPU_INSN_START(SR, SN, SN);
	SP_CONST_DATA(constant, 0);
	DEBUG_CMP_U(fcmpznes);

	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, constant);

	FP_CMP_S(result, A, B, 3);
	if  ((result == 3) && (FP_ISSIGNAN_S(A))) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
	    result = 1;
	} else
		result = (result != 0) ? 1 : 0;

	DEBUG_FLAG_END(fcmpznes);
	SET_FLAG_END;
}

/*
 * fpsr.c = (x == NaN) ? 1 : 0
 */
void
FPUV2_OP_FUNC(fcmpzuod)
{
	int result;

	FPU_INSN_START(DR, DN, DN);
	DEBUG_CMP_U(fcmpzuod);
	FP_DECL_D(A);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);

	result = (A_c == FP_CLS_NAN) ? 1 : 0;

	DEBUG_FLAG_END(fcmpzuod);
	SET_FLAG_END;
}

/*
 * fpsr.c = (x == NaN) ? 1 : 0
 */
void
FPUV2_OP_FUNC(fcmpzuos)
{
	int result;

	FPU_INSN_START(SR, SN, SN);
	DEBUG_CMP_U(fcmpzuos);

	FP_DECL_S(A);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);

	result = (A_c == FP_CLS_NAN) ? 1 : 0;

	DEBUG_FLAG_END(fcmpzuos);
	SET_FLAG_END;
}

/*
 * z = x / y
 */
void
FPUV2_OP_FUNC(fdivd)
{
	FPU_INSN_START(DR, DR, DI);
	DEBUG_DP(fdivd);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);

	FP_DIV_D(R, A, B);

	FP_PACK_DP(vrz, R);

	DEBUG_DP_END(fdivd);
	FPU_INSN_DP_END;
}

void
FPUV2_OP_FUNC(fdivs)
{
	FPU_INSN_START(SR, SR, SI);
	DEBUG_SP(fdivs);

	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);

	FP_DIV_S(R, A, B);

	FP_PACK_SP(vrz, R);

	DEBUG_SP_END(fdivs);
	FPU_INSN_SP_END;
}

/*
 * z = (float)x
 */
void
FPUV2_OP_FUNC(fdtos)
{
	FPU_INSN_START(DR, DN, SI);
	DEBUG_SP_U(fdtos);
	FP_DECL_D(A);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);

	FP_CONV(S, D, 1, 2, R, A);

	FP_PACK_SP(vrz, R);

	DEBUG_SP_END(fdtos);
	FPU_INSN_SP_END;
}

/*
 * z = (int)x
 */
void
FPUV2_OP_FUNC(fdtosi_rn)
{
	int r;

	FPU_INSN_START(DR, DN, SI);
	DEBUG_DP2SP(fdtosi, rn);
	FP_DECL_D(A);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_NEAREST);
	if  (A_c == FP_CLS_INF) {
		*(unsigned int *)vrz = (A_s == 0) ? 0x7fffffff : 0x80000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if  (A_c == FP_CLS_NAN) {
		*(unsigned int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_ROUND_D(r, A, 32, 1);
		*(unsigned int *)vrz = r;
	}
	RESTORE_ROUND_MODE;

	DEBUG_SP_END(fdtosi_rn);
	FPU_INSN_SP_END;
}

void
FPUV2_OP_FUNC(fdtosi_rz)
{
	int r;

	FPU_INSN_START(DR, DN, SI);
	DEBUG_DP2SP(fdtosi, rz);
	FP_DECL_D(A);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_ZERO);
	if (A_c == FP_CLS_INF) {
		*(unsigned int *)vrz = (A_s == 0) ? 0x7fffffff : 0x80000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if  (A_c == FP_CLS_NAN) {
		*(unsigned int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_ROUND_D(r, A, 32, 1);
		*(unsigned int *)vrz = r;
	}
	RESTORE_ROUND_MODE;

	DEBUG_SP_END(fdtosi_rz);
	FPU_INSN_SP_END;
}

void
FPUV2_OP_FUNC(fdtosi_rpi)
{
	int r;

	FPU_INSN_START(DR, DN, SI);
	DEBUG_DP2SP(fdtosi, rpi);
	FP_DECL_D(A);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_PINF);
	if (A_c == FP_CLS_INF) {
		*(unsigned int *)vrz = (A_s == 0) ? 0x7fffffff : 0x80000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(unsigned int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_ROUND_D(r, A, 32, 1);
		*(unsigned int *)vrz = r;
	}
	RESTORE_ROUND_MODE;
	DEBUG_SP_END(fdtosi_rpi);
	FPU_INSN_SP_END;
}

void
FPUV2_OP_FUNC(fdtosi_rni)
{
	int r;

	FPU_INSN_START(DR, DN, SI);
	DEBUG_DP2SP(fdtosi, rni);
	FP_DECL_D(A);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_MINF);
	if (A_c == FP_CLS_INF) {
		*(unsigned int *)vrz = (A_s == 0) ? 0x7fffffff : 0x80000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(unsigned int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_ROUND_D(r, A, 32, 1);
		*(unsigned int *)vrz = r;
	}
	RESTORE_ROUND_MODE;
	DEBUG_SP_END(fdtosi_rni);
	FPU_INSN_SP_END;
}

/*
 * z = (unsigned int)x
 */
void
FPUV2_OP_FUNC(fdtoui_rn)
{
	unsigned int r;

	FPU_INSN_START(DR, DN, SI);
	DEBUG_DP2SP(fdtoui, rn);
	FP_DECL_D(A);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_NEAREST);
	if (A_c == FP_CLS_INF) {
		*(unsigned int *)vrz = (A_s == 0) ? 0xffffffff : 0x00000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(unsigned int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_ROUND_D(r, A, 32, 0);
		*(unsigned int *)vrz = r;
	}
	RESTORE_ROUND_MODE;
	DEBUG_SP_END(fdtoui_rn);
	FPU_INSN_SP_END;
}

void
FPUV2_OP_FUNC(fdtoui_rz)
{
	unsigned int r;

	FPU_INSN_START(DR, DN, SI);
	DEBUG_DP2SP(fdtoui, rz);
	FP_DECL_D(A);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_ZERO);
	if (A_c == FP_CLS_INF) {
		*(unsigned int *)vrz = (A_s == 0) ? 0xffffffff : 0x00000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(unsigned int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_ROUND_D(r, A, 32, 0);
		*(unsigned int *)vrz = r;
	}
	RESTORE_ROUND_MODE;
	DEBUG_SP_END(fdtoui_rz);
	FPU_INSN_SP_END;
}

void
FPUV2_OP_FUNC(fdtoui_rpi)
{
	unsigned int r;

	FPU_INSN_START(DR, DN, SI);
	DEBUG_DP2SP(fdtoui, rpi);
	FP_DECL_D(A);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_PINF);
	if (A_c == FP_CLS_INF) {
		*(unsigned int *)vrz = (A_s == 0) ? 0xffffffff : 0x00000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(unsigned int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_ROUND_D(r, A, 32, 0);
		*(unsigned int *)vrz = r;
	}
	RESTORE_ROUND_MODE;
	DEBUG_SP_END(fdtoui_rpi);
	FPU_INSN_SP_END;
}

void
FPUV2_OP_FUNC(fdtoui_rni)
{
	unsigned int r;

	FPU_INSN_START(DR, DN, SI);
	DEBUG_DP2SP(fdtoui, rni);
	FP_DECL_D(A);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_MINF);
	if (A_c == FP_CLS_INF) {
		*(unsigned int *)vrz = (A_s == 0) ? 0xffffffff : 0x00000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(unsigned int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_ROUND_D(r, A, 32, 0);
		*(unsigned int *)vrz = r;
	}
	RESTORE_ROUND_MODE;
	DEBUG_SP_END(fdtoui_rni);
	FPU_INSN_SP_END;
}

/*
 * z += x * y
 */
void
FPUV2_OP_FUNC(fmacd)
{
	FPU_INSN_START(DR, DR, DR);
	DEBUG_DP_MAC(fmacd);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_D(C);
	FP_DECL_D(T);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);
	FP_UNPACK_DP(C, vrz);

	FP_MUL_D(T, A, B);
	MAC_INTERNAL_ROUND_DP;
	FP_ADD_D(R, T, C);

	FP_PACK_DP(vrz, R);

	DEBUG_DP_END(fmacd);
	FPU_INSN_DP_END;
}

void
FPUV2_OP_FUNC(fmacm)
{
	FPU_INSN_START(DR, DR, DR);
	DEBUG_DP_MAC(fmacm);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(C);
	FP_DECL_S(T);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);
	FP_UNPACK_SP(C, vrz);

	FP_MUL_S(T, A, B);
	MAC_INTERNAL_ROUND_SP;
	FP_ADD_S(R, T, C);

	FP_PACK_SP(vrz, R);

	FP_UNPACK_SP(A, vrx + 4);
	FP_UNPACK_SP(B, vry + 4);
	FP_UNPACK_SP(C, vrz + 4);

	FP_MUL_S(T, A, B);
	MAC_INTERNAL_ROUND_SP;
	FP_ADD_S(R, T, C);

	FP_PACK_SP(vrz + 4, R);
	DEBUG_DP_END(fmacm);
	FPU_INSN_DP_END;
}

void
FPUV2_OP_FUNC(fmacs)
{
	FPU_INSN_START(SR, SR, SR);
	DEBUG_SP_MAC(fmacs);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(C);
	FP_DECL_S(T);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);
	FP_UNPACK_SP(C, vrz);

	FP_MUL_S(T, A, B);
	MAC_INTERNAL_ROUND_SP;
	FP_ADD_S(R, T, C);

	FP_PACK_SP(vrz, R);

	DEBUG_SP_END(fmacs);
	FPU_INSN_SP_END;
}

/*
 * z = x[63:32]
 */
void
FPUV2_OP_FUNC(fmfvrh)
{
	union float64_components op_val1;
	unsigned int result;

	z = inst_data->inst & 0x1f;
	DEBUG_SP_INT_START(fmfvrh);
	x = CSKY_INSN_RX(inst_data->inst);
	op_val1.f64 = get_float64(x);

#ifdef __CSKYBE__
	result = (unsigned int)op_val1.i[0];
#else
	result = (unsigned int)op_val1.i[1];
#endif

	set_uint32(result, z, inst_data);
	DEBUG_SP_INT_END(fmfvrh);
}

/*
 * z = x[31:0]
 */
void
FPUV2_OP_FUNC(fmfvrl)
{
	union float64_components op_val1;
	unsigned int result;

	z = inst_data->inst & 0x1f;
	DEBUG_SP_INT_START(fmfvrl);
	x = CSKY_INSN_RX(inst_data->inst);
	op_val1.f64 = get_float64(x);

#ifdef __CSKYBE__
	result = (unsigned int)op_val1.i[1];
#else
	result = (unsigned int)op_val1.i[0];
#endif

	set_uint32(result, z, inst_data);
	DEBUG_SP_INT_END(fmfvrl);
}

/*
 * z = x
 */
void
FPUV2_OP_FUNC(fmovd)
{
	unsigned long long result;

	DEBUG_DP_U_START(fmovd);
	result = get_float64(x);

	set_float64(result, z);
	DEBUG_DP_U_END(fmovd, result);
}

void
FPUV2_OP_FUNC(fmovm)
{
	unsigned long long result;

	DEBUG_DP_U_START(fmovm);
	result = get_float64(x);

	set_float64(result, z);
	DEBUG_DP_U_END(fmovm, result);
}

void
FPUV2_OP_FUNC(fmovs)
{
	unsigned int result;

	DEBUG_SP_U_START(fmovs);
	result = get_float32(x);

	set_float32(result, z);
	DEBUG_SP_U_END(fmovs);
}

/*
 * z = x * y - z
 */
void
FPUV2_OP_FUNC(fmscd)
{
	FPU_INSN_START(DR, DR, DR);
	DEBUG_DP_MAC(fmscd);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_D(C);
	FP_DECL_D(T);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);
	FP_UNPACK_DP(C, vrz);

	FP_MUL_D(T, A, B);
	MAC_INTERNAL_ROUND_DP;
	FP_SUB_D(R, T, C);

	FP_PACK_DP(vrz, R);

	DEBUG_DP_END(fmscd);
	FPU_INSN_DP_END;
}

void
FPUV2_OP_FUNC(fmscm)
{
	FPU_INSN_START(DR, DR, DI);
	DEBUG_DP(fmscm);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(C);
	FP_DECL_S(T);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);
	FP_UNPACK_SP(C, vrz);

	FP_MUL_S(T, A, B);
	MAC_INTERNAL_ROUND_SP;
	FP_SUB_S(R, T, C);

	FP_PACK_SP(vrz, R);

	FP_UNPACK_SP(A, vrx + 4);
	FP_UNPACK_SP(B, vry + 4);
	FP_UNPACK_SP(C, vrz + 4);

	FP_MUL_S(T, A, B);
	MAC_INTERNAL_ROUND_SP;
	FP_SUB_S(R, T, C);

	FP_PACK_SP(vrz + 4, R);

	DEBUG_DP_END(fmscm);
	FPU_INSN_DP_END;
}

void
FPUV2_OP_FUNC(fmscs)
{
	FPU_INSN_START(SR, SR, SR);
	DEBUG_SP_MAC(fmscs);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(C);
	FP_DECL_S(T);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);
	FP_UNPACK_SP(C, vrz);

	FP_MUL_S(T, A, B);
	MAC_INTERNAL_ROUND_SP;
	FP_SUB_S(R, T, C);

	FP_PACK_SP(vrz, R);

	DEBUG_SP_END(fmscs);
	FPU_INSN_SP_END;
}

/*
 * z[63:32] = x
 */
void
FPUV2_OP_FUNC(fmtvrh)
{
	union float64_components result;

	x = CSKY_INSN_RX(inst_data->inst);
#ifdef __CSKYBE__
	result.i[0] = (unsigned int)get_uint32(x, inst_data);
	set_float32h(result.i[0], z);
#else
	result.i[1] = (unsigned int)get_uint32(x, inst_data);
	set_float32h(result.i[1], z);
#endif
}

/*
 * z[31:0] = x
 */
void
FPUV2_OP_FUNC(fmtvrl)
{
	union float64_components result;

	x = CSKY_INSN_RX(inst_data->inst);
#ifdef __CSKYBE__
	result.i[1] = (unsigned int)get_uint32(x, inst_data);
	set_float32(result.i[1], z);
#else
	result.i[0] = (unsigned int)get_uint32(x, inst_data);
	set_float32(result.i[0], z);
#endif
}

/*
 * z = x * y
 */
void
FPUV2_OP_FUNC(fmuld)
{
	FPU_INSN_START(DR, DR, DI);
	DEBUG_DP(fmuld);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);

	FP_MUL_D(R, A, B);

	FP_PACK_DP(vrz, R);

	DEBUG_DP_END(fmuld);
	FPU_INSN_DP_END;
}

void
FPUV2_OP_FUNC(fmulm)
{
	FPU_INSN_START(DR, DR, DI);
	DEBUG_DP(fmulm);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);

	FP_MUL_S(R, A, B);

	FP_PACK_SP(vrz, R);

	FP_UNPACK_SP(A, vrx + 4);
	FP_UNPACK_SP(B, vry + 4);

	FP_MUL_S(R, A, B);

	FP_PACK_SP(vrz + 4, R);

	DEBUG_DP_END(fmulm);
	FPU_INSN_DP_END;
}

void
FPUV2_OP_FUNC(fmuls)
{

	FPU_INSN_START(SR, SR, SI);
	DEBUG_SP(fmuls);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);

	FP_MUL_S(R, A, B);

	FP_PACK_SP(vrz, R);

	DEBUG_SP_END(fmuls);
	FPU_INSN_SP_END;
}

/*
 * z = -x
 */
void
FPUV2_OP_FUNC(fnegd)
{
	union float64_components u;

	DEBUG_DP_U_START(fnegd);
	u.f64 = get_float64(x);
#ifdef __CSKYBE__
	u.i[0] ^= 0x80000000;
#else
	u.i[1] ^= 0x80000000;
#endif
	set_float64(u.f64, z);
	DEBUG_DP_U_END(fnegd, u.f64);
}

void
FPUV2_OP_FUNC(fnegm)
{
	union float64_components u;

	DEBUG_DP_U_START(fnegm);
	u.f64 = get_float64(x);
	u.i[0] ^= 0x80000000;
	u.i[1] ^= 0x80000000;
	set_float64(u.f64, z);
	DEBUG_DP_U_END(fnegm, u.f64);
}

void
FPUV2_OP_FUNC(fnegs)
{
	unsigned int result;

	DEBUG_SP_U_START(fnegs);
	result = get_float32(x) ^ 0x80000000;
	set_float32(result, z);
	DEBUG_SP_U_END(fnegs);
}

/*
 * z -= x * y
 */
void
FPUV2_OP_FUNC(fnmacd)
{
	FPU_INSN_START(DR, DR, DR);
	DEBUG_DP_MAC(fnmacd);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_D(C);
	FP_DECL_D(T);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);
	FP_UNPACK_DP(C, vrz);

	FP_MUL_D(T, A, B);
	MAC_INTERNAL_ROUND_DP;
	FP_SUB_D(R, C, T);

	FP_PACK_DP(vrz, R);

	DEBUG_DP_END(fnmacd);
	FPU_INSN_DP_END;
}

void
FPUV2_OP_FUNC(fnmacm)
{
	FPU_INSN_START(DR, DR, DR);
	DEBUG_DP_MAC(fnmacm);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(C);
	FP_DECL_S(T);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);
	FP_UNPACK_SP(C, vrz);

	FP_MUL_S(T, A, B);
	MAC_INTERNAL_ROUND_SP;
	FP_SUB_S(R, C, T);

	FP_PACK_SP(vrz, R);

	FP_UNPACK_SP(A, vrx + 4);
	FP_UNPACK_SP(B, vry + 4);
	FP_UNPACK_SP(C, vrz + 4);

	FP_MUL_S(T, A, B);

	FP_SUB_S(R, C, T);

	FP_PACK_SP(vrz + 4, R);

	DEBUG_DP_END(fnmacm);
	FPU_INSN_DP_END;
}

void
FPUV2_OP_FUNC(fnmacs)
{
	FPU_INSN_START(SR, SR, SR);
	DEBUG_SP_MAC(fnmacs);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(C);
	FP_DECL_S(T);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);
	FP_UNPACK_SP(C, vrz);

	FP_MUL_S(T, A, B);
	MAC_INTERNAL_ROUND_SP;
	FP_SUB_S(R, C, T);

	FP_PACK_SP(vrz, R);

	DEBUG_SP_END(fnmacs);
	FPU_INSN_SP_END;
}

/*
 * z = -z -x * y
 */
void
FPUV2_OP_FUNC(fnmscd)
{
	FPU_INSN_START(DR, DR, DR);
	DEBUG_DP_MAC(fnmscd);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_D(C);
	FP_DECL_D(T);
	FP_DECL_D(N);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);
	FP_UNPACK_DP(C, vrz);

	FP_MUL_D(T, A, B);
	MAC_INTERNAL_ROUND_DP;
	FP_NEG_D(N, C);
	FP_SUB_D(R, N, T);

	FP_PACK_DP(vrz, R);

	DEBUG_DP_END(fnmscd);
	FPU_INSN_DP_END;
}

void
FPUV2_OP_FUNC(fnmscm)
{
	FPU_INSN_START(DR, DR, DR);
	DEBUG_DP_MAC(fnmscm);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(C);
	FP_DECL_S(T);
	FP_DECL_S(N);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);
	FP_UNPACK_SP(C, vrz);

	FP_MUL_S(T, A, B);
	MAC_INTERNAL_ROUND_SP;
	FP_NEG_S(N, C);
	FP_SUB_S(R, N, T);

	FP_PACK_SP(vrz, R);

	FP_UNPACK_SP(A, vrx + 4);
	FP_UNPACK_SP(B, vry + 4);
	FP_UNPACK_SP(C, vrz + 4);

	FP_MUL_S(T, A, B);
	MAC_INTERNAL_ROUND_SP;
	FP_NEG_S(N, C);
	FP_SUB_S(R, N, T);

	FP_PACK_SP(vrz + 4, R);

	DEBUG_DP_END(fnmscm);
	FPU_INSN_DP_END;
}

void
FPUV2_OP_FUNC(fnmscs)
{
	FPU_INSN_START(SR, SR, SR);
	DEBUG_SP_MAC(fnmscs);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(C);
	FP_DECL_S(T);
	FP_DECL_S(N);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);
	FP_UNPACK_SP(C, vrz);

	FP_MUL_S(T, A, B);
	MAC_INTERNAL_ROUND_SP;
	FP_NEG_S(N, C);
	FP_SUB_S(R, N, T);

	FP_PACK_SP(vrz, R);

	DEBUG_SP_END(fnmscs);
	FPU_INSN_SP_END;
}

/*
 * z = -x * y
 */
void
FPUV2_OP_FUNC(fnmuld)
{
	FPU_INSN_START(DR, DR, DI);
	DEBUG_DP(fnmuld);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_D(T);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);

	FP_MUL_D(T, A, B);
	FP_NEG_D(R, T);

	FP_PACK_DP(vrz, R);

	DEBUG_DP_END(fnmuld);
	FPU_INSN_DP_END;
}

void
FPUV2_OP_FUNC(fnmulm)
{
	FPU_INSN_START(DR, DR, DI);
	DEBUG_DP(fnmulm);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(T);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);

	FP_MUL_S(T, A, B);
	FP_NEG_S(R, T);

	FP_PACK_SP(vrz, R);

	FP_UNPACK_SP(A, vrx + 4);
	FP_UNPACK_SP(B, vry + 4);

	FP_MUL_S(T, A, B);
	FP_NEG_S(R, T);

	FP_PACK_SP(vrz + 4, R);

	DEBUG_DP_END(fnmulm);
	FPU_INSN_DP_END;
}

void
FPUV2_OP_FUNC(fnmuls)
{
	FPU_INSN_START(SR, SR, SI);
	DEBUG_SP(fnmuls);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(T);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);

	FP_MUL_S(T, A, B);
	FP_NEG_S(R, T);

	FP_PACK_SP(vrz, R);

	DEBUG_SP_END(fnmuls);
	FPU_INSN_SP_END;
}

/*
 * z = 1 / x
 */
void
FPUV2_OP_FUNC(frecipd)
{
	void *constant;

	FPU_INSN_START(DR, DN, DI);
	DEBUG_DP_U(frecipd);
	DP_CONST_DATA(constant, 1);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, constant);

	FP_DIV_D(R, B, A);

	FP_PACK_DP(vrz, R);

	DEBUG_DP_END(frecipd);
	FPU_INSN_DP_END;
}

void
FPUV2_OP_FUNC(frecips)
{
	void *constant;
	unsigned int constant_val;

	FPU_INSN_START(SR, SN, SI);
	DEBUG_SP_U(frecips);
	constant_val = get_single_constant(1);
	constant = &constant_val;
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, constant);

	FP_DIV_S(R, B, A);

	FP_PACK_SP(vrz, R);

	DEBUG_SP_END(frecips);
	FPU_INSN_SP_END;
}

/*
 * z = (double)x
 */
void
FPUV2_OP_FUNC(fsitod)
{
	FPU_INSN_START(SR, DN, DI);
	DEBUG_SP2DP(fsitod);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_FROM_INT_D(R, *(int *)vrx, 32, int);

	FP_PACK_DP(vrz, R);

	DEBUG_DP_END(fsitod);
	FPU_INSN_DP_END;

}

/*
 * z = (float)x
 */
void
FPUV2_OP_FUNC(fsitos)
{
	FPU_INSN_START(SR, SN, SI);
	DEBUG_SP_U(fsitos);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_FROM_INT_S(R, *(int *)vrx, 32, int);

	FP_PACK_SP(vrz, R);

	DEBUG_SP_END(fsitos);
	FPU_INSN_SP_END;
}

/*
 * z = x ^ 1/2
 */
void
FPUV2_OP_FUNC(fsqrtd)
{
	FPU_INSN_START(DR, DN, DI);
	DEBUG_DP_U(fsqrtd);
	FP_DECL_D(A);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);

	FP_SQRT_D(R, A);

	FP_PACK_DP(vrz, R);

	DEBUG_DP_END(fsqrtd);
	FPU_INSN_DP_END;
}

void
FPUV2_OP_FUNC(fsqrts)
{
	FPU_INSN_START(SR, SN, SI);
	DEBUG_SP_U(fsqrts);
	FP_DECL_S(A);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);

	FP_SQRT_S(R, A);

	FP_PACK_SP(vrz, R);

	DEBUG_SP_END(fsqrts);
	FPU_INSN_SP_END;
}

/*
 * z = (double)x
 */
void
FPUV2_OP_FUNC(fstod)
{
	FPU_INSN_START(SR, DN, DI);
	DEBUG_SP2DP(fstod);
	FP_DECL_S(A);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);

	FP_CONV(D, S, 2, 1, R, A);

	FP_PACK_DP(vrz, R);

	DEBUG_DP_END(fstod);
	FPU_INSN_DP_END;
}

/*
 * z = (int)x
 */
void
FPUV2_OP_FUNC(fstosi_rn)
{
	int r;

	FPU_INSN_START(SR, SN, SI);
	DEBUG_SP_CVT_U(fstosi, rn);
	FP_DECL_S(A);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_NEAREST);
	if (A_c == FP_CLS_INF) {
		*(unsigned int *)vrz = (A_s == 0) ? 0x7fffffff : 0x80000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(unsigned int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_ROUND_S(r, A, 32, 1);
		FP_SET_EXCEPTION(FP_CUR_EXCEPTIONS);
		*(unsigned int *)vrz = r;
	}
	RESTORE_ROUND_MODE;
	DEBUG_SP_END(fstosi_rn);
	FPU_INSN_SP_END;
}

void
FPUV2_OP_FUNC(fstosi_rz)
{
	int r;

	FPU_INSN_START(SR, SN, SI);
	DEBUG_SP_CVT_U(fstosi, rz);
	FP_DECL_S(A);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_ZERO);
	if (A_c == FP_CLS_INF) {
		*(unsigned int *)vrz = (A_s == 0) ? 0x7fffffff : 0x80000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(unsigned int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_ROUND_S(r, A, 32, 1);
		*(unsigned int *)vrz = r;
	}
	RESTORE_ROUND_MODE;
	DEBUG_SP_END(fstosi_rz);
	FPU_INSN_SP_END;
}

void
FPUV2_OP_FUNC(fstosi_rpi)
{
	int r;

	FPU_INSN_START(SR, SN, SI);
	DEBUG_SP_CVT_U(fstosi, rpi);
	FP_DECL_S(A);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_PINF);
	if (A_c == FP_CLS_INF) {
		*(unsigned int *)vrz = (A_s == 0) ? 0x7fffffff : 0x80000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(unsigned int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_ROUND_S(r, A, 32, 1);
		*(unsigned int *)vrz = r;
	}
	RESTORE_ROUND_MODE;
	DEBUG_SP_END(fstosi_rpi);
	FPU_INSN_SP_END;
}

void
FPUV2_OP_FUNC(fstosi_rni)
{
	int r;

	FPU_INSN_START(SR, SN, SI);
	DEBUG_SP_CVT_U(fstosi, rni);
	FP_DECL_S(A);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_MINF);
	if (A_c == FP_CLS_INF) {
		*(unsigned int *)vrz = (A_s == 0) ? 0x7fffffff : 0x80000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(unsigned int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_ROUND_S(r, A, 32, 1);
		*(unsigned int *)vrz = r;
	}
	RESTORE_ROUND_MODE;
	DEBUG_SP_END(fstosi_rni);
	FPU_INSN_SP_END;
}

/*
 * z = (unsigned int)x
 */
void
FPUV2_OP_FUNC(fstoui_rn)
{
	unsigned int r;

	FPU_INSN_START(SR, SN, SI);
	DEBUG_SP_CVT_U(fstoui, rn);
	FP_DECL_S(A);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_NEAREST);
	if (A_c == FP_CLS_INF) {
		*(unsigned int *)vrz = (A_s == 0) ? 0xffffffff : 0x00000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(unsigned int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_ROUND_S(r, A, 32, 0);
		*(unsigned int *)vrz = r;
	}
	RESTORE_ROUND_MODE;
	DEBUG_SP_END(fstoui_rn);
	FPU_INSN_SP_END;
}

void
FPUV2_OP_FUNC(fstoui_rz)
{
	unsigned int r;

	FPU_INSN_START(SR, SN, SI);
	DEBUG_SP_CVT_U(fstoui, rz);
	FP_DECL_S(A);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_ZERO);
	if (A_c == FP_CLS_INF) {
		*(unsigned int *)vrz = (A_s == 0) ? 0xffffffff : 0x00000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(unsigned int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_ROUND_S(r, A, 32, 0);
		*(unsigned int *)vrz = r;
	}
	RESTORE_ROUND_MODE;
	DEBUG_SP_END(fstoui_rz);
	FPU_INSN_SP_END;
}

void
FPUV2_OP_FUNC(fstoui_rpi)
{
	unsigned int r;

	FPU_INSN_START(SR, SN, SI);
	DEBUG_SP_CVT_U(fstoui, rpi);
	FP_DECL_S(A);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_PINF);
	if (A_c == FP_CLS_INF) {
		*(unsigned int *)vrz = (A_s == 0) ? 0xffffffff : 0x00000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(unsigned int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_ROUND_S(r, A, 32, 0);
		*(unsigned int *)vrz = r;
	}
	RESTORE_ROUND_MODE;
	DEBUG_SP_END(fstoui_rpi);
	FPU_INSN_SP_END;
}

void
FPUV2_OP_FUNC(fstoui_rni)
{
	unsigned int r;

	FPU_INSN_START(SR, SN, SI);
	DEBUG_SP_CVT_U(fstoui, rni);
	FP_DECL_S(A);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_MINF);
	if (A_c == FP_CLS_INF) {
		*(unsigned int *)vrz = (A_s == 0) ? 0xffffffff : 0x00000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(unsigned int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_ROUND_S(r, A, 32, 0);
		*(unsigned int *)vrz = r;
	}
	RESTORE_ROUND_MODE;
	DEBUG_SP_END(fstoui_rni);
	FPU_INSN_SP_END;
}

/*
 * z = x - y
 */
void
FPUV2_OP_FUNC(fsubd)
{
	FPU_INSN_START(DR, DR, DI);
	DEBUG_DP(fsubd);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);

	FP_SUB_D(R, A, B);

	FP_PACK_DP(vrz, R);

	DEBUG_DP_END(fsubd);
	FPU_INSN_DP_END;
}

void
FPUV2_OP_FUNC(fsubm)
{
	FPU_INSN_START(DR, DR, DI);
	DEBUG_DP(fsubm);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);

	FP_SUB_S(R, A, B);

	FP_PACK_SP(vrz, R);

	FP_UNPACK_SP(A, vrx + 4);
	FP_UNPACK_SP(B, vry + 4);

	FP_SUB_S(R, A, B);

	FP_PACK_SP(vrz + 4, R);

	DEBUG_DP_END(fsubm);
	FPU_INSN_DP_END;
}

void
FPUV2_OP_FUNC(fsubs)
{
	FPU_INSN_START(SR, SR, SI);
	DEBUG_SP(fsubs);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);

	FP_SUB_S(R, A, B);

	FP_PACK_SP(vrz, R);

	DEBUG_SP_END(fsubs);
	FPU_INSN_SP_END;
}

/*
 * z = (double)x
 */
void
FPUV2_OP_FUNC(fuitod)
{
	FPU_INSN_START(SR, DN, DI);
	DEBUG_SP2DP(fuitod);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_FROM_INT_D(R, *(unsigned int *)vrx, 32, int);

	FP_PACK_DP(vrz, R);

	DEBUG_DP_END(fuitod);
	FPU_INSN_DP_END;

}

/*
 * z = (float)x
 */
void
FPUV2_OP_FUNC(fuitos)
{
	FPU_INSN_START(SR, SN, SI);
	DEBUG_SP_U(fsitos);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_FROM_INT_S(R, *(unsigned int *)vrx, 32, int);

	FP_PACK_SP(vrz, R);

	DEBUG_SP_END(fuitos);
	FPU_INSN_SP_END;
}

/*
 * z = *(x + imm * 4)
 */
void
FPUV2_OP_FUNC(fldd)
{
	unsigned long long result;
	unsigned int imm;
	unsigned int op_val1;

	op_val1 = get_uint32(x, inst_data);
	imm = FPUV2_LDST_IMM8(inst_data->inst);
	result = get_float64_from_memory(op_val1 + imm * 4);

	set_float64(result, z);
}

void
FPUV2_OP_FUNC(fldm)
{
	unsigned long long result;
	unsigned int imm;
	unsigned int op_val1;

	op_val1 = get_uint32(x, inst_data);
	imm = FPUV2_LDST_IMM8(inst_data->inst);
	result = get_float64_from_memory(op_val1 + imm * 8);

	set_float64(result, z);
}

void
FPUV2_OP_FUNC(flds)
{
	unsigned int result;
	unsigned int imm;
	unsigned int op_val1;

	op_val1 = get_uint32(x, inst_data);
	imm = FPUV2_LDST_IMM8(inst_data->inst);
	result = get_float32_from_memory(op_val1 + imm * 4);

	set_float32(result, z);
}

/*
 * z = *(x)  ...
 */
void
FPUV2_OP_FUNC(fldmd)
{
	unsigned long long result;
	int i;
	unsigned int op_val1;

	op_val1 = get_uint32(x, inst_data);
	for (i = 0; i < y; i++) {
		result = get_float64_from_memory(op_val1 + i * 8);
		set_float64(result, z + i);
	}
}

void
FPUV2_OP_FUNC(fldmm)
{
	unsigned long long result;
	int i;
	unsigned int op_val1;

	op_val1 = get_uint32(x, inst_data);
	for (i = 0; i < y; i++) {
		result = get_float64_from_memory(op_val1 + i * 8);
		set_float64(result, z + i);
	}
}

void
FPUV2_OP_FUNC(fldms)
{
	unsigned int result;
	int i;
	unsigned int op_val1;

	op_val1 = get_uint32(x, inst_data);
	for (i = 0; i < y; i++) {
		result = get_float32_from_memory(op_val1 + i * 4);
		set_float32(result, z + i);
	}
}

/*
 * z = *(x + y * imm)
 */
void
FPUV2_OP_FUNC(fldrd)
{
	unsigned long long result;
	unsigned int imm, op_val1, op_val2;

	imm = FPUV2_LDST_R_IMM2(inst_data->inst);
	op_val1 = get_uint32(x, inst_data);
	op_val2 = get_uint32(y, inst_data);
	result = get_float64_from_memory(op_val1 + (op_val2 << imm));

	set_float64(result, z);
}

void
FPUV2_OP_FUNC(fldrm)
{
	unsigned long long result;
	unsigned int imm, op_val1, op_val2;

	imm =  FPUV2_LDST_R_IMM2(inst_data->inst);
	op_val1 = get_uint32(x, inst_data);
	op_val2 = get_uint32(y, inst_data);
	result = get_float64_from_memory(op_val1 + (op_val2 << imm));

	set_float64(result, z);
}

void
FPUV2_OP_FUNC(fldrs)
{
	unsigned int result;
	unsigned int imm, op_val1, op_val2;

	imm =  FPUV2_LDST_R_IMM2(inst_data->inst);
	op_val1 = get_uint32(x, inst_data);
	op_val2 = get_uint32(y, inst_data);
	result = get_float32_from_memory(op_val1 + (op_val2 << imm));

	set_float32(result, z);
}

/*
 * *(x + imm * 4) = z
 */
void
FPUV2_OP_FUNC(fstd)
{
	unsigned long long result;
	unsigned int imm, op_val1;

	imm = FPUV2_LDST_IMM8(inst_data->inst);
	op_val1 = get_uint32(x, inst_data);
	result = get_float64(z);

	set_float64_to_memory(result, op_val1 + imm * 4);
}

void
FPUV2_OP_FUNC(fstm)
{
	unsigned long long result;
	unsigned int imm, op_val1;

	imm = FPUV2_LDST_IMM8(inst_data->inst);
	op_val1 = get_uint32(x, inst_data);
	result = get_float64(z);

	set_float64_to_memory(result, op_val1 + imm * 8);
}

void
FPUV2_OP_FUNC(fsts)
{
	unsigned int result;
	unsigned int imm, op_val1;

	imm = FPUV2_LDST_IMM8(inst_data->inst);
	op_val1 = get_uint32(x, inst_data);
	result = get_float32(z);

	set_float32_to_memory(result, op_val1 + imm * 4);
}

/*
 * z = *(x)  ...
 */
void
FPUV2_OP_FUNC(fstmd)
{
	unsigned long long result;
	int i;
	unsigned int op_val1;

	op_val1 = get_uint32(x, inst_data);
	for (i = 0; i < y; i++) {
		result = get_float64(z + i);
		set_float64_to_memory(result, op_val1 + i * 8);
	}
}

void
FPUV2_OP_FUNC(fstmm)
{
	unsigned long long result;
	int i;
	unsigned int op_val1;

	op_val1 = get_uint32(x, inst_data);
	for (i = 0; i < y; i++) {
		result = get_float64(z + i);
		set_float64_to_memory(result, op_val1 + i * 8);
	}
}

void
FPUV2_OP_FUNC(fstms)
{
	unsigned int result;
	int i;
	unsigned int op_val1;

	op_val1 = get_uint32(x, inst_data);
	for (i = 0; i < y; i++) {
		result = get_float32(z + i);
		set_float32_to_memory(result, op_val1 + i * 4);
	}
}

/*
 * *(x + y * imm) = z
 */
void
FPUV2_OP_FUNC(fstrd)
{
	unsigned long long result;
	unsigned int imm, op_val1, op_val2;

	imm =  FPUV2_LDST_R_IMM2(inst_data->inst);
	op_val1 = get_uint32(x, inst_data);
	op_val2 = get_uint32(y, inst_data);
	result = get_float64(z);

	set_float64_to_memory(result, op_val1 + (op_val2 << imm));
}

void
FPUV2_OP_FUNC(fstrm)
{
	unsigned long long result;
	unsigned int imm, op_val1, op_val2;

	imm =  FPUV2_LDST_R_IMM2(inst_data->inst);
	op_val1 = get_uint32(x, inst_data);
	op_val2 = get_uint32(y, inst_data);
	result = get_float64(z);

	set_float64_to_memory(result, op_val1 + (op_val2 << imm));
}

void
FPUV2_OP_FUNC(fstrs)
{
	unsigned int result;
	unsigned int imm, op_val1, op_val2;

	imm =  FPUV2_LDST_R_IMM2(inst_data->inst);
	op_val1 = get_uint32(x, inst_data);
	op_val2 = get_uint32(y, inst_data);
	result = get_float32(z);

	set_float32_to_memory(result, op_val1 + (op_val2 << imm));
}

#define SOP_MAP(id, insn)                                                  \
	[id] = { FPU_OP_NAME(insn) }

struct instruction_op_array inst_op1[0xff] = {
	SOP_MAP(FPUV2_FABSD, fabsd),
	SOP_MAP(FPUV2_FABSM, fabsm),
	SOP_MAP(FPUV2_FABSS, fabss),
	SOP_MAP(FPUV2_FADDD, faddd),
	SOP_MAP(FPUV2_FADDM, faddm),
	SOP_MAP(FPUV2_FADDS, fadds),
	SOP_MAP(FPUV2_FCMPHSD, fcmphsd),
	SOP_MAP(FPUV2_FCMPHSS, fcmphss),
	SOP_MAP(FPUV2_FCMPLTD, fcmpltd),
	SOP_MAP(FPUV2_FCMPLTS, fcmplts),
	SOP_MAP(FPUV2_FCMPNED, fcmpned),
	SOP_MAP(FPUV2_FCMPNES, fcmpnes),
	SOP_MAP(FPUV2_FCMPUOD, fcmpuod),
	SOP_MAP(FPUV2_FCMPUOS, fcmpuos),
	SOP_MAP(FPUV2_FCMPZHSD, fcmpzhsd),
	SOP_MAP(FPUV2_FCMPZHSS, fcmpzhss),
	SOP_MAP(FPUV2_FCMPZLSD, fcmpzlsd),
	SOP_MAP(FPUV2_FCMPZLSS, fcmpzlss),
	SOP_MAP(FPUV2_FCMPZNED, fcmpzned),
	SOP_MAP(FPUV2_FCMPZNES, fcmpznes),
	SOP_MAP(FPUV2_FCMPZUOD, fcmpzuod),
	SOP_MAP(FPUV2_FCMPZUOS, fcmpzuos),
	SOP_MAP(FPUV2_FDIVD, fdivd),
	SOP_MAP(FPUV2_FDIVS, fdivs),
	SOP_MAP(FPUV2_FDTOS, fdtos),
	SOP_MAP(FPUV2_FDTOSI_RN, fdtosi_rn),
	SOP_MAP(FPUV2_FDTOSI_RZ, fdtosi_rz),
	SOP_MAP(FPUV2_FDTOSI_RPI, fdtosi_rpi),
	SOP_MAP(FPUV2_FDTOSI_RNI, fdtosi_rni),
	SOP_MAP(FPUV2_FDTOUI_RN, fdtoui_rn),
	SOP_MAP(FPUV2_FDTOUI_RZ, fdtoui_rz),
	SOP_MAP(FPUV2_FDTOUI_RPI, fdtoui_rpi),
	SOP_MAP(FPUV2_FDTOUI_RNI, fdtoui_rni),
	SOP_MAP(FPUV2_FMACD, fmacd),
	SOP_MAP(FPUV2_FMACM, fmacm),
	SOP_MAP(FPUV2_FMACS, fmacs),
	SOP_MAP(FPUV2_FMFVRH, fmfvrh),
	SOP_MAP(FPUV2_FMFVRL, fmfvrl),
	SOP_MAP(FPUV2_FMOVD, fmovd),
	SOP_MAP(FPUV2_FMOVM, fmovm),
	SOP_MAP(FPUV2_FMOVS, fmovs),
	SOP_MAP(FPUV2_FMSCD, fmscd),
	SOP_MAP(FPUV2_FMSCM, fmscm),
	SOP_MAP(FPUV2_FMSCS, fmscs),
	SOP_MAP(FPUV2_FMTVRH, fmtvrh),
	SOP_MAP(FPUV2_FMTVRL, fmtvrl),
	SOP_MAP(FPUV2_FMULD, fmuld),
	SOP_MAP(FPUV2_FMULM, fmulm),
	SOP_MAP(FPUV2_FMULS, fmuls),
	SOP_MAP(FPUV2_FNEGD, fnegd),
	SOP_MAP(FPUV2_FNEGM, fnegm),
	SOP_MAP(FPUV2_FNEGS, fnegs),
	SOP_MAP(FPUV2_FNMACD, fnmacd),
	SOP_MAP(FPUV2_FNMACM, fnmacm),
	SOP_MAP(FPUV2_FNMACS, fnmacs),
	SOP_MAP(FPUV2_FNMSCD, fnmscd),
	SOP_MAP(FPUV2_FNMSCM, fnmscm),
	SOP_MAP(FPUV2_FNMSCS, fnmscs),
	SOP_MAP(FPUV2_FNMULD, fnmuld),
	SOP_MAP(FPUV2_FNMULM, fnmulm),
	SOP_MAP(FPUV2_FNMULS, fnmuls),
	SOP_MAP(FPUV2_FRECIPD, frecipd),
	SOP_MAP(FPUV2_FRECIPS, frecips),
	SOP_MAP(FPUV2_FSITOD, fsitod),
	SOP_MAP(FPUV2_FSITOS, fsitos),
	SOP_MAP(FPUV2_FSQRTD, fsqrtd),
	SOP_MAP(FPUV2_FSQRTS, fsqrts),
	SOP_MAP(FPUV2_FSTOD, fstod),
	SOP_MAP(FPUV2_FSTOSI_RN, fstosi_rn),
	SOP_MAP(FPUV2_FSTOSI_RZ, fstosi_rz),
	SOP_MAP(FPUV2_FSTOSI_RPI, fstosi_rpi),
	SOP_MAP(FPUV2_FSTOSI_RNI, fstosi_rni),
	SOP_MAP(FPUV2_FSTOUI_RN, fstoui_rn),
	SOP_MAP(FPUV2_FSTOUI_RZ, fstoui_rz),
	SOP_MAP(FPUV2_FSTOUI_RPI, fstoui_rpi),
	SOP_MAP(FPUV2_FSTOUI_RNI, fstoui_rni),
	SOP_MAP(FPUV2_FSUBD, fsubd),
	SOP_MAP(FPUV2_FSUBM, fsubm),
	SOP_MAP(FPUV2_FSUBS, fsubs),
	SOP_MAP(FPUV2_FUITOD, fuitod),
	SOP_MAP(FPUV2_FUITOS, fuitos),
};

struct instruction_op_array inst_op2[0x1f] = {
	SOP_MAP(FPUV2_FLDD, fldd),
	SOP_MAP(FPUV2_FLDM, fldm),
	SOP_MAP(FPUV2_FLDMD, fldmd),
	SOP_MAP(FPUV2_FLDMM, fldmm),
	SOP_MAP(FPUV2_FLDMS, fldms),
	SOP_MAP(FPUV2_FLDRD, fldrd),
	SOP_MAP(FPUV2_FLDRM, fldrm),
	SOP_MAP(FPUV2_FLDRS, fldrs),
	SOP_MAP(FPUV2_FLDS, flds),
	SOP_MAP(FPUV2_FSTD, fstd),
	SOP_MAP(FPUV2_FSTM, fstm),
	SOP_MAP(FPUV2_FSTMD, fstmd),
	SOP_MAP(FPUV2_FSTMM, fstmm),
	SOP_MAP(FPUV2_FSTMS, fstms),
	SOP_MAP(FPUV2_FSTRD, fstrd),
	SOP_MAP(FPUV2_FSTRM, fstrm),
	SOP_MAP(FPUV2_FSTRS, fstrs),
	SOP_MAP(FPUV2_FSTS, fsts),
};
