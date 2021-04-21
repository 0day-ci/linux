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
#ifndef __CSKY_FP860_H__
#define __CSKY_FP860_H__

#include "math.h"

/* FPUv3 ISA OP Pcode MASK and SHIFT */
#define FPUV3_REG_MASK 0x1f
#define FPUV3_REG_SHI_RX 16
#define FPUV3_REG_SHI_RY 21
#define FPUV3_REG_SHI_RZ 0
#define FPUV3_VREG_MASK 0x1f
#define FPUV3_VREG_SHI_VRX 16
#define FPUV3_VREG_SHI_VRY 21
#define FPUV3_VREG_SHI_VRZ 0

#define FPUV3_OP_MASK 0xf
#define FPUV3_OP_SHI 26
#define FPUV3_SOP_MASK 0x3f
#define FPUV3_SOP_SHI 10
#define FPUV3_PCODE_MASK 0x1f
#define FPUV3_PCODE_SHI 0x5

#define FPUV3_IMM4_MASK 0xf
#define FPUV3_IMM4H_SHI 0x11
#define FPUV3_IMM4L_SHI 0x4
#define FPUV3_IMM2_MASK 0x3
#define FPUV3_IMM2_SHI 0x5

#define CSKY_INSN_VRX(x) ((x >> FPUV3_VREG_SHI_VRX) & FPUV3_VREG_MASK)
#define CSKY_INSN_VRY(x) ((x >> FPUV3_VREG_SHI_VRY) & FPUV3_VREG_MASK)
#define CSKY_INSN_VRZ(x) ((x >> FPUV3_VREG_SHI_VRZ) & FPUV3_VREG_MASK)

#define FPUV3_IMM8L_MASK (FPUV3_IMM4_MASK)
#define FPUV3_IMM8H_MASK (FPUV3_IMM4_MASK << FPUV3_IMM4L_SHI)
#define FPUV3_IMM8L(x) ((x >> FPUV3_IMM4L_SHI) & FPUV3_IMM8L_MASK)
#define FPUV3_IMM8H(x) ((x >> FPUV3_IMM4H_SHI) & FPUV3_IMM8H_MASK)
#define FPUV3_IMM8(x) (FPUV3_IMM8H(x) | FPUV3_IMM8L(x))

#define FPUV3_IMM2(x) ((x >> FPUV3_IMM2_SHI) & FPUV3_IMM2_MASK)

#define CSKY_INSN_OP(x) ((x >> FPUV3_OP_SHI) & FPUV3_OP_MASK)
#define CSKY_INSN_SOP(x) ((x >> FPUV3_SOP_SHI) & FPUV3_SOP_MASK)
#define CSKY_INSN_PCODE(x) ((x >> FPUV3_PCODE_SHI) & FPUV3_PCODE_MASK)

#define FPUV3_FLOAT_ARITH 0x0
#define FPUV3_FADDS 0x0
#define FPUV3_FSUBS 0x1
#define FPUV3_FMOVS 0x4
#define FPUV3_FABSS 0x6
#define FPUV3_FNEGS 0x7
#define FPUV3_FCMPZHSS 0x8
#define FPUV3_FCMPZLTS 0x9
#define FPUV3_FCMPNEZS 0xA
#define FPUV3_FCMPZUOS 0xB
#define FPUV3_FCMPHSS 0xC
#define FPUV3_FCMPLTS 0xD
#define FPUV3_FCMPNES 0xE
#define FPUV3_FCMPUOS 0xF
#define FPUV3_FMULS 0x10
#define FPUV3_FNMULS 0x11
#define FPUV3_FMACS 0x14
#define FPUV3_FMSCS 0x15
#define FPUV3_FNMACS 0x16
#define FPUV3_FNMSCS 0x17
#define FPUV3_FDIVS 0x18
#define FPUV3_FRECIPS 0x19
#define FPUV3_FSQRTS 0x1A
#define FPUV3_FINSS 0x1B

#define FPUV3_FLOAT_EXT_ARITH 0x1
#define FPUV3_FMAXNMS 0x8
#define FPUV3_FMINNMS 0x9
#define FPUV3_FCMPHZS 0xA
#define FPUV3_FCMPLSZS 0xB
#define FPUV3_FFMULAS 0x10
#define FPUV3_FFMULSS 0x11
#define FPUV3_FFNMULAS 0x12
#define FPUV3_FFNMULSS 0x13
#define FPUV3_FSELS 0x19

#define FPUV3_DOUBLE_ARITH 0x2
#define FPUV3_FADDD 0x0
#define FPUV3_FSUBD 0x1
#define FPUV3_FMOVD 0x4
#define FPUV3_FMOVXS 0x5
#define FPUV3_FABSD 0x6
#define FPUV3_FNEGD 0x7
#define FPUV3_FCMPZHSD 0x8
#define FPUV3_FCMPZLTD 0x9
#define FPUV3_FCMPZNED 0xA
#define FPUV3_FCMPZUOD 0xB
#define FPUV3_FCMPHSD 0xC
#define FPUV3_FCMPLTD 0xD
#define FPUV3_FCMPNED 0xE
#define FPUV3_FCMPUOD 0xF
#define FPUV3_FMULD 0x10
#define FPUV3_FNMULD 0x11
#define FPUV3_FMACD 0x14
#define FPUV3_FMSCD 0x15
#define FPUV3_FNMACD 0x16
#define FPUV3_FNMSCS 0x17
#define FPUV3_FDIVD 0x18
#define FPUV3_FRECIPD 0x19
#define FPUV3_FSQRTD 0x1A

#define FPUV3_DOUBLE_EXT_ARITH 0x3
#define FPUV3_FMAXNMD 0x8
#define FPUV3_FMINNMD 0x9
#define FPUV3_FCMPHZD 0xA
#define FPUV3_FCMPLSZD 0xB
#define FPUV3_FFMULAD 0x10
#define FPUV3_FFMULSD 0x11
#define FPUV3_FFNMULAD 0x12
#define FPUV3_FFNMULSD 0x13
#define FPUV3_FSELD 0x19

#define FPUV3_CONVERT 0x6
#define FPUV3_FSTOSI_RN 0x0
#define FPUV3_FSTOSI_RZ 0x1
#define FPUV3_FSTOSI_RPI 0x2
#define FPUV3_FSTOSI_RNI 0x3
#define FPUV3_FSTOUI_RN 0x4
#define FPUV3_FSTOUI_RZ 0x5
#define FPUV3_FSTOUI_RPI 0x6
#define FPUV3_FSTOUI_RNI 0x7
#define FPUV3_FDTOSI_RN 0x8
#define FPUV3_FDTOSI_RZ 0x9
#define FPUV3_FDTOSI_RPI 0xA
#define FPUV3_FDTOSI_RNI 0xB
#define FPUV3_FDTOUI_RN 0xC
#define FPUV3_FDTOUI_RZ 0xD
#define FPUV3_FDTOUI_RPI 0xE
#define FPUV3_FDTOUI_RNI 0xF
#define FPUV3_FSITOS 0x10
#define FPUV3_FUITOS 0x11
#define FPUV3_FSITOD 0x14
#define FPUV3_FIOTOD 0x15
#define FPUV3_FDTOS 0x16
#define FPUV3_FSTOD 0x17
#define FPUV3_FMTVRH 0x1A
#define FPUV3_FMTVRL 0x1B
#define FPUV3_FMFVRH 0x18
#define FPUV3_FMFVRL 0x19

#define FPUV3_TRANSFER 0x7
#define FPUV3_FMFVRD 0x18
#define FPUV3_FMFVRL2 0x1A
#define FPUV3_FMTVRD 0x1C
#define FPUV3_FMTVRL2 0x1E

#define FPUV3_LD 0x8
#define FPUV3_FLDS_MIN 0x0
#define FPUV3_FLDS_MAX 0x7
#define FPUV3_FLDD_MIN 0x8
#define FPUV3_FLDD_MAX 0xF

#define FPUV3_ST 0x9
#define FPUV3_FSTS_MIN 0x0
#define FPUV3_FSTS_MAX 0x7
#define FPUV3_FSTD_MIN 0x8
#define FPUV3_FSTD_MAX 0xF

#define FPUV3_LD_REG 0xA
#define FPUV3_FLDRS_MIN 0x0
#define FPUV3_FLDRS_MAX 0x3
#define FPUV3_FLDRD_MIN 0x8
#define FPUV3_FLDRD_MAX 0xB

#define FPUV3_ST_REG 0xB
#define FPUV3_FSTRS_MIN 0x0
#define FPUV3_FSTRS_MAX 0x3
#define FPUV3_FSTRD_MIN 0x8
#define FPUV3_FSTRD_MAX 0xB

#define FPUV3_LD_MEM 0xC
#define FPUV3_FLDMS 0x0
#define FPUV3_FLDMD 0x8
#define FPUV3_FLDMUS 0x4
#define FPUV3_FLDMUD 0xC

#define FPUV3_ST_MEM 0xD
#define FPUV3_FSTMS 0x0
#define FPUV3_FSTMD 0x8
#define FPUV3_FSTMUS 0x4
#define FPUV3_FSTMUD 0xC

#define FPUV3_CONVERT_F_IX 0x10
#define FPUV3_FFTOX_F32U32 0xA
#define FPUV3_FFTOX_F32S32 0xB
#define FPUV3_FFTOX_F64U32 0xC
#define FPUV3_FFTOX_F64S32 0xD
#define FPUV3_FFTOI_F32U32 0x1A
#define FPUV3_FFTOI_F32S32 0x1B
#define FPUV3_FFTOI_F64U32 0x1C
#define FPUV3_FFTOI_F64S32 0x1D

#define FPUV3_CONVERT_F_FI 0x11
#define FPUV3_FFTOFI_FS_RN 0x4
#define FPUV3_FFTOFI_FS_RZ 0x5
#define FPUV3_FFTOFI_FS_RPI 0x6
#define FPUV3_FFTOFI_FS_RNI 0x7
#define FPUV3_FFTOFI_FD_RN 0x8
#define FPUV3_FFTOFI_FD_RZ 0x9
#define FPUV3_FFTOFI_FD_RPI 0xA
#define FPUV3_FFTOFI_FD_RNI 0xB

#define FPUV3_CONVERT_IX_F 0x12
#define FPUV3_FXTOF_U32F32 0xA
#define FPUV3_FXTOF_S32F32 0xB
#define FPUV3_FXTOF_U32F64 0xC
#define FPUV3_FXTOF_S32F64 0xD
#define FPUV3_FITOF_U32F32 0x1A
#define FPUV3_FITOF_S32F32 0x1B
#define FPUV3_FITOF_U32F64 0x1C
#define FPUV3_FITOF_S32F64 0x1D

#define FPUV3_MOVI 0x39
#define FPUV3_MOVI_T0 0x0
#define FPUV3_MOVI_T1 0xF
#define FPUV3_MOVI_T2 0x10
#define FPUV3_MOVI_T3 0x1F

#define FPU_OP_NAME(name) fp860_##name

union emu_func {
	void (*fpu)(int x, int y, int z, struct inst_data *inst_data);
};

struct insn_pcode_array {
	union emu_func func;
};

struct insn_sop_array {
	struct insn_pcode_array *pcode;
};

struct insn_op_array {
	struct insn_sop_array *sop;
};

extern struct insn_op_array fpu_vfp_insn[];

#define FPUV3_OP_FUNC(insn)                                                    \
	FPU_OP_NAME(insn)(int x, int y, int z, struct inst_data *inst_data)

#endif
