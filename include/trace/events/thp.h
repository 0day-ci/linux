/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM thp

#if !defined(_TRACE_THP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_THP_H

#include <linux/types.h>
#include <linux/tracepoint.h>

TRACE_EVENT(hugepage_invalidate,

	    TP_PROTO(unsigned long addr, unsigned long pte),
	    TP_ARGS(addr, pte),
	    TP_STRUCT__entry(
		    __field(unsigned long, addr)
		    __field(unsigned long, pte)
		    ),

	    TP_fast_assign(
		    __entry->addr = addr;
		    __entry->pte = pte;
		    ),

	    TP_printk("hugepage invalidate at addr 0x%lx and pte = 0x%lx",
		      __entry->addr, __entry->pte)
);

TRACE_EVENT(hugepage_set_pmd,

	    TP_PROTO(unsigned long addr, unsigned long pmd),
	    TP_ARGS(addr, pmd),
	    TP_STRUCT__entry(
		    __field(unsigned long, addr)
		    __field(unsigned long, pmd)
		    ),

	    TP_fast_assign(
		    __entry->addr = addr;
		    __entry->pmd = pmd;
		    ),

	    TP_printk("Set pmd with 0x%lx with 0x%lx", __entry->addr, __entry->pmd)
);


TRACE_EVENT(hugepage_update,

	    TP_PROTO(unsigned long addr, unsigned long pte, unsigned long clr, unsigned long set),
	    TP_ARGS(addr, pte, clr, set),
	    TP_STRUCT__entry(
		    __field(unsigned long, addr)
		    __field(unsigned long, pte)
		    __field(unsigned long, clr)
		    __field(unsigned long, set)
		    ),

	    TP_fast_assign(
		    __entry->addr = addr;
		    __entry->pte = pte;
		    __entry->clr = clr;
		    __entry->set = set;

		    ),

	    TP_printk("hugepage update at addr 0x%lx and pte = 0x%lx clr = 0x%lx, set = 0x%lx", __entry->addr, __entry->pte, __entry->clr, __entry->set)
);
TRACE_EVENT(hugepage_splitting,

	    TP_PROTO(unsigned long addr, unsigned long pte),
	    TP_ARGS(addr, pte),
	    TP_STRUCT__entry(
		    __field(unsigned long, addr)
		    __field(unsigned long, pte)
		    ),

	    TP_fast_assign(
		    __entry->addr = addr;
		    __entry->pte = pte;
		    ),

	    TP_printk("hugepage splitting at addr 0x%lx and pte = 0x%lx",
		      __entry->addr, __entry->pte)
);

TRACE_EVENT(set_migration_pmd,

	TP_PROTO(struct mm_struct *mm, unsigned long address,
		 pmd_t *pmdp, struct page *page, unsigned long pmdval),

	TP_ARGS(mm, address, pmdp, page, pmdval),

	TP_STRUCT__entry(
		__field(struct mm_struct *, mm)
		__field(unsigned long, address)
		__field(pmd_t *, pmdp)
		__field(struct page *, page)
		__field(unsigned long, pmdval)
	),

	TP_fast_assign(
		__entry->mm = mm;
		__entry->address = address;
		__entry->pmdp = pmdp;
		__entry->page = page;
		__entry->pmdval = pmdval;
	),

	TP_printk("mm=%p, address=%lx, pmdp=%p, page=%p pmdval=%lx",
		__entry->mm,
		__entry->address,
		__entry->pmdp,
		__entry->page,
		__entry->pmdval)
);

TRACE_EVENT(remove_migration_pmd,

	TP_PROTO(struct mm_struct *mm, unsigned long address,
		 pmd_t *pmdp, struct page *page, unsigned long pmdval),

	TP_ARGS(mm, address, pmdp, page, pmdval),

	TP_STRUCT__entry(
		__field(struct mm_struct *, mm)
		__field(unsigned long, address)
		__field(pmd_t *, pmdp)
		__field(struct page *, page)
		__field(unsigned long, pmdval)
	),

	TP_fast_assign(
		__entry->mm = mm;
		__entry->address = address;
		__entry->pmdp = pmdp;
		__entry->page = page;
		__entry->pmdval = pmdval;
	),

	TP_printk("mm=%p, address=%lx, pmdp=%p, page=%p pmdval=%lx",
		__entry->mm,
		__entry->address,
		__entry->pmdp,
		__entry->page,
		__entry->pmdval)
);
#endif /* _TRACE_THP_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
