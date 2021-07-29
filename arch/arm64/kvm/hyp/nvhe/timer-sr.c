// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012-2015 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <clocksource/arm_arch_timer.h>
#include <linux/compiler.h>
#include <linux/kvm_host.h>

#include <asm/kvm_hyp.h>
#include <hyp/timer-sr.h>

void __kvm_timer_set_cntvoff(u64 cntvoff)
{
	write_sysreg(cntvoff, cntvoff_el2);
}

void __timer_disable_traps(struct kvm_vcpu *vcpu)
{
	u64 val;

	/* Allow physical timer/counter access for the host */
	val = read_sysreg(cnthctl_el2);
	val |= CNTHCTL_EL1PCTEN | CNTHCTL_EL1PCEN;
	write_sysreg(val, cnthctl_el2);
}

void __timer_enable_traps(struct kvm_vcpu *vcpu)
{
	u64 val;

	/*
	 * Disallow physical timer access for the guest
	 */
	val = read_sysreg(cnthctl_el2);
	val &= ~CNTHCTL_EL1PCEN;

	/*
	 * Disallow physical counter access for the guest if offsetting is
	 * requested.
	 */
	if (__timer_physical_emulation_required(vcpu))
		val &= ~CNTHCTL_EL1PCTEN;
	else
		val |= CNTHCTL_EL1PCTEN;

	write_sysreg(val, cnthctl_el2);
}
