// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2021 ARM Limited.

.arch_extension sve

.globl rdvl_sve
rdvl_sve:
	hint	34			// BTI C
	rdvl	x0, #1
	ret

.globl rdvl_sme
rdvl_sme:
	hint	34			// BTI C

	// Enter streaming mode
	mov	x16, #1
	msr	S3_3_C4_C2_2, x16

	rdvl	x0, #1

	// Leave streaming mode
	mov	x16, #0
	msr	S3_3_C4_C2_2, x16

	ret
