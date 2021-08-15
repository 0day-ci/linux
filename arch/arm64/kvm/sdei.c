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

static struct kvm_sdei_event *kvm_sdei_find_event(struct kvm *kvm,
						  unsigned long num)
{
	struct kvm_sdei_kvm *ksdei = kvm->arch.sdei;
	struct kvm_sdei_event *kse;

	list_for_each_entry(kse, &ksdei->events, link) {
		if (kse->state.num == num)
			return kse;
	}

	return NULL;
}

static void kvm_sdei_remove_events(struct kvm *kvm)
{
	struct kvm_sdei_kvm *ksdei = kvm->arch.sdei;
	struct kvm_sdei_event *kse, *tmp;

	list_for_each_entry_safe(kse, tmp, &ksdei->events, link) {
		list_del(&kse->link);
		kfree(kse);
	}
}

static struct kvm_sdei_kvm_event *kvm_sdei_find_kvm_event(struct kvm *kvm,
							  unsigned long num)
{
	struct kvm_sdei_kvm *ksdei = kvm->arch.sdei;
	struct kvm_sdei_kvm_event *kske;

	list_for_each_entry(kske, &ksdei->kvm_events, link) {
		if (kske->state.num == num)
			return kske;
	}

	return NULL;
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

static unsigned long kvm_sdei_hypercall_register(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_sdei_kvm *ksdei = kvm->arch.sdei;
	struct kvm_sdei_vcpu *vsdei = vcpu->arch.sdei;
	struct kvm_sdei_event *kse = NULL;
	struct kvm_sdei_kvm_event *kske = NULL;
	unsigned long event_num = smccc_get_arg1(vcpu);
	unsigned long event_entry = smccc_get_arg2(vcpu);
	unsigned long event_param = smccc_get_arg3(vcpu);
	unsigned long route_mode = smccc_get_arg4(vcpu);
	unsigned long route_affinity = smccc_get_arg5(vcpu);
	int index = vcpu->vcpu_idx;
	unsigned long ret = SDEI_SUCCESS;

	/* Sanity check */
	if (!(ksdei && vsdei)) {
		ret = SDEI_NOT_SUPPORTED;
		goto out;
	}

	if (!kvm_sdei_is_valid_event_num(event_num)) {
		ret = SDEI_INVALID_PARAMETERS;
		goto out;
	}

	if (!(route_mode == SDEI_EVENT_REGISTER_RM_ANY ||
	      route_mode == SDEI_EVENT_REGISTER_RM_PE)) {
		ret = SDEI_INVALID_PARAMETERS;
		goto out;
	}

	/*
	 * The KVM event could have been created if it's a private event.
	 * We needn't create a KVM event in this case.
	 */
	spin_lock(&ksdei->lock);
	kske = kvm_sdei_find_kvm_event(kvm, event_num);
	if (kske) {
		kse = kske->kse;
		index = (kse->state.type == SDEI_EVENT_TYPE_PRIVATE) ?
			vcpu->vcpu_idx : 0;

		if (kvm_sdei_is_registered(kske, index)) {
			ret = SDEI_DENIED;
			goto unlock;
		}

		kske->state.route_mode     = route_mode;
		kske->state.route_affinity = route_affinity;
		kske->state.entries[index] = event_entry;
		kske->state.params[index]  = event_param;
		kvm_sdei_set_registered(kske, index);
		goto unlock;
	}

	/* Check if the event number has been registered */
	kse = kvm_sdei_find_event(kvm, event_num);
	if (!kse) {
		ret = SDEI_INVALID_PARAMETERS;
		goto unlock;
	}

	/* Create KVM event */
	kske = kzalloc(sizeof(*kske), GFP_KERNEL);
	if (!kske) {
		ret = SDEI_OUT_OF_RESOURCE;
		goto unlock;
	}

	/* Initialize KVM event state */
	index = (kse->state.type == SDEI_EVENT_TYPE_PRIVATE) ?
		vcpu->vcpu_idx : 0;
	kske->state.num            = event_num;
	kske->state.refcount       = 0;
	kske->state.route_mode     = route_affinity;
	kske->state.route_affinity = route_affinity;
	kske->state.entries[index] = event_entry;
	kske->state.params[index] = event_param;
	kvm_sdei_set_registered(kske, index);

	/* Initialize KVM event */
	kske->kse = kse;
	kske->kvm = kvm;
	list_add_tail(&kske->link, &ksdei->kvm_events);

unlock:
	spin_unlock(&ksdei->lock);
out:
	return ret;
}

static unsigned long kvm_sdei_hypercall_enable(struct kvm_vcpu *vcpu,
					       bool enable)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_sdei_kvm *ksdei = kvm->arch.sdei;
	struct kvm_sdei_vcpu *vsdei = vcpu->arch.sdei;
	struct kvm_sdei_event *kse = NULL;
	struct kvm_sdei_kvm_event *kske = NULL;
	unsigned long event_num = smccc_get_arg1(vcpu);
	int index = 0;
	unsigned long ret = SDEI_SUCCESS;

	/* Sanity check */
	if (!(ksdei && vsdei)) {
		ret = SDEI_NOT_SUPPORTED;
		goto out;
	}

	if (!kvm_sdei_is_valid_event_num(event_num)) {
		ret = SDEI_INVALID_PARAMETERS;
		goto out;
	}

	/* Check if the KVM event exists */
	spin_lock(&ksdei->lock);
	kske = kvm_sdei_find_kvm_event(kvm, event_num);
	if (!kske) {
		ret = SDEI_INVALID_PARAMETERS;
		goto unlock;
	}

	/* Check if there is pending events */
	if (kske->state.refcount) {
		ret = SDEI_PENDING;
		goto unlock;
	}

	/* Check if it has been registered */
	kse = kske->kse;
	index = (kse->state.type == SDEI_EVENT_TYPE_PRIVATE) ?
		vcpu->vcpu_idx : 0;
	if (!kvm_sdei_is_registered(kske, index)) {
		ret = SDEI_DENIED;
		goto unlock;
	}

	/* Verify its enablement state */
	if (enable == kvm_sdei_is_enabled(kske, index)) {
		ret = SDEI_DENIED;
		goto unlock;
	}

	/* Update enablement state */
	if (enable)
		kvm_sdei_set_enabled(kske, index);
	else
		kvm_sdei_clear_enabled(kske, index);

unlock:
	spin_unlock(&ksdei->lock);
out:
	return ret;
}

static unsigned long kvm_sdei_hypercall_context(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_sdei_kvm *ksdei = kvm->arch.sdei;
	struct kvm_sdei_vcpu *vsdei = vcpu->arch.sdei;
	struct kvm_sdei_vcpu_regs *regs;
	unsigned long index = smccc_get_arg1(vcpu);
	unsigned long ret = SDEI_SUCCESS;

	/* Sanity check */
	if (!(ksdei && vsdei)) {
		ret = SDEI_NOT_SUPPORTED;
		goto out;
	}

	if (index > ARRAY_SIZE(vsdei->state.critical_regs.regs)) {
		ret = SDEI_INVALID_PARAMETERS;
		goto out;
	}

	/* Check if the pending event exists */
	spin_lock(&vsdei->lock);
	if (!(vsdei->critical_event || vsdei->normal_event)) {
		ret = SDEI_DENIED;
		goto unlock;
	}

	/* Fetch the requested register */
	regs = vsdei->critical_event ? &vsdei->state.critical_regs :
				       &vsdei->state.normal_regs;
	ret = regs->regs[index];

unlock:
	spin_unlock(&vsdei->lock);
out:
	return ret;
}

static unsigned long kvm_sdei_hypercall_complete(struct kvm_vcpu *vcpu,
						 bool resume)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_sdei_kvm *ksdei = kvm->arch.sdei;
	struct kvm_sdei_vcpu *vsdei = vcpu->arch.sdei;
	struct kvm_sdei_event *kse = NULL;
	struct kvm_sdei_kvm_event *kske = NULL;
	struct kvm_sdei_vcpu_event *ksve = NULL;
	struct kvm_sdei_vcpu_regs *regs;
	kvm_sdei_notifier notifier;
	unsigned long ret = SDEI_SUCCESS;
	int index;

	/* Sanity check */
	if (!(ksdei && vsdei)) {
		ret = SDEI_NOT_SUPPORTED;
		goto out;
	}

	spin_lock(&vsdei->lock);
	if (vsdei->critical_event) {
		ksve = vsdei->critical_event;
		regs = &vsdei->state.critical_regs;
		vsdei->critical_event = NULL;
		vsdei->state.critical_num = KVM_SDEI_INVALID_NUM;
	} else if (vsdei->normal_event) {
		ksve = vsdei->normal_event;
		regs = &vsdei->state.normal_regs;
		vsdei->normal_event = NULL;
		vsdei->state.normal_num = KVM_SDEI_INVALID_NUM;
	} else {
		ret = SDEI_DENIED;
		goto unlock;
	}

	/* Restore registers: x0 -> x17, PC, PState */
	for (index = 0; index < ARRAY_SIZE(regs->regs); index++)
		vcpu_set_reg(vcpu, index, regs->regs[index]);

	*vcpu_cpsr(vcpu) = regs->pstate;
	*vcpu_pc(vcpu) = regs->pc;

	/* Notifier */
	kske = ksve->kske;
	kse = kske->kse;
	notifier = (kvm_sdei_notifier)(kse->state.notifier);
	if (notifier)
		notifier(vcpu, kse->state.num, KVM_SDEI_NOTIFY_COMPLETED);

	/* Inject interrupt if needed */
	if (resume)
		kvm_inject_irq(vcpu);

	/*
	 * Update state. We needn't take lock in order to update the KVM
	 * event state as it's not destroyed because of the reference
	 * count.
	 */
	ksve->state.refcount--;
	kske->state.refcount--;
	if (!ksve->state.refcount) {
		list_del(&ksve->link);
		kfree(ksve);
	}

	/* Make another request if there is pending event */
	if (!(list_empty(&vsdei->critical_events) &&
	      list_empty(&vsdei->normal_events)))
		kvm_make_request(KVM_REQ_SDEI, vcpu);

unlock:
	spin_unlock(&vsdei->lock);
out:
	return ret;
}

static unsigned long kvm_sdei_hypercall_unregister(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_sdei_kvm *ksdei = kvm->arch.sdei;
	struct kvm_sdei_vcpu *vsdei = vcpu->arch.sdei;
	struct kvm_sdei_event *kse = NULL;
	struct kvm_sdei_kvm_event *kske = NULL;
	unsigned long event_num = smccc_get_arg1(vcpu);
	int index = 0;
	unsigned long ret = SDEI_SUCCESS;

	/* Sanity check */
	if (!(ksdei && vsdei)) {
		ret = SDEI_NOT_SUPPORTED;
		goto out;
	}

	if (!kvm_sdei_is_valid_event_num(event_num)) {
		ret = SDEI_INVALID_PARAMETERS;
		goto out;
	}

	/* Check if the KVM event exists */
	spin_lock(&ksdei->lock);
	kske = kvm_sdei_find_kvm_event(kvm, event_num);
	if (!kske) {
		ret = SDEI_INVALID_PARAMETERS;
		goto unlock;
	}

	/* Check if there is pending events */
	if (kske->state.refcount) {
		ret = SDEI_PENDING;
		goto unlock;
	}

	/* Check if it has been registered */
	kse = kske->kse;
	index = (kse->state.type == SDEI_EVENT_TYPE_PRIVATE) ?
		vcpu->vcpu_idx : 0;
	if (!kvm_sdei_is_registered(kske, index)) {
		ret = SDEI_DENIED;
		goto unlock;
	}

	/* The event is disabled when it's unregistered */
	kvm_sdei_clear_enabled(kske, index);
	kvm_sdei_clear_registered(kske, index);
	if (kvm_sdei_empty_registered(kske)) {
		list_del(&kske->link);
		kfree(kske);
	}

unlock:
	spin_unlock(&ksdei->lock);
out:
	return ret;
}

static unsigned long kvm_sdei_hypercall_status(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_sdei_kvm *ksdei = kvm->arch.sdei;
	struct kvm_sdei_vcpu *vsdei = vcpu->arch.sdei;
	struct kvm_sdei_event *kse = NULL;
	struct kvm_sdei_kvm_event *kske = NULL;
	unsigned long event_num = smccc_get_arg1(vcpu);
	int index = 0;
	unsigned long ret = SDEI_SUCCESS;

	/* Sanity check */
	if (!(ksdei && vsdei)) {
		ret = SDEI_NOT_SUPPORTED;
		goto out;
	}

	if (!kvm_sdei_is_valid_event_num(event_num)) {
		ret = SDEI_INVALID_PARAMETERS;
		goto out;
	}

	/*
	 * Check if the KVM event exists. None of the flags
	 * will be set if it doesn't exist.
	 */
	spin_lock(&ksdei->lock);
	kske = kvm_sdei_find_kvm_event(kvm, event_num);
	if (!kske) {
		ret = 0;
		goto unlock;
	}

	index = (kse->state.type == SDEI_EVENT_TYPE_PRIVATE) ?
		vcpu->vcpu_idx : 0;
	if (kvm_sdei_is_registered(kske, index))
		ret |= (1UL << SDEI_EVENT_STATUS_REGISTERED);
	if (kvm_sdei_is_enabled(kske, index))
		ret |= (1UL << SDEI_EVENT_STATUS_ENABLED);
	if (kske->state.refcount)
		ret |= (1UL << SDEI_EVENT_STATUS_RUNNING);

unlock:
	spin_unlock(&ksdei->lock);
out:
	return ret;
}

static unsigned long kvm_sdei_hypercall_info(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_sdei_kvm *ksdei = kvm->arch.sdei;
	struct kvm_sdei_vcpu *vsdei = vcpu->arch.sdei;
	struct kvm_sdei_event *kse = NULL;
	struct kvm_sdei_kvm_event *kske = NULL;
	unsigned long event_num = smccc_get_arg1(vcpu);
	unsigned long event_info = smccc_get_arg2(vcpu);
	unsigned long ret = SDEI_SUCCESS;

	/* Sanity check */
	if (!(ksdei && vsdei)) {
		ret = SDEI_NOT_SUPPORTED;
		goto out;
	}

	if (!kvm_sdei_is_valid_event_num(event_num)) {
		ret = SDEI_INVALID_PARAMETERS;
		goto out;
	}

	/*
	 * Check if the KVM event exists. The event might have been
	 * registered, we need fetch the information from the registered
	 * event in that case.
	 */
	spin_lock(&ksdei->lock);
	kske = kvm_sdei_find_kvm_event(kvm, event_num);
	kse = kske ? kske->kse : NULL;
	if (!kse) {
		kse = kvm_sdei_find_event(kvm, event_num);
		if (!kse) {
			ret = SDEI_INVALID_PARAMETERS;
			goto unlock;
		}
	}

	/* Retrieve the requested information */
	switch (event_info) {
	case SDEI_EVENT_INFO_EV_TYPE:
		ret = kse->state.type;
		break;
	case SDEI_EVENT_INFO_EV_SIGNALED:
		ret = kse->state.signaled;
		break;
	case SDEI_EVENT_INFO_EV_PRIORITY:
		ret = kse->state.priority;
		break;
	case SDEI_EVENT_INFO_EV_ROUTING_MODE:
	case SDEI_EVENT_INFO_EV_ROUTING_AFF:
		if (kse->state.type != SDEI_EVENT_TYPE_SHARED) {
			ret = SDEI_INVALID_PARAMETERS;
			break;
		}

		if (event_info == SDEI_EVENT_INFO_EV_ROUTING_MODE) {
			ret = kske ? kske->state.route_mode :
				     SDEI_EVENT_REGISTER_RM_ANY;
		} else {
			ret = kske ? kske->state.route_affinity : 0;
		}

		break;
	default:
		ret = SDEI_INVALID_PARAMETERS;
	}

unlock:
	spin_unlock(&ksdei->lock);
out:
	return ret;
}

static unsigned long kvm_sdei_hypercall_route(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_sdei_kvm *ksdei = kvm->arch.sdei;
	struct kvm_sdei_vcpu *vsdei = vcpu->arch.sdei;
	struct kvm_sdei_event *kse = NULL;
	struct kvm_sdei_kvm_event *kske = NULL;
	unsigned long event_num = smccc_get_arg1(vcpu);
	unsigned long route_mode = smccc_get_arg2(vcpu);
	unsigned long route_affinity = smccc_get_arg3(vcpu);
	int index = 0;
	unsigned long ret = SDEI_SUCCESS;

	/* Sanity check */
	if (!(ksdei && vsdei)) {
		ret = SDEI_NOT_SUPPORTED;
		goto out;
	}

	if (!kvm_sdei_is_valid_event_num(event_num)) {
		ret = SDEI_INVALID_PARAMETERS;
		goto out;
	}

	if (!(route_mode == SDEI_EVENT_REGISTER_RM_ANY ||
	      route_mode == SDEI_EVENT_REGISTER_RM_PE)) {
		ret = SDEI_INVALID_PARAMETERS;
		goto out;
	}

	/* Check if the KVM event has been registered */
	spin_lock(&ksdei->lock);
	kske = kvm_sdei_find_kvm_event(kvm, event_num);
	if (!kske) {
		ret = SDEI_INVALID_PARAMETERS;
		goto unlock;
	}

	/* Validate KVM event state */
	kse = kske->kse;
	if (kse->state.type != SDEI_EVENT_TYPE_SHARED) {
		ret = SDEI_INVALID_PARAMETERS;
		goto unlock;
	}

	if (!kvm_sdei_is_registered(kske, index) ||
	    kvm_sdei_is_enabled(kske, index)     ||
	    kske->state.refcount) {
		ret = SDEI_DENIED;
		goto unlock;
	}

	/* Update state */
	kske->state.route_mode     = route_mode;
	kske->state.route_affinity = route_affinity;

unlock:
	spin_unlock(&ksdei->lock);
out:
	return ret;
}

static unsigned long kvm_sdei_hypercall_mask(struct kvm_vcpu *vcpu,
					     bool mask)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_sdei_kvm *ksdei = kvm->arch.sdei;
	struct kvm_sdei_vcpu *vsdei = vcpu->arch.sdei;
	unsigned long ret = SDEI_SUCCESS;

	/* Sanity check */
	if (!(ksdei && vsdei)) {
		ret = SDEI_NOT_SUPPORTED;
		goto out;
	}

	spin_lock(&vsdei->lock);

	/* Check the state */
	if (mask == vsdei->state.masked) {
		ret = SDEI_DENIED;
		goto unlock;
	}

	/* Update the state */
	vsdei->state.masked = mask ? 1 : 0;

unlock:
	spin_unlock(&vsdei->lock);
out:
	return ret;
}

static unsigned long kvm_sdei_hypercall_reset(struct kvm_vcpu *vcpu,
					      bool private)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_sdei_kvm *ksdei = kvm->arch.sdei;
	struct kvm_sdei_vcpu *vsdei = vcpu->arch.sdei;
	unsigned int mask = private ? (1 << SDEI_EVENT_TYPE_PRIVATE) :
				      (1 << SDEI_EVENT_TYPE_SHARED);
	unsigned long ret = SDEI_SUCCESS;

	/* Sanity check */
	if (!(ksdei && vsdei)) {
		ret = SDEI_NOT_SUPPORTED;
		goto out;
	}

	spin_lock(&ksdei->lock);
	kvm_sdei_remove_kvm_events(kvm, mask, false);
	spin_unlock(&ksdei->lock);
out:
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
		ret = kvm_sdei_hypercall_register(vcpu);
		break;
	case SDEI_1_0_FN_SDEI_EVENT_ENABLE:
		ret = kvm_sdei_hypercall_enable(vcpu, true);
		break;
	case SDEI_1_0_FN_SDEI_EVENT_DISABLE:
		ret = kvm_sdei_hypercall_enable(vcpu, false);
		break;
	case SDEI_1_0_FN_SDEI_EVENT_CONTEXT:
		ret = kvm_sdei_hypercall_context(vcpu);
		break;
	case SDEI_1_0_FN_SDEI_EVENT_COMPLETE:
		has_result = false;
		ret = kvm_sdei_hypercall_complete(vcpu, false);
		break;
	case SDEI_1_0_FN_SDEI_EVENT_COMPLETE_AND_RESUME:
		has_result = false;
		ret = kvm_sdei_hypercall_complete(vcpu, true);
		break;
	case SDEI_1_0_FN_SDEI_EVENT_UNREGISTER:
		ret = kvm_sdei_hypercall_unregister(vcpu);
		break;
	case SDEI_1_0_FN_SDEI_EVENT_STATUS:
		ret = kvm_sdei_hypercall_status(vcpu);
		break;
	case SDEI_1_0_FN_SDEI_EVENT_GET_INFO:
		ret = kvm_sdei_hypercall_info(vcpu);
		break;
	case SDEI_1_0_FN_SDEI_EVENT_ROUTING_SET:
		ret = kvm_sdei_hypercall_route(vcpu);
		break;
	case SDEI_1_0_FN_SDEI_PE_MASK:
		ret = kvm_sdei_hypercall_mask(vcpu, true);
		break;
	case SDEI_1_0_FN_SDEI_PE_UNMASK:
		ret = kvm_sdei_hypercall_mask(vcpu, false);
		break;
	case SDEI_1_0_FN_SDEI_INTERRUPT_BIND:
	case SDEI_1_0_FN_SDEI_INTERRUPT_RELEASE:
		ret = SDEI_NOT_SUPPORTED;
		break;
	case SDEI_1_0_FN_SDEI_PRIVATE_RESET:
		ret = kvm_sdei_hypercall_reset(vcpu, true);
		break;
	case SDEI_1_0_FN_SDEI_SHARED_RESET:
		ret = kvm_sdei_hypercall_reset(vcpu, false);
		break;
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

int kvm_sdei_register_notifier(struct kvm *kvm,
			       unsigned long num,
			       kvm_sdei_notifier notifier)
{
	struct kvm_sdei_kvm *ksdei = kvm->arch.sdei;
	struct kvm_sdei_event *kse = NULL;
	int ret = 0;

	if (!ksdei) {
		ret = -EPERM;
		goto out;
	}

	spin_lock(&ksdei->lock);

	kse = kvm_sdei_find_event(kvm, num);
	if (!kse) {
		ret = -EINVAL;
		goto unlock;
	}

	kse->state.notifier = (unsigned long)notifier;

unlock:
	spin_unlock(&ksdei->lock);
out:
	return ret;
}

void kvm_sdei_deliver(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_sdei_kvm *ksdei = kvm->arch.sdei;
	struct kvm_sdei_vcpu *vsdei = vcpu->arch.sdei;
	struct kvm_sdei_event *kse = NULL;
	struct kvm_sdei_kvm_event *kske = NULL;
	struct kvm_sdei_vcpu_event *ksve = NULL;
	struct kvm_sdei_vcpu_regs *regs = NULL;
	kvm_sdei_notifier notifier;
	unsigned long pstate;
	int index = 0;

	/* Sanity check */
	if (!(ksdei && vsdei))
		return;

	/* The critical event can't be preempted */
	spin_lock(&vsdei->lock);
	if (vsdei->critical_event)
		goto unlock;

	/*
	 * The normal event can be preempted by the critical event.
	 * However, the normal event can't be preempted by another
	 * normal event.
	 */
	ksve = list_first_entry_or_null(&vsdei->critical_events,
			struct kvm_sdei_vcpu_event, link);
	if (!ksve && !vsdei->normal_event) {
		ksve = list_first_entry_or_null(&vsdei->normal_events,
				struct kvm_sdei_vcpu_event, link);
	}

	if (!ksve)
		goto unlock;

	kske = ksve->kske;
	kse = kske->kse;
	if (kse->state.priority == SDEI_EVENT_PRIORITY_CRITICAL) {
		vsdei->critical_event = ksve;
		vsdei->state.critical_num = ksve->state.num;
		regs = &vsdei->state.critical_regs;
	} else {
		vsdei->normal_event = ksve;
		vsdei->state.normal_num = ksve->state.num;
		regs = &vsdei->state.normal_regs;
	}

	/* Save registers: x0 -> x17, PC, PState */
	for (index = 0; index < ARRAY_SIZE(regs->regs); index++)
		regs->regs[index] = vcpu_get_reg(vcpu, index);

	regs->pc = *vcpu_pc(vcpu);
	regs->pstate = *vcpu_cpsr(vcpu);

	/*
	 * Inject SDEI event: x0 -> x3, PC, PState. We needn't take lock
	 * for the KVM event as it can't be destroyed because of its
	 * reference count.
	 */
	for (index = 0; index < ARRAY_SIZE(regs->regs); index++)
		vcpu_set_reg(vcpu, index, 0);

	index = (kse->state.type == SDEI_EVENT_TYPE_PRIVATE) ?
		vcpu->vcpu_idx : 0;
	vcpu_set_reg(vcpu, 0, kske->state.num);
	vcpu_set_reg(vcpu, 1, kske->state.params[index]);
	vcpu_set_reg(vcpu, 2, regs->pc);
	vcpu_set_reg(vcpu, 3, regs->pstate);

	pstate = regs->pstate;
	pstate |= (PSR_D_BIT | PSR_A_BIT | PSR_I_BIT | PSR_F_BIT);
	pstate &= ~PSR_MODE_MASK;
	pstate |= PSR_MODE_EL1h;
	pstate &= ~PSR_MODE32_BIT;

	vcpu_write_sys_reg(vcpu, regs->pstate, SPSR_EL1);
	*vcpu_cpsr(vcpu) = pstate;
	*vcpu_pc(vcpu) = kske->state.entries[index];

	/* Notifier */
	notifier = (kvm_sdei_notifier)(kse->state.notifier);
	if (notifier)
		notifier(vcpu, kse->state.num, KVM_SDEI_NOTIFY_DELIVERED);

unlock:
	spin_unlock(&vsdei->lock);
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

static long kvm_sdei_set_event(struct kvm *kvm,
			       struct kvm_sdei_event_state *kse_state)
{
	struct kvm_sdei_kvm *ksdei = kvm->arch.sdei;
	struct kvm_sdei_event *kse = NULL;

	if (!kvm_sdei_is_valid_event_num(kse_state->num))
		return -EINVAL;

	if (!(kse_state->type == SDEI_EVENT_TYPE_SHARED ||
	      kse_state->type == SDEI_EVENT_TYPE_PRIVATE))
		return -EINVAL;

	if (!(kse_state->priority == SDEI_EVENT_PRIORITY_NORMAL ||
	      kse_state->priority == SDEI_EVENT_PRIORITY_CRITICAL))
		return -EINVAL;

	kse = kvm_sdei_find_event(kvm, kse_state->num);
	if (kse)
		return -EEXIST;

	kse = kzalloc(sizeof(*kse), GFP_KERNEL);
	if (!kse)
		return -ENOMEM;

	kse->state = *kse_state;
	kse->kvm = kvm;
	list_add_tail(&kse->link, &ksdei->events);

	return 0;
}

static long kvm_sdei_get_kevent_count(struct kvm *kvm, int *count)
{
	struct kvm_sdei_kvm *ksdei = kvm->arch.sdei;
	struct kvm_sdei_kvm_event *kske = NULL;
	int total = 0;

	list_for_each_entry(kske, &ksdei->kvm_events, link) {
		total++;
	}

	*count = total;
	return 0;
}

static long kvm_sdei_get_kevent(struct kvm *kvm,
				struct kvm_sdei_kvm_event_state *kske_state)
{
	struct kvm_sdei_kvm *ksdei = kvm->arch.sdei;
	struct kvm_sdei_kvm_event *kske = NULL;

	/*
	 * The first entry is fetched if the event number is invalid.
	 * Otherwise, the next entry is fetched.
	 */
	if (!kvm_sdei_is_valid_event_num(kske_state->num)) {
		kske = list_first_entry_or_null(&ksdei->kvm_events,
				struct kvm_sdei_kvm_event, link);
	} else {
		kske = kvm_sdei_find_kvm_event(kvm, kske_state->num);
		if (kske && !list_is_last(&kske->link, &ksdei->kvm_events))
			kske = list_next_entry(kske, link);
		else
			kske = NULL;
	}

	if (!kske)
		return -ENOENT;

	*kske_state = kske->state;

	return 0;
}

static long kvm_sdei_set_kevent(struct kvm *kvm,
				struct kvm_sdei_kvm_event_state *kske_state)
{
	struct kvm_sdei_kvm *ksdei = kvm->arch.sdei;
	struct kvm_sdei_event *kse = NULL;
	struct kvm_sdei_kvm_event *kske = NULL;

	/* Sanity check */
	if (!kvm_sdei_is_valid_event_num(kske_state->num))
		return -EINVAL;

	if (!(kske_state->route_mode == SDEI_EVENT_REGISTER_RM_ANY ||
	      kske_state->route_mode == SDEI_EVENT_REGISTER_RM_PE))
		return -EINVAL;

	/* Check if the event number is valid */
	kse = kvm_sdei_find_event(kvm, kske_state->num);
	if (!kse)
		return -ENOENT;

	/* Check if the event has been populated */
	kske = kvm_sdei_find_kvm_event(kvm, kske_state->num);
	if (kske)
		return -EEXIST;

	kske = kzalloc(sizeof(*kske), GFP_KERNEL);
	if (!kske)
		return -ENOMEM;

	kske->state = *kske_state;
	kske->kse   = kse;
	kske->kvm   = kvm;
	list_add_tail(&kske->link, &ksdei->kvm_events);

	return 0;
}

long kvm_sdei_vm_ioctl(struct kvm *kvm, unsigned long arg)
{
	struct kvm_sdei_kvm *ksdei = kvm->arch.sdei;
	struct kvm_sdei_cmd *cmd = NULL;
	void __user *argp = (void __user *)arg;
	bool copy = false;
	long ret = 0;

	/* Sanity check */
	if (!ksdei) {
		ret = -EPERM;
		goto out;
	}

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		ret = -ENOMEM;
		goto out;
	}

	if (copy_from_user(cmd, argp, sizeof(*cmd))) {
		ret = -EFAULT;
		goto out;
	}

	spin_lock(&ksdei->lock);

	switch (cmd->cmd) {
	case KVM_SDEI_CMD_GET_VERSION:
		copy = true;
		cmd->version = (1 << 16);       /* v1.0.0 */
		break;
	case KVM_SDEI_CMD_SET_EVENT:
		ret = kvm_sdei_set_event(kvm, &cmd->kse_state);
		break;
	case KVM_SDEI_CMD_GET_KEVENT_COUNT:
		copy = true;
		ret = kvm_sdei_get_kevent_count(kvm, &cmd->count);
		break;
	case KVM_SDEI_CMD_GET_KEVENT:
		copy = true;
		ret = kvm_sdei_get_kevent(kvm, &cmd->kske_state);
		break;
	case KVM_SDEI_CMD_SET_KEVENT:
		ret = kvm_sdei_set_kevent(kvm, &cmd->kske_state);
		break;
	default:
		ret = -EINVAL;
	}

	spin_unlock(&ksdei->lock);
out:
	if (!ret && copy && copy_to_user(argp, cmd, sizeof(*cmd)))
		ret = -EFAULT;

	kfree(cmd);
	return ret;
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
