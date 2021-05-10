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
