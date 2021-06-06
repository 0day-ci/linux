// SPDX-License-Identifier: GPL-2.0

#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <asm/sbi.h>
#include <asm/mmu_context.h>

void flush_tlb_all(void)
{
	sbi_remote_sfence_vma(NULL, 0, -1);
}

/*
 * This function must not be called with mm_cpumask(mm) being null.
 * Kernel may panic if cmask is NULL.
 */
static void __sbi_tlb_flush_range(struct mm_struct *mm,
				  unsigned long start,
				  unsigned long size)
{
	struct cpumask *cmask = mm_cpumask(mm);
	struct cpumask hmask;
	unsigned int cpuid;
	bool local;

	if (cpumask_empty(cmask))
		return;

	cpuid = get_cpu();

	/*
	 * check if the tlbflush needs to be sent to other CPUs, local
	 * cpu is the only cpu present in cpumask.
	 */
	local = !(cpumask_any_but(cmask, cpuid) < nr_cpu_ids);

	if (static_branch_likely(&use_asid_allocator)) {
		unsigned long asid = atomic_long_read(&mm->context.id);

		if (likely(local)) {
			if (size == -1)
				local_flush_tlb_all_asid(asid);
			else
				local_flush_tlb_range_asid(start, size, asid);
		} else {
			riscv_cpuid_to_hartid_mask(cmask, &hmask);
			sbi_remote_sfence_vma_asid(cpumask_bits(&hmask),
						   start, size, asid);
		}
	} else {
		if (likely(local)) {
			/*
			 * FIXME: The non-ASID code switches to a global flush
			 * once flushing more than a single page. It's made by
			 * commit 6efb16b1d551 (RISC-V: Issue a tlb page flush
			 * if possible).
			 */
			if (size <= PAGE_SIZE)
				local_flush_tlb_page(start);
			else
				local_flush_tlb_all();
		} else {
			riscv_cpuid_to_hartid_mask(cmask, &hmask);
			sbi_remote_sfence_vma(cpumask_bits(&hmask),
					      start, size);
		}
	}

	put_cpu();
}

void flush_tlb_mm(struct mm_struct *mm)
{
	__sbi_tlb_flush_range(mm, 0, -1);
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long addr)
{
	__sbi_tlb_flush_range(vma->vm_mm, addr, PAGE_SIZE);
}

void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
		     unsigned long end)
{
	__sbi_tlb_flush_range(vma->vm_mm, start, end - start);
}
