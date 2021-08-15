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
	__u64	notifier;
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

#define KVM_SDEI_CMD_GET_VERSION		0
#define KVM_SDEI_CMD_SET_EVENT			1
#define KVM_SDEI_CMD_GET_KEVENT_COUNT		2
#define KVM_SDEI_CMD_GET_KEVENT			3
#define KVM_SDEI_CMD_SET_KEVENT			4
#define KVM_SDEI_CMD_GET_VEVENT_COUNT		5
#define KVM_SDEI_CMD_GET_VEVENT			6
#define KVM_SDEI_CMD_SET_VEVENT			7
#define KVM_SDEI_CMD_GET_VCPU_STATE		8
#define KVM_SDEI_CMD_SET_VCPU_STATE		9

struct kvm_sdei_cmd {
	__u32						cmd;
	union {
		__u32					version;
		__u32					count;
		__u64					num;
		struct kvm_sdei_event_state		kse_state;
		struct kvm_sdei_kvm_event_state		kske_state;
		struct kvm_sdei_vcpu_event_state	ksve_state;
		struct kvm_sdei_vcpu_state		ksv_state;
	};
};

#endif /* !__ASSEMBLY__ */
#endif /* _UAPI__ASM_KVM_SDEI_H */
