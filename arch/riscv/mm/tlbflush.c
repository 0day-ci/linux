// SPDX-License-Identifier: GPL-2.0
/*
 * TLB flush implementation.
 *
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
 */

#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <asm/sbi.h>

static void ipi_flush_tlb_all(void *info)
{
	local_flush_tlb_all();
}

void flush_tlb_all(void)
{
	if (!riscv_use_ipi_for_rfence())
		sbi_remote_sfence_vma(NULL, 0, -1);
	else
		on_each_cpu(ipi_flush_tlb_all, NULL, 1);
}

struct flush_range_data {
	unsigned long start;
	unsigned long size;
};

static void ipi_flush_range(void *info)
{
	struct flush_range_data *data = info;

	/* local cpu is the only cpu present in cpumask */
	if (data->size <= PAGE_SIZE)
		local_flush_tlb_page(data->start);
	else
		local_flush_tlb_all();
}

/*
 * This function must not be called with NULL cpumask.
 * Kernel may panic if cmask is NULL.
 */
static void flush_range(struct cpumask *cmask, unsigned long start,
			unsigned long size)
{
	struct flush_range_data info;
	struct cpumask hmask;
	unsigned int cpuid;

	if (cpumask_empty(cmask))
		return;

	info.start = start;
	info.size = size;

	cpuid = get_cpu();

	if (cpumask_any_but(cmask, cpuid) >= nr_cpu_ids) {
		ipi_flush_range(&info);
	} else {
		if (!riscv_use_ipi_for_rfence()) {
			riscv_cpuid_to_hartid_mask(cmask, &hmask);
			sbi_remote_sfence_vma(cpumask_bits(&hmask),
					      start, size);
		} else {
			on_each_cpu_mask(cmask, ipi_flush_range, &info, 1);
		}
	}

	put_cpu();
}

void flush_tlb_mm(struct mm_struct *mm)
{
	flush_range(mm_cpumask(mm), 0, -1);
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long addr)
{
	flush_range(mm_cpumask(vma->vm_mm), addr, PAGE_SIZE);
}

void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
		     unsigned long end)
{
	flush_range(mm_cpumask(vma->vm_mm), start, end - start);
}
