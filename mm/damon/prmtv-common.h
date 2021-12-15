/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common Primitives for Data Access Monitoring
 *
 * Author: SeongJae Park <sj@kernel.org>
 */

#include <linux/damon.h>
#include <linux/random.h>

/* Get a random number in [l, r) */
#define damon_rand(l, r) (l + prandom_u32_max(r - l))

struct page *damon_get_page(unsigned long pfn);

void damon_ptep_mkold(pte_t *pte, struct mm_struct *mm, unsigned long addr);
void damon_pmdp_mkold(pmd_t *pmd, struct mm_struct *mm, unsigned long addr);

#ifdef CONFIG_HUGETLB_PAGE
void damon_hugetlb_mkold(pte_t *pte, struct mm_struct *mm,
			 struct vm_area_struct *vma, unsigned long addr);
#else
static inline void damon_hugetlb_mkold(pte_t *pte, struct mm_struct *mm,
				       struct vm_area_struct *vma,
				       unsigned long addr)
{
}
#endif

int damon_pageout_score(struct damon_ctx *c, struct damon_region *r,
			struct damos *s);
