// SPDX-License-Identifier: GPL-2.0
#include "kvm_util.h"
#include "processor.h"

/*
 * Using this hypercall for now because it is already defined. It is
 * used by SLOF to ask QEMU to copy memory regions, so it is close
 * enough for our purposes.
 */
#define KVMPPC_H_LOGICAL_MEMOP 0xf001


void ucall_init(struct kvm_vm *vm, void *arg)
{
}

void ucall_uninit(struct kvm_vm *vm)
{
}

static inline int __ucall(uint64_t args)
{
	return hcall(KVMPPC_H_LOGICAL_MEMOP, args);
}

/*
 * This function runs inside the guest, so avoid optimizations that
 * could add an indirect call via PLT and disable vector instructions
 * like the kernel does.
 */
__attribute__((optimize(0), target("no-altivec,no-vsx")))
void ucall(uint64_t cmd, int nargs, ...)
{
	struct ucall uc = {
		.cmd = cmd,
	};
	va_list va;
	int i;

	nargs = nargs <= UCALL_MAX_ARGS ? nargs : UCALL_MAX_ARGS;

	va_start(va, nargs);
	for (i = 0; i < nargs; ++i)
		uc.args[i] = va_arg(va, uint64_t);
	va_end(va);

	__ucall((uint64_t)&uc);
}

uint64_t get_ucall(struct kvm_vm *vm, uint32_t vcpu_id, struct ucall *uc)
{
	struct kvm_run *run = vcpu_state(vm, vcpu_id);
	struct ucall ucall = {};

	if (uc)
		memset(uc, 0, sizeof(*uc));

	if (run->exit_reason == KVM_EXIT_PAPR_HCALL &&
	    run->papr_hcall.nr == KVMPPC_H_LOGICAL_MEMOP) {
		memcpy(&ucall, addr_gva2hva(vm, run->papr_hcall.args[0]),
					    sizeof(ucall));
		if (uc)
			memcpy(uc, &ucall, sizeof(ucall));
	}

	return ucall.cmd;
}
