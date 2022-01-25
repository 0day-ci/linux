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

	.inst	0x4bf5820		// RDSVL x0, #1

	ret
