/* SPDX-License-Identifier: GPL-2.0-only */
.globl hcall;

hcall:
	sc	1
	blr
