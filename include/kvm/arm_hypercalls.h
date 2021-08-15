/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019 Arm Ltd. */

#ifndef __KVM_ARM_HYPERCALLS_H
#define __KVM_ARM_HYPERCALLS_H

#include <asm/kvm_emulate.h>

#define SMCCC_DECLARE_GET_FUNC(type, name, reg)			\
static inline type smccc_get_##name(struct kvm_vcpu *vcpu)	\
{								\
	return vcpu_get_reg(vcpu, reg);				\
}

SMCCC_DECLARE_GET_FUNC(u32,           function, 0)
SMCCC_DECLARE_GET_FUNC(unsigned long, arg1,     1)
SMCCC_DECLARE_GET_FUNC(unsigned long, arg2,     2)
SMCCC_DECLARE_GET_FUNC(unsigned long, arg3,     3)
SMCCC_DECLARE_GET_FUNC(unsigned long, arg4,     4)
SMCCC_DECLARE_GET_FUNC(unsigned long, arg5,     5)
SMCCC_DECLARE_GET_FUNC(unsigned long, arg6,     6)
SMCCC_DECLARE_GET_FUNC(unsigned long, arg7,     7)
SMCCC_DECLARE_GET_FUNC(unsigned long, arg8,     8)

static inline void smccc_set_retval(struct kvm_vcpu *vcpu,
				    unsigned long a0,
				    unsigned long a1,
				    unsigned long a2,
				    unsigned long a3)
{
	vcpu_set_reg(vcpu, 0, a0);
	vcpu_set_reg(vcpu, 1, a1);
	vcpu_set_reg(vcpu, 2, a2);
	vcpu_set_reg(vcpu, 3, a3);
}

int kvm_hvc_call_handler(struct kvm_vcpu *vcpu);

#endif
