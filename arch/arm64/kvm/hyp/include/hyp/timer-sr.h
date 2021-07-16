/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Google LLC
 * Author: Oliver Upton <oupton@google.com>
 */

#ifndef __ARM64_KVM_HYP_TIMER_SR_H__
#define __ARM64_KVM_HYP_TIMER_SR_H__

#include <linux/compiler.h>
#include <linux/kvm_host.h>

#include <asm/kvm_asm.h>
#include <asm/kvm_hyp.h>

static inline bool __timer_physical_emulation_required(struct kvm_vcpu *vcpu)
{
	return __vcpu_sys_reg(vcpu, CNTPOFF_EL2);
}

static inline u64 __timer_read_cntpct(struct kvm_vcpu *vcpu)
{
	return read_sysreg(cntpct_el0) - __vcpu_sys_reg(vcpu, CNTPOFF_EL2);
}

#endif /* __ARM64_KVM_HYP_TIMER_SR_H__ */
