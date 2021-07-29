// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012-2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <asm/kvm_host.h>
#include <asm/kvm_hyp.h>
#include <hyp/timer-sr.h>

void __kvm_timer_set_cntvoff(u64 cntvoff)
{
	write_sysreg(cntvoff, cntvoff_el2);
}

void __timer_enable_traps(struct kvm_vcpu *vcpu)
{
	/* When HCR_EL2.E2H == 1, EL1PCEN nad EL1PCTEN are shifted by 10 */
	u32 cnthctl_shift = 10;
	u64 val, mask;

	mask = CNTHCTL_EL1PCEN << cnthctl_shift;
	mask |= CNTHCTL_EL1PCTEN << cnthctl_shift;

	val = read_sysreg(cnthctl_el2);

	/*
	 * VHE systems allow the guest direct access to the EL1 physical
	 * timer/counter if offsetting isn't requested.
	 */
	if (__timer_physical_emulation_required(vcpu))
		val &= ~mask;
	else
		val |= mask;

	write_sysreg(val, cnthctl_el2);
}

void __timer_disable_traps(struct kvm_vcpu *vcpu) {}
