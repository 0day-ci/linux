/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, Microsoft Corporation.
 *
 * Authors:
 *   Nuno Das Neves <nudasnev@microsoft.com>
 *   Lillian Grassin-Drake <ligrassi@microsoft.com>
 *   Vineeth Pillai <viremana@linux.microsoft.com>
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/mshyperv.h>

#include "mshv.h"

int hv_call_withdraw_memory(u64 count, int node, u64 partition_id)
{
	struct hv_withdraw_memory_in *input_page;
	struct hv_withdraw_memory_out *output_page;
	struct page *page;
	u16 completed;
	unsigned long remaining = count;
	u64 status;
	int i;
	unsigned long flags;

	page = alloc_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;
	output_page = page_address(page);

	while (remaining) {
		local_irq_save(flags);

		input_page = (struct hv_withdraw_memory_in *)(*this_cpu_ptr(
			hyperv_pcpu_input_arg));

		input_page->partition_id = partition_id;
		input_page->proximity_domain_info.as_uint64 = 0;
		status = hv_do_rep_hypercall(
			HVCALL_WITHDRAW_MEMORY,
			min(remaining, HV_WITHDRAW_BATCH_SIZE), 0, input_page,
			output_page);

		local_irq_restore(flags);

		completed = hv_repcomp(status);

		for (i = 0; i < completed; i++)
			__free_page(pfn_to_page(output_page->gpa_page_list[i]));

		if (!hv_result_success(status)) {
			if (hv_result(status) == HV_STATUS_NO_RESOURCES)
				status = HV_STATUS_SUCCESS;
			else
				pr_err("%s: %s\n", __func__,
				       hv_status_to_string(status));
			break;
		}

		remaining -= completed;
	}
	free_page((unsigned long)output_page);

	return hv_status_to_errno(status);
}


int hv_call_create_partition(
		u64 flags,
		struct hv_partition_creation_properties creation_properties,
		u64 *partition_id)
{
	struct hv_create_partition_in *input;
	struct hv_create_partition_out *output;
	u64 status;
	int ret;
	unsigned long irq_flags;
	int i;

	do {
		local_irq_save(irq_flags);
		input = (struct hv_create_partition_in *)(*this_cpu_ptr(
			hyperv_pcpu_input_arg));
		output = (struct hv_create_partition_out *)(*this_cpu_ptr(
			hyperv_pcpu_output_arg));

		input->flags = flags;
		input->proximity_domain_info.as_uint64 = 0;
		input->compatibility_version = HV_COMPATIBILITY_20_H1;
		for (i = 0; i < HV_PARTITION_PROCESSOR_FEATURE_BANKS; ++i)
			input->partition_creation_properties
				.disabled_processor_features.as_uint64[i] = 0;
		input->partition_creation_properties
			.disabled_processor_xsave_features.as_uint64 = 0;
		input->isolation_properties.as_uint64 = 0;

		status = hv_do_hypercall(HVCALL_CREATE_PARTITION,
					 input, output);

		if (hv_result(status) != HV_STATUS_INSUFFICIENT_MEMORY) {
			if (hv_result_success(status))
				*partition_id = output->partition_id;
			else
				pr_err("%s: %s\n",
				       __func__, hv_status_to_string(status));
			local_irq_restore(irq_flags);
			ret = hv_status_to_errno(status);
			break;
		}
		local_irq_restore(irq_flags);
		ret = hv_call_deposit_pages(NUMA_NO_NODE,
					    hv_current_partition_id, 1);
	} while (!ret);

	return ret;
}

int hv_call_initialize_partition(u64 partition_id)
{
	struct hv_initialize_partition input;
	u64 status;
	int ret;

	input.partition_id = partition_id;

	ret = hv_call_deposit_pages(
				NUMA_NO_NODE,
				partition_id,
				HV_INIT_PARTITION_DEPOSIT_PAGES);
	if (ret)
		return ret;

	do {
		status = hv_do_fast_hypercall8(
				HVCALL_INITIALIZE_PARTITION,
				*(u64*)&input);

		if (hv_result(status) != HV_STATUS_INSUFFICIENT_MEMORY) {
			if (!hv_result_success(status))
				pr_err("%s: %s\n",
				       __func__, hv_status_to_string(status));
			ret = hv_status_to_errno(status);
			break;
		}
		ret = hv_call_deposit_pages(NUMA_NO_NODE, partition_id, 1);
	} while (!ret);

	return ret;
}

int hv_call_finalize_partition(u64 partition_id)
{
	struct hv_finalize_partition input;
	u64 status;

	input.partition_id = partition_id;
	status = hv_do_fast_hypercall8(
			HVCALL_FINALIZE_PARTITION,
			*(u64*)&input);

	if (!hv_result_success(status))
		pr_err("%s: %s\n", __func__, hv_status_to_string(status));

	return hv_status_to_errno(status);
}

int hv_call_delete_partition(u64 partition_id)
{
	struct hv_delete_partition input;
	u64 status;

	input.partition_id = partition_id;
	status = hv_do_fast_hypercall8(HVCALL_DELETE_PARTITION, *(u64*)&input);

	if (!hv_result_success(status))
		pr_err("%s: %s\n", __func__, hv_status_to_string(status));

	return hv_status_to_errno(status);
}

