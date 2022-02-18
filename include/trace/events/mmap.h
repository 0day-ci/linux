/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mmap

#if !defined(_TRACE_MMAP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MMAP_H

#include <linux/tracepoint.h>
#include <../mm/internal.h>

TRACE_EVENT(vm_unmapped_area,

	TP_PROTO(unsigned long addr, struct vm_unmapped_area_info *info),

	TP_ARGS(addr, info),

	TP_STRUCT__entry(
		__field(unsigned long,	addr)
		__field(unsigned long,	total_vm)
		__field(unsigned long,	flags)
		__field(unsigned long,	length)
		__field(unsigned long,	low_limit)
		__field(unsigned long,	high_limit)
		__field(unsigned long,	align_mask)
		__field(unsigned long,	align_offset)
	),

	TP_fast_assign(
		__entry->addr = addr;
		__entry->total_vm = current->mm->total_vm;
		__entry->flags = info->flags;
		__entry->length = info->length;
		__entry->low_limit = info->low_limit;
		__entry->high_limit = info->high_limit;
		__entry->align_mask = info->align_mask;
		__entry->align_offset = info->align_offset;
	),

	TP_printk("addr=0x%lx err=%ld total_vm=0x%lx flags=0x%lx len=0x%lx lo=0x%lx hi=0x%lx mask=0x%lx ofs=0x%lx\n",
		IS_ERR_VALUE(__entry->addr) ? 0 : __entry->addr,
		IS_ERR_VALUE(__entry->addr) ? __entry->addr : 0,
		__entry->total_vm, __entry->flags, __entry->length,
		__entry->low_limit, __entry->high_limit, __entry->align_mask,
		__entry->align_offset)
);

TRACE_EVENT(vm_av_merge,

	TP_PROTO(int merged, enum vma_merge_res merge_prev, enum vma_merge_res merge_next, enum vma_merge_res merge_both),

	TP_ARGS(merged, merge_prev, merge_next, merge_both),

	TP_STRUCT__entry(
		__field(int,			merged)
		__field(enum vma_merge_res,	predecessor_different_av)
		__field(enum vma_merge_res,	successor_different_av)
		__field(enum vma_merge_res,	predecessor_with_successor_different_av)
		__field(int,			diff_count)
		__field(int,			failed_count)
	),

	TP_fast_assign(
		__entry->merged = merged == 0;
		__entry->predecessor_different_av = merge_prev;
		__entry->successor_different_av = merge_next;
		__entry->predecessor_with_successor_different_av = merge_both;
		__entry->diff_count = (merge_prev == AV_MERGE_DIFFERENT)
		+ (merge_next == AV_MERGE_DIFFERENT) + (merge_both == AV_MERGE_DIFFERENT);
		__entry->failed_count = (merge_prev == AV_MERGE_FAILED)
		+ (merge_next == AV_MERGE_FAILED) + (merge_both == AV_MERGE_FAILED);
	),

	TP_printk("merged=%d predecessor=%d successor=%d predecessor_with_successor=%d diff_count=%d failed_count=%d\n",
		__entry->merged,
		__entry->predecessor_different_av, __entry->successor_different_av,
		__entry->predecessor_with_successor_different_av,
		__entry->diff_count, __entry->failed_count)

);

TRACE_EVENT(vm_pgoff_merge,

	TP_PROTO(struct vm_area_struct *vma, bool anon_pgoff_updated),

	TP_ARGS(vma, anon_pgoff_updated),

	TP_STRUCT__entry(
		__field(bool,	faulted)
		__field(bool,	updated)
	),

	TP_fast_assign(
		__entry->faulted = vma->anon_vma;
		__entry->updated = anon_pgoff_updated;
	),

	TP_printk("faulted=%d updated=%d\n",
		__entry->faulted, __entry->updated)
);
#endif

/* This part must be outside protection */
#include <trace/define_trace.h>
