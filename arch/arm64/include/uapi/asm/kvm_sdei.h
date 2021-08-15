/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Definitions of various KVM SDEI event states.
 *
 * Copyright (C) 2021 Red Hat, Inc.
 *
 * Author(s): Gavin Shan <gshan@redhat.com>
 */

#ifndef _UAPI__ASM_KVM_SDEI_H
#define _UAPI__ASM_KVM_SDEI_H

#ifndef __ASSEMBLY__
#include <linux/types.h>

#define KVM_SDEI_MAX_VCPUS	512
#define KVM_SDEI_INVALID_NUM	0
#define KVM_SDEI_DEFAULT_NUM	0x40400000

struct kvm_sdei_event_state {
	__u64	num;

	__u8	type;
	__u8	signaled;
	__u8	priority;
};

struct kvm_sdei_kvm_event_state {
	__u64	num;
	__u32	refcount;

	__u8	route_mode;
	__u64	route_affinity;
	__u64	entries[KVM_SDEI_MAX_VCPUS];
	__u64	params[KVM_SDEI_MAX_VCPUS];
	__u64	registered[KVM_SDEI_MAX_VCPUS/64];
	__u64	enabled[KVM_SDEI_MAX_VCPUS/64];
};

struct kvm_sdei_vcpu_event_state {
	__u64	num;
	__u32	refcount;
};

struct kvm_sdei_vcpu_regs {
	__u64	regs[18];
	__u64	pc;
	__u64	pstate;
};

struct kvm_sdei_vcpu_state {
	__u8				masked;
	__u64				critical_num;
	__u64				normal_num;
	struct kvm_sdei_vcpu_regs	critical_regs;
	struct kvm_sdei_vcpu_regs	normal_regs;
};

#endif /* !__ASSEMBLY__ */
#endif /* _UAPI__ASM_KVM_SDEI_H */
