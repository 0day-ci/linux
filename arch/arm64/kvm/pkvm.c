// SPDX-License-Identifier: GPL-2.0-only
/*
 * KVM host (EL1) interface to Protected KVM (pkvm) code at EL2.
 *
 * Copyright (C) 2021 Google LLC
 * Author: Will Deacon <will@kernel.org>
 */

#include <linux/kvm_host.h>
#include <linux/mm.h>
#include <linux/of_fdt.h>
#include <linux/of_reserved_mem.h>

static struct reserved_mem *pkvm_firmware_mem;

static int __init pkvm_firmware_rmem_err(struct reserved_mem *rmem,
					 const char *reason)
{
	phys_addr_t end = rmem->base + rmem->size;

	kvm_err("Ignoring pkvm guest firmware memory reservation [%pa - %pa]: %s\n",
		&rmem->base, &end, reason);
	return -EINVAL;
}

static int __init pkvm_firmware_rmem_init(struct reserved_mem *rmem)
{
	unsigned long node = rmem->fdt_node;

	if (kvm_get_mode() != KVM_MODE_PROTECTED)
		return pkvm_firmware_rmem_err(rmem, "protected mode not enabled");

	if (pkvm_firmware_mem)
		return pkvm_firmware_rmem_err(rmem, "duplicate reservation");

	if (!of_get_flat_dt_prop(node, "no-map", NULL))
		return pkvm_firmware_rmem_err(rmem, "missing \"no-map\" property");

	if (of_get_flat_dt_prop(node, "reusable", NULL))
		return pkvm_firmware_rmem_err(rmem, "\"reusable\" property unsupported");

	if (!PAGE_ALIGNED(rmem->base))
		return pkvm_firmware_rmem_err(rmem, "base is not page-aligned");

	if (!PAGE_ALIGNED(rmem->size))
		return pkvm_firmware_rmem_err(rmem, "size is not page-aligned");

	pkvm_firmware_mem = rmem;
	return 0;
}
RESERVEDMEM_OF_DECLARE(pkvm_firmware, "linux,pkvm-guest-firmware-memory",
		       pkvm_firmware_rmem_init);

static int pkvm_init_el2_context(struct kvm *kvm)
{
	kvm_pr_unimpl("Stage-2 protection is not yet implemented\n");
	return -EINVAL;
}

static int pkvm_init_firmware_slot(struct kvm *kvm, u64 slotid)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *slot;

	if (slotid >= KVM_MEM_SLOTS_NUM || !pkvm_firmware_mem)
		return -EINVAL;

	slots = kvm_memslots(kvm);
	if (!slots)
		return -ENOENT;

	slot = id_to_memslot(slots, slotid);
	if (!slot)
		return -ENOENT;

	if (slot->flags)
		return -EINVAL;

	if ((slot->npages << PAGE_SHIFT) < pkvm_firmware_mem->size)
		return -ENOMEM;

	kvm->arch.pkvm.firmware_slot = slot;
	return 0;
}

static void pkvm_teardown_firmware_slot(struct kvm *kvm)
{
	kvm->arch.pkvm.firmware_slot = NULL;
}

static int pkvm_enable(struct kvm *kvm, u64 slotid)
{
	int ret;

	ret = pkvm_init_firmware_slot(kvm, slotid);
	if (ret)
		return ret;

	ret = pkvm_init_el2_context(kvm);
	if (ret)
		pkvm_teardown_firmware_slot(kvm);

	return ret;
}

static int pkvm_vm_ioctl_enable(struct kvm *kvm, u64 slotid)
{
	int ret = 0;

	mutex_lock(&kvm->lock);
	if (kvm_vm_is_protected(kvm)) {
		ret = -EPERM;
		goto out_kvm_unlock;
	}

	mutex_lock(&kvm->slots_lock);
	ret = pkvm_enable(kvm, slotid);
	if (ret)
		goto out_slots_unlock;

	kvm->arch.pkvm.enabled = true;
out_slots_unlock:
	mutex_unlock(&kvm->slots_lock);
out_kvm_unlock:
	mutex_unlock(&kvm->lock);
	return ret;
}

static int pkvm_vm_ioctl_info(struct kvm *kvm,
			      struct kvm_protected_vm_info __user *info)
{
	struct kvm_protected_vm_info kinfo = {
		.firmware_size = pkvm_firmware_mem ?
				 pkvm_firmware_mem->size :
				 0,
	};

	return copy_to_user(info, &kinfo, sizeof(kinfo)) ? -EFAULT : 0;
}

int kvm_arm_vm_ioctl_pkvm(struct kvm *kvm, struct kvm_enable_cap *cap)
{
	if (cap->args[1] || cap->args[2] || cap->args[3])
		return -EINVAL;

	switch (cap->flags) {
	case KVM_CAP_ARM_PROTECTED_VM_FLAGS_ENABLE:
		return pkvm_vm_ioctl_enable(kvm, cap->args[0]);
	case KVM_CAP_ARM_PROTECTED_VM_FLAGS_INFO:
		return pkvm_vm_ioctl_info(kvm, (void __user *)cap->args[0]);
	default:
		return -EINVAL;
	}

	return 0;
}
