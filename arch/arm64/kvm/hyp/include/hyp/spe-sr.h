// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 - ARM Ltd
 * Author: Alexandru Elisei <alexandru.elisei@arm.com>
 */

#ifndef __ARM64_KVM_HYP_SPE_SR_H__
#define __ARM64_KVM_HYP_SPE_SR_H__

#include <linux/kvm_host.h>

#include <asm/kvm_asm.h>

static inline void __spe_save_common_state(struct kvm_cpu_context *ctxt)
{
	ctxt_sys_reg(ctxt, PMSICR_EL1) = read_sysreg_s(SYS_PMSICR_EL1);
	ctxt_sys_reg(ctxt, PMSIRR_EL1) = read_sysreg_s(SYS_PMSIRR_EL1);
	ctxt_sys_reg(ctxt, PMSFCR_EL1) = read_sysreg_s(SYS_PMSFCR_EL1);
	ctxt_sys_reg(ctxt, PMSEVFR_EL1) = read_sysreg_s(SYS_PMSEVFR_EL1);
	ctxt_sys_reg(ctxt, PMSLATFR_EL1) = read_sysreg_s(SYS_PMSLATFR_EL1);
}

static inline void __spe_restore_common_state(struct kvm_cpu_context *ctxt)
{
	write_sysreg_s(ctxt_sys_reg(ctxt, PMSICR_EL1), SYS_PMSICR_EL1);
	write_sysreg_s(ctxt_sys_reg(ctxt, PMSIRR_EL1), SYS_PMSIRR_EL1);
	write_sysreg_s(ctxt_sys_reg(ctxt, PMSFCR_EL1), SYS_PMSFCR_EL1);
	write_sysreg_s(ctxt_sys_reg(ctxt, PMSEVFR_EL1), SYS_PMSEVFR_EL1);
	write_sysreg_s(ctxt_sys_reg(ctxt, PMSLATFR_EL1), SYS_PMSLATFR_EL1);
}

#endif /* __ARM64_KVM_HYP_SPE_SR_H__ */
