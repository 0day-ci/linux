// SPDX-License-Identifier: GPL-2.0
/*
 * Free some vmemmap pages of HugeTLB
 *
 * Copyright (c) 2020, Bytedance. All rights reserved.
 *
 *     Author: Muchun Song <songmuchun@bytedance.com>
 */
#ifndef _LINUX_HUGETLB_VMEMMAP_H
#define _LINUX_HUGETLB_VMEMMAP_H
#include <linux/hugetlb.h>

/*
 * There are a lot of struct page structures associated with each HugeTLB page.
 * For tail pages, the value of compound_head is the same. So we can reuse first
 * page of tail page structures. We map the virtual addresses of the remaining
 * pages of tail page structures to the first tail page struct, and then free
 * these page frames. Therefore, we need to reserve two pages as vmemmap areas.
 */
#define RESERVE_VMEMMAP_NR		2U
#define RESERVE_VMEMMAP_SIZE		(RESERVE_VMEMMAP_NR << PAGE_SHIFT)

#ifdef CONFIG_HUGETLB_PAGE_FREE_VMEMMAP
int alloc_huge_page_vmemmap(struct hstate *h, struct page *head);
void free_huge_page_vmemmap(struct hstate *h, struct page *head);
int demote_huge_page_vmemmap(struct hstate *h, struct page *head);
void hugetlb_vmemmap_init(struct hstate *h);

/*
 * How many vmemmap pages associated with a HugeTLB page that can be freed
 * to the buddy allocator.
 */
static inline unsigned int free_vmemmap_pages_per_hpage(struct hstate *h)
{
	return h->nr_free_vmemmap_pages;
}
#else
static inline int alloc_huge_page_vmemmap(struct hstate *h, struct page *head)
{
	return 0;
}

static inline void free_huge_page_vmemmap(struct hstate *h, struct page *head)
{
}

static inline int demote_huge_page_vmemmap(struct hstate *h, struct page *head)
{
	return 0;
}

static inline void hugetlb_vmemmap_init(struct hstate *h)
{
}

static inline unsigned int free_vmemmap_pages_per_hpage(struct hstate *h)
{
	return 0;
}
#endif /* CONFIG_HUGETLB_PAGE_FREE_VMEMMAP */
#endif /* _LINUX_HUGETLB_VMEMMAP_H */
