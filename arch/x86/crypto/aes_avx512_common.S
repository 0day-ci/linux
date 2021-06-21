/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright © 2021 Intel Corporation.
 *
 * Collection of macros which are shared between AVX512 versions of
 * AESNI CTR and GCM algorithms.
 */

#include <linux/linkage.h>
#include <asm/inst.h>

/*
 * Generic macro to produce code that executes OPCODE instruction
 * on selected number of AES blocks (16 bytes long ) between 0 and 16.
 * All three operands of the instruction come from registers.
 */
#define ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16(NUM_BLOCKS, OPCODE, DST0, DST1, DST2, DST3, SRC1_0, SRC1_1, SRC1_2, SRC1_3, SRC2_0, SRC2_1, SRC2_2, SRC2_3) \
.set blocks_left,NUM_BLOCKS;						\
.if NUM_BLOCKS < 4;							\
	.if blocks_left == 1;						\
		OPCODE	XWORD(SRC2_0), XWORD(SRC1_0), XWORD(DST0);	\
	.elseif blocks_left == 2;					\
		OPCODE	YWORD(SRC2_0), YWORD(SRC1_0), YWORD(DST0);	\
	.elseif blocks_left == 3;					\
		OPCODE	SRC2_0, SRC1_0, DST0;				\
	.endif;								\
.elseif NUM_BLOCKS >= 4 && NUM_BLOCKS < 8;				\
	OPCODE	SRC2_0, SRC1_0, DST0;					\
	.set blocks_left, blocks_left - 4;				\
	.if blocks_left == 1;						\
		OPCODE	XWORD(SRC2_1), XWORD(SRC1_1), XWORD(DST1);	\
	.elseif blocks_left == 2;					\
		OPCODE	YWORD(SRC2_1), YWORD(SRC1_1), YWORD(DST1);	\
	.elseif blocks_left == 3;					\
		OPCODE	SRC2_1, SRC1_1, DST1;				\
	.endif;								\
.elseif NUM_BLOCKS >= 8 && NUM_BLOCKS < 12;				\
	OPCODE	SRC2_0, SRC1_0, DST0;					\
	.set blocks_left, blocks_left - 4;				\
	OPCODE	SRC2_1, SRC1_1, DST1;					\
	.set blocks_left, blocks_left - 4;				\
	.if blocks_left == 1;						\
		OPCODE	XWORD(SRC2_2), XWORD(SRC1_2), XWORD(DST2);	\
	.elseif blocks_left == 2;					\
		OPCODE	YWORD(SRC2_2), YWORD(SRC1_2), YWORD(DST2);	\
	.elseif blocks_left == 3;					\
		OPCODE	SRC2_2, SRC1_2, DST2;				\
	.endif;								\
.elseif NUM_BLOCKS >= 12 && NUM_BLOCKS < 16;				\
	OPCODE	SRC2_0, SRC1_0, DST0;					\
	.set blocks_left, blocks_left - 4;				\
	OPCODE	SRC2_1, SRC1_1, DST1;					\
	.set blocks_left, blocks_left - 4;				\
	OPCODE	SRC2_2, SRC1_2, DST2;					\
	.set blocks_left, blocks_left - 4;				\
	.if blocks_left == 1;						\
		OPCODE	XWORD(SRC2_3), XWORD(SRC1_3), XWORD(DST3);	\
	.elseif blocks_left == 2;					\
		OPCODE	YWORD(SRC2_3), YWORD(SRC1_3), YWORD(DST3);	\
	.elseif blocks_left == 3;					\
		OPCODE	SRC2_3, SRC1_3, DST3;				\
	.endif;								\
.else;									\
	OPCODE	SRC2_0, SRC1_0, DST0;					\
	.set blocks_left, blocks_left - 4;				\
	OPCODE	SRC2_1, SRC1_1, DST1;					\
	.set blocks_left, blocks_left - 4;				\
	OPCODE	SRC2_2, SRC1_2, DST2;					\
	.set blocks_left, blocks_left - 4;				\
	OPCODE	SRC2_3, SRC1_3, DST3;					\
	.set blocks_left, blocks_left - 4;				\
.endif;

/*
 * Handles AES encryption rounds. It handles special cases: the last and
 * first rounds. Optionally, it performs XOR with data after the last AES
 * round. Uses NROUNDS parameter to check what needs to be done for the
 * current round.
 */
#define ZMM_AESENC_ROUND_BLOCKS_0_16(L0B0_3, L0B4_7, L0B8_11, L0B12_15, KEY, ROUND, D0_3, D4_7, D8_11, D12_15, NUMBL, NROUNDS) \
.if ROUND < 1;				\
	ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16(NUMBL, vpxorq, L0B0_3, L0B4_7, L0B8_11, L0B12_15, L0B0_3, L0B4_7, L0B8_11, L0B12_15, KEY, KEY, KEY, KEY)	\
.endif;					\
.if (ROUND >= 1) && (ROUND <= NROUNDS);	\
	ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16(NUMBL, vaesenc, L0B0_3, L0B4_7, L0B8_11, L0B12_15, L0B0_3, L0B4_7, L0B8_11, L0B12_15, KEY, KEY, KEY, KEY)	\
.endif;					\
.if ROUND > NROUNDS;			\
	ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16(NUMBL, vaesenclast, L0B0_3, L0B4_7, L0B8_11, L0B12_15, L0B0_3, L0B4_7, L0B8_11, L0B12_15, KEY, KEY, KEY, KEY)	\
	.ifnc D0_3, no_data;		\
	.ifnc D4_7, no_data;		\
	.ifnc D8_11, no_data;		\
	.ifnc D12_15, no_data;		\
	ZMM_OPCODE3_DSTR_SRC1R_SRC2R_BLOCKS_0_16(NUMBL, vpxorq, L0B0_3, L0B4_7, L0B8_11, L0B12_15, L0B0_3, L0B4_7, L0B8_11, L0B12_15, D0_3, D4_7, D8_11, D12_15)	\
	.endif;				\
	.endif;				\
	.endif;				\
	.endif;				\
.endif;

/*
 * Loads specified number of AES blocks into ZMM registers using mask register
 * for the last loaded register (xmm, ymm or zmm). Loads take place at 1 byte
 * granularity.
 */
#define ZMM_LOAD_MASKED_BLOCKS_0_16(NUM_BLOCKS, INP, DATA_OFFSET, DST0, DST1, DST2, DST3, MASK)	\
.set src_offset,0;						\
.set blocks_left, NUM_BLOCKS;					\
.if NUM_BLOCKS <= 4;						\
	.if blocks_left == 1;					\
		vmovdqu8	src_offset(INP, DATA_OFFSET), XWORD(DST0){MASK}{z};	\
	.elseif blocks_left == 2;				\
		vmovdqu8	src_offset(INP, DATA_OFFSET), YWORD(DST0){MASK}{z};	\
	.elseif (blocks_left == 3 || blocks_left == 4);		\
		vmovdqu8	src_offset(INP, DATA_OFFSET), DST0{MASK}{z};		\
	.endif;							\
.elseif NUM_BLOCKS > 4 && NUM_BLOCKS <= 8;			\
	vmovdqu8	src_offset(INP, DATA_OFFSET), DST0;	\
	.set blocks_left, blocks_left - 4;			\
	.set src_offset, src_offset + 64;			\
	.if blocks_left == 1;					\
		vmovdqu8	src_offset(INP, DATA_OFFSET), XWORD(DST1){MASK}{z};	\
	.elseif blocks_left == 2;				\
		vmovdqu8	src_offset(INP, DATA_OFFSET), YWORD(DST1){MASK}{z};	\
	.elseif (blocks_left == 3 || blocks_left == 4);		\
		vmovdqu8	src_offset(INP, DATA_OFFSET), DST1{MASK}{z};		\
	.endif;							\
.elseif NUM_BLOCKS > 8 && NUM_BLOCKS <= 12;			\
	vmovdqu8	src_offset(INP, DATA_OFFSET), DST0;	\
	.set blocks_left, blocks_left - 4;			\
	.set src_offset, src_offset + 64;			\
	vmovdqu8	src_offset(INP, DATA_OFFSET), DST1;	\
	.set blocks_left, blocks_left - 4;			\
	.set src_offset, src_offset + 64;			\
	.if blocks_left == 1;					\
		vmovdqu8	src_offset(INP, DATA_OFFSET), XWORD(DST2){MASK}{z};	\
	.elseif blocks_left == 2;				\
		vmovdqu8	src_offset(INP, DATA_OFFSET), YWORD(DST2){MASK}{z};	\
	.elseif (blocks_left == 3 || blocks_left == 4);		\
		vmovdqu8	src_offset(INP, DATA_OFFSET), DST2{MASK}{z};		\
	.endif;							\
.else;								\
	vmovdqu8	src_offset(INP, DATA_OFFSET), DST0;	\
	.set blocks_left, blocks_left - 4;			\
	.set src_offset, src_offset + 64;			\
	vmovdqu8	src_offset(INP, DATA_OFFSET), DST1;	\
	.set blocks_left, blocks_left - 4;			\
	.set src_offset, src_offset + 64;			\
	vmovdqu8	src_offset(INP, DATA_OFFSET), DST2;	\
	.set blocks_left, blocks_left - 4;			\
	.set src_offset, src_offset + 64;			\
	.if blocks_left == 1;					\
		vmovdqu8	src_offset(INP, DATA_OFFSET), XWORD(DST3){MASK}{z};	\
	.elseif blocks_left == 2;				\
		vmovdqu8	src_offset(INP, DATA_OFFSET), YWORD(DST3){MASK}{z};	\
	.elseif (blocks_left == 3 || blocks_left == 4);		\
		vmovdqu8	src_offset(INP, DATA_OFFSET), DST3{MASK}{z};		\
	.endif;							\
.endif;

/*
 * Stores specified number of AES blocks from ZMM registers with mask register
 * for the last loaded register (xmm, ymm or zmm). Stores take place at 1 byte
 * granularity.
 */
#define ZMM_STORE_MASKED_BLOCKS_0_16(NUM_BLOCKS, OUTP, DATA_OFFSET, SRC0, SRC1, SRC2, SRC3, MASK) \
.set blocks_left, NUM_BLOCKS;								\
.set dst_offset, 0;									\
.if NUM_BLOCKS <= 4;									\
	.if blocks_left == 1;								\
		vmovdqu8	XWORD(SRC0), dst_offset(OUTP, DATA_OFFSET){MASK};	\
	.elseif blocks_left == 2;							\
		vmovdqu8	YWORD(SRC0), dst_offset(OUTP, DATA_OFFSET){MASK};	\
	.elseif (blocks_left == 3 || blocks_left == 4);					\
		vmovdqu8	SRC0, dst_offset(OUTP, DATA_OFFSET){MASK};		\
	.endif;										\
.elseif NUM_BLOCKS > 4 && NUM_BLOCKS <=8;						\
	vmovdqu8	SRC0, dst_offset(OUTP, DATA_OFFSET);				\
	.set blocks_left, blocks_left - 4;						\
	.set dst_offset, dst_offset + 64;						\
	.if blocks_left == 1;								\
		vmovdqu8	XWORD(SRC1), dst_offset(OUTP, DATA_OFFSET){MASK};	\
	.elseif blocks_left == 2;							\
		vmovdqu8	YWORD(SRC1), dst_offset(OUTP, DATA_OFFSET){MASK};	\
	.elseif (blocks_left == 3 || blocks_left == 4);					\
		vmovdqu8	SRC1, dst_offset(OUTP, DATA_OFFSET){MASK};		\
	.endif;										\
.elseif NUM_BLOCKS > 8 && NUM_BLOCKS <= 12;						\
	vmovdqu8	SRC0, dst_offset(OUTP, DATA_OFFSET);				\
	.set blocks_left, blocks_left - 4;						\
	.set dst_offset, dst_offset + 64;						\
	vmovdqu8	SRC1, dst_offset(OUTP, DATA_OFFSET);				\
	.set blocks_left, blocks_left - 4;						\
	.set dst_offset, dst_offset + 64;						\
	.if blocks_left == 1;								\
		vmovdqu8	XWORD(SRC2), dst_offset(OUTP, DATA_OFFSET){MASK};	\
	.elseif blocks_left == 2;							\
		vmovdqu8	YWORD(SRC2), dst_offset(OUTP, DATA_OFFSET){MASK};	\
	.elseif (blocks_left == 3 || blocks_left == 4);					\
		vmovdqu8	SRC2, dst_offset(OUTP, DATA_OFFSET){MASK};		\
	.endif;										\
.else;											\
	vmovdqu8	SRC0, dst_offset(OUTP, DATA_OFFSET);				\
	.set blocks_left, blocks_left - 4;						\
	.set dst_offset, dst_offset + 64;						\
	vmovdqu8	SRC1, dst_offset(OUTP, DATA_OFFSET);				\
	.set blocks_left, blocks_left - 4;						\
	.set dst_offset, dst_offset + 64;						\
	vmovdqu8	SRC2, dst_offset(OUTP, DATA_OFFSET);				\
	.set blocks_left, blocks_left - 4;						\
	.set dst_offset, dst_offset + 64;						\
	.if blocks_left == 1;								\
		vmovdqu8	XWORD(SRC3), dst_offset(OUTP, DATA_OFFSET){MASK};	\
	.elseif blocks_left == 2;							\
		vmovdqu8	YWORD(SRC3), dst_offset(OUTP, DATA_OFFSET){MASK};	\
	.elseif (blocks_left == 3 || blocks_left == 4);					\
		vmovdqu8	SRC3, dst_offset(OUTP, DATA_OFFSET){MASK};		\
	.endif;										\
.endif;

/* Loads specified number of AES blocks into ZMM registers */
#define ZMM_LOAD_BLOCKS_0_16(NUM_BLOCKS, INP, DATA_OFFSET, DST0, DST1, DST2, DST3, FLAGS) \
.set src_offset, 0;									\
.set blocks_left, NUM_BLOCKS % 4;							\
.if NUM_BLOCKS < 4;									\
	.if blocks_left == 1;								\
		vmovdqu8	src_offset(INP, DATA_OFFSET), XWORD(DST0);      	\
	.elseif blocks_left == 2;							\
		vmovdqu8	src_offset(INP, DATA_OFFSET), YWORD(DST0);      	\
	.elseif blocks_left == 3;							\
		.ifc FLAGS, load_4_instead_of_3;					\
			vmovdqu8	src_offset(INP, DATA_OFFSET), DST0;     	\
		.else;									\
			vmovdqu8	src_offset(INP, DATA_OFFSET), YWORD(DST0);	\
			vinserti64x2    $2, src_offset + 32(INP, DATA_OFFSET), DST0, DST0;	\
		.endif;									\
	.endif;										\
.elseif NUM_BLOCKS >= 4 && NUM_BLOCKS < 8;						\
	vmovdqu8	src_offset(INP, DATA_OFFSET), DST0;				\
	.set src_offset, src_offset + 64;						\
	.if blocks_left == 1;								\
		vmovdqu8	src_offset(INP, DATA_OFFSET), XWORD(DST1);		\
	.elseif blocks_left == 2;							\
		vmovdqu8	src_offset(INP, DATA_OFFSET), YWORD(DST1);		\
	.elseif blocks_left == 3;							\
		.ifc FLAGS, load_4_instead_of_3;					\
			vmovdqu8	src_offset(INP, DATA_OFFSET), DST1;		\
		.else;									\
			vmovdqu8	src_offset(INP, DATA_OFFSET), YWORD(DST1);	\
			vinserti64x2	$2, src_offset + 32(INP, DATA_OFFSET), DST1, DST1;	\
		.endif;									\
	.endif;										\
.elseif NUM_BLOCKS >= 8 && NUM_BLOCKS < 12;						\
	vmovdqu8	src_offset(INP, DATA_OFFSET), DST0;				\
	.set src_offset, src_offset + 64;						\
	vmovdqu8	src_offset(INP, DATA_OFFSET), DST1;				\
	.set src_offset, src_offset + 64;						\
	.if blocks_left == 1;								\
		vmovdqu8	src_offset(INP, DATA_OFFSET), XWORD(DST2);		\
	.elseif blocks_left == 2;							\
		vmovdqu8	src_offset(INP, DATA_OFFSET), YWORD(DST2);		\
	.elseif blocks_left == 3;							\
		.ifc FLAGS, load_4_instead_of_3;					\
		vmovdqu8	src_offset(INP, DATA_OFFSET), DST2;			\
		.else;									\
		vmovdqu8	src_offset(INP, DATA_OFFSET), YWORD(DST2);		\
		vinserti64x2	$2, src_offset + 32(INP, DATA_OFFSET), DST2, DST2;	\
		.endif;									\
       .endif;										\
.else;											\
	vmovdqu8	src_offset(INP, DATA_OFFSET), DST0;				\
	.set src_offset, src_offset + 64;			      			\
	vmovdqu8	src_offset(INP, DATA_OFFSET), DST1;				\
	.set src_offset, src_offset + 64;						\
	vmovdqu8	src_offset(INP, DATA_OFFSET), DST2;				\
	.set src_offset, src_offset + 64;						\
	.if blocks_left == 1;								\
		vmovdqu8	src_offset(INP, DATA_OFFSET), XWORD(DST3);		\
	.elseif blocks_left == 2;							\
		vmovdqu8	src_offset(INP, DATA_OFFSET), YWORD(DST3);		\
	.elseif blocks_left == 3;							\
		.ifc FLAGS, load_4_instead_of_3;					\
		vmovdqu8	src_offset(INP, DATA_OFFSET), DST3;			\
		.else;									\
		vmovdqu8	src_offset(INP, DATA_OFFSET), YWORD(DST3);		\
		vinserti64x2    $2, src_offset + 32(INP, DATA_OFFSET), DST3, DST3;	\
		.endif;									\
	.endif;										\
.endif;

/* Stores specified number of AES blocks from ZMM registers */
#define ZMM_STORE_BLOCKS_0_16(NUM_BLOCKS, OUTP, DATA_OFFSET, SRC0, SRC1, SRC2, SRC3)	\
.set dst_offset, 0;								\
.set blocks_left, NUM_BLOCKS % 4;						\
.if NUM_BLOCKS < 4;								\
	.if blocks_left == 1;							\
		vmovdqu8	XWORD(SRC0), dst_offset(OUTP, DATA_OFFSET);	\
	.elseif blocks_left == 2;						\
		vmovdqu8	YWORD(SRC0), dst_offset(OUTP, DATA_OFFSET);	\
	.elseif blocks_left == 3;						\
		vmovdqu8	YWORD(SRC0), dst_offset(OUTP, DATA_OFFSET);	\
		vextracti32x4	$2, SRC0, dst_offset + 32(OUTP, DATA_OFFSET);	\
	.endif;									\
.elseif NUM_BLOCKS >= 4 && NUM_BLOCKS < 8;					\
	vmovdqu8	SRC0, dst_offset(OUTP, DATA_OFFSET);			\
	.set dst_offset, dst_offset + 64;					\
	.if blocks_left == 1;							\
		vmovdqu8	XWORD(SRC1), dst_offset(OUTP, DATA_OFFSET);	\
	.elseif blocks_left == 2;						\
		vmovdqu8	YWORD(SRC1), dst_offset(OUTP, DATA_OFFSET);	\
	.elseif blocks_left == 3;						\
		vmovdqu8	YWORD(SRC1), dst_offset(OUTP, DATA_OFFSET);	\
		vextracti32x4	$2, SRC1, dst_offset + 32(OUTP, DATA_OFFSET);	\
	.endif;									\
.elseif NUM_BLOCKS >= 8 && NUM_BLOCKS < 12;					\
	vmovdqu8	SRC0, dst_offset(OUTP, DATA_OFFSET);			\
	.set dst_offset, dst_offset + 64;					\
	vmovdqu8	SRC1, dst_offset(OUTP, DATA_OFFSET);			\
	.set dst_offset, dst_offset + 64;					\
	.if blocks_left == 1;							\
		vmovdqu8	XWORD(SRC2), dst_offset(OUTP, DATA_OFFSET);     \
	.elseif blocks_left == 2;						\
		vmovdqu8	YWORD(SRC2), dst_offset(OUTP, DATA_OFFSET);	\
	.elseif blocks_left == 3;						\
		vmovdqu8	YWORD(SRC2), dst_offset(OUTP, DATA_OFFSET);	\
		vextracti32x4   $2, SRC2, dst_offset + 32(OUTP, DATA_OFFSET);	\
	.endif;									\
.else;										\
	vmovdqu8	SRC0, dst_offset(OUTP, DATA_OFFSET);			\
	.set dst_offset, dst_offset + 64;					\
	vmovdqu8	SRC1, dst_offset(OUTP, DATA_OFFSET);			\
	.set dst_offset, dst_offset + 64;					\
	vmovdqu8	SRC2, dst_offset(OUTP, DATA_OFFSET);			\
	.set dst_offset, dst_offset + 64;					\
	.if blocks_left == 1;							\
		vmovdqu8	XWORD(SRC3), dst_offset(OUTP, DATA_OFFSET);	\
	.elseif blocks_left == 2;						\
		vmovdqu8	YWORD(SRC3), dst_offset(OUTP, DATA_OFFSET);	\
	.elseif blocks_left == 3;						\
		vmovdqu8	YWORD(SRC3), dst_offset(OUTP, DATA_OFFSET);	\
		vextracti32x4	$2, SRC3, dst_offset + 32(OUTP, DATA_OFFSET);	\
	.endif;									\
.endif;
