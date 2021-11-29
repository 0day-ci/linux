/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_TRACE_VGIC_NESTED_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_VGIC_NESTED_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvm

#define SLR_ENTRY_VALS(x)							\
	" ",									\
	!!(__entry->lrs[x] & ICH_LR_HW),		   			\
	!!(__entry->lrs[x] & ICH_LR_PENDING_BIT),	   			\
	!!(__entry->lrs[x] & ICH_LR_ACTIVE_BIT),	   			\
	__entry->lrs[x] & ICH_LR_VIRTUAL_ID_MASK,				\
	(__entry->lrs[x] & ICH_LR_PHYS_ID_MASK) >> ICH_LR_PHYS_ID_SHIFT,	\
	(__entry->orig_lrs[x] & ICH_LR_PHYS_ID_MASK) >> ICH_LR_PHYS_ID_SHIFT

TRACE_EVENT(vgic_create_shadow_lrs,
	TP_PROTO(struct kvm_vcpu *vcpu, int nr_lr, u64 *lrs, u64 *orig_lrs),
	TP_ARGS(vcpu, nr_lr, lrs, orig_lrs),

	TP_STRUCT__entry(
		__field(	int,	nr_lr			)
		__array(	u64,	lrs,		16	)
		__array(	u64,	orig_lrs,	16	)
	),

	TP_fast_assign(
		__entry->nr_lr		= nr_lr;
		memcpy(__entry->lrs, lrs, 16 * sizeof(u64));
		memcpy(__entry->orig_lrs, orig_lrs, 16 * sizeof(u64));
	),

	TP_printk("nr_lr: %d\n"
		  "%50sLR[ 0]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu (%5llu)\n"
		  "%50sLR[ 1]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu (%5llu)\n"
		  "%50sLR[ 2]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu (%5llu)\n"
		  "%50sLR[ 3]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu (%5llu)\n"
		  "%50sLR[ 4]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu (%5llu)\n"
		  "%50sLR[ 5]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu (%5llu)\n"
		  "%50sLR[ 6]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu (%5llu)\n"
		  "%50sLR[ 7]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu (%5llu)\n"
		  "%50sLR[ 8]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu (%5llu)\n"
		  "%50sLR[ 9]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu (%5llu)\n"
		  "%50sLR[10]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu (%5llu)\n"
		  "%50sLR[11]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu (%5llu)\n"
		  "%50sLR[12]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu (%5llu)\n"
		  "%50sLR[13]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu (%5llu)\n"
		  "%50sLR[14]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu (%5llu)\n"
		  "%50sLR[15]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu (%5llu)",
		  __entry->nr_lr,
		  SLR_ENTRY_VALS(0), SLR_ENTRY_VALS(1), SLR_ENTRY_VALS(2),
		  SLR_ENTRY_VALS(3), SLR_ENTRY_VALS(4), SLR_ENTRY_VALS(5),
		  SLR_ENTRY_VALS(6), SLR_ENTRY_VALS(7), SLR_ENTRY_VALS(8),
		  SLR_ENTRY_VALS(9), SLR_ENTRY_VALS(10), SLR_ENTRY_VALS(11),
		  SLR_ENTRY_VALS(12), SLR_ENTRY_VALS(13), SLR_ENTRY_VALS(14),
		  SLR_ENTRY_VALS(15))
);

#define LR_ENTRY_VALS(x)							\
	" ",									\
	!!(__entry->lrs[x] & ICH_LR_HW),		   			\
	!!(__entry->lrs[x] & ICH_LR_PENDING_BIT),	   			\
	!!(__entry->lrs[x] & ICH_LR_ACTIVE_BIT),	   			\
	__entry->lrs[x] & ICH_LR_VIRTUAL_ID_MASK,				\
	(__entry->lrs[x] & ICH_LR_PHYS_ID_MASK) >> ICH_LR_PHYS_ID_SHIFT

TRACE_EVENT(vgic_put_nested,
	TP_PROTO(struct kvm_vcpu *vcpu, int nr_lr, u64 *lrs),
	TP_ARGS(vcpu, nr_lr, lrs),

	TP_STRUCT__entry(
		__field(	int,	nr_lr			)
		__array(	u64,	lrs,		16	)
	),

	TP_fast_assign(
		__entry->nr_lr		= nr_lr;
		memcpy(__entry->lrs, lrs, 16 * sizeof(u64));
	),

	TP_printk("nr_lr: %d\n"
		  "%50sLR[ 0]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu\n"
		  "%50sLR[ 1]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu\n"
		  "%50sLR[ 2]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu\n"
		  "%50sLR[ 3]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu\n"
		  "%50sLR[ 4]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu\n"
		  "%50sLR[ 5]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu\n"
		  "%50sLR[ 6]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu\n"
		  "%50sLR[ 7]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu\n"
		  "%50sLR[ 8]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu\n"
		  "%50sLR[ 9]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu\n"
		  "%50sLR[10]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu\n"
		  "%50sLR[11]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu\n"
		  "%50sLR[12]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu\n"
		  "%50sLR[13]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu\n"
		  "%50sLR[14]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu\n"
		  "%50sLR[15]: HW: %d P: %d: A: %d vINTID: %5llu pINTID: %5llu",
		  __entry->nr_lr,
		  LR_ENTRY_VALS(0), LR_ENTRY_VALS(1), LR_ENTRY_VALS(2),
		  LR_ENTRY_VALS(3), LR_ENTRY_VALS(4), LR_ENTRY_VALS(5),
		  LR_ENTRY_VALS(6), LR_ENTRY_VALS(7), LR_ENTRY_VALS(8),
		  LR_ENTRY_VALS(9), LR_ENTRY_VALS(10), LR_ENTRY_VALS(11),
		  LR_ENTRY_VALS(12), LR_ENTRY_VALS(13), LR_ENTRY_VALS(14),
		  LR_ENTRY_VALS(15))
);

TRACE_EVENT(vgic_nested_hw_emulate,
	TP_PROTO(int lr, u64 lr_val, u32 l1_intid),
	TP_ARGS(lr, lr_val, l1_intid),

	TP_STRUCT__entry(
		__field(	int,	lr		)
		__field(	u64,	lr_val		)
		__field(	u32,	l1_intid	)
	),

	TP_fast_assign(
		__entry->lr		= lr;
		__entry->lr_val		= lr_val;
		__entry->l1_intid	= l1_intid;
	),

	TP_printk("lr: %d LR %llx L1 INTID: %u\n",
		  __entry->lr, __entry->lr_val, __entry->l1_intid)
);

#endif /* _TRACE_VGIC_NESTED_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH vgic/
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE vgic-nested-trace

/* This part must be outside protection */
#include <trace/define_trace.h>
