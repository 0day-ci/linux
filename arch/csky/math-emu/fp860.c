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
#include "fp860.h"

void FPUV3_OP_FUNC(fadds)
{
	FPU_INSN_START(SR, SR, SI);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);

	FP_ADD_S(R, A, B);

	FP_PACK_SP(vrz, R);

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fsubs)
{
	FPU_INSN_START(SR, SR, SI);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);

	FP_SUB_S(R, A, B);

	FP_PACK_SP(vrz, R);

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fmovs)
{
	unsigned int result;

	result = get_float32(x);
	set_float32(result, z);
}

void FPUV3_OP_FUNC(fabss)
{
	unsigned int result;

	result = get_float32(x) & 0x7fffffff;
	set_float32(result, z);
}

void FPUV3_OP_FUNC(fnegs)
{
	unsigned int result;

	result = get_float32(x) ^ 0x80000000;
	set_float32(result, z);
}

void FPUV3_OP_FUNC(fcmpzhss)
{
	int result;
	void *constant;

	FPU_INSN_START(SR, SN, SN);
	SP_CONST_DATA(constant, 0);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, constant);

	FP_CMP_S(result, A, B, 3);
	if ((result == 3) && (A_c == FP_CLS_NAN)) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
		result = 0;
	} else
		result = ((result == 0) || (result == 1)) ? 1 : 0;

	SET_FLAG_END;
}

void FPUV3_OP_FUNC(fcmpzlts)
{
	int result;
	void *constant;

	FPU_INSN_START(SR, SN, SN);
	SP_CONST_DATA(constant, 0);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, constant);

	FP_CMP_S(result, A, B, 3);
	if ((result == 3) && (A_c == FP_CLS_NAN)) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
		result = 0;
	} else
		result = (result == -1) ? 1 : 0;

	SET_FLAG_END;
}

void FPUV3_OP_FUNC(fmuls)
{
	FPU_INSN_START(SR, SR, SI);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);

	FP_MUL_S(R, A, B);

	FP_PACK_SP(vrz, R);

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fnmuls)
{
	FPU_INSN_START(SR, SR, SI);
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

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fmacs)
{
	FPU_INSN_START(SR, SR, SR);

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

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fmscs)
{
	FPU_INSN_START(SR, SR, SR);
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

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fnmacs)
{
	FPU_INSN_START(SR, SR, SR);
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

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fnmscs)
{
	FPU_INSN_START(SR, SR, SR);
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

	FP_NEG_S(C, C);

	FP_MUL_S(T, A, B);
	MAC_INTERNAL_ROUND_SP;
	FP_NEG_S(N, C);
	FP_SUB_S(R, N, T);

	FP_PACK_SP(vrz, R);

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fdivs)
{
	FPU_INSN_START(SR, SR, SI);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);

	FP_DIV_S(R, A, B);

	FP_PACK_SP(vrz, R);

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(frecips)
{
	void *constant;

	FPU_INSN_START(SR, SN, SI);
	SP_CONST_DATA(constant, 1);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, constant);

	FP_DIV_S(R, B, A);

	FP_PACK_SP(vrz, R);

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fsqrts)
{
	FPU_INSN_START(SR, SN, SI);
	FP_DECL_S(A);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);

	FP_SQRT_S(R, A);

	FP_PACK_SP(vrz, R);

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(finss)
{
	FPU_INSN_START(SR, SR, SI);
	FP_DECL_S(A);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, (vrz || (vrx || 0xFFFF) << 16));

	FP_PACK_SP(vrz, R);

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fcmpnezs)
{
	int result;
	void *constant;

	FPU_INSN_START(SR, SN, SN);
	SP_CONST_DATA(constant, 0);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, constant);

	FP_CMP_S(result, A, B, 3);
	if ((result == 3) && (FP_ISSIGNAN_S(A))) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
		result = 1;
	} else
		result = (result != 0) ? 1 : 0;

	SET_FLAG_END;
}

void FPUV3_OP_FUNC(fcmpzuos)
{
	int result;

	FPU_INSN_START(SR, SN, SN);
	FP_DECL_S(A);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);

	result = (A_c == FP_CLS_NAN) ? 1 : 0;

	SET_FLAG_END;
}

void FPUV3_OP_FUNC(fcmphss)
{
	int result;

	FPU_INSN_START(SR, SR, SN);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);

	FP_CMP_S(result, A, B, 3);
	if ((result == 3) && ((A_c == FP_CLS_NAN) || (B_c == FP_CLS_NAN))) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
		result = 0;
	} else
		result = ((result == 0) || (result == 1)) ? 1 : 0;

	SET_FLAG_END;
}

void FPUV3_OP_FUNC(fcmplts)
{
	int result;

	FPU_INSN_START(SR, SR, SN);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);

	FP_CMP_S(result, A, B, 3);
	if ((result == 3) && ((A_c == FP_CLS_NAN) || (B_c == FP_CLS_NAN))) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
		result = 0;
	} else
		result = (result == -1) ? 1 : 0;

	SET_FLAG_END;
}

void FPUV3_OP_FUNC(fcmpnes)
{
	int result;

	FPU_INSN_START(SR, SR, SN);
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

	SET_FLAG_END;
}

void FPUV3_OP_FUNC(fcmpuos)
{
	int result;

	FPU_INSN_START(SR, SR, SN);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);

	result = (A_c == FP_CLS_NAN) || (B_c == FP_CLS_NAN) ? 1 : 0;

	SET_FLAG_END;
}

void FPUV3_OP_FUNC(fcmphzs)
{
	int result;
	void *constant;

	FPU_INSN_START(SR, SN, SN);
	SP_CONST_DATA(constant, 0);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, constant);

	FP_CMP_S(result, A, B, 3);
	if ((result == 3) && (A_c == FP_CLS_NAN)) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
		result = 0;
	} else
		result = (result == 1) ? 1 : 0;

	SET_FLAG_END;
}

void FPUV3_OP_FUNC(fcmplszs)
{
	int result;
	void *constant;

	FPU_INSN_START(SR, SN, SN);
	SP_CONST_DATA(constant, 0);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, constant);

	FP_CMP_S(result, A, B, 3);
	if ((result == 3) && (A_c == FP_CLS_NAN)) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
		result = 0;
	} else
		result = ((result == 0) || (result == -1)) ? 1 : 0;

	SET_FLAG_END;
}

void FPUV3_OP_FUNC(fmaxnms)
{
	long result;

	FPU_INSN_START(SR, SR, SI);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);

	FP_CMP_S(result, A, B, 3);
	if ((result == 3) && ((A_c == FP_CLS_NAN) || (B_c == FP_CLS_NAN))) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		if (result == -1)
			FP_PACK_SP(vrz, B);
		else
			FP_PACK_SP(vrz, A);
	}

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fminnms)
{
	long result;

	FPU_INSN_START(SR, SR, SI);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);

	FP_CMP_S(result, A, B, 3);
	if ((result == 3) && ((A_c == FP_CLS_NAN) || (B_c == FP_CLS_NAN))) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		if (result == -1 || result == 0)
			FP_PACK_SP(vrz, A);
		else
			FP_PACK_SP(vrz, B);
	}

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fsels)
{
	char cmp;

	FPU_INSN_START(SR, SR, SI);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);

	cmp = get_fsr_c(inst_data->regs);
	if (cmp)
		FP_PACK_SP(vrz, B);
	else
		FP_PACK_SP(vrz, A);

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(ffmulas)
{
	union fs_data a, b, c;

	FPU_INSN_START(SR, SR, SR);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(C);
	FP_DECL_S(T);
	FP_DECL_S(R);
	FP_DECL_EX;

	a.n = x_val;
	b.n = y_val;
	c.n = z_val;

	FP_UNPACK_SP(A, &a.f);
	FP_UNPACK_SP(B, &b.f);
	FP_UNPACK_SP(C, &c.f);

	FP_FMA_S(R, A, B, C);

	FP_PACK_SP(vrz, R);

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(ffmulss)
{
	union fs_data a, b, c;

	FPU_INSN_START(SR, SR, SR);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(C);
	FP_DECL_S(T);
	FP_DECL_S(R);
	FP_DECL_EX;

	a.n = x_val;
	b.n = y_val;
	c.n = z_val;

	FP_UNPACK_SP(A, &a.f);
	FP_UNPACK_SP(B, &b.f);
	FP_UNPACK_SP(C, &c.f);

	FP_NEG_S(A, A);
	FP_FMA_S(R, A, B, C);

	FP_PACK_SP(vrz, R);

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(ffnmulas)
{
	union fs_data a, b, c;

	FPU_INSN_START(SR, SR, SR);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(C);
	FP_DECL_S(T);
	FP_DECL_S(N);
	FP_DECL_S(R);
	FP_DECL_EX;

	a.n = x_val;
	b.n = y_val;
	c.n = z_val;

	FP_UNPACK_SP(A, &a.f);
	FP_UNPACK_SP(B, &b.f);
	FP_UNPACK_SP(C, &c.f);

	FP_FMA_S(R, A, B, C);
	FP_NEG_S(R, R);

	FP_PACK_SP(vrz, R);

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(ffnmulss)
{
	union fs_data a, b, c;

	FPU_INSN_START(SR, SR, SR);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_S(C);
	FP_DECL_S(T);
	FP_DECL_S(R);
	FP_DECL_EX;

	a.n = x_val;
	b.n = y_val;
	c.n = z_val;

	FP_UNPACK_SP(A, &a.f);
	FP_UNPACK_SP(B, &b.f);
	FP_UNPACK_SP(C, &c.f);

	FP_NEG_S(C, C);
	FP_FMA_S(R, A, B, C);

	FP_PACK_SP(vrz, R);

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(faddd)
{
	FPU_INSN_START(DR, DR, DR);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);

	FP_ADD_D(R, A, B);

	FP_PACK_DP(vrz, R);

	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(fsubd)
{
	FPU_INSN_START(DR, DR, DR);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);

	FP_SUB_D(R, A, B);

	FP_PACK_DP(vrz, R);

	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(fcmphsd)
{
	int result;

	FPU_INSN_START(DR, DR, DN);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);

	FP_CMP_D(result, A, B, 3);
	if ((result == 3) && ((A_c == FP_CLS_NAN) || (B_c == FP_CLS_NAN))) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
		result = 0;
	} else
		result = ((result == 0) || (result == 1)) ? 1 : 0;

	SET_FLAG_END;
}

void FPUV3_OP_FUNC(fcmpzhsd)
{
	int result;
	void *constant;

	FPU_INSN_START(DR, DN, DN);
	DP_CONST_DATA(constant, 0);
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

	SET_FLAG_END;
}

void FPUV3_OP_FUNC(fcmpltd)
{
	int result;

	FPU_INSN_START(DR, DR, DN);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);

	FP_CMP_D(result, A, B, 3);
	if ((result == 3) && (((A_c == FP_CLS_NAN) || (B_c == FP_CLS_NAN)))) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
		result = 0;
	} else
		result = (result == -1) ? 1 : 0;

	SET_FLAG_END;
}

void FPUV3_OP_FUNC(fcmpzltd)
{
	int result;
	void *constant;

	FPU_INSN_START(DR, DN, DN);
	DP_CONST_DATA(constant, 0);
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
		result = (result == -1) ? 1 : 0;

	SET_FLAG_END;
}

void FPUV3_OP_FUNC(fcmpned)
{
	int result;

	FPU_INSN_START(DR, DR, DN);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);

	FP_CMP_D(result, A, B, 3);
	if ((result == 3) && (FP_ISSIGNAN_D(A) || FP_ISSIGNAN_D(B))) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
		result = 1;
	} else
		result = (result != 0) ? 1 : 0;

	SET_FLAG_END;
}

void FPUV3_OP_FUNC(fcmpzned)
{
	int result;
	void *constant;

	FPU_INSN_START(DR, DN, DN);
	DP_CONST_DATA(constant, 0);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, constant);

	FP_CMP_D(result, A, B, 3);
	if ((result == 3) && (FP_ISSIGNAN_D(A))) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
		result = 1;
	} else
		result = (result != 0) ? 1 : 0;

	SET_FLAG_END;
}

void FPUV3_OP_FUNC(fcmpuod)
{
	int result;

	FPU_INSN_START(DR, DR, DN);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);

	result = (A_c == FP_CLS_NAN) || (B_c == FP_CLS_NAN) ? 1 : 0;

	SET_FLAG_END;
}

void FPUV3_OP_FUNC(fcmpzuod)
{
	int result;

	FPU_INSN_START(DR, DN, DN);
	FP_DECL_D(A);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);

	result = (A_c == FP_CLS_NAN) ? 1 : 0;

	SET_FLAG_END;
}

void FPUV3_OP_FUNC(fmovd)
{
	unsigned long long result;

	result = get_float64(x);
	set_float64(result, z);
}

void FPUV3_OP_FUNC(fmovxs)
{
	unsigned int result;

	result = get_float32(x) & 0xFF00;
	set_float32(result, z);
}

void FPUV3_OP_FUNC(fabsd)
{
	union float64_components u;

	u.f64 = get_float64(x);
#ifdef __CSKYBE__
	u.i[0] &= 0x7fffffff;
#else
	u.i[1] &= 0x7fffffff;
#endif
	set_float64(u.f64, z);
}

void FPUV3_OP_FUNC(fnegd)
{
	union float64_components u;

	u.f64 = get_float64(x);
#ifdef __CSKYBE__
	u.i[0] ^= 0x80000000;
#else
	u.i[1] ^= 0x80000000;
#endif
	set_float64(u.f64, z);
}

void FPUV3_OP_FUNC(fmuld)
{
	FPU_INSN_START(DR, DR, DR);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);

	FP_MUL_D(R, A, B);

	FP_PACK_DP(vrz, R);

	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(fdivd)
{
	FPU_INSN_START(DR, DR, DR);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);

	FP_DIV_D(R, A, B);

	FP_PACK_DP(vrz, R);

	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(fnmuld)
{
	FPU_INSN_START(DR, DR, DR);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);

	FP_MUL_D(R, A, B);
	FP_NEG_D(R, R);

	FP_PACK_DP(vrz, R);

	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(fmacd)
{
	FPU_INSN_START(DR, DR, DR);
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

	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(fnmacd)
{
	FPU_INSN_START(DR, DR, DR);
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

	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(fnmscd)
{
	FPU_INSN_START(DR, DR, DR);
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

	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(fmscd)
{
	FPU_INSN_START(DR, DR, DR);
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

	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(frecipd)
{
	void *constant;

	FPU_INSN_START(DR, DN, DI);
	DP_CONST_DATA(constant, 1);

	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, constant);

	FP_DIV_D(R, B, A);

	FP_PACK_DP(vrz, R);

	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(fsqrtd)
{
	FPU_INSN_START(DR, DN, DI);
	FP_DECL_D(A);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);

	FP_SQRT_D(R, A);

	FP_PACK_DP(vrz, R);

	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(fcmphzd)
{
	int result;
	void *constant;

	FPU_INSN_START(DR, DN, DN);
	DP_CONST_DATA(constant, 0);
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
		result = (result == 1) ? 1 : 0;

	SET_FLAG_END;
}

void FPUV3_OP_FUNC(fcmplszd)
{
	int result;
	void *constant;

	FPU_INSN_START(DR, DN, DN);
	DP_CONST_DATA(constant, 0);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, constant);

	FP_CMP_D(result, A, B, 3);
	if ((result == 3) && (FP_ISSIGNAN_D(A))) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
		result = 0;
	} else
		result = ((result == 0) || (result == -1)) ? 1 : 0;

	SET_FLAG_END;
}

void FPUV3_OP_FUNC(fmaxnmd)
{
	long result;

	FPU_INSN_START(DR, DR, DR);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);

	FP_CMP_D(result, A, B, 3);
	if ((result == 3) && ((A_c == FP_CLS_NAN) || (B_c == FP_CLS_NAN))) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		if (result == -1)
			FP_PACK_DP(vrz, B);
		else
			FP_PACK_DP(vrz, A);
	}

	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(fminnmd)
{
	long result;

	FPU_INSN_START(DR, DR, DR);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);

	FP_CMP_D(result, A, B, 3);
	if ((result == 3) && ((A_c == FP_CLS_NAN) || (B_c == FP_CLS_NAN))) {
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		if (result == -1 || result == 0)
			FP_PACK_DP(vrz, A);
		else
			FP_PACK_DP(vrz, B);
	}

	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(fseld)
{
	FPU_INSN_START(DR, DR, DR);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_EX;
	char cmp;

	FP_UNPACK_DP(A, vrx);
	FP_UNPACK_DP(B, vry);

	cmp = get_fsr_c(inst_data->regs);
	if (cmp)
		FP_PACK_DP(vrz, B);
	else
		FP_PACK_DP(vrz, A);

	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(ffmulad)
{
	union fd_data a, b, c;

	FPU_INSN_START(DR, DR, DR);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_D(C);
	FP_DECL_D(T);
	FP_DECL_D(R);
	FP_DECL_EX;

	a.n = x_val;
	b.n = y_val;
	c.n = z_val;

	FP_UNPACK_DP(A, &a.d);
	FP_UNPACK_DP(B, &b.d);
	FP_UNPACK_DP(C, &c.d);

	FP_FMA_D(R, A, B, C);

	FP_PACK_DP(vrz, R);

	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(ffmulsd)
{
	union fd_data a, b, c;

	FPU_INSN_START(DR, DR, DR);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_D(C);
	FP_DECL_D(T);
	FP_DECL_D(R);
	FP_DECL_EX;

	a.n = x_val;
	b.n = y_val;
	c.n = z_val;

	FP_UNPACK_DP(A, &a.d);
	FP_UNPACK_DP(B, &b.d);
	FP_UNPACK_DP(C, &c.d);

	FP_NEG_D(A, A);
	FP_FMA_D(R, A, B, C);

	FP_PACK_DP(vrz, R);

	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(ffnmulad)
{
	union fd_data a, b, c;

	FPU_INSN_START(DR, DR, DR);
	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_D(C);
	FP_DECL_D(T);
	FP_DECL_D(N);
	FP_DECL_D(R);
	FP_DECL_EX;

	a.n = x_val;
	b.n = y_val;
	c.n = z_val;

	FP_UNPACK_DP(A, &a.d);
	FP_UNPACK_DP(B, &b.d);
	FP_UNPACK_DP(C, &c.d);

	FP_FMA_D(R, A, B, C);
	FP_NEG_D(R, R);

	FP_PACK_DP(vrz, R);

	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(ffnmulsd)
{
	union fd_data a, b, c;

	FPU_INSN_START(DR, DR, DR);

	a.n = x_val;
	b.n = y_val;
	c.n = z_val;

	FP_DECL_D(A);
	FP_DECL_D(B);
	FP_DECL_D(C);
	FP_DECL_D(T);
	FP_DECL_D(N);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, &a.d);
	FP_UNPACK_DP(B, &b.d);
	FP_UNPACK_DP(C, &c.d);

	FP_NEG_D(C, C);
	FP_FMA_D(R, A, B, C);
	FP_PACK_DP(vrz, R);

	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(fsitos)
{
	FPU_INSN_START(SR, SN, SI);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_FROM_INT_S(R, *(int *)vrx, 32, int);

	FP_PACK_SP(vrz, R);

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fuitos)
{
	FPU_INSN_START(SR, SN, SI);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_FROM_INT_S(R, *(unsigned int *)vrx, 32, int);

	FP_PACK_SP(vrz, R);

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fsitod)
{
	FPU_INSN_START(SR, SN, DI);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_FROM_INT_D(R, *(int *)vrx, 32, int);

	FP_PACK_DP(vrz, R);

	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(fuitod)
{
	FPU_INSN_START(SR, SN, DI);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_FROM_INT_D(R, *(unsigned int *)vrx, 32, int);

	FP_PACK_DP(vrz, R);

	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(fstod)
{
	FPU_INSN_START(SR, SN, DI);
	FP_DECL_S(A);
	FP_DECL_D(R);
	FP_DECL_EX;

	x_val = x_val & 0xffffffff;
	FP_UNPACK_SP(A, vrx);

	FP_CONV(D, S, 2, 1, R, A);

	FP_PACK_DP(vrz, R);

	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(fdtos)
{
	FPU_INSN_START(DR, DN, SI);
	FP_DECL_D(A);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	FP_CONV(S, D, 1, 2, R, A);
	FP_PACK_SP(vrz, R);

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fstosi_rn)
{
	int r;

	FPU_INSN_START(SR, SN, SI);
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
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fstosi_rz)
{
	int r;

	FPU_INSN_START(SR, SN, SI);
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
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fstosi_rpi)
{
	int r;

	FPU_INSN_START(SR, SN, SI);
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
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fstosi_rni)
{
	int r;

	FPU_INSN_START(SR, SN, SI);
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
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fstoui_rn)
{
	unsigned int r;

	FPU_INSN_START(SR, SN, SI);
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
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fstoui_rz)
{
	unsigned int r;

	FPU_INSN_START(SR, SN, SI);
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
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fstoui_rpi)
{
	unsigned int r;

	FPU_INSN_START(SR, SN, SI);
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
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fstoui_rni)
{
	unsigned int r;

	FPU_INSN_START(SR, SN, SI);
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
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fdtosi_rn)
{
	int r;

	FPU_INSN_START(DR, DN, SI);
	FP_DECL_D(A);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_NEAREST);
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
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fdtosi_rz)
{
	int r;

	FPU_INSN_START(DR, DN, SI);
	FP_DECL_D(A);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_ZERO);
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
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fdtosi_rpi)
{
	int r;

	FPU_INSN_START(DR, DN, SI);
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
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fdtosi_rni)
{
	int r;

	FPU_INSN_START(DR, DN, SI);
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
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fdtoui_rn)
{
	unsigned int r;

	FPU_INSN_START(DR, DN, SI);
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
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fdtoui_rz)
{
	unsigned int r;

	FPU_INSN_START(DR, DN, SI);
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
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fdtoui_rpi)
{
	unsigned int r;

	FPU_INSN_START(DR, DN, SI);
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
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fdtoui_rni)
{
	unsigned int r;

	FPU_INSN_START(DR, DN, SI);
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
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fmtvrl)
{
	union float64_components result;

	x = (inst_data->inst >> 16) & 0x1f;
#ifdef __CSKYBE__
	result.i[1] = (unsigned int)get_uint32(x, inst_data);
	set_float32(result.i[1], z);
#else
	result.i[0] = (unsigned int)get_uint32(x, inst_data);
	set_float32(result.i[0], z);
#endif
}

void FPUV3_OP_FUNC(fmfvrl)
{
	union float64_components op_val1;
	unsigned int result;

	x = inst_data->inst & 0x1f;
	z = (inst_data->inst >> 16) & 0x1f;
	op_val1.f64 = get_float64(z);

#ifdef __CSKYBE__
	result = (unsigned int)op_val1.i[1];
#else
	result = (unsigned int)op_val1.i[0];
#endif

	set_uint32(result, x, inst_data);
}

void FPUV3_OP_FUNC(fmfvrh)
{
	union float64_components op_val1;
	unsigned int result;

	z = inst_data->inst & 0x1f;
	x = (inst_data->inst >> 16) & 0x1f;
	op_val1.f64 = get_float64(x);

#ifdef __CSKYBE__
	result = (unsigned int)op_val1.i[0];
#else
	result = (unsigned int)op_val1.i[1];
#endif

	set_uint32(result, z, inst_data);
}

void FPUV3_OP_FUNC(fmtvrh)
{
	union float64_components result;

	x = (inst_data->inst >> 16) & 0x1f;
#ifdef __CSKYBE__
	result.i[0] = (unsigned int)get_uint32(x, inst_data);
	set_float32h(result.i[0], z);
#else
	result.i[1] = (unsigned int)get_uint32(x, inst_data);
	set_float32h(result.i[1], z);
#endif
}

void FPUV3_OP_FUNC(fmtvrd)
{
	union float64_components result;

#ifdef __CSKYBE__
	result.i[0] = (unsigned int)get_uint32(y, inst_data);
	result.i[1] = (unsigned int)get_uint32(x, inst_data);
#else
	result.i[0] = (unsigned int)get_uint32(x, inst_data);
	result.i[1] = (unsigned int)get_uint32(y, inst_data);
#endif

	set_float64(result.f64, z);
}

void FPUV3_OP_FUNC(fmtvrl2)
{
	FPU_INSN_START(SR, SR, SI);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	FP_UNPACK_SP(B, vry);

	FP_PACK_SP(vrz, A);
	FP_PACK_SP(vrz + 4, B);

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fmfvrd)
{
	union float64_components op_val;
	unsigned int result;

	x = inst_data->inst & 0x1f;
	y = (inst_data->inst >> 21) & 0x1f;
	z = (inst_data->inst >> 16) & 0x1f;
	op_val.f64 = get_float64(z);

#ifdef __CSKYBE__
	result = (unsigned int)op_val.i[0];
	set_uint32(result, x, inst_data);
	result = (unsigned int)op_val.i[1];
	set_uint32(result, y, inst_data);
#else
	result = (unsigned int)op_val.i[0];
	set_uint32(result, x, inst_data);
	result = (unsigned int)op_val.i[1];
	set_uint32(result, y, inst_data);
#endif
}

void FPUV3_OP_FUNC(fmfvrl2)
{
	FPU_INSN_START(SR, SR, SI);
	FP_DECL_S(A);
	FP_DECL_S(B);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrz);
	FP_UNPACK_SP(B, vrz + 4);

	FP_PACK_SP(vrx, A);
	FP_PACK_SP(vry, B);

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fldrs)
{
	unsigned int result;
	unsigned int imm, op_val1, op_val2;

	imm = FPUV3_IMM2(inst_data->inst);
	op_val1 = get_uint32(x, inst_data);
	op_val2 = get_uint32(y, inst_data);
	result = get_float32_from_memory(op_val1 + (op_val2 << imm));

	set_float32(result, z);
}

void FPUV3_OP_FUNC(fldrd)
{
	unsigned long long result;
	unsigned int imm, op_val1, op_val2;

	imm = FPUV3_IMM2(inst_data->inst);
	op_val1 = get_uint32(x, inst_data);
	op_val2 = get_uint32(y, inst_data);
	result = get_float64_from_memory(op_val1 + (op_val2 << imm));

	set_float64(result, z);
}

void FPUV3_OP_FUNC(fstrs)
{
	unsigned int result;
	unsigned int imm, op_val1, op_val2;

	imm = FPUV3_IMM2(inst_data->inst);
	op_val1 = get_uint32(x, inst_data);
	op_val2 = get_uint32(y, inst_data);
	result = get_float32(z);

	set_float32_to_memory(result, op_val1 + (op_val2 << imm));
}

void FPUV3_OP_FUNC(fstrd)
{
	unsigned long long result;
	unsigned int imm, op_val1, op_val2;

	imm = FPUV3_IMM2(inst_data->inst);
	op_val1 = get_uint32(x, inst_data);
	op_val2 = get_uint32(y, inst_data);
	result = get_float64(z);

	set_float64_to_memory(result, op_val1 + (op_val2 << imm));
}

void FPUV3_OP_FUNC(fldms)
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

void FPUV3_OP_FUNC(fldmd)
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

void FPUV3_OP_FUNC(fldmus)
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

void FPUV3_OP_FUNC(fldmud)
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

void FPUV3_OP_FUNC(fstms)
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

void FPUV3_OP_FUNC(fstmd)
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

void FPUV3_OP_FUNC(fstmus)
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

void FPUV3_OP_FUNC(fstmud)
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

void FPUV3_OP_FUNC(fftox_f32s32)
{
	int r;

	FPU_INSN_START(SR, SR, SI);
	FP_DECL_S(A);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);

	if (A_c == FP_CLS_INF) {
		*(int *)vrz = (A_s == 0) ? 0x7fffffff : 0x80000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_S(r, A, 32, 1);
		*(int *)vrz = r;
	}
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fftox_f32u32)
{
	unsigned int r;

	FPU_INSN_START(SR, SR, SI);
	FP_DECL_S(A);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);

	if (A_c == FP_CLS_INF) {
		*(unsigned int *)vrz = (A_s == 0) ? 0xffffffff : 0x00000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(unsigned int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_S(r, A, 32, 0);
		*(unsigned int *)vrz = r;
	}
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fftox_f64s32)
{
	int r;

	FPU_INSN_START(DR, DR, DR);
	FP_DECL_D(A);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);

	if (A_c == FP_CLS_INF) {
		*(int *)vrz = (A_s == 0) ? 0x7fffffff : 0x80000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_D(r, A, 32, 1);
		*(int *)vrz = r;
	}
	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(fftox_f64u32)
{
	unsigned int r;

	FPU_INSN_START(DR, DR, DR);
	FP_DECL_D(A);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);

	if (A_c == FP_CLS_INF) {
		*(unsigned int *)vrz = (A_s == 0) ? 0xffffffff : 0x00000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(unsigned int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_D(r, A, 32, 0);
		*(unsigned int *)vrz = r;
	}
	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(fftoi_f32s32)
{
	int r;

	FPU_INSN_START(SR, SN, SI);
	FP_DECL_S(A);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	if (A_c == FP_CLS_INF) {
		*(unsigned int *)vrz = (A_s == 0) ? 0x7fffffff : 0x80000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(unsigned int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_S(r, A, 32, 1);
		*(unsigned int *)vrz = r;
	}
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fftoi_f32u32)
{
	unsigned int r;

	FPU_INSN_START(SR, SN, SI);
	FP_DECL_S(A);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	if (A_c == FP_CLS_INF) {
		*(unsigned int *)vrz = (A_s == 0) ? 0xffffffff : 0x00000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(unsigned int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_S(r, A, 32, 0);
		*(unsigned int *)vrz = r;
	}

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fftoi_f64s32)
{
	int r;

	FPU_INSN_START(DR, DN, SI);
	FP_DECL_D(A);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	if (A_c == FP_CLS_INF) {
		*(unsigned int *)vrz = (A_s == 0) ? 0x7fffffff : 0x80000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(unsigned int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_D(r, A, 32, 1);
		*(unsigned int *)vrz = r;
	}
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fftoi_f64u32)
{
	unsigned int r;

	FPU_INSN_START(DR, DN, SI);
	FP_DECL_D(A);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	if (A_c == FP_CLS_INF) {
		*(unsigned int *)vrz = (A_s == 0) ? 0xffffffff : 0x00000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(unsigned int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_D(r, A, 32, 0);
		*(unsigned int *)vrz = r;
	}

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fftofi_fs_rn)
{
	int r;

	FPU_INSN_START(SR, SR, SI);
	FP_DECL_S(A);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_NEAREST);
	if (A_c == FP_CLS_INF) {
		*(int *)vrz = (A_s == 0) ? 0x7fffffff : 0x80000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_ROUND_S(r, A, 32, 1);
		FP_FROM_INT_S(R, r, 32, int);
		FP_PACK_SP(vrz, R);
	}
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fftofi_fs_rz)
{
	int r;

	FPU_INSN_START(SR, SR, SI);
	FP_DECL_S(A);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_ZERO);
	if (A_c == FP_CLS_INF) {
		*(int *)vrz = (A_s == 0) ? 0x7fffffff : 0x80000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_ROUND_S(r, A, 32, 1);
		FP_FROM_INT_S(R, r, 32, int);
		FP_PACK_SP(vrz, R);
	}
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fftofi_fs_rpi)
{
	int r;

	FPU_INSN_START(SR, SR, SI);
	FP_DECL_S(A);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_PINF);
	if (A_c == FP_CLS_INF) {
		*(int *)vrz = (A_s == 0) ? 0x7fffffff : 0x80000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_ROUND_S(r, A, 32, 1);
		FP_FROM_INT_S(R, r, 32, int);
		FP_PACK_SP(vrz, R);
	}
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fftofi_fs_rni)
{
	int r;

	FPU_INSN_START(SR, SR, SI);
	FP_DECL_S(A);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_UNPACK_SP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_MINF);
	if (A_c == FP_CLS_INF) {
		*(int *)vrz = (A_s == 0) ? 0x7fffffff : 0x80000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(int *)vrz = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_ROUND_S(r, A, 32, 1);
		FP_FROM_INT_S(R, r, 32, int);
		FP_PACK_SP(vrz, R);
	}
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fftofi_fd_rn)
{
	unsigned long long r;

	FPU_INSN_START(DR, DR, DR);
	FP_DECL_D(A);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_NEAREST);
	if (A_c == FP_CLS_INF) {
		*(int *)vrx = (A_s == 0) ? 0x7fffffff : 0x80000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(int *)vrx = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_ROUND_D(r, A, 64, 2);
		FP_FROM_INT_D(R, r, 64, long);
		FP_PACK_DP(vrz, R);
	}
	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(fftofi_fd_rz)
{
	unsigned long long r;

	FPU_INSN_START(DR, DR, DR);
	FP_DECL_D(A);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_ZERO);
	if (A_c == FP_CLS_INF) {
		*(int *)vrx = (A_s == 0) ? 0x7fffffff : 0x80000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(int *)vrx = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_ROUND_D(r, A, 64, 2);
		FP_FROM_INT_D(R, r, 64, long);
		FP_PACK_DP(vrz, R);
	}
	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(fftofi_fd_rpi)
{
	unsigned long long r;

	FPU_INSN_START(DR, DR, DR);
	FP_DECL_D(A);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_PINF);
	if (A_c == FP_CLS_INF) {
		*(int *)vrx = (A_s == 0) ? 0x7fffffff : 0x80000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(int *)vrx = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_ROUND_D(r, A, 64, 2);
		FP_FROM_INT_D(R, r, 64, long);
		FP_PACK_DP(vrz, R);
	}
	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(fftofi_fd_rni)
{
	unsigned long long r;

	FPU_INSN_START(DR, DR, DR);
	FP_DECL_D(A);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_UNPACK_DP(A, vrx);
	SET_AND_SAVE_RM(FP_RND_MINF);
	if (A_c == FP_CLS_INF) {
		*(int *)vrx = (A_s == 0) ? 0x7fffffff : 0x80000000;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else if (A_c == FP_CLS_NAN) {
		*(int *)vrx = 0xffffffff;
		FP_SET_EXCEPTION(FP_EX_INVALID);
	} else {
		FP_TO_INT_ROUND_D(r, A, 64, 2);
		FP_FROM_INT_D(R, r, 64, long);
		FP_PACK_DP(vrz, R);
	}
	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(fxtof_s32f32)
{
	int a;

	FPU_INSN_START(SR, SR, SI);
	a = *(int *)vrx;

	FP_DECL_S(R);
	FP_DECL_EX;

	FP_FROM_INT_S(R, a, 32, int);

	FP_PACK_SP(vrz, R);
	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fxtof_u32f32)
{
	unsigned int a;

	FPU_INSN_START(SR, SR, SI);
	a = *(unsigned int *)vrx;

	FP_DECL_S(R);
	FP_DECL_EX;

	FP_FROM_INT_S(R, a, 32, int);

	FP_PACK_SP(vrz, R);

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fxtof_s32f64)
{
	int a;

	FPU_INSN_START(SR, SR, SI);
	a = *(int *)vrx;

	FP_DECL_D(R);
	FP_DECL_EX;

	FP_FROM_INT_D(R, a, 32, int);

	FP_PACK_DP(vrz, R);

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fxtof_u32f64)
{
	unsigned int a;

	FPU_INSN_START(DR, DR, DR);
	a = *(unsigned int *)vrx;

	FP_DECL_D(R);
	FP_DECL_EX;

	FP_FROM_INT_D(R, a, 32, int);

	FP_PACK_DP(vrz, R);

	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(fitof_s32f32)
{
	FPU_INSN_START(SR, SN, SI);
	FP_DECL_S(R);
	FP_DECL_EX;

	FP_FROM_INT_S(R, *(int *)vrx, 32, int);

	FP_PACK_SP(vrz, R);

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fitof_u32f32)
{
	FPU_INSN_START(SR, SN, SI);

	FP_DECL_S(R);
	FP_DECL_EX;

	FP_FROM_INT_S(R, *(unsigned int *)vrx, 32, int);

	FP_PACK_SP(vrz, R);

	FPU_INSN_SP_END;
}

void FPUV3_OP_FUNC(fitof_s32f64)
{
	unsigned long long result;
	long long x_val, z_val;
	void *vrx, *vrz;

	x_val = (int)get_float32(x);
	vrx = &x_val;
	vrz = &z_val;

	FP_DECL_D(R);
	FP_DECL_EX;

	FP_FROM_INT_D(R, x_val, 32, int);

	FP_PACK_DP(vrz, R);

	result = *(unsigned long long *)vrz;

	set_float64(result, z);

	if (FP_CUR_EXCEPTIONS)
		raise_float_exception(FP_CUR_EXCEPTIONS);
}

void FPUV3_OP_FUNC(fitof_u32f64)
{
	FPU_INSN_START(SR, SN, DI);
	FP_DECL_D(R);
	FP_DECL_EX;

	FP_FROM_INT_D(R, *(unsigned int *)vrx, 32, int);

	FP_PACK_DP(vrz, R);

	FPU_INSN_DP_END;
}

void FPUV3_OP_FUNC(flds)
{
	unsigned int result;
	unsigned int imm;
	unsigned int op_val1;
	unsigned int vrz, rx;

	rx = (inst_data->inst >> FPUV3_REG_SHI_RX) & FPUV3_REG_MASK;
	vrz = (inst_data->inst & 0xf) | ((inst_data->inst >> 21) & 0x10);

	op_val1 = get_uint32(rx, inst_data);
	imm = FPUV3_IMM8(inst_data->inst);
	result = get_float32_from_memory(op_val1 + imm * 4);

	set_float32(result, vrz);
}

void FPUV3_OP_FUNC(fldd)
{
	unsigned long long result;
	unsigned int imm;
	unsigned int op_val1;
	unsigned int vrz, rx;

	rx = (inst_data->inst >> FPUV3_REG_SHI_RX) & FPUV3_REG_MASK;
	vrz = (inst_data->inst & 0xf) | ((inst_data->inst >> 21) & 0x10);

	op_val1 = get_uint32(rx, inst_data);
	imm = FPUV3_IMM8(inst_data->inst);
	result = get_float64_from_memory(op_val1 + imm * 4);

	set_float64(result, vrz);
}

void FPUV3_OP_FUNC(fsts)
{
	unsigned int result;
	unsigned int imm, op_val1;
	unsigned int vrz, rx;

	rx = (inst_data->inst >> FPUV3_REG_SHI_RX) & FPUV3_REG_MASK;
	vrz = (inst_data->inst & 0xf) | ((inst_data->inst >> 21) & 0x10);

	imm = FPUV3_IMM8(inst_data->inst);
	op_val1 = get_uint32(rx, inst_data);
	result = get_float32(vrz);

	set_float32_to_memory(result, op_val1 + imm * 4);
}

void FPUV3_OP_FUNC(fstd)
{
	unsigned long long result;
	unsigned int imm, op_val1;
	unsigned int rx, vrz;

	rx = (inst_data->inst >> FPUV3_REG_SHI_RX) & FPUV3_REG_MASK;
	vrz = (inst_data->inst & 0xf) | ((inst_data->inst >> 21) & 0x10);

	imm = FPUV3_IMM8(inst_data->inst);
	op_val1 = get_uint32(rx, inst_data);
	result = get_float64(vrz);

	set_float64_to_memory(result, op_val1 + imm * 4);
}

void FPUV3_OP_FUNC(fmovi)
{
	unsigned int rz, imm4, imm8, sign, type;
	union float64_components val;
	void *vrz;
	unsigned int z_val;

	type = (inst_data->inst >> 6) & 0x3;
	sign = (inst_data->inst >> 5) & 0x1;
	imm4 = (inst_data->inst >> 16) & 0xf;
	imm8 = (((inst_data->inst >> 20) & 0x3f) << 2) +
	       ((inst_data->inst >> 8) & 0x3);
	rz = (inst_data->inst >> FPUV3_VREG_SHI_VRZ) & FPUV3_VREG_MASK;

	/* calculate value. */
	val.f = ((imm8 << 3) + (1 << 11)) * 1.0 / (1 << imm4);
	if (sign)
		val.f = val.f * (-1);

	if (type == 0x1) {
		vrz = &z_val;
		FP_DECL_D(A);
		FP_DECL_S(R);
		FP_DECL_EX;
		FP_UNPACK_DP(A, &val.f64);
		FP_CONV(S, D, 1, 2, R, A);
		FP_PACK_SP(vrz, R);
		set_float32(*(unsigned int *)vrz, rz);
		if (FP_CUR_EXCEPTIONS)
			raise_float_exception(FP_CUR_EXCEPTIONS);
	} else if (type == 0x2) {
		set_float64(val.f64, rz);
	} else {
	}
}

#define FPU_PCODE_INSN(pcode) fpu_pcode_insn##pcode
#define FPU_PCODE_DEFINE(sop)                                                  \
	struct insn_pcode_array FPU_PCODE_INSN(sop)[FPUV3_PCODE_MAX]

#define SOP_MAP(sop)                                                  \
	[sop] = { FPU_PCODE_INSN(sop) }
#define PCODE_MAP(id, insn)                                                  \
	[id] = { FPU_OP_NAME(insn) }
#define PCODE_RANGE_MAP(id1, id2, insn)                                                  \
	[id1... id2] = { FPU_OP_NAME(insn) }
#define FPUV3_SOP 0xD
#define FPUV3_OP_MAX 0xF
#define FPUV3_SOP_MAX 0x3F
#define FPUV3_PCODE_MAX 0x20

FPU_PCODE_DEFINE(FPUV3_FLOAT_ARITH) = {
	PCODE_MAP(FPUV3_FADDS, fadds),
	PCODE_MAP(FPUV3_FSUBS, fsubs),
	PCODE_MAP(FPUV3_FMOVS, fmovs),
	PCODE_MAP(FPUV3_FABSS, fabss),
	PCODE_MAP(FPUV3_FNEGS, fnegs),
	PCODE_MAP(FPUV3_FCMPZHSS, fcmpzhss),
	PCODE_MAP(FPUV3_FCMPZLTS, fcmpzlts),
	PCODE_MAP(FPUV3_FCMPNEZS, fcmpnezs),
	PCODE_MAP(FPUV3_FCMPZUOS, fcmpzuos),
	PCODE_MAP(FPUV3_FCMPHSS, fcmphss),
	PCODE_MAP(FPUV3_FCMPLTS, fcmplts),
	PCODE_MAP(FPUV3_FCMPNES, fcmpnes),
	PCODE_MAP(FPUV3_FCMPUOS, fcmpuos),
	PCODE_MAP(FPUV3_FMULS, fmuls),
	PCODE_MAP(FPUV3_FNMULS, fnmuls),
	PCODE_MAP(FPUV3_FMACS, fmacs),
	PCODE_MAP(FPUV3_FMSCS, fmscs),
	PCODE_MAP(FPUV3_FNMACS, fnmacs),
	PCODE_MAP(FPUV3_FNMSCS, fnmscs),
	PCODE_MAP(FPUV3_FDIVS, fdivs),
	PCODE_MAP(FPUV3_FRECIPS, frecips),
	PCODE_MAP(FPUV3_FSQRTS, fsqrts),
	PCODE_MAP(FPUV3_FINSS, finss),
};

FPU_PCODE_DEFINE(FPUV3_FLOAT_EXT_ARITH) = {
	PCODE_MAP(FPUV3_FMAXNMS, fmaxnms),
	PCODE_MAP(FPUV3_FMINNMS, fminnms),
	PCODE_MAP(FPUV3_FCMPHZS, fcmphzs),
	PCODE_MAP(FPUV3_FCMPLSZS, fcmplszs),
	PCODE_MAP(FPUV3_FFMULAS, ffmulas),
	PCODE_MAP(FPUV3_FFMULSS, ffmulss),
	PCODE_MAP(FPUV3_FFNMULAS, ffnmulas),
	PCODE_MAP(FPUV3_FFNMULSS, ffnmulss),
	PCODE_MAP(FPUV3_FSELS, fsels),
};

FPU_PCODE_DEFINE(FPUV3_DOUBLE_ARITH) = {
	PCODE_MAP(FPUV3_FADDD, faddd),
	PCODE_MAP(FPUV3_FSUBD, fsubd),
	PCODE_MAP(FPUV3_FMOVD, fmovd),
	PCODE_MAP(FPUV3_FMOVXS, fmovxs),
	PCODE_MAP(FPUV3_FABSD, fabsd),
	PCODE_MAP(FPUV3_FNEGD, fnegd),
	PCODE_MAP(FPUV3_FCMPZHSD, fcmpzhsd),
	PCODE_MAP(FPUV3_FCMPZLTD, fcmpzltd),
	PCODE_MAP(FPUV3_FCMPZNED, fcmpzned),
	PCODE_MAP(FPUV3_FCMPZUOD, fcmpzuod),
	PCODE_MAP(FPUV3_FCMPHSD, fcmphsd),
	PCODE_MAP(FPUV3_FCMPLTD, fcmpltd),
	PCODE_MAP(FPUV3_FCMPNED, fcmpned),
	PCODE_MAP(FPUV3_FCMPUOD, fcmpuod),
	PCODE_MAP(FPUV3_FMULD, fmuld),
	PCODE_MAP(FPUV3_FNMULD, fnmuld),
	PCODE_MAP(FPUV3_FMACD, fmacd),
	PCODE_MAP(FPUV3_FMSCD, fmscd),
	PCODE_MAP(FPUV3_FNMACD, fnmacd),
	PCODE_MAP(FPUV3_FNMSCS, fnmscd),
	PCODE_MAP(FPUV3_FDIVD, fdivd),
	PCODE_MAP(FPUV3_FRECIPD, frecipd),
	PCODE_MAP(FPUV3_FSQRTD, fsqrtd),
};

FPU_PCODE_DEFINE(FPUV3_DOUBLE_EXT_ARITH) = {
	PCODE_MAP(FPUV3_FMAXNMD, fmaxnmd),
	PCODE_MAP(FPUV3_FMINNMD, fminnmd),
	PCODE_MAP(FPUV3_FCMPHZD, fcmphzd),
	PCODE_MAP(FPUV3_FCMPLSZD, fcmplszd),
	PCODE_MAP(FPUV3_FFMULAD, ffmulad),
	PCODE_MAP(FPUV3_FFMULSD, ffmulsd),
	PCODE_MAP(FPUV3_FFNMULAD, ffnmulad),
	PCODE_MAP(FPUV3_FFNMULSD, ffnmulsd),
	PCODE_MAP(FPUV3_FSELD, fseld),
};

FPU_PCODE_DEFINE(FPUV3_CONVERT) = {
	PCODE_MAP(FPUV3_FSTOSI_RN, fstosi_rn),
	PCODE_MAP(FPUV3_FSTOSI_RZ, fstosi_rz),
	PCODE_MAP(FPUV3_FSTOSI_RPI, fstosi_rpi),
	PCODE_MAP(FPUV3_FSTOSI_RNI, fstosi_rni),
	PCODE_MAP(FPUV3_FSTOUI_RN, fstoui_rn),
	PCODE_MAP(FPUV3_FSTOUI_RZ, fstoui_rz),
	PCODE_MAP(FPUV3_FSTOUI_RPI, fstoui_rpi),
	PCODE_MAP(FPUV3_FSTOUI_RNI, fstoui_rni),
	PCODE_MAP(FPUV3_FDTOSI_RN, fdtosi_rn),
	PCODE_MAP(FPUV3_FDTOSI_RZ, fdtosi_rz),
	PCODE_MAP(FPUV3_FDTOSI_RPI, fdtosi_rpi),
	PCODE_MAP(FPUV3_FDTOSI_RNI, fdtosi_rni),
	PCODE_MAP(FPUV3_FDTOUI_RN, fdtoui_rn),
	PCODE_MAP(FPUV3_FDTOUI_RZ, fdtoui_rz),
	PCODE_MAP(FPUV3_FDTOUI_RPI, fdtoui_rpi),
	PCODE_MAP(FPUV3_FDTOUI_RNI, fdtoui_rni),
	PCODE_MAP(FPUV3_FSITOS, fsitos),
	PCODE_MAP(FPUV3_FUITOS, fuitos),
	PCODE_MAP(FPUV3_FSITOD, fsitod),
	PCODE_MAP(FPUV3_FIOTOD, fuitod),
	PCODE_MAP(FPUV3_FDTOS, fdtos),
	PCODE_MAP(FPUV3_FSTOD, fstod),
	PCODE_MAP(FPUV3_FMTVRH, fmtvrh),
	PCODE_MAP(FPUV3_FMTVRL, fmtvrl), //FMTVR.32.1
	PCODE_MAP(FPUV3_FMFVRH, fmfvrh),
	PCODE_MAP(FPUV3_FMFVRL, fmfvrl), //FMFVR.32.1
};

FPU_PCODE_DEFINE(FPUV3_TRANSFER) = {
	PCODE_MAP(FPUV3_FMFVRD, fmfvrd),
	PCODE_MAP(FPUV3_FMFVRL2, fmfvrl2),
	PCODE_MAP(FPUV3_FMTVRD, fmtvrd),
	PCODE_MAP(FPUV3_FMTVRL2, fmtvrl2),
};

FPU_PCODE_DEFINE(FPUV3_LD) = {
	PCODE_RANGE_MAP(FPUV3_FLDS_MIN, FPUV3_FLDS_MAX, flds),
	PCODE_RANGE_MAP(FPUV3_FLDD_MIN, FPUV3_FLDD_MAX, fldd),
};

FPU_PCODE_DEFINE(FPUV3_ST) = {
	PCODE_RANGE_MAP(FPUV3_FSTS_MIN, FPUV3_FSTS_MAX, fsts),
	PCODE_RANGE_MAP(FPUV3_FSTD_MIN, FPUV3_FSTD_MAX, fstd),
};

FPU_PCODE_DEFINE(FPUV3_LD_REG) = {
	PCODE_RANGE_MAP(FPUV3_FLDRS_MIN, FPUV3_FLDRS_MAX, fldrs),
	PCODE_RANGE_MAP(FPUV3_FLDRD_MIN, FPUV3_FLDRD_MAX, fldrd),
};

FPU_PCODE_DEFINE(FPUV3_ST_REG) = {
	PCODE_RANGE_MAP(FPUV3_FSTRS_MIN, FPUV3_FSTRS_MAX, fstrs),
	PCODE_RANGE_MAP(FPUV3_FSTRD_MIN, FPUV3_FSTRD_MAX, fstrd),
};

FPU_PCODE_DEFINE(FPUV3_LD_MEM) = {
	PCODE_MAP(FPUV3_FLDMS, fldms),
	PCODE_MAP(FPUV3_FLDMD, fldmd),
	PCODE_MAP(FPUV3_FLDMUS, fldmus),
	PCODE_MAP(FPUV3_FLDMUD, fldmud),
};

FPU_PCODE_DEFINE(FPUV3_ST_MEM) = {
	PCODE_MAP(FPUV3_FSTMS, fstms),
	PCODE_MAP(FPUV3_FSTMD, fstmd),
	PCODE_MAP(FPUV3_FSTMUS, fstmus),
	PCODE_MAP(FPUV3_FSTMUD, fstmud),
};

FPU_PCODE_DEFINE(FPUV3_CONVERT_F_IX) = {
	PCODE_MAP(FPUV3_FFTOX_F32U32, fftox_f32u32), //fstoux
	PCODE_MAP(FPUV3_FFTOX_F32S32, fftox_f32s32), //fstosx
	PCODE_MAP(FPUV3_FFTOX_F64U32, fftox_f64u32), //fdtoux
	PCODE_MAP(FPUV3_FFTOX_F64S32, fftox_f64s32), //fdtosx
	PCODE_MAP(FPUV3_FFTOI_F32U32, fftoi_f32u32), //fstoui
	PCODE_MAP(FPUV3_FFTOI_F32S32, fftoi_f32s32), //fstosi
	PCODE_MAP(FPUV3_FFTOI_F64U32, fftoi_f64u32), //fdtoui
	PCODE_MAP(FPUV3_FFTOI_F64S32, fftoi_f64s32), //fdtosi
};

FPU_PCODE_DEFINE(FPUV3_CONVERT_F_FI) = {
	PCODE_MAP(FPUV3_FFTOFI_FS_RN, fftofi_fs_rn),
	PCODE_MAP(FPUV3_FFTOFI_FS_RZ, fftofi_fs_rz),
	PCODE_MAP(FPUV3_FFTOFI_FS_RPI, fftofi_fs_rpi),
	PCODE_MAP(FPUV3_FFTOFI_FS_RNI, fftofi_fs_rni),
	PCODE_MAP(FPUV3_FFTOFI_FD_RN, fftofi_fd_rn),
	PCODE_MAP(FPUV3_FFTOFI_FD_RZ, fftofi_fd_rz),
	PCODE_MAP(FPUV3_FFTOFI_FD_RPI, fftofi_fd_rpi),
	PCODE_MAP(FPUV3_FFTOFI_FD_RNI, fftofi_fd_rni),
};

FPU_PCODE_DEFINE(FPUV3_CONVERT_IX_F) = {
	PCODE_MAP(FPUV3_FXTOF_U32F32, fxtof_u32f32), //uxtofs
	PCODE_MAP(FPUV3_FXTOF_S32F32, fxtof_s32f32), //sxtofs
	PCODE_MAP(FPUV3_FXTOF_U32F64, fxtof_u32f64), //uxtofd
	PCODE_MAP(FPUV3_FXTOF_S32F64, fxtof_s32f64), //sxtofd
	PCODE_MAP(FPUV3_FITOF_U32F32, fitof_u32f32), //fuitos
	PCODE_MAP(FPUV3_FITOF_S32F32, fitof_s32f32), //fsitos
	PCODE_MAP(FPUV3_FITOF_U32F64, fitof_u32f64), //fuitod
	PCODE_MAP(FPUV3_FITOF_S32F64, fitof_s32f64), //fsitod
};

FPU_PCODE_DEFINE(FPUV3_MOVI) = {
	PCODE_RANGE_MAP(FPUV3_MOVI_T0, FPUV3_MOVI_T1, fmovi),
	PCODE_RANGE_MAP(FPUV3_MOVI_T2, FPUV3_MOVI_T3, fmovi),
};

struct insn_sop_array sop_insn[FPUV3_SOP_MAX] = {
	SOP_MAP(FPUV3_FLOAT_ARITH),
	SOP_MAP(FPUV3_FLOAT_ARITH),
	SOP_MAP(FPUV3_FLOAT_EXT_ARITH),
	SOP_MAP(FPUV3_DOUBLE_ARITH),
	SOP_MAP(FPUV3_DOUBLE_EXT_ARITH),
	SOP_MAP(FPUV3_CONVERT),
	SOP_MAP(FPUV3_TRANSFER),
	SOP_MAP(FPUV3_LD),
	SOP_MAP(FPUV3_ST),
	SOP_MAP(FPUV3_LD_REG),
	SOP_MAP(FPUV3_ST_REG),
	SOP_MAP(FPUV3_LD_MEM),
	SOP_MAP(FPUV3_ST_MEM),
	SOP_MAP(FPUV3_CONVERT_F_IX),
	SOP_MAP(FPUV3_CONVERT_F_FI),
	SOP_MAP(FPUV3_CONVERT_IX_F),
	SOP_MAP(FPUV3_MOVI),
};

struct insn_op_array fpu_vfp_insn[FPUV3_OP_MAX] = { [FPUV3_SOP] = {
							    sop_insn } };
