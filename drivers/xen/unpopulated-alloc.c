// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/memremap.h>
#include <linux/slab.h>

#include <asm/page.h>

#include <xen/page.h>
#include <xen/xen.h>

static DEFINE_MUTEX(list_lock);
static struct page *page_list;
static unsigned int list_count;

static struct resource *target_resource;
static struct resource xen_resource = {
	.name = "Xen unallocated space",
};

int __weak arch_xen_unpopulated_init_resource(struct resource *res)
{
	return -ENOSYS;
}

static int fill_list(unsigned int nr_pages)
{
	struct dev_pagemap *pgmap;
	struct resource *res, *tmp_res = NULL;
	void *vaddr;
	unsigned int i, alloc_pages = round_up(nr_pages, PAGES_PER_SECTION);
	int ret;

	/*
	 * Try to use Xen resource the first and fall back to default resource
	 * if arch doesn't offer one.
	 */
	if (!target_resource) {
		ret = arch_xen_unpopulated_init_resource(&xen_resource);
		if (!ret) {
			target_resource = &xen_resource;
		} else if (ret == -ENOSYS) {
			target_resource = &iomem_resource;
		} else {
			pr_err("Cannot initialize Xen resource\n");
			return ret;
		}
	}

	res = kzalloc(sizeof(*res), GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	res->name = "Xen scratch";
	res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;

	ret = allocate_resource(target_resource, res,
				alloc_pages * PAGE_SIZE, 0, -1,
				PAGES_PER_SECTION * PAGE_SIZE, NULL, NULL);
	if (ret < 0) {
		pr_err("Cannot allocate new IOMEM resource\n");
		goto err_resource;
	}

	/*
	 * Reserve the region previously allocated from Xen resource to avoid
	 * re-using it by someone else.
	 */
	if (target_resource != &iomem_resource) {
		tmp_res = kzalloc(sizeof(*tmp_res), GFP_KERNEL);
		if (!res) {
			ret = -ENOMEM;
			goto err_insert;
		}

		tmp_res->name = res->name;
		tmp_res->start = res->start;
		tmp_res->end = res->end;
		tmp_res->flags = res->flags;

		ret = insert_resource(&iomem_resource, tmp_res);
		if (ret < 0) {
			pr_err("Cannot insert IOMEM resource [%llx - %llx]\n",
			       tmp_res->start, tmp_res->end);
			kfree(tmp_res);
			goto err_insert;
		}
	}

	pgmap = kzalloc(sizeof(*pgmap), GFP_KERNEL);
	if (!pgmap) {
		ret = -ENOMEM;
		goto err_pgmap;
	}

	pgmap->type = MEMORY_DEVICE_GENERIC;
	pgmap->range = (struct range) {
		.start = res->start,
		.end = res->end,
	};
	pgmap->nr_range = 1;
	pgmap->owner = res;

#ifdef CONFIG_XEN_HAVE_PVMMU
        /*
         * memremap will build page tables for the new memory so
         * the p2m must contain invalid entries so the correct
         * non-present PTEs will be written.
         *
         * If a failure occurs, the original (identity) p2m entries
         * are not restored since this region is now known not to
         * conflict with any devices.
         */
	if (!xen_feature(XENFEAT_auto_translated_physmap)) {
		xen_pfn_t pfn = PFN_DOWN(res->start);

		for (i = 0; i < alloc_pages; i++) {
			if (!set_phys_to_machine(pfn + i, INVALID_P2M_ENTRY)) {
				pr_warn("set_phys_to_machine() failed, no memory added\n");
				ret = -ENOMEM;
				goto err_memremap;
			}
                }
	}
#endif

	vaddr = memremap_pages(pgmap, NUMA_NO_NODE);
	if (IS_ERR(vaddr)) {
		pr_err("Cannot remap memory range\n");
		ret = PTR_ERR(vaddr);
		goto err_memremap;
	}

	for (i = 0; i < alloc_pages; i++) {
		struct page *pg = virt_to_page(vaddr + PAGE_SIZE * i);

		BUG_ON(!virt_addr_valid(vaddr + PAGE_SIZE * i));
		pg->zone_device_data = page_list;
		page_list = pg;
		list_count++;
	}

	return 0;

err_memremap:
	kfree(pgmap);
err_pgmap:
	if (tmp_res) {
		release_resource(tmp_res);
		kfree(tmp_res);
	}
err_insert:
	release_resource(res);
err_resource:
	kfree(res);
	return ret;
}

/**
 * xen_alloc_unpopulated_pages - alloc unpopulated pages
 * @nr_pages: Number of pages
 * @pages: pages returned
 * @return 0 on success, error otherwise
 */
int xen_alloc_unpopulated_pages(unsigned int nr_pages, struct page **pages)
{
	unsigned int i;
	int ret = 0;

	mutex_lock(&list_lock);
	if (list_count < nr_pages) {
		ret = fill_list(nr_pages - list_count);
		if (ret)
			goto out;
	}

	for (i = 0; i < nr_pages; i++) {
		struct page *pg = page_list;

		BUG_ON(!pg);
		page_list = pg->zone_device_data;
		list_count--;
		pages[i] = pg;

#ifdef CONFIG_XEN_HAVE_PVMMU
		if (!xen_feature(XENFEAT_auto_translated_physmap)) {
			ret = xen_alloc_p2m_entry(page_to_pfn(pg));
			if (ret < 0) {
				unsigned int j;

				for (j = 0; j <= i; j++) {
					pages[j]->zone_device_data = page_list;
					page_list = pages[j];
					list_count++;
				}
				goto out;
			}
		}
#endif
	}

out:
	mutex_unlock(&list_lock);
	return ret;
}
EXPORT_SYMBOL(xen_alloc_unpopulated_pages);

/**
 * xen_free_unpopulated_pages - return unpopulated pages
 * @nr_pages: Number of pages
 * @pages: pages to return
 */
void xen_free_unpopulated_pages(unsigned int nr_pages, struct page **pages)
{
	unsigned int i;

	mutex_lock(&list_lock);
	for (i = 0; i < nr_pages; i++) {
		pages[i]->zone_device_data = page_list;
		page_list = pages[i];
		list_count++;
	}
	mutex_unlock(&list_lock);
}
EXPORT_SYMBOL(xen_free_unpopulated_pages);

#ifdef CONFIG_XEN_PV
static int __init init(void)
{
	unsigned int i;

	if (!xen_domain())
		return -ENODEV;

	if (!xen_pv_domain())
		return 0;

	/*
	 * Initialize with pages from the extra memory regions (see
	 * arch/x86/xen/setup.c).
	 */
	for (i = 0; i < XEN_EXTRA_MEM_MAX_REGIONS; i++) {
		unsigned int j;

		for (j = 0; j < xen_extra_mem[i].n_pfns; j++) {
			struct page *pg =
				pfn_to_page(xen_extra_mem[i].start_pfn + j);

			pg->zone_device_data = page_list;
			page_list = pg;
			list_count++;
		}
	}

	return 0;
}
subsys_initcall(init);
#endif
