// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */
#include <linux/bug.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/pagemap.h>
#include <linux/ptrace.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/highmem.h>
#include <linux/swap.h>
#include <linux/proc_fs.h>
#include <linux/pfn.h>
#include <linux/hardirq.h>
#include <linux/gfp.h>
#include <linux/initrd.h>
#include <linux/mmzone.h>

#include <asm/asm-offsets.h>
#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/dma.h>
#include <asm/mmu_context.h>
#include <asm/sections.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>

/*
 * We have up to 8 empty zeroed pages so we can map one of the right colour
 * when needed.	 Since page is never written to after the initialization we
 * don't have to care about aliases on other CPUs.
 */
unsigned long empty_zero_page, zero_page_mask;
EXPORT_SYMBOL_GPL(empty_zero_page);
EXPORT_SYMBOL(zero_page_mask);

void setup_zero_pages(void)
{
	unsigned int order, i;
	struct page *page;

	order = 0;

	empty_zero_page = __get_free_pages(GFP_KERNEL | __GFP_ZERO, order);
	if (!empty_zero_page)
		panic("Oh boy, that early out of memory?");

	page = virt_to_page((void *)empty_zero_page);
	split_page(page, order);
	for (i = 0; i < (1 << order); i++, page++)
		mark_page_reserved(page);

	zero_page_mask = ((PAGE_SIZE << order) - 1) & PAGE_MASK;
}

void copy_user_highpage(struct page *to, struct page *from,
	unsigned long vaddr, struct vm_area_struct *vma)
{
	void *vfrom, *vto;

	vto = kmap_atomic(to);
	vfrom = kmap_atomic(from);
	copy_page(vto, vfrom);
	kunmap_atomic(vfrom);
	kunmap_atomic(vto);
	/* Make sure this page is cleared on other CPU's too before using it */
	smp_wmb();
}

void copy_to_user_page(struct vm_area_struct *vma,
	struct page *page, unsigned long vaddr, void *dst, const void *src,
	unsigned long len)
{
	memcpy(dst, src, len);
}

void copy_from_user_page(struct vm_area_struct *vma,
	struct page *page, unsigned long vaddr, void *dst, const void *src,
	unsigned long len)
{
	memcpy(dst, src, len);
}
EXPORT_SYMBOL_GPL(copy_from_user_page);

#ifndef CONFIG_NUMA
void __init paging_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES];

#ifdef CONFIG_ZONE_DMA
	max_zone_pfns[ZONE_DMA] = MAX_DMA_PFN;
#endif
#ifdef CONFIG_ZONE_DMA32
	max_zone_pfns[ZONE_DMA32] = MAX_DMA32_PFN;
#endif
	max_zone_pfns[ZONE_NORMAL] = max_low_pfn;

	free_area_init(max_zone_pfns);
}

void __init mem_init(void)
{
	max_mapnr = max_low_pfn;
	high_memory = (void *) __va(max_low_pfn << PAGE_SHIFT);

	memblock_free_all();
	setup_zero_pages();	/* Setup zeroed pages.  */
}
#endif /* !CONFIG_NUMA */

void free_init_pages(const char *what, unsigned long begin, unsigned long end)
{
	unsigned long pfn;

	for (pfn = PFN_UP(begin); pfn < PFN_DOWN(end); pfn++) {
		struct page *page = pfn_to_page(pfn);
		void *addr = phys_to_virt(PFN_PHYS(pfn));

		memset(addr, POISON_FREE_INITMEM, PAGE_SIZE);
		free_reserved_page(page);
	}
	pr_info("Freeing %s: %ldk freed\n", what, (end - begin) >> 10);
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	free_reserved_area((void *)start, (void *)end, POISON_FREE_INITMEM,
			   "initrd");
}
#endif

void __ref free_initmem(void)
{
	free_initmem_default(POISON_FREE_INITMEM);
}

#ifdef CONFIG_MEMORY_HOTPLUG
int arch_add_memory(int nid, u64 start, u64 size, struct mhp_params *params)
{
	unsigned long start_pfn = start >> PAGE_SHIFT;
	unsigned long nr_pages = size >> PAGE_SHIFT;
	int ret;

	ret = __add_pages(nid, start_pfn, nr_pages, params);

	if (ret)
		pr_warn("%s: Problem encountered in __add_pages() as ret=%d\n",
				__func__,  ret);

	return ret;
}

#ifdef CONFIG_HAVE_ARCH_NODEDATA_EXTENSION
pg_data_t *arch_alloc_nodedata(int nid)
{
	return kzalloc(sizeof(pg_data_t), GFP_KERNEL);
}

void arch_free_nodedata(pg_data_t *pgdat)
{
	kfree(pgdat);
}

void arch_refresh_nodedata(int nid, pg_data_t *pgdat)
{
	BUG();
}
#endif

#ifdef CONFIG_NUMA
int memory_add_physaddr_to_nid(u64 start)
{
	int nid;

	nid = pa_to_nid(start);
	return nid;
}
EXPORT_SYMBOL_GPL(memory_add_physaddr_to_nid);
#endif

#ifdef CONFIG_MEMORY_HOTREMOVE
void arch_remove_memory(u64 start, u64 size, struct vmem_altmap *altmap)
{
	unsigned long start_pfn = start >> PAGE_SHIFT;
	unsigned long nr_pages = size >> PAGE_SHIFT;
	struct page *page = pfn_to_page(start_pfn);

	/* With altmap the first mapped page is offset from @start */
	if (altmap)
		page += vmem_altmap_offset(altmap);
	__remove_pages(start_pfn, nr_pages, altmap);
}
#endif
#endif

/*
 * Align swapper_pg_dir in to 64K, allows its address to be loaded
 * with a single LUI instruction in the TLB handlers.  If we used
 * __aligned(64K), its size would get rounded up to the alignment
 * size, and waste space.  So we place it in its own section and align
 * it in the linker script.
 */
pgd_t swapper_pg_dir[_PTRS_PER_PGD] __section(".bss..swapper_pg_dir");

pgd_t invalid_pg_dir[_PTRS_PER_PGD] __page_aligned_bss;
#ifndef __PAGETABLE_PUD_FOLDED
pud_t invalid_pud_table[PTRS_PER_PUD] __page_aligned_bss;
#endif
#ifndef __PAGETABLE_PMD_FOLDED
pmd_t invalid_pmd_table[PTRS_PER_PMD] __page_aligned_bss;
EXPORT_SYMBOL_GPL(invalid_pmd_table);
#endif
pte_t invalid_pte_table[PTRS_PER_PTE] __page_aligned_bss;
EXPORT_SYMBOL(invalid_pte_table);
