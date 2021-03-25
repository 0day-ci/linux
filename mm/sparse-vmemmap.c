// SPDX-License-Identifier: GPL-2.0
/*
 * Virtual Memory Map support
 *
 * (C) 2007 sgi. Christoph Lameter.
 *
 * Virtual memory maps allow VM primitives pfn_to_page, page_to_pfn,
 * virt_to_page, page_address() to be implemented as a base offset
 * calculation without memory access.
 *
 * However, virtual mappings need a page table and TLBs. Many Linux
 * architectures already map their physical space using 1-1 mappings
 * via TLBs. For those arches the virtual memory map is essentially
 * for free if we use the same page size as the 1-1 mappings. In that
 * case the overhead consists of a few additional pages that are
 * allocated to create a view of memory for vmemmap.
 *
 * The architecture is expected to provide a vmemmap_populate() function
 * to instantiate the mapping.
 */
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/memblock.h>
#include <linux/memremap.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>
#include <linux/sched.h>
#include <asm/dma.h>
#include <asm/pgalloc.h>

/*
 * Allocate a block of memory to be used to back the virtual memory map
 * or to back the page tables that are used to create the mapping.
 * Uses the main allocators if they are available, else bootmem.
 */

static void * __ref __earlyonly_bootmem_alloc(int node,
				unsigned long size,
				unsigned long align,
				unsigned long goal)
{
	return memblock_alloc_try_nid_raw(size, align, goal,
					       MEMBLOCK_ALLOC_ACCESSIBLE, node);
}

void * __meminit vmemmap_alloc_block(unsigned long size, int node)
{
	/* If the main allocator is up use that, fallback to bootmem. */
	if (slab_is_available()) {
		gfp_t gfp_mask = GFP_KERNEL|__GFP_RETRY_MAYFAIL|__GFP_NOWARN;
		int order = get_order(size);
		static bool warned;
		struct page *page;

		page = alloc_pages_node(node, gfp_mask, order);
		if (page)
			return page_address(page);

		if (!warned) {
			warn_alloc(gfp_mask & ~__GFP_NOWARN, NULL,
				   "vmemmap alloc failure: order:%u", order);
			warned = true;
		}
		return NULL;
	} else
		return __earlyonly_bootmem_alloc(node, size, size,
				__pa(MAX_DMA_ADDRESS));
}

static void * __meminit altmap_alloc_block_buf(unsigned long size,
					       struct vmem_altmap *altmap);

/* need to make sure size is all the same during early stage */
void * __meminit vmemmap_alloc_block_buf(unsigned long size, int node,
					 struct vmem_altmap *altmap)
{
	void *ptr;

	if (altmap)
		return altmap_alloc_block_buf(size, altmap);

	ptr = sparse_buffer_alloc(size);
	if (!ptr)
		ptr = vmemmap_alloc_block(size, node);
	return ptr;
}

static unsigned long __meminit vmem_altmap_next_pfn(struct vmem_altmap *altmap)
{
	return altmap->base_pfn + altmap->reserve + altmap->alloc
		+ altmap->align;
}

static unsigned long __meminit vmem_altmap_nr_free(struct vmem_altmap *altmap)
{
	unsigned long allocated = altmap->alloc + altmap->align;

	if (altmap->free > allocated)
		return altmap->free - allocated;
	return 0;
}

static void * __meminit altmap_alloc_block_buf(unsigned long size,
					       struct vmem_altmap *altmap)
{
	unsigned long pfn, nr_pfns, nr_align;

	if (size & ~PAGE_MASK) {
		pr_warn_once("%s: allocations must be multiple of PAGE_SIZE (%ld)\n",
				__func__, size);
		return NULL;
	}

	pfn = vmem_altmap_next_pfn(altmap);
	nr_pfns = size >> PAGE_SHIFT;
	nr_align = 1UL << find_first_bit(&nr_pfns, BITS_PER_LONG);
	nr_align = ALIGN(pfn, nr_align) - pfn;
	if (nr_pfns + nr_align > vmem_altmap_nr_free(altmap))
		return NULL;

	altmap->alloc += nr_pfns;
	altmap->align += nr_align;
	pfn += nr_align;

	pr_debug("%s: pfn: %#lx alloc: %ld align: %ld nr: %#lx\n",
			__func__, pfn, altmap->alloc, altmap->align, nr_pfns);
	return __va(__pfn_to_phys(pfn));
}

void __meminit vmemmap_verify(pte_t *pte, int node,
				unsigned long start, unsigned long end)
{
	unsigned long pfn = pte_pfn(*pte);
	int actual_node = early_pfn_to_nid(pfn);

	if (node_distance(actual_node, node) > LOCAL_DISTANCE)
		pr_warn("[%lx-%lx] potential offnode page_structs\n",
			start, end - 1);
}

pte_t * __meminit vmemmap_pte_populate(pmd_t *pmd, unsigned long addr, int node,
				       struct vmem_altmap *altmap, void *block)
{
	pte_t *pte = pte_offset_kernel(pmd, addr);
	if (pte_none(*pte)) {
		pte_t entry;
		void *p = block;

		if (!block) {
			p = vmemmap_alloc_block_buf(PAGE_SIZE, node, altmap);
			if (!p)
				return NULL;
		} else if (!altmap) {
			get_page(virt_to_page(block));
		}
		entry = pfn_pte(__pa(p) >> PAGE_SHIFT, PAGE_KERNEL);
		set_pte_at(&init_mm, addr, pte, entry);
	}
	return pte;
}

static void * __meminit vmemmap_alloc_block_zero(unsigned long size, int node)
{
	void *p = vmemmap_alloc_block(size, node);

	if (!p)
		return NULL;
	memset(p, 0, size);

	return p;
}

pmd_t * __meminit vmemmap_pmd_populate(pud_t *pud, unsigned long addr, int node,
				       void *block)
{
	pmd_t *pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd)) {
		void *p = block;

		if (!block) {
			p = vmemmap_alloc_block_zero(PAGE_SIZE, node);
			if (!p)
				return NULL;
		} else {
			get_page(virt_to_page(block));
		}
		pmd_populate_kernel(&init_mm, pmd, p);
	}
	return pmd;
}

pud_t * __meminit vmemmap_pud_populate(p4d_t *p4d, unsigned long addr, int node)
{
	pud_t *pud = pud_offset(p4d, addr);
	if (pud_none(*pud)) {
		void *p = vmemmap_alloc_block_zero(PAGE_SIZE, node);
		if (!p)
			return NULL;
		pud_populate(&init_mm, pud, p);
	}
	return pud;
}

p4d_t * __meminit vmemmap_p4d_populate(pgd_t *pgd, unsigned long addr, int node)
{
	p4d_t *p4d = p4d_offset(pgd, addr);
	if (p4d_none(*p4d)) {
		void *p = vmemmap_alloc_block_zero(PAGE_SIZE, node);
		if (!p)
			return NULL;
		p4d_populate(&init_mm, p4d, p);
	}
	return p4d;
}

pgd_t * __meminit vmemmap_pgd_populate(unsigned long addr, int node)
{
	pgd_t *pgd = pgd_offset_k(addr);
	if (pgd_none(*pgd)) {
		void *p = vmemmap_alloc_block_zero(PAGE_SIZE, node);
		if (!p)
			return NULL;
		pgd_populate(&init_mm, pgd, p);
	}
	return pgd;
}

static int __meminit vmemmap_populate_pmd_address(unsigned long addr, int node,
						  struct vmem_altmap *altmap,
						  void *page, pmd_t **ptr)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	pgd = vmemmap_pgd_populate(addr, node);
	if (!pgd)
		return -ENOMEM;
	p4d = vmemmap_p4d_populate(pgd, addr, node);
	if (!p4d)
		return -ENOMEM;
	pud = vmemmap_pud_populate(p4d, addr, node);
	if (!pud)
		return -ENOMEM;
	pmd = vmemmap_pmd_populate(pud, addr, node, page);
	if (!pmd)
		return -ENOMEM;
	if (ptr)
		*ptr = pmd;
	return 0;
}

static int __meminit vmemmap_populate_address(unsigned long addr, int node,
					      struct vmem_altmap *altmap,
					      void *page, void **ptr)
{
	pmd_t *pmd;
	pte_t *pte;

	if (vmemmap_populate_pmd_address(addr, node, altmap, NULL, &pmd))
		return -ENOMEM;

	pte = vmemmap_pte_populate(pmd, addr, node, altmap, page);
	if (!pte)
		return -ENOMEM;
	vmemmap_verify(pte, node, addr, addr + PAGE_SIZE);

	if (ptr)
		*ptr = __va(__pfn_to_phys(pte_pfn(*pte)));
	return 0;
}

int __meminit vmemmap_populate_basepages(unsigned long start, unsigned long end,
					 int node, struct vmem_altmap *altmap)
{
	unsigned long addr = start;

	for (; addr < end; addr += PAGE_SIZE) {
		if (vmemmap_populate_address(addr, node, altmap, NULL, NULL))
			return -ENOMEM;
	}

	return 0;
}

static int __meminit vmemmap_populate_range(unsigned long start,
					    unsigned long end,
					    int node, void *page)
{
	unsigned long addr = start;

	for (; addr < end; addr += PAGE_SIZE) {
		if (vmemmap_populate_address(addr, node, NULL, page, NULL))
			return -ENOMEM;
	}

	return 0;
}

static inline int __meminit vmemmap_populate_page(unsigned long addr, int node,
						  void **ptr)
{
	return vmemmap_populate_address(addr, node, NULL, NULL, ptr);
}

static int __meminit vmemmap_populate_pmd_range(unsigned long start,
						unsigned long end,
						int node, void *page)
{
	unsigned long addr = start;

	for (; addr < end; addr += PMD_SIZE) {
		if (vmemmap_populate_pmd_address(addr, node, NULL, page, NULL))
			return -ENOMEM;
	}

	return 0;
}

static pmd_t * __meminit vmemmap_lookup_address(unsigned long addr)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	pgd = pgd_offset_k(addr);
	if (pgd_none(*pgd))
		return NULL;

	p4d = p4d_offset(pgd, addr);
	if (p4d_none(*p4d))
		return NULL;

	pud = pud_offset(p4d, addr);
	if (pud_none(*pud))
		return NULL;

	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd))
		return NULL;

	return pmd;
}

static int __meminit vmemmap_populate_compound_pages(unsigned long start_pfn,
						     unsigned long start,
						     unsigned long end, int node,
						     struct dev_pagemap *pgmap)
{
	unsigned long offset, size, addr;

	/*
	 * For compound pages bigger than section size (e.g. 1G) fill the rest
	 * of sections as tail pages.
	 *
	 * Note that memremap_pages() resets @nr_range value and will increment
	 * it after each range successful onlining. Thus the value or @nr_range
	 * at section memmap populate corresponds to the in-progress range
	 * being onlined that we care about.
	 */
	offset = PFN_PHYS(start_pfn) - pgmap->ranges[pgmap->nr_range].start;
	if (!IS_ALIGNED(offset, pgmap_align(pgmap)) &&
	    pgmap_align(pgmap) > SUBSECTION_SIZE) {
		pmd_t *pmdp;
		pte_t *ptep;

		addr = start - PAGE_SIZE;
		pmdp = vmemmap_lookup_address(addr);
		if (!pmdp)
			return -ENOMEM;

		/* Reuse the tail pages vmemmap pmd page */
		if (offset % pgmap->align > PFN_PHYS(PAGES_PER_SECTION))
			return vmemmap_populate_pmd_range(start, end, node,
						page_to_virt(pmd_page(*pmdp)));

		/* Populate the tail pages vmemmap pmd page */
		ptep = pte_offset_kernel(pmdp, addr);
		if (pte_none(*ptep))
			return -ENOMEM;

		return vmemmap_populate_range(start, end, node,
					      page_to_virt(pte_page(*ptep)));
	}

	size = min(end - start, pgmap_pfn_align(pgmap) * sizeof(struct page));
	for (addr = start; addr < end; addr += size) {
		unsigned long next = addr, last = addr + size;
		void *block;

		/* Populate the head page vmemmap page */
		if (vmemmap_populate_page(addr, node, NULL))
			return -ENOMEM;

		/* Populate the tail pages vmemmap page */
		block = NULL;
		next = addr + PAGE_SIZE;
		if (vmemmap_populate_page(next, node, &block))
			return -ENOMEM;

		/* Reuse the previous page for the rest of tail pages */
		next += PAGE_SIZE;
		if (vmemmap_populate_range(next, last, node, block))
			return -ENOMEM;
	}

	return 0;
}

struct page * __meminit __populate_section_memmap(unsigned long pfn,
		unsigned long nr_pages, int nid, struct vmem_altmap *altmap,
		struct dev_pagemap *pgmap)
{
	unsigned long start = (unsigned long) pfn_to_page(pfn);
	unsigned long end = start + nr_pages * sizeof(struct page);
	unsigned int align = pgmap_align(pgmap);
	int r;

	if (WARN_ON_ONCE(!IS_ALIGNED(pfn, PAGES_PER_SUBSECTION) ||
		!IS_ALIGNED(nr_pages, PAGES_PER_SUBSECTION)))
		return NULL;

	if (align > PAGE_SIZE && !altmap)
		r = vmemmap_populate_compound_pages(pfn, start, end, nid, pgmap);
	else
		r = vmemmap_populate(start, end, nid, altmap);

	if (r < 0)
		return NULL;

	return pfn_to_page(pfn);
}
