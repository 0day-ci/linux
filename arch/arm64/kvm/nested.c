// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 - Columbia University and Linaro Ltd.
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

#include <linux/kvm.h>
#include <linux/kvm_host.h>

#include <asm/kvm_emulate.h>
#include <asm/kvm_nested.h>
#include <asm/sysreg.h>

#include "sys_regs.h"

/*
 * Inject wfx to the virtual EL2 if this is not from the virtual EL2 and
 * the virtual HCR_EL2.TWX is set. Otherwise, let the host hypervisor
 * handle this.
 */
int handle_wfx_nested(struct kvm_vcpu *vcpu, bool is_wfe)
{
	u64 hcr_el2 = __vcpu_sys_reg(vcpu, HCR_EL2);

	if (vcpu_mode_el2(vcpu))
		return -EINVAL;

	if ((is_wfe && (hcr_el2 & HCR_TWE)) || (!is_wfe && (hcr_el2 & HCR_TWI)))
		return kvm_inject_nested_sync(vcpu, kvm_vcpu_get_esr(vcpu));

	return -EINVAL;
}

/*
 * Our emulated CPU doesn't support all the possible features. For the
 * sake of simplicity (and probably mental sanity), wipe out a number
 * of feature bits we don't intend to support for the time being.
 * This list should get updated as new features get added to the NV
 * support, and new extension to the architecture.
 */
void access_nested_id_reg(struct kvm_vcpu *v, struct sys_reg_params *p,
			  const struct sys_reg_desc *r)
{
	u32 id = sys_reg((u32)r->Op0, (u32)r->Op1,
			 (u32)r->CRn, (u32)r->CRm, (u32)r->Op2);
	u64 val, tmp;

	if (!nested_virt_in_use(v))
		return;

	val = p->regval;

	switch (id) {
	case SYS_ID_AA64ISAR0_EL1:
		/* Support everything but O.S. and Range TLBIs */
		val &= ~(FEATURE(ID_AA64ISAR0_TLB)	|
			 GENMASK_ULL(27, 24)		|
			 GENMASK_ULL(3, 0));
		break;

	case SYS_ID_AA64ISAR1_EL1:
		/* Support everything but PtrAuth and Spec Invalidation */
		val &= ~(GENMASK_ULL(63, 56)		|
			 FEATURE(ID_AA64ISAR1_SPECRES)	|
			 FEATURE(ID_AA64ISAR1_GPI)	|
			 FEATURE(ID_AA64ISAR1_GPA)	|
			 FEATURE(ID_AA64ISAR1_API)	|
			 FEATURE(ID_AA64ISAR1_APA));
		break;

	case SYS_ID_AA64PFR0_EL1:
		/* No AMU, MPAM, S-EL2, RAS or SVE */
		val &= ~(GENMASK_ULL(55, 52)		|
			 FEATURE(ID_AA64PFR0_AMU)	|
			 FEATURE(ID_AA64PFR0_MPAM)	|
			 FEATURE(ID_AA64PFR0_SEL2)	|
			 FEATURE(ID_AA64PFR0_RAS)	|
			 FEATURE(ID_AA64PFR0_SVE)	|
			 FEATURE(ID_AA64PFR0_EL3)	|
			 FEATURE(ID_AA64PFR0_EL2));
		/* 64bit EL2/EL3 only */
		val |= FIELD_PREP(FEATURE(ID_AA64PFR0_EL2), 0b0001);
		val |= FIELD_PREP(FEATURE(ID_AA64PFR0_EL3), 0b0001);
		break;

	case SYS_ID_AA64PFR1_EL1:
		/* Only support SSBS */
		val &= FEATURE(ID_AA64PFR1_SSBS);
		break;

	case SYS_ID_AA64MMFR0_EL1:
		/* Hide ECV, FGT, ExS, Secure Memory */
		val &= ~(GENMASK_ULL(63, 43)			|
			 FEATURE(ID_AA64MMFR0_TGRAN4_2)		|
			 FEATURE(ID_AA64MMFR0_TGRAN16_2)	|
			 FEATURE(ID_AA64MMFR0_TGRAN64_2)	|
			 FEATURE(ID_AA64MMFR0_SNSMEM));

		/* Disallow unsupported S2 page sizes */
		switch (PAGE_SIZE) {
		case SZ_64K:
			val |= FIELD_PREP(FEATURE(ID_AA64MMFR0_TGRAN16_2), 0b0001);
			/* Fall through */
		case SZ_16K:
			val |= FIELD_PREP(FEATURE(ID_AA64MMFR0_TGRAN4_2), 0b0001);
			/* Fall through */
		case SZ_4K:
			/* Support everything */
			break;
		}
		/* Advertize supported S2 page sizes */
		switch (PAGE_SIZE) {
		case SZ_4K:
			val |= FIELD_PREP(FEATURE(ID_AA64MMFR0_TGRAN4_2), 0b0010);
			/* Fall through */
		case SZ_16K:
			val |= FIELD_PREP(FEATURE(ID_AA64MMFR0_TGRAN16_2), 0b0010);
			/* Fall through */
		case SZ_64K:
			val |= FIELD_PREP(FEATURE(ID_AA64MMFR0_TGRAN64_2), 0b0010);
			break;
		}
		/* Cap PARange to 40bits */
		tmp = FIELD_GET(FEATURE(ID_AA64MMFR0_PARANGE), val);
		if (tmp > 0b0010) {
			val &= ~FEATURE(ID_AA64MMFR0_PARANGE);
			val |= FIELD_PREP(FEATURE(ID_AA64MMFR0_PARANGE), 0b0010);
		}
		break;

	case SYS_ID_AA64MMFR1_EL1:
		val &= (FEATURE(ID_AA64MMFR1_PAN)	|
			FEATURE(ID_AA64MMFR1_LOR)	|
			FEATURE(ID_AA64MMFR1_HPD)	|
			FEATURE(ID_AA64MMFR1_VHE)	|
			FEATURE(ID_AA64MMFR1_VMIDBITS));
		break;

	case SYS_ID_AA64MMFR2_EL1:
		val &= ~(FEATURE(ID_AA64MMFR2_EVT)	|
			 FEATURE(ID_AA64MMFR2_BBM)	|
			 FEATURE(ID_AA64MMFR2_TTL)	|
			 GENMASK_ULL(47, 44)		|
			 FEATURE(ID_AA64MMFR2_ST)	|
			 FEATURE(ID_AA64MMFR2_CCIDX)	|
			 FEATURE(ID_AA64MMFR2_LVA));

		/* Force TTL support */
		val |= FIELD_PREP(FEATURE(ID_AA64MMFR2_TTL), 0b0001);
		break;

	case SYS_ID_AA64DFR0_EL1:
		/* Only limited support for PMU, Debug, BPs and WPs */
		val &= (FEATURE(ID_AA64DFR0_PMSVER)	|
			FEATURE(ID_AA64DFR0_WRPS)	|
			FEATURE(ID_AA64DFR0_BRPS)	|
			FEATURE(ID_AA64DFR0_DEBUGVER));

		/* Cap PMU to ARMv8.1 */
		tmp = FIELD_GET(FEATURE(ID_AA64DFR0_PMUVER), val);
		if (tmp > 0b0100) {
			val &= ~FEATURE(ID_AA64DFR0_PMUVER);
			val |= FIELD_PREP(FEATURE(ID_AA64DFR0_PMUVER), 0b0100);
		}
		/* Cap Debug to ARMv8.1 */
		tmp = FIELD_GET(FEATURE(ID_AA64DFR0_DEBUGVER), val);
		if (tmp > 0b0111) {
			val &= ~FEATURE(ID_AA64DFR0_DEBUGVER);
			val |= FIELD_PREP(FEATURE(ID_AA64DFR0_DEBUGVER), 0b0111);
		}
		break;

	default:
		/* Unknown register, just wipe it clean */
		val = 0;
		break;
	}

	p->regval = val;
}
