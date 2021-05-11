/* SPDX-License-Identifier: GPL-2.0 */

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
