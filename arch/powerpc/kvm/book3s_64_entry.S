/* SPDX-License-Identifier: GPL-2.0-only */
#include <asm/asm-offsets.h>
#include <asm/cache.h>
#include <asm/exception-64s.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_book3s_asm.h>
#include <asm/ppc_asm.h>
#include <asm/reg.h>

/*
 * This is branched to from interrupt handlers in exception-64s.S which set
 * IKVM_REAL or IKVM_VIRT, if HSTATE_IN_GUEST was found to be non-zero.
 */
.global	kvmppc_interrupt
.balign IFETCH_ALIGN_BYTES
kvmppc_interrupt:
	/*
	 * Register contents:
	 * R12		= (guest CR << 32) | interrupt vector
	 * R13		= PACA
	 * guest R12 saved in shadow VCPU SCRATCH0
	 * guest R13 saved in SPRN_SCRATCH0
	 */
	std	r9,HSTATE_SCRATCH2(r13)
	lbz	r9,HSTATE_IN_GUEST(r13)
	cmpwi	r9,KVM_GUEST_MODE_SKIP
	beq-	.Lmaybe_skip
.Lno_skip:
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	cmpwi	r9,KVM_GUEST_MODE_HOST_HV
	beq	kvmppc_bad_host_intr
#ifdef CONFIG_KVM_BOOK3S_PR_POSSIBLE
	cmpwi	r9,KVM_GUEST_MODE_GUEST
	ld	r9,HSTATE_SCRATCH2(r13)
	beq	kvmppc_interrupt_pr
#endif
	b	kvmppc_interrupt_hv
#else
	ld	r9,HSTATE_SCRATCH2(r13)
	b	kvmppc_interrupt_pr
#endif

/*
 * KVM uses a trick where it is running in MSR[HV]=1 mode in real-mode with the
 * guest MMU context loaded, and it sets KVM_GUEST_MODE_SKIP and enables
 * MSR[DR]=1 while leaving MSR[IR]=0, so it continues to fetch HV instructions
 * but loads and stores will access the guest context. This is used to load
 * the faulting instruction without walking page tables.
 *
 * However the guest context may not be able to translate, or it may cause a
 * machine check or other issue, which will result in a fault in the host
 * (even with KVM-HV).
 *
 * These faults are caught here and if the fault was (or was likely) due to
 * that load, then we just return with the PC advanced +4 and skip the load,
 * which then goes to the slow path and walks the page tables.
 */
.Lmaybe_skip:
	cmpwi	r12,BOOK3S_INTERRUPT_MACHINE_CHECK
	beq	1f
	cmpwi	r12,BOOK3S_INTERRUPT_DATA_STORAGE
	beq	1f
	cmpwi	r12,BOOK3S_INTERRUPT_DATA_SEGMENT
	beq	1f
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	/* HSRR interrupts have 2 added to trap vector */
	cmpwi	r12,BOOK3S_INTERRUPT_H_DATA_STORAGE | 0x2
	beq	2f
#endif
	b	.Lno_skip
1:	mfspr	r9,SPRN_SRR0
	addi	r9,r9,4
	mtspr	SPRN_SRR0,r9
	ld	r12,HSTATE_SCRATCH0(r13)
	ld	r9,HSTATE_SCRATCH2(r13)
	GET_SCRATCH0(r13)
	RFI_TO_KERNEL
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
2:	mfspr	r9,SPRN_HSRR0
	addi	r9,r9,4
	mtspr	SPRN_HSRR0,r9
	ld	r12,HSTATE_SCRATCH0(r13)
	ld	r9,HSTATE_SCRATCH2(r13)
	GET_SCRATCH0(r13)
	HRFI_TO_KERNEL
#endif
