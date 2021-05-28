// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, Microsoft Corporation.
 *
 * Authors:
 *   Nuno Das Neves <nudasnev@microsoft.com>
 *   Lillian Grassin-Drake <ligrassi@microsoft.com>
 *   Vineeth Remanan Pillai <viremana@linux.microsoft.com>
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/mshv.h>
#include <asm/mshyperv.h>

#include "mshv.h"

int mshv_synic_init(unsigned int cpu)
{
	union hv_synic_simp simp;
	union hv_synic_sint sint;
	union hv_synic_scontrol sctrl;
	struct hv_message_page **msg_page =
			this_cpu_ptr(mshv.synic_message_page);

	/* Setup the Synic's message page */
	simp.as_uint64 = hv_get_register(HV_REGISTER_SIMP);
	simp.simp_enabled = true;
	*msg_page = memremap(simp.base_simp_gpa << HV_HYP_PAGE_SHIFT,
			     HV_HYP_PAGE_SIZE,
                             MEMREMAP_WB);
	if (!msg_page) {
		pr_err("%s: memremap failed\n", __func__);
		return -EFAULT;
	}
	hv_set_register(HV_REGISTER_SIMP, simp.as_uint64);

	/* Enable intercepts */
	sint.as_uint64 = 0;
	sint.vector = HYPERVISOR_CALLBACK_VECTOR;
	sint.masked = false;
#ifdef HV_DEPRECATING_AEOI_RECOMMENDED
	sint.auto_eoi =	!(ms_hyperv.hints & HV_DEPRECATING_AEOI_RECOMMENDED);
#else
	sint.auto_eoi = 0;
#endif
	hv_set_register(HV_REGISTER_SINT0 + HV_SYNIC_INTERCEPTION_SINT_INDEX,
			sint.as_uint64);

	/* Enable global synic bit */
	sctrl.as_uint64 = hv_get_register(HV_REGISTER_SCONTROL);
	sctrl.enable = 1;
	hv_set_register(HV_REGISTER_SCONTROL, sctrl.as_uint64);

	return 0;
}

int mshv_synic_cleanup(unsigned int cpu)
{
	union hv_synic_sint sint;
	union hv_synic_simp simp;
	union hv_synic_scontrol sctrl;
	struct hv_message_page **msg_page =
			this_cpu_ptr(mshv.synic_message_page);

	/* Disable the interrupt */
	sint.as_uint64 = hv_get_register(HV_REGISTER_SINT0 + HV_SYNIC_INTERCEPTION_SINT_INDEX);
	sint.masked = true;
	hv_set_register(HV_REGISTER_SINT0 + HV_SYNIC_INTERCEPTION_SINT_INDEX,
			sint.as_uint64);

	/* Disable Synic's message page */
	simp.as_uint64 = hv_get_register(HV_REGISTER_SIMP);
	simp.simp_enabled = false;
	hv_set_register(HV_REGISTER_SIMP, simp.as_uint64);
	memunmap(*msg_page);

	/* Disable global synic bit */
	sctrl.as_uint64 = hv_get_register(HV_REGISTER_SCONTROL);
	sctrl.enable = 0;
	hv_set_register(HV_REGISTER_SCONTROL, sctrl.as_uint64);

	return 0;
}
