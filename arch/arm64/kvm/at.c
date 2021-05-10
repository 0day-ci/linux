// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 - Linaro Ltd
 * Author: Jintack Lim <jintack.lim@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>

struct mmu_config {
	u64	ttbr0;
	u64	ttbr1;
	u64	tcr;
	u64	sctlr;
	u64	vttbr;
	u64	vtcr;
	u64	hcr;
};

static void __mmu_config_save(struct mmu_config *config)
{
	config->ttbr0	= read_sysreg_el1(SYS_TTBR0);
	config->ttbr1	= read_sysreg_el1(SYS_TTBR1);
	config->tcr	= read_sysreg_el1(SYS_TCR);
	config->sctlr	= read_sysreg_el1(SYS_SCTLR);
	config->vttbr	= read_sysreg(vttbr_el2);
	config->vtcr	= read_sysreg(vtcr_el2);
	config->hcr	= read_sysreg(hcr_el2);
}

static void __mmu_config_restore(struct mmu_config *config)
{
	write_sysreg_el1(config->ttbr0,	SYS_TTBR0);
	write_sysreg_el1(config->ttbr1,	SYS_TTBR1);
	write_sysreg_el1(config->tcr,	SYS_TCR);
	write_sysreg_el1(config->sctlr,	SYS_SCTLR);
	write_sysreg(config->vttbr,	vttbr_el2);
	write_sysreg(config->vtcr,	vtcr_el2);
	write_sysreg(config->hcr,	hcr_el2);

	isb();
}

void __kvm_at_s1e01(struct kvm_vcpu *vcpu, u32 op, u64 vaddr)
{
	struct kvm_cpu_context *ctxt = &vcpu->arch.ctxt;
	struct mmu_config config;
	struct kvm_s2_mmu *mmu;

	spin_lock(&vcpu->kvm->mmu_lock);

	/*
	 * If HCR_EL2.{E2H,TGE} == {1,1}, the MMU context is already
	 * the right one (as we trapped from vEL2).
	 */
	if (vcpu_el2_e2h_is_set(vcpu) && vcpu_el2_tge_is_set(vcpu))
		goto skip_mmu_switch;

	/*
	 * FIXME: Obtaining the S2 MMU for a guest guest is horribly
	 * racy, and we may not find it (evicted by another vcpu, for
	 * example).
	 */
	mmu = lookup_s2_mmu(vcpu->kvm,
			    vcpu_read_sys_reg(vcpu, VTTBR_EL2),
			    vcpu_read_sys_reg(vcpu, HCR_EL2));

	if (WARN_ON(!mmu))
		goto out;

	/* We've trapped, so everything is live on the CPU. */
	__mmu_config_save(&config);

	write_sysreg_el1(ctxt_sys_reg(ctxt, TTBR0_EL1),	SYS_TTBR0);
	write_sysreg_el1(ctxt_sys_reg(ctxt, TTBR1_EL1),	SYS_TTBR1);
	write_sysreg_el1(ctxt_sys_reg(ctxt, TCR_EL1),	SYS_TCR);
	write_sysreg_el1(ctxt_sys_reg(ctxt, SCTLR_EL1),	SYS_SCTLR);
	write_sysreg(kvm_get_vttbr(mmu),		vttbr_el2);
	/*
	 * REVISIT: do we need anything from the guest's VTCR_EL2? If
	 * looks like keeping the hosts configuration is the right
	 * thing to do at this stage (and we could avoid save/restore
	 * it. Keep the host's version for now.
	 */
	write_sysreg((config.hcr & ~HCR_TGE) | HCR_VM,	hcr_el2);

	isb();

skip_mmu_switch:

	switch (op) {
	case OP_AT_S1E1R:
	case OP_AT_S1E1RP:
		asm volatile("at s1e1r, %0" : : "r" (vaddr));
		break;
	case OP_AT_S1E1W:
	case OP_AT_S1E1WP:
		asm volatile("at s1e1w, %0" : : "r" (vaddr));
		break;
	case OP_AT_S1E0R:
		asm volatile("at s1e0r, %0" : : "r" (vaddr));
		break;
	case OP_AT_S1E0W:
		asm volatile("at s1e0w, %0" : : "r" (vaddr));
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}

	isb();

	ctxt_sys_reg(ctxt, PAR_EL1) = read_sysreg(par_el1);

	/*
	 * Failed? let's leave the building now.
	 *
	 * FIXME: how about a failed translation because the shadow S2
	 * wasn't populated? We may need to perform a SW PTW,
	 * populating our shadow S2 and retry the instruction.
	 */
	if (ctxt_sys_reg(ctxt, PAR_EL1) & 1)
		goto nopan;

	/* No PAN? No problem. */
	if (!(*vcpu_cpsr(vcpu) & PSR_PAN_BIT))
		goto nopan;

	/*
	 * For PAN-involved AT operations, perform the same
	 * translation, using EL0 this time.
	 */
	switch (op) {
	case OP_AT_S1E1RP:
		asm volatile("at s1e0r, %0" : : "r" (vaddr));
		break;
	case OP_AT_S1E1WP:
		asm volatile("at s1e0w, %0" : : "r" (vaddr));
		break;
	default:
		goto nopan;
	}

	/*
	 * If the EL0 translation has succeeded, we need to pretend
	 * the AT operation has failed, as the PAN setting forbids
	 * such a translation.
	 *
	 * FIXME: we hardcode a Level-3 permission fault. We really
	 * should return the real fault level.
	 */
	if (!(read_sysreg(par_el1) & 1))
		ctxt_sys_reg(ctxt, PAR_EL1) = 0x1f;

nopan:
	if (!(vcpu_el2_e2h_is_set(vcpu) && vcpu_el2_tge_is_set(vcpu)))
		__mmu_config_restore(&config);

out:
	spin_unlock(&vcpu->kvm->mmu_lock);
}

void __kvm_at_s1e2(struct kvm_vcpu *vcpu, u32 op, u64 vaddr)
{
	struct kvm_cpu_context *ctxt = &vcpu->arch.ctxt;
	struct mmu_config config;
	struct kvm_s2_mmu *mmu;
	u64 val;

	spin_lock(&vcpu->kvm->mmu_lock);

	mmu = &vcpu->kvm->arch.mmu;

	/* We've trapped, so everything is live on the CPU. */
	__mmu_config_save(&config);

	if (vcpu_el2_e2h_is_set(vcpu)) {
		write_sysreg_el1(ctxt_sys_reg(ctxt, TTBR0_EL2),	SYS_TTBR0);
		write_sysreg_el1(ctxt_sys_reg(ctxt, TTBR1_EL2),	SYS_TTBR1);
		write_sysreg_el1(ctxt_sys_reg(ctxt, TCR_EL2),	SYS_TCR);
		write_sysreg_el1(ctxt_sys_reg(ctxt, SCTLR_EL2),	SYS_SCTLR);

		val = config.hcr;
	} else {
		write_sysreg_el1(ctxt_sys_reg(ctxt, TTBR0_EL2),	SYS_TTBR0);
		val = translate_tcr_el2_to_tcr_el1(ctxt_sys_reg(ctxt, TCR_EL2));
		write_sysreg_el1(val, SYS_TCR);
		val = translate_sctlr_el2_to_sctlr_el1(ctxt_sys_reg(ctxt, SCTLR_EL2));
		write_sysreg_el1(val, SYS_SCTLR);

		val = config.hcr | HCR_NV | HCR_NV1;
	}

	write_sysreg(kvm_get_vttbr(mmu),		vttbr_el2);
	/* FIXME: write S2 MMU VTCR_EL2? */
	write_sysreg((val & ~HCR_TGE) | HCR_VM,		hcr_el2);

	isb();

	switch (op) {
	case OP_AT_S1E2R:
		asm volatile("at s1e1r, %0" : : "r" (vaddr));
		break;
	case OP_AT_S1E2W:
		asm volatile("at s1e1w, %0" : : "r" (vaddr));
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}

	isb();

	/* FIXME: handle failed translation due to shadow S2 */
	ctxt_sys_reg(ctxt, PAR_EL1) = read_sysreg(par_el1);

	__mmu_config_restore(&config);
	spin_unlock(&vcpu->kvm->mmu_lock);
}
