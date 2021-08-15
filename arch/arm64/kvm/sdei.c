// SPDX-License-Identifier: GPL-2.0-only
/*
 * SDEI virtualization support.
 *
 * Copyright (C) 2021 Red Hat, Inc.
 *
 * Author(s): Gavin Shan <gshan@redhat.com>
 */

#include <linux/kernel.h>
#include <linux/kvm_host.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <kvm/arm_hypercalls.h>

static struct kvm_sdei_event_state defined_kse[] = {
	{ KVM_SDEI_DEFAULT_NUM,
	  SDEI_EVENT_TYPE_PRIVATE,
	  1,
	  SDEI_EVENT_PRIORITY_CRITICAL
	},
};

static void kvm_sdei_remove_events(struct kvm *kvm)
{
	struct kvm_sdei_kvm *ksdei = kvm->arch.sdei;
	struct kvm_sdei_event *kse, *tmp;

	list_for_each_entry_safe(kse, tmp, &ksdei->events, link) {
		list_del(&kse->link);
		kfree(kse);
	}
}

static void kvm_sdei_remove_kvm_events(struct kvm *kvm,
				       unsigned int mask,
				       bool force)
{
	struct kvm_sdei_kvm *ksdei = kvm->arch.sdei;
	struct kvm_sdei_event *kse;
	struct kvm_sdei_kvm_event *kske, *tmp;

	list_for_each_entry_safe(kske, tmp, &ksdei->kvm_events, link) {
		kse = kske->kse;

		if (!((1 << kse->state.type) & mask))
			continue;

		if (!force && kske->state.refcount)
			continue;

		list_del(&kske->link);
		kfree(kske);
	}
}

static void kvm_sdei_remove_vcpu_events(struct kvm_vcpu *vcpu)
{
	struct kvm_sdei_vcpu *vsdei = vcpu->arch.sdei;
	struct kvm_sdei_vcpu_event *ksve, *tmp;

	list_for_each_entry_safe(ksve, tmp, &vsdei->critical_events, link) {
		list_del(&ksve->link);
		kfree(ksve);
	}

	list_for_each_entry_safe(ksve, tmp, &vsdei->normal_events, link) {
		list_del(&ksve->link);
		kfree(ksve);
	}
}

static unsigned long kvm_sdei_hypercall_version(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_sdei_kvm *ksdei = kvm->arch.sdei;
	struct kvm_sdei_vcpu *vsdei = vcpu->arch.sdei;
	unsigned long ret = SDEI_NOT_SUPPORTED;

	if (!(ksdei && vsdei))
		return ret;

	/* v1.0.0 */
	ret = (1UL << SDEI_VERSION_MAJOR_SHIFT);

	return ret;
}

int kvm_sdei_hypercall(struct kvm_vcpu *vcpu)
{
	u32 func = smccc_get_function(vcpu);
	bool has_result = true;
	unsigned long ret;

	switch (func) {
	case SDEI_1_0_FN_SDEI_VERSION:
		ret = kvm_sdei_hypercall_version(vcpu);
		break;
	case SDEI_1_0_FN_SDEI_EVENT_REGISTER:
	case SDEI_1_0_FN_SDEI_EVENT_ENABLE:
	case SDEI_1_0_FN_SDEI_EVENT_DISABLE:
	case SDEI_1_0_FN_SDEI_EVENT_CONTEXT:
	case SDEI_1_0_FN_SDEI_EVENT_COMPLETE:
	case SDEI_1_0_FN_SDEI_EVENT_COMPLETE_AND_RESUME:
	case SDEI_1_0_FN_SDEI_EVENT_UNREGISTER:
	case SDEI_1_0_FN_SDEI_EVENT_STATUS:
	case SDEI_1_0_FN_SDEI_EVENT_GET_INFO:
	case SDEI_1_0_FN_SDEI_EVENT_ROUTING_SET:
	case SDEI_1_0_FN_SDEI_PE_MASK:
	case SDEI_1_0_FN_SDEI_PE_UNMASK:
	case SDEI_1_0_FN_SDEI_INTERRUPT_BIND:
	case SDEI_1_0_FN_SDEI_INTERRUPT_RELEASE:
	case SDEI_1_0_FN_SDEI_PRIVATE_RESET:
	case SDEI_1_0_FN_SDEI_SHARED_RESET:
	default:
		ret = SDEI_NOT_SUPPORTED;
	}

	/*
	 * We don't have return value for COMPLETE or COMPLETE_AND_RESUME
	 * hypercalls. Otherwise, the restored context will be corrupted.
	 */
	if (has_result)
		smccc_set_retval(vcpu, ret, 0, 0, 0);

	return 1;
}

void kvm_sdei_init_vm(struct kvm *kvm)
{
	struct kvm_sdei_kvm *ksdei;
	struct kvm_sdei_event *kse;
	int i;

	ksdei = kzalloc(sizeof(*ksdei), GFP_KERNEL);
	if (!ksdei)
		return;

	spin_lock_init(&ksdei->lock);
	INIT_LIST_HEAD(&ksdei->events);
	INIT_LIST_HEAD(&ksdei->kvm_events);

	/*
	 * Populate the defined KVM SDEI events. The whole functionality
	 * will be disabled on any errors.
	 */
	for (i = 0; i < ARRAY_SIZE(defined_kse); i++) {
		kse = kzalloc(sizeof(*kse), GFP_KERNEL);
		if (!kse) {
			kvm_sdei_remove_events(kvm);
			kfree(ksdei);
			return;
		}

		kse->kvm   = kvm;
		kse->state = defined_kse[i];
		list_add_tail(&kse->link, &ksdei->events);
	}

	kvm->arch.sdei = ksdei;
}

void kvm_sdei_create_vcpu(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_sdei_vcpu *vsdei;

	if (!kvm->arch.sdei)
		return;

	vsdei = kzalloc(sizeof(*vsdei), GFP_KERNEL);
	if (!vsdei)
		return;

	spin_lock_init(&vsdei->lock);
	vsdei->state.masked       = 1;
	vsdei->state.critical_num = KVM_SDEI_INVALID_NUM;
	vsdei->state.normal_num   = KVM_SDEI_INVALID_NUM;
	vsdei->critical_event     = NULL;
	vsdei->normal_event       = NULL;
	INIT_LIST_HEAD(&vsdei->critical_events);
	INIT_LIST_HEAD(&vsdei->normal_events);

	vcpu->arch.sdei = vsdei;
}

void kvm_sdei_destroy_vcpu(struct kvm_vcpu *vcpu)
{
	struct kvm_sdei_vcpu *vsdei = vcpu->arch.sdei;

	if (vsdei) {
		spin_lock(&vsdei->lock);
		kvm_sdei_remove_vcpu_events(vcpu);
		spin_unlock(&vsdei->lock);

		kfree(vsdei);
		vcpu->arch.sdei = NULL;
	}
}

void kvm_sdei_destroy_vm(struct kvm *kvm)
{
	struct kvm_sdei_kvm *ksdei = kvm->arch.sdei;
	unsigned int mask = (1 << SDEI_EVENT_TYPE_PRIVATE) |
			    (1 << SDEI_EVENT_TYPE_SHARED);

	if (ksdei) {
		spin_lock(&ksdei->lock);
		kvm_sdei_remove_kvm_events(kvm, mask, true);
		kvm_sdei_remove_events(kvm);
		spin_unlock(&ksdei->lock);

		kfree(ksdei);
		kvm->arch.sdei = NULL;
	}
}
