/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Accelerated GCM implementation with ARMv8 PMULL instructions
 * and unroll factors.
 *
 * Copyright (C) 2021  Arm.ltd. <xiaokang.qian@arm.com>
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

.arch	armv8-a+crypto
.text

.macro push_stack
	stp	x19, x20, [sp, #-128]!
	stp	x21, x22, [sp, #16]
	stp	x23, x24, [sp, #32]
	stp	x25, x26, [sp, #48]
	stp	d8, d9, [sp, #64]
	stp	d10, d11, [sp, #80]
	stp	d12, d13, [sp, #96]
	stp	d14, d15, [sp, #112]
.endm

.macro pop_stack
	ldp	x21, x22, [sp, #16]
	ldp	x23, x24, [sp, #32]
	ldp	x25, x26, [sp, #48]
	ldp	d8, d9, [sp, #64]
	ldp	d10, d11, [sp, #80]
	ldp	d12, d13, [sp, #96]
	ldp	d14, d15, [sp, #112]
	ldp	x19, x20, [sp], #128
.endm

.macro load_const
	movi	v8.8b, #0xc2
	shl	d8, d8, #56               //mod_constant
.endm

.macro gcm_tidy_up high:req, mid:req, low:req, tmp1:req, tmp2:req
	eor	\tmp1\().16b, \low\().16b, \high\().16b //MODULO-karatsuba tidy up
	eor	\mid\().16b, \mid\().16b, \tmp1\().16b  //MODULO-karatsuba tidy up
	pmull	\tmp2\().1q, \high\().1d, v8.1d
	ext	\high\().16b, \high\().16b, \high\().16b, #8
	eor	\mid\().16b, \mid\().16b, \tmp2\().16b //MODULO - fold into mid
	eor	\mid\().16b, \mid\().16b, \high\().16b //MODULO - fold into mid
	pmull	\high\().1q, \mid\().1d, v8.1d  //MODULO - mid 64b align with low
	ext	\mid\().16b, \mid\().16b, \mid\().16b, #8
	eor	\low\().16b, \low\().16b, \high\().16b //MODULO - fold into low
	eor	\low\().16b, \low\().16b, \mid\().16b //MODULO - fold into low
.endm

.macro karasuba_multiply res:req, h:req, tmp1:req, tmp2:req, tmp3:req
	pmull	\tmp1\().1q, \res\().1d, \h\().1d    //GHASH final block - low
	eor	\tmp2\().8b, \tmp2\().8b, \res\().8b //GHASH final block - mid
	pmull2	\tmp3\().1q, \res\().2d, \h\().2d    //GHASH final block - high
	pmull	\tmp2\().1q, \tmp2\().1d, v16.1d     //GHASH final block - mid
	eor	v11.16b, v11.16b, \tmp1\().16b       //GHASH final block - low
	eor	v9.16b, v9.16b, \tmp3\().16b         //GHASH final block - high
	eor	v10.16b, v10.16b, \tmp2\().16b       //GHASH final block - mid
.endm

.macro aes_encrypt_round    block:req,key:req
	aese	\block\().16b,\key\().16b
	aesmc	\block\().16b,\block\().16b
.endm

.macro aes_enc_extra_round rd_num:req
	.if \rd_num == 12
	add     x19,x8,#176
	aes_encrypt_round	v0, v27         //AES block 0 - round 9
	aes_encrypt_round	v3, v27         //AES block 3 - round 9
	aes_encrypt_round	v2, v27         //AES block 2 - round 9
	aes_encrypt_round	v1, v27         //AES block 1 - round 9
	ldr	q27, [x19],#16                  //load rk9
	aes_encrypt_round	v0, v28         //AES block 0 - round 10
	aes_encrypt_round	v2, v28         //AES block 2 - round 10
	aes_encrypt_round	v1, v28         //AES block 1 - round 10
	aes_encrypt_round	v3, v28         //AES block 3 - round 10
	ldr	q28, [x19],#16                  //load rk10
	.elseif \rd_num == 14
	aes_encrypt_round	v1, v27          //AES block 1 - round 11
	aes_encrypt_round	v2, v27          //AES block 2 - round 11
	aes_encrypt_round	v0, v27          //AES block 0 - round 11
	aes_encrypt_round	v3, v27          //AES block 3 - round 11
	ldr	q27, [x19],#16                   //load rk9
	aes_encrypt_round	v1, v28          //AES block 1 - round 12
	aes_encrypt_round	v2, v28          //AES block 2 - round 12
	aes_encrypt_round	v0, v28          //AES block 0 - round 12
	aes_encrypt_round	v3, v28          //AES block 3 - round 12
	ldr	q28, [x19],#16                   //load rk10
	.endif
	fmov	x13, d28                         //load last second block
	fmov	x14, v28.d[1]                    //load last second block
.endm

.macro aes_enc_iv_init
	ldr	q18, [x8, #0]                   //load rk0
	ldr	q19, [x8, #16]                   //load rk1
	mov	w11, #(0x1 << 24)		// BE '1U'
	ld1	{v0.16b}, [x25]
	mov	v0.s[3], w11
	aes_encrypt_round	v0, v18         //AES block 0 - round 0
	ldr	q20, [x8, #32]                   //load rk2
	aes_encrypt_round	v0, v19         //AES block 0 - round 1
	ldr	q21, [x8, #48]                  //load rk3
	aes_encrypt_round	v0, v20         //AES block 0 - round 2
	ldr	q22, [x8, #64]                  //load rk4
	aes_encrypt_round	v0, v21         //AES block 0 - round 3
	ldr	q23, [x8, #80]                  //load rk5
	aes_encrypt_round	v0, v22          //AES block 0 - round 4
	ldr	q24, [x8, #96]                  //load rk6
	aes_encrypt_round	v0, v23          //AES block 0 - round 5
	ldr	q25, [x8, #112]                  //load rk7
	aes_encrypt_round	v0, v24          //AES block 0 - round 6
	ldr	q26, [x8, #128]                  //load rk8
	aes_encrypt_round	v0, v25          //AES block 0 - round 7
	ldr	q27, [x8, #144]                  //load rk9
.endm

.macro aes_enc_iv_common rd_num:req
	.if \rd_num == 12
	aes_encrypt_round	v0, v26          //AES block 0 - round 8
	ldr	q26, [x19],#16                  //load rk10
	aes_encrypt_round	v0, v27         //AES block 0 - round 9
	ldr	q27, [x19],#16                  //load rk9
	.elseif \rd_num == 14
	aes_encrypt_round	v0, v26         //AES block 0 - round 10
	ldr	q26, [x19],#16                  //load rk10
	aes_encrypt_round	v0, v27          //AES block 0 - round 11
	ldr	q27, [x19],#16                   //load rk9
	.endif
.endm

.macro aes_enc_iv_final
	aes_encrypt_round	v0, v26          //AES block 0 - round 12
	ldr	q26, [x19],#16                   //load rk10
	aese	v0.16b, v27.16b                 //AES block 0 - round 9
	eor	v4.16b, v26.16b, v0.16b          //AES block 0 - result
.endm

.macro load_initial_tag    dst:req,buf:req
	ld1	{\dst\().16b}, [\buf]
	ext	\dst\().16b, \dst\().16b, \dst\().16b, #8
	rev64	\dst\().16b, \dst\().16b
.endm

SYM_FUNC_START(pmull_gcm_encrypt_unroll)
	push_stack
	mov	x25, x4
	mov	x15, x7
	mov	x8, x5
	lsr	x5, x1, #3                      //byte_len
	mov	x26, x6
	load_initial_tag v11,x3
	cbz	x1, .Lenc_final_tag_pre
	ldp	x10, x11, [x25]                 //ctr96_b64, ctr96_t32
	ldp	x13, x14, [x8, #160]            //load rk10
	load_initial_tag v11,x3
	ldr	q27, [x8, #144]                 //load rk9
	add	x4, x0, x1, lsr #3              //end_input_ptr
	sub	x5, x5, #1                      //byte_len - 1
	lsr	x12, x11, #32
	ldr	q15, [x3, #112]                 //load h4l | h4h
	ext	v15.16b, v15.16b, v15.16b, #8
	fmov	d1, x10                         //CTR block 1
	rev	w12, w12                        //rev_ctr32
	add	w12, w12, #1                    //increment rev_ctr32
	orr	w11, w11, w11
	ldr	q18, [x8, #0]                   //load rk0
	rev	w9, w12                         //CTR block 1
	add	w12, w12, #1                    //CTR block 1
	fmov	d3, x10                         //CTR block 3
	ldr	q28, [x8, #160]                 //load rk10
	orr	x9, x11, x9, lsl #32            //CTR block 1
	//load initial counter so that start first AES block quickly
	ld1	{ v0.16b}, [x25]
	fmov	v1.d[1], x9                     //CTR block 1
	rev	w9, w12                         //CTR block 2
	fmov	d2, x10                         //CTR block 2
	orr	x9, x11, x9, lsl #32            //CTR block 2
	add	w12, w12, #1                    //CTR block 2
	fmov	v2.d[1], x9                     //CTR block 2
	rev	w9, w12                         //CTR block 3
	orr	x9, x11, x9, lsl #32            //CTR block 3
	ldr	q19, [x8, #16]                  //load rk1
	add	w12, w12, #1                    //CTR block 3
	fmov	v3.d[1], x9                     //CTR block 3
	ldr	q14, [x3, #80]                  //load h3l | h3h
	ext	v14.16b, v14.16b, v14.16b, #8
	aes_encrypt_round	v1, v18         //AES block 1 - round 0
	ldr	q20, [x8, #32]                  //load rk2
	aes_encrypt_round	v2, v18         //AES block 2 - round 0
	ldr	q12, [x3, #32]                  //load h1l | h1h
	ext	v12.16b, v12.16b, v12.16b, #8
	aes_encrypt_round	v0, v18         //AES block 0 - round 0
	ldr	q26, [x8, #128]                 //load rk8
	aes_encrypt_round	v3, v18         //AES block 3 - round 0
	ldr	q21, [x8, #48]                  //load rk3
	aes_encrypt_round	v2, v19         //AES block 2 - round 1
	trn2	v17.2d,  v14.2d,    v15.2d      //h4l | h3l
	aes_encrypt_round	v0, v19         //AES block 0 - round 1
	ldr	q24, [x8, #96]                  //load rk6
	aes_encrypt_round	v1, v19         //AES block 1 - round 1
	ldr	q25, [x8, #112]                 //load rk7
	aes_encrypt_round	v3, v19         //AES block 3 - round 1
	trn1	v9.2d, v14.2d,    v15.2d        //h4h | h3h
	aes_encrypt_round	v0, v20         //AES block 0 - round 2
	ldr	q23, [x8, #80]                  //load rk5
	aes_encrypt_round	v1, v20         //AES block 1 - round 2
	ldr	q13, [x3, #64]                  //load h2l | h2h
	ext	v13.16b, v13.16b, v13.16b, #8
	aes_encrypt_round	v3, v20         //AES block 3 - round 2
	aes_encrypt_round	v2, v20         //AES block 2 - round 2
	eor	v17.16b, v17.16b, v9.16b        //h4k | h3k
	aes_encrypt_round	v0, v21         //AES block 0 - round 3
	aes_encrypt_round	v1, v21         //AES block 1 - round 3
	aes_encrypt_round	v2, v21         //AES block 2 - round 3
	ldr	q22, [x8, #64]                  //load rk4
	aes_encrypt_round	v3, v21         //AES block 3 - round 3
	//bytes be processed in main loop(at least 1 byte be handled by tail)
	and	x5, x5, #0xffffffffffffffc0
	trn2	v16.2d,  v12.2d,    v13.2d      //h2l | h1l
	aes_encrypt_round	v3, v22          //AES block 3 - round 4
	add	x5, x5, x0
	aes_encrypt_round	v2, v22          //AES block 2 - round 4
	cmp	x0, x5                   //check if we have <= 4 blocks
	aes_encrypt_round	v0, v22          //AES block 0 - round 4
	aes_encrypt_round	v3, v23          //AES block 3 - round 5
	aes_encrypt_round	v2, v23          //AES block 2 - round 5
	aes_encrypt_round	v0, v23          //AES block 0 - round 5
	aes_encrypt_round	v3, v24          //AES block 3 - round 6
	aes_encrypt_round	v1, v22          //AES block 1 - round 4
	aes_encrypt_round	v2, v24          //AES block 2 - round 6
	trn1	v8.2d,    v12.2d,    v13.2d     //h2h | h1h
	aes_encrypt_round	v0, v24          //AES block 0 - round 6
	aes_encrypt_round	v1, v23          //AES block 1 - round 5
	aes_encrypt_round	v1, v24          //AES block 1 - round 6
	aes_encrypt_round	v3, v25          //AES block 3 - round 7
	aes_encrypt_round	v0, v25          //AES block 0 - round 7
	aes_encrypt_round	v2, v25          //AES block 2 - round 7
	aes_encrypt_round	v0, v26          //AES block 0 - round 8
	aes_encrypt_round	v1, v25          //AES block 1 - round 7
	aes_encrypt_round	v2, v26          //AES block 2 - round 8
	aes_encrypt_round	v3, v26          //AES block 3 - round 8
	aes_encrypt_round	v1, v26          //AES block 1 - round 8

	mov	x6, x26
	sub	x6, x6, #10
	cbz	x6, .Lleft_rounds
	aes_enc_extra_round 12
	sub	x6, x6, #2
	cbz	x6, .Lleft_rounds
	aes_enc_extra_round 14

.Lleft_rounds:
	aese	v2.16b, v27.16b                  //AES block 2 - round 9
	aese	v0.16b, v27.16b                 //AES block 0 - round 9
	eor	v16.16b, v16.16b, v8.16b        //h2k | h1k
	aese	v1.16b, v27.16b                 //AES block 1 - round 9
	aese	v3.16b, v27.16b                 //AES block 3 - round 9
	b.ge	.L128_enc_tail                  //handle tail

	ldp	x6, x7, [x0, #0]                //AES block 0 - load plaintext
	ldp	x21, x22, [x0, #32]             //AES block 2 - load plaintext
	ldp	x19, x20, [x0, #16]             //AES block 1 - load plaintext
	ldp	x23, x24, [x0, #48]             //AES block 3 - load plaintext
	eor	x6, x6, x13                     //AES block 0 - round 10 low
	eor	x7, x7, x14                     //AES block 0 - round 10 high
	eor	x21, x21, x13                   //AES block 2 - round 10 low
	fmov	d4, x6                          //AES block 0 - mov low
	eor	x19, x19, x13                   //AES block 1 - round 10 low
	eor	x22, x22, x14                   //AES block 2 - round 10 high
	fmov	v4.d[1], x7                     //AES block 0 - mov high
	fmov	d5, x19                         //AES block 1 - mov low
	eor	x20, x20, x14                   //AES block 1 - round 10 high
	eor	x23, x23, x13                   //AES block 3 - round 10 low
	fmov	v5.d[1], x20                    //AES block 1 - mov high
	fmov	d6, x21                         //AES block 2 - mov low
	eor	x24, x24, x14                   //AES block 3 - round 10 high
	rev	w9, w12                         //CTR block 4
	fmov	v6.d[1], x22                    //AES block 2 - mov high
	orr	x9, x11, x9, lsl #32            //CTR block 4
	eor	v4.16b, v4.16b, v0.16b          //AES block 0 - result
	fmov	d0, x10                         //CTR block 4
	add	w12, w12, #1                    //CTR block 4
	fmov	v0.d[1], x9                     //CTR block 4
	rev	w9, w12                         //CTR block 5
	eor	v5.16b, v5.16b, v1.16b          //AES block 1 - result
	fmov	d1, x10                         //CTR block 5
	orr	x9, x11, x9, lsl #32            //CTR block 5
	add	w12, w12, #1                    //CTR block 5
	add	x0, x0, #64                     //AES input_ptr update
	fmov	v1.d[1], x9                     //CTR block 5
	fmov	d7, x23                         //AES block 3 - mov low
	rev	w9, w12                         //CTR block 6
	st1	{ v4.16b}, [x2], #16            //AES block 0 - store result
	fmov	v7.d[1], x24                    //AES block 3 - mov high
	orr	x9, x11, x9, lsl #32            //CTR block 6
	add	w12, w12, #1                    //CTR block 6
	eor	v6.16b, v6.16b, v2.16b          //AES block 2 - result
	st1	{ v5.16b}, [x2], #16            //AES block 1 - store result
	fmov	d2, x10                         //CTR block 6
	cmp	x0, x5                   //check if we have <= 8 blocks
	fmov	v2.d[1], x9                     //CTR block 6
	rev	w9, w12                         //CTR block 7
	st1	{ v6.16b}, [x2], #16            //AES block 2 - store result
	orr	x9, x11, x9, lsl #32            //CTR block 7
	eor	v7.16b, v7.16b, v3.16b          //AES block 3 - result
	st1	{ v7.16b}, [x2], #16            //AES block 3 - store result
	b.ge	.L128_enc_prepretail            //do prepretail
.L128_enc_main_loop:	//main	loop start
	ldp	x23, x24, [x0, #48]           //AES block 4k+3 - load plaintext
	rev64	v4.16b, v4.16b                  //GHASH block 4k
	rev64	v6.16b, v6.16b                  //GHASH block 4k+2
	aes_encrypt_round	v2, v18          //AES block 4k+6 - round 0
	fmov	d3, x10                         //CTR block 4k+3
	ext	v11.16b, v11.16b, v11.16b, #8   //PRE 0
	rev64	v5.16b, v5.16b                  //GHASH block 4k+1
	aes_encrypt_round	v1, v18          //AES block 4k+5 - round 0
	add	w12, w12, #1                    //CTR block 4k+3
	fmov	v3.d[1], x9                     //CTR block 4k+3
	aes_encrypt_round	v0, v18          //AES block 4k+4 - round 0
	mov	d31, v6.d[1]                    //GHASH block 4k+2 - mid
	aes_encrypt_round	v2, v19          //AES block 4k+6 - round 1
	mov	d30, v5.d[1]                    //GHASH block 4k+1 - mid
	aes_encrypt_round	v1, v19          //AES block 4k+5 - round 1
	eor	v4.16b, v4.16b, v11.16b         //PRE 1
	aes_encrypt_round	v3, v18          //AES block 4k+7 - round 0
	eor	x24, x24, x14                   //AES block 4k+3 - round 10 high
	pmull2	v28.1q, v5.2d, v14.2d           //GHASH block 4k+1 - high
	eor	v31.8b, v31.8b, v6.8b           //GHASH block 4k+2 - mid
	ldp	x6, x7, [x0, #0]            //AES block 4k+4 - load plaintext
	aes_encrypt_round	v0, v19          //AES block 4k+4 - round 1
	rev	w9, w12                         //CTR block 4k+8
	eor	v30.8b, v30.8b, v5.8b           //GHASH block 4k+1 - mid
	mov	d8, v4.d[1]                     //GHASH block 4k - mid
	orr	x9, x11, x9, lsl #32            //CTR block 4k+8
	pmull2	v9.1q, v4.2d, v15.2d            //GHASH block 4k - high
	add	w12, w12, #1                    //CTR block 4k+8
	mov	d10, v17.d[1]                   //GHASH block 4k - mid
	aes_encrypt_round	v0, v20          //AES block 4k+4 - round 2
	pmull	v11.1q, v4.1d, v15.1d           //GHASH block 4k - low
	eor	v8.8b, v8.8b, v4.8b             //GHASH block 4k - mid
	aes_encrypt_round	v1, v20          //AES block 4k+5 - round 2
	aes_encrypt_round	v0, v21          //AES block 4k+4 - round 3
	eor	v9.16b, v9.16b, v28.16b         //GHASH block 4k+1 - high
	pmull	v28.1q, v6.1d, v13.1d           //GHASH block 4k+2 - low
	pmull	v10.1q, v8.1d, v10.1d           //GHASH block 4k - mid
	rev64	v7.16b, v7.16b                  //GHASH block 4k+3
	pmull	v30.1q, v30.1d, v17.1d          //GHASH block 4k+1 - mid
	pmull	v29.1q, v5.1d, v14.1d           //GHASH block 4k+1 - low
	ins	v31.d[1], v31.d[0]              //GHASH block 4k+2 - mid
	pmull2	v8.1q, v6.2d, v13.2d            //GHASH block 4k+2 - high
	eor	x7, x7, x14                     //AES block 4k+4 - round 10 high
	eor	v10.16b, v10.16b, v30.16b       //GHASH block 4k+1 - mid
	mov	d30, v7.d[1]                    //GHASH block 4k+3 - mid
	aes_encrypt_round	v3, v19          //AES block 4k+7 - round 1
	eor	v11.16b, v11.16b, v29.16b       //GHASH block 4k+1 - low
	aes_encrypt_round	v2, v20          //AES block 4k+6 - round 2
	eor	x6, x6, x13                     //AES block 4k+4 - round 10 low
	aes_encrypt_round	v1, v21          //AES block 4k+5 - round 3
	eor	v30.8b, v30.8b, v7.8b           //GHASH block 4k+3 - mid
	pmull2	v4.1q, v7.2d, v12.2d            //GHASH block 4k+3 - high
	aes_encrypt_round	v2, v21          //AES block 4k+6 - round 3
	eor	v9.16b, v9.16b, v8.16b          //GHASH block 4k+2 - high
	pmull2	v31.1q, v31.2d, v16.2d          //GHASH block 4k+2 - mid
	pmull	v29.1q, v7.1d, v12.1d           //GHASH block 4k+3 - low
	movi	v8.8b, #0xc2
	pmull	v30.1q, v30.1d, v16.1d          //GHASH block 4k+3 - mid
	eor	v11.16b, v11.16b, v28.16b       //GHASH block 4k+2 - low
	aes_encrypt_round	v1, v22          //AES block 4k+5 - round 4
	aes_encrypt_round	v3, v20          //AES block 4k+7 - round 2
	shl	d8, d8, #56                     //mod_constant
	aes_encrypt_round	v0, v22          //AES block 4k+4 - round 4
	eor	v9.16b, v9.16b, v4.16b          //GHASH block 4k+3 - high
	aes_encrypt_round	v1, v23          //AES block 4k+5 - round 5
	ldp	x19, x20, [x0, #16]           //AES block 4k+5 - load plaintext
	aes_encrypt_round	v3, v21          //AES block 4k+7 - round 3
	eor	v10.16b, v10.16b, v31.16b       //GHASH block 4k+2 - mid
	aes_encrypt_round	v0, v23          //AES block 4k+4 - round 5
	ldp	x21, x22, [x0, #32]           //AES block 4k+6 - load plaintext
	pmull	v31.1q, v9.1d, v8.1d            //MODULO - top 64b align with mid
	eor	v11.16b, v11.16b, v29.16b       //GHASH block 4k+3 - low
	aes_encrypt_round	v2, v22          //AES block 4k+6 - round 4
	eor	x19, x19, x13                     //AES block 4k+5 - round 10 low
	aes_encrypt_round	v3, v22          //AES block 4k+7 - round 4
	eor	v10.16b, v10.16b, v30.16b       //GHASH block 4k+3 - mid
	aes_encrypt_round	v1, v24          //AES block 4k+5 - round 6
	eor	x23, x23, x13                     //AES block 4k+3 - round 10 low
	aes_encrypt_round	v2, v23          //AES block 4k+6 - round 5
	eor	v30.16b, v11.16b, v9.16b        //MODULO - karatsuba tidy up
	fmov	d4, x6                          //AES block 4k+4 - mov low
	aes_encrypt_round	v0, v24          //AES block 4k+4 - round 6
	fmov	v4.d[1], x7                     //AES block 4k+4 - mov high
	add	x0, x0, #64                     //AES input_ptr update
	fmov	d7, x23                         //AES block 4k+3 - mov low
	ext	v9.16b, v9.16b, v9.16b, #8      //MODULO - other top alignment
	aes_encrypt_round	v3, v23          //AES block 4k+7 - round 5
	fmov	d5, x19                         //AES block 4k+5 - mov low
	aes_encrypt_round	v0, v25          //AES block 4k+4 - round 7
	eor	v10.16b, v10.16b, v30.16b       //MODULO - karatsuba tidy up
	aes_encrypt_round	v2, v24         //AES block 4k+6 - round 6
	eor	x20, x20, x14                   //AES block 4k+5 - round 10 high
	aes_encrypt_round	v1, v25         //AES block 4k+5 - round 7
	fmov	v5.d[1], x20                    //AES block 4k+5 - mov high
	aes_encrypt_round	v0, v26          //AES block 4k+4 - round 8
	fmov	v7.d[1], x24                    //AES block 4k+3 - mov high
	aes_encrypt_round	v3, v24          //AES block 4k+7 - round 6
	cmp	x0, x5                   //.LOOP CONTROL
	aes_encrypt_round	v1, v26          //AES block 4k+5 - round 8
	eor	v10.16b, v10.16b, v31.16b       //MODULO - fold into mid
	eor	x21, x21, x13                   //AES block 4k+6 - round 10 low
	eor	x22, x22, x14                   //AES block 4k+6 - round 10 high
	ldr	q27, [x8, #144]                 //load rk9
	aes_encrypt_round	v3, v25          //AES block 4k+7 - round 7
	fmov	d6, x21                         //AES block 4k+6 - mov low
	fmov	v6.d[1], x22                    //AES block 4k+6 - mov high
	aes_encrypt_round	v2, v25          //AES block 4k+6 - round 7
	ldr	q28, [x8, #160]                 //load rk9
	aes_encrypt_round	v3, v26          //AES block 4k+7 - round 8
	eor	v10.16b, v10.16b, v9.16b        //MODULO - fold into mid
	aes_encrypt_round	v2, v26          //AES block 4k+6 - round 8
	mov	x6, x26
	sub	x6,x6,#10
	cbz	x6, .Lleft2_rounds
	aes_enc_extra_round 12
	sub	x6,x6,#2
	cbz	x6, .Lleft2_rounds
	aes_enc_extra_round 14
.Lleft2_rounds:
	aese	v0.16b, v27.16b                 //AES block 4k+4 - round 9
	eor	v4.16b, v4.16b, v0.16b          //AES block 4k+4 - result
	fmov	d0, x10                         //CTR block 4k+8
	fmov	v0.d[1], x9                     //CTR block 4k+8
	rev	w9, w12                         //CTR block 4k+9
	add	w12, w12, #1                    //CTR block 4k+9
	aese	v1.16b, v27.16b                 //AES block 4k+5 - round 9
	eor	v5.16b, v5.16b, v1.16b          //AES block 4k+5 - result
	orr	x9, x11, x9, lsl #32            //CTR block 4k+9
	fmov	d1, x10                         //CTR block 4k+9
	pmull	v9.1q, v10.1d, v8.1d            //MODULO - mid 64b align with low
	fmov	v1.d[1], x9                     //CTR block 4k+9
	rev	w9, w12                         //CTR block 4k+10
	aese	v2.16b, v27.16b                 //AES block 4k+6 - round 9
	st1	{ v4.16b}, [x2], #16            //AES block 4k+4 - store result
	eor	v6.16b, v6.16b, v2.16b          //AES block 4k+6 - result
	orr	x9, x11, x9, lsl #32            //CTR block 4k+10
	aese	v3.16b, v27.16b                 //AES block 4k+7 - round 9
	add	w12, w12, #1                    //CTR block 4k+10
	ext	v10.16b, v10.16b, v10.16b, #8   //MODULO - other mid alignment
	fmov	d2, x10                         //CTR block 4k+10
	eor	v11.16b, v11.16b, v9.16b        //MODULO - fold into low
	st1	{ v5.16b}, [x2], #16            //AES block 4k+5 - store result
	fmov	v2.d[1], x9                     //CTR block 4k+10
	st1	{ v6.16b}, [x2], #16            //AES block 4k+6 - store result
	rev	w9, w12                         //CTR block 4k+11
	orr	x9, x11, x9, lsl #32            //CTR block 4k+11
	eor	v7.16b, v7.16b, v3.16b          //AES block 4k+3 - result
	eor	v11.16b, v11.16b, v10.16b       //MODULO - fold into low
	st1	{ v7.16b}, [x2], #16            //AES block 4k+3 - store result
	b.lt	.L128_enc_main_loop
.L128_enc_prepretail:	//PREPRETAIL
	rev64	v4.16b, v4.16b                  //GHASH block 4k (only t0 is free)
	fmov	d3, x10                         //CTR block 4k+3
	rev64	v5.16b, v5.16b                  //GHASH block 4k+1 (t0 and t1 free)
	ext	v11.16b, v11.16b, v11.16b, #8   //PRE 0
	add	w12, w12, #1                    //CTR block 4k+3
	fmov	v3.d[1], x9                     //CTR block 4k+3
	aes_encrypt_round	v1, v18         //AES block 4k+5 - round 0
	rev64	v6.16b, v6.16b                  //GHASH block 4k+2
	pmull	v29.1q, v5.1d, v14.1d           //GHASH block 4k+1 - low
	rev64	v7.16b, v7.16b                  //GHASH block 4k+3
	eor	v4.16b, v4.16b, v11.16b         //PRE 1
	pmull2	v28.1q, v5.2d, v14.2d           //GHASH block 4k+1 - high
	aes_encrypt_round	v3, v18         //AES block 4k+7 - round 0
	mov	d30, v5.d[1]                    //GHASH block 4k+1 - mid
	pmull	v11.1q, v4.1d, v15.1d           //GHASH block 4k - low
	mov	d8, v4.d[1]                     //GHASH block 4k - mid
	mov	d31, v6.d[1]                    //GHASH block 4k+2 - mid
	mov	d10, v17.d[1]                   //GHASH block 4k - mid
	aes_encrypt_round	v1, v19         //AES block 4k+5 - round 1
	eor	v30.8b, v30.8b, v5.8b           //GHASH block 4k+1 - mid
	eor	v8.8b, v8.8b, v4.8b             //GHASH block 4k - mid
	pmull2	v9.1q, v4.2d, v15.2d            //GHASH block 4k - high
	eor	v31.8b, v31.8b, v6.8b           //GHASH block 4k+2 - mid
	aes_encrypt_round	v3, v19         //AES block 4k+7 - round 1
	pmull	v30.1q, v30.1d, v17.1d          //GHASH block 4k+1 - mid
	eor	v11.16b, v11.16b, v29.16b       //GHASH block 4k+1 - low
	pmull	v10.1q, v8.1d, v10.1d           //GHASH block 4k - mid
	aes_encrypt_round	v0, v18         //AES block 4k+4 - round 0
	ins	v31.d[1], v31.d[0]              //GHASH block 4k+2 - mid
	aes_encrypt_round	v2, v18         //AES block 4k+6 - round 0
	eor	v10.16b, v10.16b, v30.16b       //GHASH block 4k+1 - mid
	mov	d30, v7.d[1]                    //GHASH block 4k+3 - mid
	aes_encrypt_round	v0, v19          //AES block 4k+4 - round 1
	eor	v9.16b, v9.16b, v28.16b         //GHASH block 4k+1 - high
	pmull2	v31.1q, v31.2d, v16.2d          //GHASH block 4k+2 - mid
	pmull2	v8.1q, v6.2d, v13.2d            //GHASH block 4k+2 - high
	eor	v30.8b, v30.8b, v7.8b           //GHASH block 4k+3 - mid
	pmull2	v4.1q, v7.2d, v12.2d            //GHASH block 4k+3 - high
	pmull	v28.1q, v6.1d, v13.1d           //GHASH block 4k+2 - low
	aes_encrypt_round	v2, v19          //AES block 4k+6 - round 1
	eor	v9.16b, v9.16b, v8.16b          //GHASH block 4k+2 - high
	aes_encrypt_round	v0, v20          //AES block 4k+4 - round 2
	pmull	v29.1q, v7.1d, v12.1d           //GHASH block 4k+3 - low
	movi	v8.8b, #0xc2
	aes_encrypt_round	v2, v20          //AES block 4k+6 - round 2
	eor	v11.16b, v11.16b, v28.16b       //GHASH block 4k+2 - low
	aes_encrypt_round	v3, v20          //AES block 4k+7 - round 2
	pmull	v30.1q, v30.1d, v16.1d          //GHASH block 4k+3 - mid
	eor	v10.16b, v10.16b, v31.16b       //GHASH block 4k+2 - mid
	aes_encrypt_round	v2, v21          //AES block 4k+6 - round 3
	aes_encrypt_round	v1, v20          //AES block 4k+5 - round 2
	eor	v9.16b, v9.16b, v4.16b          //GHASH block 4k+3 - high
	aes_encrypt_round	v0, v21          //AES block 4k+4 - round 3
	eor	v10.16b, v10.16b, v30.16b       //GHASH block 4k+3 - mid
	shl	d8, d8, #56               //mod_constant
	aes_encrypt_round	v1, v21          //AES block 4k+5 - round 3
	eor	v11.16b, v11.16b, v29.16b       //GHASH block 4k+3 - low
	aes_encrypt_round	v0, v22          //AES block 4k+4 - round 4
	pmull	v28.1q, v9.1d, v8.1d
	eor	v10.16b, v10.16b, v9.16b        //karatsuba tidy up
	aes_encrypt_round	v1, v22          //AES block 4k+5 - round 4
	aes_encrypt_round	v0, v23          //AES block 4k+4 - round 5
	ext	v9.16b, v9.16b, v9.16b, #8
	aes_encrypt_round	v3, v21          //AES block 4k+7 - round 3
	aes_encrypt_round	v2, v22          //AES block 4k+6 - round 4
	eor	v10.16b, v10.16b, v11.16b
	aes_encrypt_round	v0, v24          //AES block 4k+4 - round 6
	aes_encrypt_round	v3, v22          //AES block 4k+7 - round 4
	aes_encrypt_round	v1, v23          //AES block 4k+5 - round 5
	aes_encrypt_round	v2, v23          //AES block 4k+6 - round 5
	eor	v10.16b, v10.16b, v28.16b
	aes_encrypt_round	v3, v23          //AES block 4k+7 - round 5
	aes_encrypt_round	v1, v24          //AES block 4k+5 - round 6
	aes_encrypt_round	v2, v24          //AES block 4k+6 - round 6
	aes_encrypt_round	v3, v24          //AES block 4k+7 - round 6
	eor	v10.16b, v10.16b, v9.16b
	ldr	q27, [x8, #144]                                //load rk9
	aes_encrypt_round	v0, v25          //AES block 4k+4 - round 7
	aes_encrypt_round	v2, v25          //AES block 4k+6 - round 7
	aes_encrypt_round	v3, v25          //AES block 4k+7 - round 7
	pmull	v28.1q, v10.1d, v8.1d
	aes_encrypt_round	v1, v25          //AES block 4k+5 - round 7
	ext	v10.16b, v10.16b, v10.16b, #8
	aes_encrypt_round	v3, v26          //AES block 4k+7 - round 8
	aes_encrypt_round	v0, v26          //AES block 4k+4 - round 8
	eor	v11.16b, v11.16b, v28.16b
	aes_encrypt_round	v1, v26          //AES block 4k+5 - round 8
	ldr	q28, [x8, #160]                                //load rk9
	aes_encrypt_round	v2, v26          //AES block 4k+6 - round 8

	mov	x6, x26
	sub	x6,x6,#10
	cbz	x6, .Lleft3_rounds
	aes_enc_extra_round 12
	sub	x6,x6,#2
	cbz	x6, .Lleft3_rounds
	aes_enc_extra_round 14

.Lleft3_rounds:
	aese	v0.16b, v27.16b                 //AES block 4k+4 - round 9
	aese	v3.16b, v27.16b                 //AES block 4k+7 - round 9
	aese	v1.16b, v27.16b                 //AES block 4k+5 - round 9
	eor	v11.16b, v11.16b, v10.16b
	aese	v2.16b, v27.16b                 //AES block 4k+6 - round 9
.L128_enc_tail:	//TAIL
	sub	x5, x4, x0   //main_end_input_ptr is number of bytes left
	ldp	x6, x7, [x0], #16           //AES block 4k+4 - load plaintext
	cmp	x5, #48
	ext	v8.16b, v11.16b, v11.16b, #8    //prepare final partial tag
	eor	x6, x6, x13                     //AES block 4k+4 - round 10 low
	eor	x7, x7, x14                     //AES block 4k+4 - round 10 high
	fmov	d4, x6                          //AES block 4k+4 - mov low
	fmov	v4.d[1], x7                     //AES block 4k+4 - mov high
	eor	v5.16b, v4.16b, v0.16b          //AES block 4k+4 - result
	b.gt	.L128_enc_blocks_more_than_3
	sub	w12, w12, #1
	movi	v11.8b, #0
	mov	v3.16b, v2.16b
	cmp	x5, #32
	mov	v2.16b, v1.16b
	movi	v9.8b, #0
	movi	v10.8b, #0
	b.gt	.L128_enc_blocks_more_than_2
	mov	v3.16b, v1.16b
	cmp	x5, #16
	sub	w12, w12, #1
	b.gt	.L128_enc_blocks_more_than_1
	sub	w12, w12, #1
	b	.L128_enc_blocks_less_than_1
.L128_enc_blocks_more_than_3:	//blocks	left >  3
	st1	{ v5.16b}, [x2], #16    //AES final-3 block  - store result
	ldp	x6, x7, [x0], #16       //AES final-2 block-load input low&high
	rev64	v4.16b, v5.16b          //GHASH final-3 block
	eor	v4.16b, v4.16b, v8.16b  //feed in partial tag
	eor	x7, x7, x14             //AES final-2 block - round 10 high
	eor	x6, x6, x13             //AES final-2 block - round 10 low
	fmov	d5, x6                  //AES final-2 block - mov low
	movi	v8.8b, #0               //suppress further partial tag feed in
	fmov	v5.d[1], x7             //AES final-2 block - mov high
	pmull	v11.1q, v4.1d, v15.1d   //GHASH final-3 block - low
	mov	d22, v4.d[1]            //GHASH final-3 block - mid
	pmull2	v9.1q, v4.2d, v15.2d    //GHASH final-3 block - high
	mov	d10, v17.d[1]           //GHASH final-3 block - mid
	eor	v5.16b, v5.16b, v1.16b  //AES final-2 block - result
	eor	v22.8b, v22.8b, v4.8b   //GHASH final-3 block - mid
	pmull	v10.1q, v22.1d, v10.1d  //GHASH final-3 block - mid
.L128_enc_blocks_more_than_2:	//blocks	left >  2
	st1	{ v5.16b}, [x2], #16    //AES final-2 block - store result
	rev64	v4.16b, v5.16b          //GHASH final-2 block
	ldp	x6, x7, [x0], #16       //AES final-1 block-load input low&high
	eor	v4.16b, v4.16b, v8.16b  //feed in partial tag
	eor	x6, x6, x13             //AES final-1 block - round 10 low
	fmov	d5, x6                  //AES final-1 block - mov low
	eor	x7, x7, x14             //AES final-1 block - round 10 high
	pmull2	v20.1q, v4.2d, v14.2d   //GHASH final-2 block - high
	fmov	v5.d[1], x7             //AES final-1 block - mov high
	mov	d22, v4.d[1]            //GHASH final-2 block - mid
	pmull	v21.1q, v4.1d, v14.1d   //GHASH final-2 block - low
	eor	v9.16b, v9.16b, v20.16b //GHASH final-2 block - high
	eor	v22.8b, v22.8b, v4.8b   //GHASH final-2 block - mid
	eor	v5.16b, v5.16b, v2.16b  //AES final-1 block - result
	eor	v11.16b, v11.16b, v21.16b       //GHASH final-2 block - low
	pmull	v22.1q, v22.1d, v17.1d  //GHASH final-2 block - mid
	movi	v8.8b, #0               //suppress further partial tag feed in
	eor	v10.16b, v10.16b, v22.16b       //GHASH final-2 block - mid
.L128_enc_blocks_more_than_1:	//blocks	left >  1
	st1	{ v5.16b}, [x2], #16    //AES final-1 block - store result
	rev64	v4.16b, v5.16b          //GHASH final-1 block
	ldp	x6, x7, [x0], #16       //AES final block - load input low & high
	eor	v4.16b, v4.16b, v8.16b  //feed in partial tag
	eor	x7, x7, x14             //AES final block - round 10 high
	eor	x6, x6, x13             //AES final block - round 10 low
	fmov	d5, x6                  //AES final block - mov low
	pmull2	v20.1q, v4.2d, v13.2d   //GHASH final-1 block - high
	fmov	v5.d[1], x7             //AES final block - mov high
	mov	d22, v4.d[1]            //GHASH final-1 block - mid
	pmull	v21.1q, v4.1d, v13.1d   //GHASH final-1 block - low
	eor	v22.8b, v22.8b, v4.8b   //GHASH final-1 block - mid
	eor	v5.16b, v5.16b, v3.16b  //AES final block - result
	ins	v22.d[1], v22.d[0]      //GHASH final-1 block - mid
	pmull2	v22.1q, v22.2d, v16.2d  //GHASH final-1 block - mid
	eor	v11.16b, v11.16b, v21.16b       //GHASH final-1 block - low
	eor	v9.16b, v9.16b, v20.16b         //GHASH final-1 block - high
	eor	v10.16b, v10.16b, v22.16b       //GHASH final-1 block - mid
	movi	v8.8b, #0                       //suppress further partial tag feed in
.L128_enc_blocks_less_than_1:	//blocks	left <= 1
	and	x1, x1, #127                    //bit_length %= 128
	mvn	x13, xzr                        //rk10_l = 0xffffffffffffffff
	mvn	x14, xzr                        //rk10_h = 0xffffffffffffffff
	sub	x1, x1, #128                    //bit_length -= 128
	neg	x1, x1                          //bit_length = 128 - #bits
	and	x1, x1, #127                    //bit_length %= 128
	lsr	x14, x14, x1
	cmp	x1, #64
	csel	x6, x13, x14, lt
	csel	x7, x14, xzr, lt
	fmov	d0, x6                          //ctr0b is mask for last block
	fmov	v0.d[1], x7
	//possibly partial last block has zeroes in highest bits
	and	v5.16b, v5.16b, v0.16b
	rev64	v4.16b, v5.16b                  //GHASH final block
	eor	v4.16b, v4.16b, v8.16b          //feed in partial tag
	mov	d8, v4.d[1]                     //GHASH final block - mid
	//load existing bytes where the possibly partial last block is to be stored
	ld1	{ v18.16b}, [x2]
	rev	w9, w12
	karasuba_multiply v4, v12, v20, v8, v21
	load_const
	gcm_tidy_up v9, v10, v11, v30, v31
	//insert existing bytes in top end of result
	bif	v5.16b, v18.16b, v0.16b
	st1	{ v5.16b}, [x2]                 //store all 16B
	str	w9, [x25, #12]                  //store the updated counter
	b	.Lenc_final_tag

.Lenc_final_tag_pre:
	ldr	q12, [x3, #32]                         //load h1l | h1h
	ext	v12.16b, v12.16b, v12.16b, #8
	ldr	q13, [x3, #64]                  //load h2l | h2h
	ext	v13.16b, v13.16b, v13.16b, #8
	trn2	v16.2d,  v12.2d,    v13.2d                      //h2l | h1l
	trn1	v8.2d,    v12.2d,    v13.2d                      //h2h | h1h
	eor	v16.16b, v16.16b, v8.16b                     //h2k | h1k
.Lenc_final_tag:
	cbz	x15, .Lrounds_enc_store

	ld1	{ v5.16b}, [x15]            //load length
	ext	v5.16b, v5.16b, v5.16b, #8   //PRE 0
	rev64	v5.16b, v5.16b                  //GHASH block 4k+1
	eor	v4.16b, v5.16b, v11.16b          //final tax eor with length
	ext	v4.16b, v4.16b, v4.16b, #8   //PRE 0
	movi	v8.8b, #0                       //suppress further partial tag
	mov	d8, v4.d[1]                     //GHASH block 4k - mid
	movi	v11.8b, #0
	movi	v9.8b, #0
	movi	v10.8b, #0
	karasuba_multiply v4, v12, v20, v8, v21
	load_const
	gcm_tidy_up v9, v10, v11, v30, v31
	mov	x6, x26
	ext	v11.16b, v11.16b, v11.16b, #8   //PRE 0
	rev64	v11.16b, v11.16b                  //Final has tag
	aes_enc_iv_init

	add	x19,x8,#160
	sub	x6,x6,#10
	cbz	x6, .Lenc_enc_iv_final
	aes_enc_iv_common       12
	sub	x6,x6,#2
	cbz	x6, .Lenc_enc_iv_final
	aes_enc_iv_common       14
.Lenc_enc_iv_final:
	aes_enc_iv_final

	eor	v11.16b, v4.16b, v11.16b          //final tax eor with length
	st1	{ v11.16b }, [x15]
	b	.L128_enc_ret

.Lrounds_enc_store:
	ext	v11.16b, v11.16b, v11.16b, #8   //PRE 0
	rev64	v11.16b, v11.16b                  //GHASH block 4k+1
	st1	{ v11.16b }, [x3]
.L128_enc_ret:
	mov	w0, #0x0
	pop_stack
	ret
SYM_FUNC_END(pmull_gcm_encrypt_unroll)

SYM_FUNC_START(pmull_gcm_decrypt_unroll)
	push_stack
	mov	x25, x4
	mov	x15, x7
	mov	x8, x5
	lsr	x5, x1, #3                      //byte_len
	mov	x26, x6
	load_initial_tag v11,x3
	cbz	x1, .Ldec_final_tag_pre

	ldp	x10, x11, [x25]                 //ctr96_b64, ctr96_t32
	sub	x5, x5, #1                      //byte_len - 1
	ldr	q18, [x8, #0]                   //load rk0
	and	x5, x5, #0xffffffffffffffc0
	ld1	{ v0.16b}, [x25]
	ldr	q28, [x8, #160]                 //load rk10
	ldr	q13, [x3, #64]                  //load h2l | h2h
	ext	v13.16b, v13.16b, v13.16b, #8
	lsr	x12, x11, #32
	fmov	d2, x10                         //CTR block 2
	ldr	q19, [x8, #16]                  //load rk1
	orr	w11, w11, w11
	rev	w12, w12                        //rev_ctr32
	fmov	d1, x10                         //CTR block 1
	add	w12, w12, #1                    //increment rev_ctr32
	aes_encrypt_round	v0, v18         //AES block 0 - round 0
	rev	w9, w12                         //CTR block 1
	orr	x9, x11, x9, lsl #32            //CTR block 1
	ldr	q20, [x8, #32]                  //load rk2
	add	w12, w12, #1                    //CTR block 1
	fmov	v1.d[1], x9                     //CTR block 1
	rev	w9, w12                         //CTR block 2
	add	w12, w12, #1                    //CTR block 2
	aes_encrypt_round	v0, v19         //AES block 0 - round 1
	orr	x9, x11, x9, lsl #32            //CTR block 2
	fmov	v2.d[1], x9                     //CTR block 2
	rev	w9, w12                         //CTR block 3
	fmov	d3, x10                         //CTR block 3
	orr	x9, x11, x9, lsl #32            //CTR block 3
	add	w12, w12, #1                    //CTR block 3
	fmov	v3.d[1], x9                     //CTR block 3
	add	x4, x0, x1, lsr #3              //end_input_ptr
	aes_encrypt_round	v1, v18          //AES block 1 - round 0
	ldr	q21, [x8, #48]                                 //load rk3
	aes_encrypt_round	v0, v20          //AES block 0 - round 2
	ldr	q24, [x8, #96]                                 //load rk6
	aes_encrypt_round	v2, v18          //AES block 2 - round 0
	ldr	q25, [x8, #112]                                //load rk7
	aes_encrypt_round	v1, v19          //AES block 1 - round 1
	ldr	q22, [x8, #64]                                 //load rk4
	aes_encrypt_round	v3, v18          //AES block 3 - round 0
	aes_encrypt_round	v2, v19          //AES block 2 - round 1
	aes_encrypt_round	v1, v20          //AES block 1 - round 2
	ldp	x13, x14, [x8, #160]                     //load rk10
	aes_encrypt_round	v3, v19          //AES block 3 - round 1
	aes_encrypt_round	v0, v21          //AES block 0 - round 3
	ldr	q23, [x8, #80]                                 //load rk5
	aes_encrypt_round	v1, v21          //AES block 1 - round 3
	aes_encrypt_round	v3, v20          //AES block 3 - round 2
	aes_encrypt_round	v2, v20          //AES block 2 - round 2
	ldr	q27, [x8, #144]                                //load rk9
	aes_encrypt_round	v1, v22          //AES block 1 - round 4
	aes_encrypt_round	v3, v21          //AES block 3 - round 3
	aes_encrypt_round	v2, v21          //AES block 2 - round 3
	ldr	q14, [x3, #80]                         //load h3l | h3h
	ext	v14.16b, v14.16b, v14.16b, #8
	aes_encrypt_round	v0, v22          //AES block 0 - round 4
	ldr	q26, [x8, #128]                                //load rk8
	aes_encrypt_round	v1, v23          //AES block 1 - round 5
	aes_encrypt_round	v2, v22          //AES block 2 - round 4
	aes_encrypt_round	v3, v22          //AES block 3 - round 4
	aes_encrypt_round	v0, v23          //AES block 0 - round 5
	aes_encrypt_round	v2, v23          //AES block 2 - round 5
	ldr	q12, [x3, #32]                         //load h1l | h1h
	ext	v12.16b, v12.16b, v12.16b, #8
	aes_encrypt_round	v3, v23          //AES block 3 - round 5
	aes_encrypt_round	v0, v24          //AES block 0 - round 6
	aes_encrypt_round	v1, v24          //AES block 1 - round 6
	aes_encrypt_round	v3, v24          //AES block 3 - round 6
	aes_encrypt_round	v2, v24          //AES block 2 - round 6
	trn1	v8.2d,    v12.2d,    v13.2d                      //h2h | h1h
	ldr	q15, [x3, #112]                        //load h4l | h4h
	ext	v15.16b, v15.16b, v15.16b, #8
	trn2	v16.2d,  v12.2d,    v13.2d                      //h2l | h1l
	add	x5, x5, x0
	aes_encrypt_round	v1, v25          //AES block 1 - round 7
	aes_encrypt_round	v2, v25          //AES block 2 - round 7
	aes_encrypt_round	v0, v25          //AES block 0 - round 7
	eor	v16.16b, v16.16b, v8.16b                     //h2k | h1k
	aes_encrypt_round	v3, v25          //AES block 3 - round 7
	aes_encrypt_round	v1, v26          //AES block 1 - round 8
	trn2	v17.2d,  v14.2d,    v15.2d                      //h4l | h3l
	aes_encrypt_round	v2, v26          //AES block 2 - round 8
	aes_encrypt_round	v3, v26          //AES block 3 - round 8
	aes_encrypt_round	v0, v26          //AES block 0 - round 8

	mov	x6, x26
	sub	x6, x6, #10
	cbz	x6, .Lleft_dec_rounds
	aes_enc_extra_round 12
	sub	x6, x6, #2
	cbz	x6, .Lleft_dec_rounds
	aes_enc_extra_round 14

.Lleft_dec_rounds:
	trn1	v9.2d, v14.2d,    v15.2d        //h4h | h3h
	aese	v2.16b, v27.16b                 //AES block 2 - round 9
	aese	v3.16b, v27.16b                 //AES block 3 - round 9
	aese	v0.16b, v27.16b                 //AES block 0 - round 9
	cmp	x0, x5                   //check if we have <= 4 blocks
	aese	v1.16b, v27.16b                 //AES block 1 - round 9
	eor	v17.16b, v17.16b, v9.16b        //h4k | h3k
	b.ge	.L128_dec_tail                  //handle tail
	ldr	q5, [x0, #16]                   //AES block 1 - load ciphertext
	ldr	q4, [x0, #0]                    //AES block 0 - load ciphertext
	eor	v1.16b, v5.16b, v1.16b          //AES block 1 - result
	ldr	q6, [x0, #32]                   //AES block 2 - load ciphertext
	eor	v0.16b, v4.16b, v0.16b          //AES block 0 - result
	rev64	v4.16b, v4.16b                  //GHASH block 0
	rev	w9, w12                         //CTR block 4
	orr	x9, x11, x9, lsl #32            //CTR block 4
	add	w12, w12, #1                    //CTR block 4
	ldr	q7, [x0, #48]                   //AES block 3 - load ciphertext
	rev64	v5.16b, v5.16b                  //GHASH block 1
	add	x0, x0, #64                     //AES input_ptr update
	mov	x19, v1.d[0]                    //AES block 1 - mov low
	mov	x20, v1.d[1]                    //AES block 1 - mov high
	mov	x6, v0.d[0]                     //AES block 0 - mov low
	cmp	x0, x5                   //check if we have <= 8 blocks
	mov	x7, v0.d[1]                     //AES block 0 - mov high
	fmov	d0, x10                         //CTR block 4
	fmov	v0.d[1], x9                     //CTR block 4
	rev	w9, w12                         //CTR block 5
	eor	x19, x19, x13                   //AES block 1 - round 10 low
	fmov	d1, x10                         //CTR block 5
	add	w12, w12, #1                    //CTR block 5
	orr	x9, x11, x9, lsl #32            //CTR block 5
	fmov	v1.d[1], x9                     //CTR block 5
	rev	w9, w12                         //CTR block 6
	add	w12, w12, #1                    //CTR block 6
	orr	x9, x11, x9, lsl #32            //CTR block 6
	eor	x20, x20, x14                   //AES block 1 - round 10 high
	eor	x6, x6, x13                   //AES block 0 - round 10 low
	eor	v2.16b, v6.16b, v2.16b          //AES block 2 - result
	eor	x7, x7, x14                   //AES block 0 - round 10 high
	stp	x6, x7, [x2], #16               //AES block 0 - store result
	stp	x19, x20, [x2], #16             //AES block 1 - store result
	b.ge	.L128_dec_prepretail            //do prepretail
.L128_dec_main_loop:	//main	loop start
	eor	v3.16b, v7.16b, v3.16b          //AES block 4k+3 - result
	ext	v11.16b, v11.16b, v11.16b, #8   //PRE 0
	mov	x21, v2.d[0]                    //AES block 4k+2 - mov low
	pmull2	v28.1q, v5.2d, v14.2d           //GHASH block 4k+1 - high
	mov	x22, v2.d[1]                    //AES block 4k+2 - mov high
	aes_encrypt_round	v1, v18          //AES block 4k+5 - round 0
	fmov	d2, x10                         //CTR block 4k+6
	rev64	v6.16b, v6.16b                  //GHASH block 4k+2
	fmov	v2.d[1], x9                     //CTR block 4k+6
	rev	w9, w12                         //CTR block 4k+7
	mov	x23, v3.d[0]                    //AES block 4k+3 - mov low
	eor	v4.16b, v4.16b, v11.16b         //PRE 1
	mov	d30, v5.d[1]                    //GHASH block 4k+1 - mid
	aes_encrypt_round	v1, v19         //AES block 4k+5 - round 1
	rev64	v7.16b, v7.16b                  //GHASH block 4k+3
	pmull	v29.1q, v5.1d, v14.1d           //GHASH block 4k+1 - low
	mov	x24, v3.d[1]                    //AES block 4k+3 - mov high
	orr	x9, x11, x9, lsl #32            //CTR block 4k+7
	pmull	v11.1q, v4.1d, v15.1d           //GHASH block 4k - low
	fmov	d3, x10                         //CTR block 4k+7
	eor	v30.8b, v30.8b, v5.8b           //GHASH block 4k+1 - mid
	aes_encrypt_round	v1, v20          //AES block 4k+5 - round 2
	fmov	v3.d[1], x9                     //CTR block 4k+7
	aes_encrypt_round	v2, v18          //AES block 4k+6 - round 0
	mov	d10, v17.d[1]                   //GHASH block 4k - mid
	pmull2	v9.1q, v4.2d, v15.2d            //GHASH block 4k - high
	eor	v11.16b, v11.16b, v29.16b       //GHASH block 4k+1 - low
	pmull	v29.1q, v7.1d, v12.1d           //GHASH block 4k+3 - low
	aes_encrypt_round	v1, v21          //AES block 4k+5 - round 3
	mov	d8, v4.d[1]                     //GHASH block 4k - mid
	aes_encrypt_round	v3, v18          //AES block 4k+7 - round 0
	eor	v9.16b, v9.16b, v28.16b         //GHASH block 4k+1 - high
	aes_encrypt_round	v0, v18          //AES block 4k+4 - round 0
	pmull	v28.1q, v6.1d, v13.1d           //GHASH block 4k+2 - low
	eor	v8.8b, v8.8b, v4.8b             //GHASH block 4k - mid
	aes_encrypt_round	v3, v19          //AES block 4k+7 - round 1
	eor	x23, x23, x13                   //AES block 4k+3 - round 10 low
	pmull	v30.1q, v30.1d, v17.1d          //GHASH block 4k+1 - mid
	eor	x22, x22, x14                   //AES block 4k+2 - round 10 high
	mov	d31, v6.d[1]                    //GHASH block 4k+2 - mid
	aes_encrypt_round	v0, v19          //AES block 4k+4 - round 1
	eor	v11.16b, v11.16b, v28.16b       //GHASH block 4k+2 - low
	pmull	v10.1q, v8.1d, v10.1d           //GHASH block 4k - mid
	aes_encrypt_round	v3, v20          //AES block 4k+7 - round 2
	eor	v31.8b, v31.8b, v6.8b           //GHASH block 4k+2 - mid
	aes_encrypt_round	v0, v20          //AES block 4k+4 - round 2
	aes_encrypt_round	v1, v22          //AES block 4k+5 - round 4
	eor	v10.16b, v10.16b, v30.16b       //GHASH block 4k+1 - mid
	pmull2	v8.1q, v6.2d, v13.2d            //GHASH block 4k+2 - high
	aes_encrypt_round	v0, v21          //AES block 4k+4 - round 3
	ins	v31.d[1], v31.d[0]              //GHASH block 4k+2 - mid
	pmull2	v4.1q, v7.2d, v12.2d            //GHASH block 4k+3 - high
	aes_encrypt_round	v2, v19          //AES block 4k+6 - round 1
	mov	d30, v7.d[1]                    //GHASH block 4k+3 - mid
	aes_encrypt_round	v0, v22          //AES block 4k+4 - round 4
	eor	v9.16b, v9.16b, v8.16b          //GHASH block 4k+2 - high
	pmull2	v31.1q, v31.2d, v16.2d          //GHASH block 4k+2 - mid
	eor	x24, x24, x14                   //AES block 4k+3 - round 10 high
	aes_encrypt_round	v2, v20          //AES block 4k+6 - round 2
	eor	v30.8b, v30.8b, v7.8b           //GHASH block 4k+3 - mid
	aes_encrypt_round	v1, v23          //AES block 4k+5 - round 5
	eor	x21, x21, x13                   //AES block 4k+2 - round 10 low
	aes_encrypt_round	v0, v23          //AES block 4k+4 - round 5
	movi	v8.8b, #0xc2
	aes_encrypt_round	v2, v21          //AES block 4k+6 - round 3
	eor	v11.16b, v11.16b, v29.16b       //GHASH block 4k+3 - low
	aes_encrypt_round	v1, v24          //AES block 4k+5 - round 6
	aes_encrypt_round	v0, v24          //AES block 4k+4 - round 6
	eor	v10.16b, v10.16b, v31.16b       //GHASH block 4k+2 - mid
	aes_encrypt_round	v2, v22          //AES block 4k+6 - round 4
	stp	x21, x22, [x2], #16        //AES block 4k+2 - store result
	pmull	v30.1q, v30.1d, v16.1d          //GHASH block 4k+3 - mid
	eor	v9.16b, v9.16b, v4.16b          //GHASH block 4k+3 - high
	ldr	q4, [x0, #0]                    //AES block 4k+4 - load cipher
	aes_encrypt_round	v1, v25          //AES block 4k+5 - round 7
	add	w12, w12, #1                    //CTR block 4k+7
	aes_encrypt_round	v0, v25          //AES block 4k+4 - round 7
	shl	d8, d8, #56                     //mod_constant
	aes_encrypt_round	v2, v23          //AES block 4k+6 - round 5
	eor	v10.16b, v10.16b, v30.16b       //GHASH block 4k+3 - mid
	aes_encrypt_round	v1, v26          //AES block 4k+5 - round 8
	stp	x23, x24, [x2], #16             //AES block 4k+3 - store result
	aes_encrypt_round	v0, v26          //AES block 4k+4 - round 8
	eor	v30.16b, v11.16b, v9.16b        //MODULO - karatsuba tidy up
	ldr	q27, [x8, #144]                 //load rk9
	aes_encrypt_round	v3, v21         //AES block 4k+7 - round 3
	rev	w9, w12                         //CTR block 4k+8
	pmull	v31.1q, v9.1d, v8.1d            //MODULO - top 64b align with mid
	ldr	q5, [x0, #16]                   //AES block 4k+5 - ciphertext
	ext	v9.16b, v9.16b, v9.16b, #8      //MODULO - other top alignment
	ldr	q28, [x8, #160]                 //load rk9
	orr	x9, x11, x9, lsl #32            //CTR block 4k+8
	aes_encrypt_round	v3, v22         //AES block 4k+7 - round 4
	eor	v10.16b, v10.16b, v30.16b       //MODULO - karatsuba tidy up
	aes_encrypt_round	v2, v24         //AES block 4k+6 - round 6
	aes_encrypt_round	v3, v23         //AES block 4k+7 - round 5
	ldr	q6, [x0, #32]                   //AES block 4k+6 - ciphertext
	add	w12, w12, #1                    //CTR block 4k+8
	eor	v10.16b, v10.16b, v31.16b       //MODULO - fold into mid
	aes_encrypt_round	v2, v25         //AES block 4k+6 - round 7
	ldr	q7, [x0, #48]                   //AES block 4k+3 - ciphertext
	aes_encrypt_round	v3, v24         //AES block 4k+7 - round 6
	add	x0, x0, #64                     //AES input_ptr update
	aes_encrypt_round	v3, v25         //AES block 4k+7 - round 7
	eor	v10.16b, v10.16b, v9.16b        //MODULO - fold into mid
	aes_encrypt_round	v2, v26         //AES block 4k+6 - round 8
	aes_encrypt_round	v3, v26         //AES block 4k+7 - round 8

	mov	x6, x26
	sub	x6,x6,#10
	cbz	x6, .Lleft2_dec_rounds
	aes_enc_extra_round	12
	sub	x6,x6,#2
	cbz	x6, .Lleft2_dec_rounds
	aes_enc_extra_round 14

.Lleft2_dec_rounds:
	aese	v0.16b, v27.16b                 //AES block 4k+4 - round 9
	aese	v1.16b, v27.16b                 //AES block 4k+5 - round 9
	eor	v0.16b, v4.16b, v0.16b          //AES block 4k+4 - result
	eor	v1.16b, v5.16b, v1.16b          //AES block 4k+5 - result
	pmull	v8.1q, v10.1d, v8.1d     //MODULO - mid 64b align with low
	rev64	v5.16b, v5.16b                  //GHASH block 4k+5
	mov	x7, v0.d[1]                     //AES block 4k+4 - mov high
	mov	x6, v0.d[0]                     //AES block 4k+4 - mov low
	fmov	d0, x10                         //CTR block 4k+8
	fmov	v0.d[1], x9                     //CTR block 4k+8
	rev	w9, w12                         //CTR block 4k+9
	aese	v2.16b, v27.16b                 //AES block 4k+6 - round 9
	orr	x9, x11, x9, lsl #32            //CTR block 4k+9
	ext	v10.16b, v10.16b, v10.16b, #8   //MODULO - other mid alignment
	eor	x7, x7, x14                   //AES block 4k+4 - round 10 high
	eor	v11.16b, v11.16b, v8.16b        //MODULO - fold into low
	mov	x20, v1.d[1]                    //AES block 4k+5 - mov high
	eor	x6, x6, x13                   //AES block 4k+4 - round 10 low
	eor	v2.16b, v6.16b, v2.16b          //AES block 4k+6 - result
	mov	x19, v1.d[0]                    //AES block 4k+5 - mov low
	add	w12, w12, #1                    //CTR block 4k+9
	aese	v3.16b, v27.16b                 //AES block 4k+7 - round 9
	fmov	d1, x10                         //CTR block 4k+9
	cmp	x0, x5                   //.LOOP CONTROL
	rev64	v4.16b, v4.16b                  //GHASH block 4k+4
	eor	v11.16b, v11.16b, v10.16b       //MODULO - fold into low
	fmov	v1.d[1], x9                     //CTR block 4k+9
	rev	w9, w12                         //CTR block 4k+10
	add	w12, w12, #1                    //CTR block 4k+10
	eor	x20, x20, x14                   //AES block 4k+5 - round 10 high
	stp	x6, x7, [x2], #16        //AES block 4k+4 - store result
	eor	x19, x19, x13                   //AES block 4k+5 - round 10 low
	stp	x19, x20, [x2], #16        //AES block 4k+5 - store result
	orr	x9, x11, x9, lsl #32            //CTR block 4k+10
	b.lt	.L128_dec_main_loop
.L128_dec_prepretail:	//PREPRETAIL
	ext	v11.16b, v11.16b, v11.16b, #8   //PRE 0
	mov	x21, v2.d[0]                    //AES block 4k+2 - mov low
	mov	d30, v5.d[1]                    //GHASH block 4k+1 - mid
	aes_encrypt_round	v0, v18         //AES block 4k+4 - round 0
	eor	v3.16b, v7.16b, v3.16b          //AES block 4k+3 - result
	aes_encrypt_round	v1, v18         //AES block 4k+5 - round 0
	mov	x22, v2.d[1]                    //AES block 4k+2 - mov high
	eor	v4.16b, v4.16b, v11.16b         //PRE 1
	fmov	d2, x10                         //CTR block 4k+6
	rev64	v6.16b, v6.16b                  //GHASH block 4k+2
	aes_encrypt_round	v0, v19         //AES block 4k+4 - round 1
	fmov	v2.d[1], x9                     //CTR block 4k+6
	rev	w9, w12                         //CTR block 4k+7
	mov	x23, v3.d[0]                    //AES block 4k+3 - mov low
	eor	v30.8b, v30.8b, v5.8b           //GHASH block 4k+1 - mid
	pmull	v11.1q, v4.1d, v15.1d           //GHASH block 4k - low
	mov	d10, v17.d[1]                   //GHASH block 4k - mid
	mov	x24, v3.d[1]                    //AES block 4k+3 - mov high
	aes_encrypt_round	v1, v19         //AES block 4k+5 - round 1
	mov	d31, v6.d[1]                    //GHASH block 4k+2 - mid
	aes_encrypt_round	v0, v20         //AES block 4k+4 - round 2
	orr	x9, x11, x9, lsl #32            //CTR block 4k+7
	pmull	v29.1q, v5.1d, v14.1d           //GHASH block 4k+1 - low
	mov	d8, v4.d[1]                     //GHASH block 4k - mid
	fmov	d3, x10                         //CTR block 4k+7
	aes_encrypt_round	v2, v18         //AES block 4k+6 - round 0
	fmov	v3.d[1], x9                     //CTR block 4k+7
	pmull	v30.1q, v30.1d, v17.1d          //GHASH block 4k+1 - mid
	eor	v31.8b, v31.8b, v6.8b           //GHASH block 4k+2 - mid
	rev64	v7.16b, v7.16b                  //GHASH block 4k+3
	aes_encrypt_round	v2, v19         //AES block 4k+6 - round 1
	eor	v8.8b, v8.8b, v4.8b             //GHASH block 4k - mid
	pmull2	v9.1q, v4.2d, v15.2d            //GHASH block 4k - high
	aes_encrypt_round	v3, v18         //AES block 4k+7 - round 0
	ins	v31.d[1], v31.d[0]              //GHASH block 4k+2 - mid
	pmull2	v28.1q, v5.2d, v14.2d           //GHASH block 4k+1 - high
	pmull	v10.1q, v8.1d, v10.1d           //GHASH block 4k - mid
	eor	v11.16b, v11.16b, v29.16b       //GHASH block 4k+1 - low
	pmull	v29.1q, v7.1d, v12.1d           //GHASH block 4k+3 - low
	pmull2	v31.1q, v31.2d, v16.2d          //GHASH block 4k+2 - mid
	eor	v9.16b, v9.16b, v28.16b         //GHASH block 4k+1 - high
	eor	v10.16b, v10.16b, v30.16b       //GHASH block 4k+1 - mid
	pmull2	v4.1q, v7.2d, v12.2d            //GHASH block 4k+3 - high
	pmull2	v8.1q, v6.2d, v13.2d            //GHASH block 4k+2 - high
	mov	d30, v7.d[1]                    //GHASH block 4k+3 - mid
	aes_encrypt_round	v1, v20         //AES block 4k+5 - round 2
	eor	v10.16b, v10.16b, v31.16b       //GHASH block 4k+2 - mid
	pmull	v28.1q, v6.1d, v13.1d           //GHASH block 4k+2 - low
	eor	v9.16b, v9.16b, v8.16b          //GHASH block 4k+2 - high
	movi	v8.8b, #0xc2
	aes_encrypt_round	v3, v19         //AES block 4k+7 - round 1
	eor	v30.8b, v30.8b, v7.8b           //GHASH block 4k+3 - mid
	eor	v11.16b, v11.16b, v28.16b       //GHASH block 4k+2 - low
	aes_encrypt_round	v2, v20         //AES block 4k+6 - round 2
	eor	v9.16b, v9.16b, v4.16b          //GHASH block 4k+3 - high
	aes_encrypt_round	v3, v20         //AES block 4k+7 - round 2
	eor	x23, x23, x13                   //AES block 4k+3 - round 10 low
	pmull	v30.1q, v30.1d, v16.1d          //GHASH block 4k+3 - mid
	eor	x21, x21, x13                   //AES block 4k+2 - round 10 low
	eor	v11.16b, v11.16b, v29.16b       //GHASH block 4k+3 - low
	aes_encrypt_round	v2, v21         //AES block 4k+6 - round 3
	aes_encrypt_round	v1, v21         //AES block 4k+5 - round 3
	shl	d8, d8, #56               //mod_constant
	aes_encrypt_round	v0, v21         //AES block 4k+4 - round 3
	aes_encrypt_round	v2, v22         //AES block 4k+6 - round 4
	eor	v10.16b, v10.16b, v30.16b       //GHASH block 4k+3 - mid
	aes_encrypt_round	v1, v22         //AES block 4k+5 - round 4
	aes_encrypt_round	v3, v21         //AES block 4k+7 - round 3
	eor	v30.16b, v11.16b, v9.16b        //MODULO - karatsuba tidy up
	aes_encrypt_round	v2, v23         //AES block 4k+6 - round 5
	aes_encrypt_round	v1, v23         //AES block 4k+5 - round 5
	aes_encrypt_round	v3, v22         //AES block 4k+7 - round 4
	aes_encrypt_round	v0, v22         //AES block 4k+4 - round 4
	eor	v10.16b, v10.16b, v30.16b       //MODULO - karatsuba tidy up
	pmull	v31.1q, v9.1d, v8.1d           //MODULO - top 64b align with mid
	aes_encrypt_round	v1, v24         //AES block 4k+5 - round 6
	ext	v9.16b, v9.16b, v9.16b, #8      //MODULO - other top alignment
	aes_encrypt_round	v3, v23         //AES block 4k+7 - round 5
	aes_encrypt_round	v0, v23         //AES block 4k+4 - round 5
	eor	v10.16b, v10.16b, v31.16b       //MODULO - fold into mid
	aes_encrypt_round	v1, v25         //AES block 4k+5 - round 7
	aes_encrypt_round	v2, v24         //AES block 4k+6 - round 6
	ldr	q27, [x8, #144]                 //load rk9
	aes_encrypt_round	v0, v24         //AES block 4k+4 - round 6
	aes_encrypt_round	v1, v26         //AES block 4k+5 - round 8
	eor	v10.16b, v10.16b, v9.16b        //MODULO - fold into mid
	aes_encrypt_round	v3, v24         //AES block 4k+7 - round 6
	ldr	q28, [x8, #160]                 //load rk9
	aes_encrypt_round	v0, v25         //AES block 4k+4 - round 7
	pmull	v8.1q, v10.1d, v8.1d     //MODULO - mid 64b align with low
	eor	x24, x24, x14                   //AES block 4k+3 - round 10 high
	aes_encrypt_round	v2, v25         //AES block 4k+6 - round 7
	ext	v10.16b, v10.16b, v10.16b, #8   //MODULO - other mid alignment
	aes_encrypt_round	v3, v25         //AES block 4k+7 - round 7
	aes_encrypt_round	v0, v26         //AES block 4k+4 - round 8
	eor	v11.16b, v11.16b, v8.16b        //MODULO - fold into low
	aes_encrypt_round	v2, v26         //AES block 4k+6 - round 8
	aes_encrypt_round	v3, v26         //AES block 4k+7 - round 8
	mov	x6, x26
	sub	x6,x6,#10
	cbz	x6, .Lleft3_dec_rounds
	aes_enc_extra_round	12
	sub	x6,x6,#2
	cbz	x6, .Lleft3_dec_rounds
	aes_enc_extra_round	14
.Lleft3_dec_rounds:
	eor	x22, x22, x14                   //AES block 4k+2 - round 10 high
	aese	v0.16b, v27.16b                 //AES block 4k+4 - round 9
	stp	x21, x22, [x2], #16        //AES block 4k+2 - store result
	aese	v1.16b, v27.16b                 //AES block 4k+5 - round 9
	aese	v2.16b, v27.16b                 //AES block 4k+6 - round 9
	add	w12, w12, #1                    //CTR block 4k+7
	stp	x23, x24, [x2], #16        //AES block 4k+3 - store result
	aese	v3.16b, v27.16b                 //AES block 4k+7 - round 9
	eor	v11.16b, v11.16b, v10.16b       //MODULO - fold into low
.L128_dec_tail:	//TAIL
	sub	x5, x4, x0   //main_end_input_ptr is number of bytes left
	ld1	{ v5.16b}, [x0], #16            //AES block 4k+4 - load cipher
	eor	v0.16b, v5.16b, v0.16b          //AES block 4k+4 - result
	mov	x7, v0.d[1]                     //AES block 4k+4 - mov high
	mov	x6, v0.d[0]                     //AES block 4k+4 - mov low
	cmp	x5, #48
	eor	x7, x7, x14                     //AES block 4k+4 - round 10 high
	ext	v8.16b, v11.16b, v11.16b, #8    //prepare final partial tag
	eor	x6, x6, x13                     //AES block 4k+4 - round 10 low
	b.gt	.L128_dec_blocks_more_than_3
	mov	v3.16b, v2.16b
	sub	w12, w12, #1
	movi	v11.8b, #0
	movi	v9.8b, #0
	mov	v2.16b, v1.16b
	movi	v10.8b, #0
	cmp	x5, #32
	b.gt	.L128_dec_blocks_more_than_2
	cmp	x5, #16
	mov	v3.16b, v1.16b
	sub	w12, w12, #1
	b.gt	.L128_dec_blocks_more_than_1
	sub	w12, w12, #1
	b	.L128_dec_blocks_less_than_1
.L128_dec_blocks_more_than_3:	//blocks	left >  3
	rev64	v4.16b, v5.16b                  //GHASH final-3 block
	ld1	{ v5.16b}, [x0], #16            //final-2 block - load cipher
	eor	v4.16b, v4.16b, v8.16b          //feed in partial tag
	mov	d10, v17.d[1]                   //GHASH final-3 block - mid
	stp	x6, x7, [x2], #16        //AES final-3 block  - store result
	eor	v0.16b, v5.16b, v1.16b          //AES final-2 block - result
	mov	d22, v4.d[1]                    //GHASH final-3 block - mid
	mov	x7, v0.d[1]                     //AES final-2 block - mov high
	pmull	v11.1q, v4.1d, v15.1d           //GHASH final-3 block - low
	mov	x6, v0.d[0]                     //AES final-2 block - mov low
	pmull2	v9.1q, v4.2d, v15.2d            //GHASH final-3 block - high
	eor	v22.8b, v22.8b, v4.8b           //GHASH final-3 block - mid
	movi	v8.8b, #0                       //suppress further partial tag
	eor	x7, x7, x14                   //final-2 block - round 10 high
	pmull	v10.1q, v22.1d, v10.1d          //GHASH final-3 block - mid
	eor	x6, x6, x13                   //AES final-2 block - round 10 low
.L128_dec_blocks_more_than_2:	//blocks	left >  2
	rev64	v4.16b, v5.16b                  //GHASH final-2 block
	ld1	{ v5.16b}, [x0], #16          //final-1 block - load ciphertext
	eor	v4.16b, v4.16b, v8.16b          //feed in partial tag
	eor	v0.16b, v5.16b, v2.16b          //AES final-1 block - result
	stp	x6, x7, [x2], #16        //AES final-2 block  - store result
	mov	d22, v4.d[1]                    //GHASH final-2 block - mid
	pmull	v21.1q, v4.1d, v14.1d           //GHASH final-2 block - low
	pmull2	v20.1q, v4.2d, v14.2d           //GHASH final-2 block - high
	mov	x6, v0.d[0]                     //AES final-1 block - mov low
	mov	x7, v0.d[1]                     //AES final-1 block - mov high
	eor	v22.8b, v22.8b, v4.8b           //GHASH final-2 block - mid
	movi	v8.8b, #0                       //suppress further partial tag
	pmull	v22.1q, v22.1d, v17.1d          //GHASH final-2 block - mid
	eor	x6, x6, x13                   //AES final-1 block - round 10 low
	eor	v11.16b, v11.16b, v21.16b       //GHASH final-2 block - low
	eor	v9.16b, v9.16b, v20.16b         //GHASH final-2 block - high
	eor	v10.16b, v10.16b, v22.16b       //GHASH final-2 block - mid
	eor	x7, x7, x14                   //final-1 block - round 10 high
.L128_dec_blocks_more_than_1:	//blocks	left >  1
	rev64	v4.16b, v5.16b                  //GHASH final-1 block
	ld1	{ v5.16b}, [x0], #16            //final block - load ciphertext
	eor	v4.16b, v4.16b, v8.16b          //feed in partial tag
	mov	d22, v4.d[1]                    //GHASH final-1 block - mid
	eor	v0.16b, v5.16b, v3.16b          //AES final block - result
	eor	v22.8b, v22.8b, v4.8b           //GHASH final-1 block - mid
	stp	x6, x7, [x2], #16        //AES final-1 block  - store result
	mov	x6, v0.d[0]                     //AES final block - mov low
	mov	x7, v0.d[1]                     //AES final block - mov high
	ins	v22.d[1], v22.d[0]              //GHASH final-1 block - mid
	pmull	v21.1q, v4.1d, v13.1d           //GHASH final-1 block - low
	pmull2	v20.1q, v4.2d, v13.2d           //GHASH final-1 block - high
	pmull2	v22.1q, v22.2d, v16.2d          //GHASH final-1 block - mid
	movi	v8.8b, #0                       //suppress further partial tag
	eor	v11.16b, v11.16b, v21.16b       //GHASH final-1 block - low
	eor	v9.16b, v9.16b, v20.16b         //GHASH final-1 block - high
	eor	x7, x7, x14                   //AES final block - round 10 high
	eor	x6, x6, x13                   //AES final block - round 10 low
	eor	v10.16b, v10.16b, v22.16b       //GHASH final-1 block - mid
.L128_dec_blocks_less_than_1:	//blocks        left <= 1
	mvn	x14, xzr                        //rk10_h = 0xffffffffffffffff
	and	x1, x1, #127                    //bit_length %= 128
	mvn	x13, xzr                        //rk10_l = 0xffffffffffffffff
	sub	x1, x1, #128                    //bit_length -= 128
	neg	x1, x1          //bit_length = 128 - #bits in input
	and	x1, x1, #127    //bit_length %= 128
	lsr	x14, x14, x1    //rk10_h is mask for top 64b of last block
	cmp	x1, #64
	csel	x10, x14, xzr, lt
	csel	x9, x13, x14, lt
	fmov	d0, x9                          //ctr0b is mask for last block
	mov	v0.d[1], x10
	and	v5.16b, v5.16b, v0.16b
	rev64	v4.16b, v5.16b                  //GHASH final block
	eor	v4.16b, v4.16b, v8.16b          //feed in partial tag
	ldp	x4, x5, [x2] //load existing bytes we need to not overwrite
	and	x7, x7, x10
	mov	d8, v4.d[1]                     //GHASH final block - mid
	bic	x4, x4, x9                      //mask out low existing bytes
	and	x6, x6, x9
	rev	w9, w12
	bic	x5, x5, x10                     //mask out high existing bytes
	orr	x6, x6, x4
	orr	x7, x7, x5
	str	w9, [x25, #12]                  //store the updated counter
	stp	x6, x7, [x2]
	karasuba_multiply v4, v12, v20, v8, v21
	load_const
	gcm_tidy_up v9, v10, v11, v30, v31
	b	.Ldec_final_tag

.Ldec_final_tag_pre:
	ldr	q12, [x3, #32]                         //load h1l | h1h
	ext	v12.16b, v12.16b, v12.16b, #8
	ldr	q13, [x3, #64]                  //load h2l | h2h
	ext	v13.16b, v13.16b, v13.16b, #8
	trn2	v16.2d,  v12.2d,    v13.2d                      //h2l | h1l
	trn1	v8.2d,    v12.2d,    v13.2d                      //h2h | h1h
	eor	v16.16b, v16.16b, v8.16b                     //h2k | h1k
.Ldec_final_tag:
	cbz	x15, .Lrounds_dec_ret

	ld1	{ v5.16b}, [x15], #16            //load length
	ext	v5.16b, v5.16b, v5.16b, #8   //PRE 0
	rev64	v5.16b, v5.16b                  //GHASH block 4k+1
	eor	v4.16b, v5.16b, v11.16b          //final tax eor with length
	ext	v4.16b, v4.16b, v4.16b, #8   //PRE 0
	movi	v8.8b, #0                       //suppress further partial tag
	mov	d8, v4.d[1]                     //GHASH block 4k - mid
	movi	v11.8b, #0
	movi	v9.8b, #0
	movi	v10.8b, #0
	karasuba_multiply v4, v12, v20, v8, v21
	load_const
	gcm_tidy_up v9, v10, v11, v30, v31
	mov	x6, x26
	ext	v11.16b, v11.16b, v11.16b, #8   //PRE 0
	rev64	v11.16b, v11.16b                  //Final has tag
	aes_enc_iv_init

	add	x19,x8,#160
	sub	x6,x6,#10
	cbz	x6, .Ldec_enc_iv_final
	aes_enc_iv_common       12
	sub	x6,x6,#2
	cbz	x6, .Ldec_enc_iv_final
	aes_enc_iv_common       14
.Ldec_enc_iv_final:
	aes_enc_iv_final

	eor	v11.16b, v4.16b, v11.16b       //final tax eor with length
	ldp	x9, x10, [sp, #128]            //Load otag pointer and authsize
	adr_l	x26, .Lpermute_table
	ld1	{ v5.16b}, [x9], #16           //load otag
	add	x26, x26, x10
	ld1	{v9.16b}, [x26]                // load permute vector

	cmeq	v5.16b, v5.16b, v11.16b        // compare tags
	mvn	v5.16b, v5.16b                 // -1 for fail, 0 for pass
	tbl	v5.16b, {v5.16b}, v9.16b       // keep authsize bytes only
	sminv	b0, v5.16b                     // signed minimum across XL
	smov	w0, v0.b[0]                    // return b0

.Lrounds_dec_ret:
	ext	v11.16b, v11.16b, v11.16b, #8   //PRE 0
	rev64	v11.16b, v11.16b
	st1	{ v11.16b }, [x3]
	pop_stack
	ret
SYM_FUNC_END(pmull_gcm_decrypt_unroll)
.align	6
.Lpermute_table:
        .byte           0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
        .byte           0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
        .byte            0x0,  0x1,  0x2,  0x3,  0x4,  0x5,  0x6,  0x7
        .byte            0x8,  0x9,  0xa,  0xb,  0xc,  0xd,  0xe,  0xf
        .byte           0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
        .byte           0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
        .byte            0x0,  0x1,  0x2,  0x3,  0x4,  0x5,  0x6,  0x7
        .byte            0x8,  0x9,  0xa,  0xb,  0xc,  0xd,  0xe,  0xf
        .previous
