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
#include <linux/random.h>
#include <linux/mshv.h>
#include <asm/mshyperv.h>

#include "mshv.h"

void mshv_isr(void)
{
	struct hv_message_page **msg_page =
			this_cpu_ptr(mshv.synic_message_page);
	struct hv_message *msg;
	u32 message_type;
	struct mshv_partition *partition;
	struct mshv_vp *vp;
	u64 partition_id;
	u32 vp_index;
	int i;
	unsigned long flags;
	struct task_struct *task;

	if (unlikely(!(*msg_page))) {
		pr_err("%s: Missing synic page!\n", __func__);
		return;
	}

	msg = &((*msg_page)->sint_message[HV_SYNIC_INTERCEPTION_SINT_INDEX]);

	/*
	 * If the type isn't set, there isn't really a message;
	 * it may be some other hyperv interrupt
	 */
	message_type = msg->header.message_type;
	if (message_type == HVMSG_NONE)
		return;

	/* Look for the partition */
	partition_id = msg->header.sender;

	/* Hold this lock for the rest of the isr, because the partition could
	 * be released anytime.
	 * e.g. the MSHV_RUN_VP thread could wake on another cpu; it could
	 * release the partition unless we hold this!
	 */
	spin_lock_irqsave(&mshv.partitions.lock, flags);

	for (i = 0; i < MSHV_MAX_PARTITIONS; i++) {
		partition = mshv.partitions.array[i];
		if (partition && partition->id == partition_id)
			break;
	}

	if (unlikely(i == MSHV_MAX_PARTITIONS)) {
		pr_err("%s: failed to find partition\n", __func__);
		goto unlock_out;
	}

	/*
	 * Since we directly index the vp, and it has to exist for us to be here
	 * (because the vp is only deleted when the partition is), no additional
	 * locking is needed here
	 */
	vp_index = ((struct hv_x64_intercept_message_header *)msg->u.payload)->vp_index;
	vp = partition->vps.array[vp_index];
	if (unlikely(!vp)) {
		pr_err("%s: failed to find vp\n", __func__);
		goto unlock_out;
	}

	memcpy(vp->run.intercept_message, msg, sizeof(struct hv_message));

	if (unlikely(!vp->run.task)) {
		pr_err("%s: vp run task not set\n", __func__);
		goto unlock_out;
	}

	/* Save the task and reset it so we can wake without racing */
	task = vp->run.task;
	vp->run.task = NULL;

	/*
	 * up the semaphore before waking so that we don't race with
	 * down_trylock
	 */
	up(&vp->run.sem);

	/*
	 * Finally, wake the process. If it wakes the vp and generates
	 * another intercept then the message will be queued by the hypervisor
	 */
	wake_up_process(task);

unlock_out:
	spin_unlock_irqrestore(&mshv.partitions.lock, flags);

	/* Acknowledge message with hypervisor */
	msg->header.message_type = HVMSG_NONE;
	wrmsrl(HV_X64_MSR_EOM, 0);

	add_interrupt_randomness(HYPERVISOR_CALLBACK_VECTOR, 0);
}

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
	if (!(*msg_page)) {
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
