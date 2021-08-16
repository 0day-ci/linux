// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012-2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <asm/kvm_hyp.h>

void __kvm_timer_set_cntvoff(u64 cntvoff)
{
	write_sysreg(cntvoff, cntvoff_el2);
}

void __kvm_timer_set_cntpoff(u64 cntpoff)
{
	write_sysreg_s(cntpoff, SYS_CNTPOFF_EL2);
}
