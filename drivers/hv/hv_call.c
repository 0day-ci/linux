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

int hv_call_map_gpa_pages(
		u64 partition_id,
		u64 gpa_target,
		u64 page_count, u32 flags,
		struct page **pages)
{
	struct hv_map_gpa_pages *input_page;
	u64 status;
	int i;
	struct page **p;
	u32 completed = 0;
	unsigned long remaining = page_count;
	int rep_count;
	unsigned long irq_flags;
	int ret = 0;

	while (remaining) {

		rep_count = min(remaining, HV_MAP_GPA_BATCH_SIZE);

		local_irq_save(irq_flags);
		input_page = (struct hv_map_gpa_pages *)(*this_cpu_ptr(
			hyperv_pcpu_input_arg));

		input_page->target_partition_id = partition_id;
		input_page->target_gpa_base = gpa_target;
		input_page->map_flags = flags;

		for (i = 0, p = pages; i < rep_count; i++, p++)
			input_page->source_gpa_page_list[i] = page_to_pfn(*p);
		status = hv_do_rep_hypercall(
			HVCALL_MAP_GPA_PAGES, rep_count, 0, input_page, NULL);
		local_irq_restore(irq_flags);

		completed = hv_repcomp(status);

		if (hv_result(status) == HV_STATUS_INSUFFICIENT_MEMORY) {
			ret = hv_call_deposit_pages(NUMA_NO_NODE,
						    partition_id,
                                                    HV_MAP_GPA_DEPOSIT_PAGES);
			if (ret)
				break;
		} else if (!hv_result_success(status)) {
			pr_err("%s: completed %llu out of %llu, %s\n",
			       __func__,
			       page_count - remaining, page_count,
			       hv_status_to_string(status));
			ret = hv_status_to_errno(status);
			break;
		}

		pages += completed;
		remaining -= completed;
		gpa_target += completed;
	}

	if (ret && remaining < page_count) {
		pr_err("%s: Partially succeeded; mapped regions may be in invalid state",
		       __func__);
		ret = -EBADFD;
	}

	return ret;
}

int hv_call_unmap_gpa_pages(
		u64 partition_id,
		u64 gpa_target,
		u64 page_count, u32 flags)
{
	struct hv_unmap_gpa_pages *input_page;
	u64 status;
	int ret = 0;
	u32 completed = 0;
	unsigned long remaining = page_count;
	int rep_count;
	unsigned long irq_flags;

	while (remaining) {
		local_irq_save(irq_flags);
		input_page = (struct hv_unmap_gpa_pages *)(*this_cpu_ptr(
			hyperv_pcpu_input_arg));

		input_page->target_partition_id = partition_id;
		input_page->target_gpa_base = gpa_target;
		input_page->unmap_flags = flags;
		rep_count = min(remaining, HV_MAP_GPA_BATCH_SIZE);
		status = hv_do_rep_hypercall(
			HVCALL_UNMAP_GPA_PAGES, rep_count, 0, input_page, NULL);
		local_irq_restore(irq_flags);

		completed = hv_repcomp(status);
		if (!hv_result_success(status)) {
			pr_err("%s: completed %llu out of %llu, %s\n",
			       __func__,
			       page_count - remaining, page_count,
			       hv_status_to_string(status));
			ret = hv_status_to_errno(status);
			break;
		}

		remaining -= completed;
		gpa_target += completed;
	}

	if (ret && remaining < page_count) {
		pr_err("%s: Partially succeeded; mapped regions may be in invalid state",
		       __func__);
		ret = -EBADFD;
	}

	return ret;
}

int hv_call_get_vp_registers(
		u32 vp_index,
		u64 partition_id,
		u16 count,
		struct hv_register_assoc *registers)
{
	struct hv_get_vp_registers *input_page;
	union hv_register_value *output_page;
	u16 completed = 0;
	unsigned long remaining = count;
	int rep_count, i;
	u64 status;
	unsigned long flags;

	local_irq_save(flags);

	input_page = (struct hv_get_vp_registers *)(*this_cpu_ptr(
		hyperv_pcpu_input_arg));
	output_page = (union hv_register_value *)(*this_cpu_ptr(
		hyperv_pcpu_output_arg));

	input_page->partition_id = partition_id;
	input_page->vp_index = vp_index;
	input_page->input_vtl = 0;
	input_page->rsvd_z8 = 0;
	input_page->rsvd_z16 = 0;

	while (remaining) {
		rep_count = min(remaining, HV_GET_REGISTER_BATCH_SIZE);
		for (i = 0; i < rep_count; ++i) {
			input_page->names[i] = registers[i].name;
		}

		status = hv_do_rep_hypercall(HVCALL_GET_VP_REGISTERS, rep_count,
					     0, input_page, output_page);
		if (!hv_result_success(status)) {
			pr_err("%s: completed %li out of %u, %s\n",
			       __func__,
			       count - remaining, count,
			       hv_status_to_string(status));
			break;
		}
		completed = hv_repcomp(status);
		for (i = 0; i < completed; ++i) {
			registers[i].value = output_page[i];
		}

		registers += completed;
		remaining -= completed;
	}
	local_irq_restore(flags);

	return hv_status_to_errno(status);
}

int hv_call_set_vp_registers(
		u32 vp_index,
		u64 partition_id,
		u16 count,
		struct hv_register_assoc *registers)
{
	struct hv_set_vp_registers *input_page;
	u16 completed = 0;
	unsigned long remaining = count;
	int rep_count;
	u64 status;
	unsigned long flags;

	local_irq_save(flags);
	input_page = (struct hv_set_vp_registers *)(*this_cpu_ptr(
		hyperv_pcpu_input_arg));

	input_page->partition_id = partition_id;
	input_page->vp_index = vp_index;
	input_page->input_vtl = 0;
	input_page->rsvd_z8 = 0;
	input_page->rsvd_z16 = 0;

	while (remaining) {
		rep_count = min(remaining, HV_SET_REGISTER_BATCH_SIZE);
		memcpy(input_page->elements, registers,
			sizeof(struct hv_register_assoc) * rep_count);

		status = hv_do_rep_hypercall(HVCALL_SET_VP_REGISTERS, rep_count,
					     0, input_page, NULL);
		if (!hv_result_success(status)) {
			pr_err("%s: completed %li out of %u, %s\n",
			       __func__,
			       count - remaining, count,
			       hv_status_to_string(status));
			break;
		}
		completed = hv_repcomp(status);
		registers += completed;
		remaining -= completed;
	}

	local_irq_restore(flags);

	return hv_status_to_errno(status);
}

int hv_call_install_intercept(
		u64 partition_id,
		u32 access_type,
		enum hv_intercept_type intercept_type,
		union hv_intercept_parameters intercept_parameter)
{
	struct hv_install_intercept *input;
	unsigned long flags;
	u64 status;
	int ret;

	do {
		local_irq_save(flags);
		input = (struct hv_install_intercept *)(*this_cpu_ptr(
					hyperv_pcpu_input_arg));
		input->partition_id = partition_id;
		input->access_type = access_type;
		input->intercept_type = intercept_type;
		input->intercept_parameter = intercept_parameter;
		status = hv_do_hypercall(
				HVCALL_INSTALL_INTERCEPT, input, NULL);

		local_irq_restore(flags);
		if (hv_result(status) != HV_STATUS_INSUFFICIENT_MEMORY) {
			if (!hv_result_success(status))
				pr_err("%s: %s\n", __func__,
				       hv_status_to_string(status));
			ret = hv_status_to_errno(status);
			break;
		}

		ret = hv_call_deposit_pages(NUMA_NO_NODE, partition_id, 1);

	} while (!ret);

	return ret;
}

