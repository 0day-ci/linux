/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Definitions of various KVM SDEI events.
 *
 * Copyright (C) 2021 Red Hat, Inc.
 *
 * Author(s): Gavin Shan <gshan@redhat.com>
 */

#ifndef __ARM64_KVM_SDEI_H__
#define __ARM64_KVM_SDEI_H__

#include <uapi/linux/arm_sdei.h>
#include <uapi/asm/kvm_sdei.h>
#include <linux/bitmap.h>
#include <linux/list.h>
#include <linux/spinlock.h>

struct kvm_vcpu;

typedef void (*kvm_sdei_notifier)(struct kvm_vcpu *vcpu,
				  unsigned long num,
				  unsigned int state);
enum {
	KVM_SDEI_NOTIFY_DELIVERED,
	KVM_SDEI_NOTIFY_COMPLETED,
};

struct kvm_sdei_event {
	struct kvm_sdei_event_state		state;
	struct kvm				*kvm;
	struct list_head			link;
};

struct kvm_sdei_kvm_event {
	struct kvm_sdei_kvm_event_state		state;
	struct kvm_sdei_event			*kse;
	struct kvm				*kvm;
	struct list_head			link;
};

struct kvm_sdei_vcpu_event {
	struct kvm_sdei_vcpu_event_state	state;
	struct kvm_sdei_kvm_event		*kske;
	struct kvm_vcpu				*vcpu;
	struct list_head			link;
};

struct kvm_sdei_kvm {
	spinlock_t		lock;
	struct list_head	events;		/* kvm_sdei_event */
	struct list_head	kvm_events;	/* kvm_sdei_kvm_event */
};

struct kvm_sdei_vcpu {
	spinlock_t                      lock;
	struct kvm_sdei_vcpu_state      state;
	struct kvm_sdei_vcpu_event      *critical_event;
	struct kvm_sdei_vcpu_event      *normal_event;
	struct list_head                critical_events;
	struct list_head                normal_events;
};

/*
 * According to SDEI specification (v1.0), the event number spans 32-bits
 * and the lower 24-bits are used as the (real) event number. I don't
 * think we can use that much SDEI numbers in one system. So we reserve
 * two bits from the 24-bits real event number, to indicate its types:
 * physical event and virtual event. One reserved bit is enough for now,
 * but two bits are reserved for possible extension in future.
 *
 * The physical events are owned by underly firmware while the virtual
 * events are used by VMM and KVM.
 */
#define KVM_SDEI_EV_NUM_TYPE_SHIFT	22
#define KVM_SDEI_EV_NUM_TYPE_MASK	3
#define KVM_SDEI_EV_NUM_TYPE_PHYS	0
#define KVM_SDEI_EV_NUM_TYPE_VIRT	1

static inline bool kvm_sdei_is_valid_event_num(unsigned long num)
{
	unsigned long type;

	if (num >> 32)
		return false;

	type = (num >> KVM_SDEI_EV_NUM_TYPE_SHIFT) & KVM_SDEI_EV_NUM_TYPE_MASK;
	if (type != KVM_SDEI_EV_NUM_TYPE_VIRT)
		return false;

	return true;
}

/* Accessors for the registration or enablement states of KVM event */
#define KVM_SDEI_FLAG_FUNC(field)					   \
static inline bool kvm_sdei_is_##field(struct kvm_sdei_kvm_event *kske,	   \
				       unsigned int index)		   \
{									   \
	return !!test_bit(index, (void *)(kske->state.field));		   \
}									   \
									   \
static inline bool kvm_sdei_empty_##field(struct kvm_sdei_kvm_event *kske) \
{									   \
	return bitmap_empty((void *)(kske->state.field),		   \
			    KVM_SDEI_MAX_VCPUS);			   \
}									   \
static inline void kvm_sdei_set_##field(struct kvm_sdei_kvm_event *kske,   \
					unsigned int index)		   \
{									   \
	set_bit(index, (void *)(kske->state.field));			   \
}									   \
static inline void kvm_sdei_clear_##field(struct kvm_sdei_kvm_event *kske, \
					  unsigned int index)		   \
{									   \
	clear_bit(index, (void *)(kske->state.field));			   \
}

KVM_SDEI_FLAG_FUNC(registered)
KVM_SDEI_FLAG_FUNC(enabled)

/* APIs */
void kvm_sdei_init_vm(struct kvm *kvm);
void kvm_sdei_create_vcpu(struct kvm_vcpu *vcpu);
int kvm_sdei_hypercall(struct kvm_vcpu *vcpu);
int kvm_sdei_register_notifier(struct kvm *kvm, unsigned long num,
			       kvm_sdei_notifier notifier);
void kvm_sdei_deliver(struct kvm_vcpu *vcpu);
long kvm_sdei_vm_ioctl(struct kvm *kvm, unsigned long arg);
long kvm_sdei_vcpu_ioctl(struct kvm_vcpu *vcpu, unsigned long arg);
void kvm_sdei_destroy_vcpu(struct kvm_vcpu *vcpu);
void kvm_sdei_destroy_vm(struct kvm *kvm);

#endif /* __ARM64_KVM_SDEI_H__ */
