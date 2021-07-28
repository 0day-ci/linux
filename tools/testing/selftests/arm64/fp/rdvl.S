// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2021 ARM Limited.

.arch_extension sve

.globl rdvl_sve
rdvl_sve:
	rdvl	x0, #1
	ret
