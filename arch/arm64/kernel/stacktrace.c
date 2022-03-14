// SPDX-License-Identifier: GPL-2.0-only
/*
 * Stack tracing support
 *
 * Copyright (C) 2012 ARM Ltd.
 */
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/ftrace.h>
#include <linux/kprobes.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>
#include <linux/stacktrace.h>

#include <asm/irq.h>
#include <asm/pointer_auth.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_hyp.h>
#include <asm/stack_pointer.h>
#include <asm/stacktrace.h>

/*
 * AArch64 PCS assigns the frame pointer to x29.
 *
 * A simple function prologue looks like this:
 * 	sub	sp, sp, #0x10
 *   	stp	x29, x30, [sp]
 *	mov	x29, sp
 *
 * A simple function epilogue looks like this:
 *	mov	sp, x29
 *	ldp	x29, x30, [sp]
 *	add	sp, sp, #0x10
 */


static notrace void start_backtrace(struct stackframe *frame, unsigned long fp,
				    unsigned long pc)
{
	frame->fp = fp;
	frame->pc = pc;
#ifdef CONFIG_KRETPROBES
	frame->kr_cur = NULL;
#endif

	/*
	 * Prime the first unwind.
	 *
	 * In unwind_frame() we'll check that the FP points to a valid stack,
	 * which can't be STACK_TYPE_UNKNOWN, and the first unwind will be
	 * treated as a transition to whichever stack that happens to be. The
	 * prev_fp value won't be used, but we set it to 0 such that it is
	 * definitely not an accessible stack address.
	 */
	bitmap_zero(frame->stacks_done, __NR_STACK_TYPES);
	frame->prev_fp = 0;
	frame->prev_type = STACK_TYPE_UNKNOWN;
}
NOKPROBE_SYMBOL(start_backtrace);

/*
 * Unwind from one frame record (A) to the next frame record (B).
 *
 * We terminate early if the location of B indicates a malformed chain of frame
 * records (e.g. a cycle), determined based on the location and fp value of A
 * and the location (but not the fp value) of B.
 */
static int notrace __unwind_frame(struct stackframe *frame, struct stack_info *info,
		unsigned long (*translate_fp)(unsigned long, enum stack_type))
{
	unsigned long fp = frame->fp;

	if (fp & 0x7)
		return -EINVAL;

	if (test_bit(info->type, frame->stacks_done))
		return -EINVAL;

	/*
	 * As stacks grow downward, any valid record on the same stack must be
	 * at a strictly higher address than the prior record.
	 *
	 * Stacks can nest in several valid orders, e.g.
	 *
	 * TASK -> IRQ -> OVERFLOW -> SDEI_NORMAL
	 * TASK -> SDEI_NORMAL -> SDEI_CRITICAL -> OVERFLOW
	 * KVM_NVHE_HYP -> KVM_NVHE_OVERFLOW
	 *
	 * ... but the nesting itself is strict. Once we transition from one
	 * stack to another, it's never valid to unwind back to that first
	 * stack.
	 */
	if (info->type == frame->prev_type) {
		if (fp <= frame->prev_fp)
			return -EINVAL;
	} else {
		set_bit(frame->prev_type, frame->stacks_done);
	}

	/* Record fp as prev_fp before attempting to get the next fp */
	frame->prev_fp = fp;

	/*
	 * If fp is not from the current address space perform the
	 * necessary translation before dereferencing it to get next fp.
	 */
	if (translate_fp)
		fp = translate_fp(fp, info->type);
	if (!fp)
		return -EINVAL;

	/*
	 * Record this frame record's values and location. The prev_fp and
	 * prev_type are only meaningful to the next __unwind_frame() invocation.
	 */
	frame->fp = READ_ONCE_NOCHECK(*(unsigned long *)(fp));
	frame->pc = READ_ONCE_NOCHECK(*(unsigned long *)(fp + 8));
	frame->pc = ptrauth_strip_insn_pac(frame->pc);
	frame->prev_type = info->type;

	return 0;
}

static int notrace unwind_frame(struct task_struct *tsk, struct stackframe *frame)
{
	unsigned long fp = frame->fp;
	struct stack_info info;
	int err;

	if (!tsk)
		tsk = current;

	/* Final frame; nothing to unwind */
	if (fp == (unsigned long)task_pt_regs(tsk)->stackframe)
		return -ENOENT;

	if (!on_accessible_stack(tsk, fp, 16, &info))
		return -EINVAL;

	err = __unwind_frame(frame, &info, NULL);
	if (err)
		return err;

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	if (tsk->ret_stack &&
		(frame->pc == (unsigned long)return_to_handler)) {
		unsigned long orig_pc;
		/*
		 * This is a case where function graph tracer has
		 * modified a return address (LR) in a stack frame
		 * to hook a function return.
		 * So replace it to an original value.
		 */
		orig_pc = ftrace_graph_ret_addr(tsk, NULL, frame->pc,
						(void *)frame->fp);
		if (WARN_ON_ONCE(frame->pc == orig_pc))
			return -EINVAL;
		frame->pc = orig_pc;
	}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
#ifdef CONFIG_KRETPROBES
	if (is_kretprobe_trampoline(frame->pc))
		frame->pc = kretprobe_find_ret_addr(tsk, (void *)frame->fp, &frame->kr_cur);
#endif

	return 0;
}
NOKPROBE_SYMBOL(unwind_frame);

static void notrace __walk_stackframe(struct task_struct *tsk, struct stackframe *frame,
		bool (*fn)(void *, unsigned long), void *data,
		int (*unwind_frame_fn)(struct task_struct *tsk, struct stackframe *frame))
{
	while (1) {
		int ret;

		if (!fn(data, frame->pc))
			break;
		ret = unwind_frame_fn(tsk, frame);
		if (ret < 0)
			break;
	}
}

static void notrace walk_stackframe(struct task_struct *tsk,
				    struct stackframe *frame,
				    bool (*fn)(void *, unsigned long), void *data)
{
	__walk_stackframe(tsk, frame, fn, data, unwind_frame);
}
NOKPROBE_SYMBOL(walk_stackframe);

static bool dump_backtrace_entry(void *arg, unsigned long where)
{
	char *loglvl = arg;
	printk("%s %pSb\n", loglvl, (void *)where);
	return true;
}

void dump_backtrace(struct pt_regs *regs, struct task_struct *tsk,
		    const char *loglvl)
{
	pr_debug("%s(regs = %p tsk = %p)\n", __func__, regs, tsk);

	if (regs && user_mode(regs))
		return;

	if (!tsk)
		tsk = current;

	if (!try_get_task_stack(tsk))
		return;

	printk("%sCall trace:\n", loglvl);
	arch_stack_walk(dump_backtrace_entry, (void *)loglvl, tsk, regs);

	put_task_stack(tsk);
}

void show_stack(struct task_struct *tsk, unsigned long *sp, const char *loglvl)
{
	dump_backtrace(NULL, tsk, loglvl);
	barrier();
}

noinline notrace void arch_stack_walk(stack_trace_consume_fn consume_entry,
			      void *cookie, struct task_struct *task,
			      struct pt_regs *regs)
{
	struct stackframe frame;

	if (regs)
		start_backtrace(&frame, regs->regs[29], regs->pc);
	else if (task == current)
		start_backtrace(&frame,
				(unsigned long)__builtin_frame_address(1),
				(unsigned long)__builtin_return_address(0));
	else
		start_backtrace(&frame, thread_saved_fp(task),
				thread_saved_pc(task));

	walk_stackframe(task, &frame, consume_entry, cookie);
}

#ifdef CONFIG_NVHE_EL2_DEBUG
DECLARE_PER_CPU(unsigned long, kvm_arm_hyp_stack_page);
DECLARE_KVM_NVHE_PER_CPU(unsigned long [PAGE_SIZE/sizeof(long)], hyp_overflow_stack);
DECLARE_KVM_NVHE_PER_CPU(struct kvm_nvhe_panic_info, kvm_panic_info);

static inline bool kvm_nvhe_on_overflow_stack(unsigned long sp, unsigned long size,
				 struct stack_info *info)
{
	struct kvm_nvhe_panic_info *panic_info = this_cpu_ptr_nvhe_sym(kvm_panic_info);
	unsigned long low = (unsigned long)panic_info->hyp_overflow_stack_base;
	unsigned long high = low + PAGE_SIZE;

	return on_stack(sp, size, low, high, STACK_TYPE_KVM_NVHE_OVERFLOW, info);
}

static inline bool kvm_nvhe_on_hyp_stack(unsigned long sp, unsigned long size,
				 struct stack_info *info)
{
	struct kvm_nvhe_panic_info *panic_info = this_cpu_ptr_nvhe_sym(kvm_panic_info);
	unsigned long low = (unsigned long)panic_info->hyp_stack_base;
	unsigned long high = low + PAGE_SIZE;

	return on_stack(sp, size, low, high, STACK_TYPE_KVM_NVHE_HYP, info);
}

static inline bool kvm_nvhe_on_accessible_stack(unsigned long sp, unsigned long size,
				       struct stack_info *info)
{
	if (info)
		info->type = STACK_TYPE_UNKNOWN;

	if (kvm_nvhe_on_hyp_stack(sp, size, info))
		return true;
	if (kvm_nvhe_on_overflow_stack(sp, size, info))
		return true;

	return false;
}

static unsigned long kvm_nvhe_hyp_stack_kern_va(unsigned long addr)
{
	struct kvm_nvhe_panic_info *panic_info = this_cpu_ptr_nvhe_sym(kvm_panic_info);
	unsigned long hyp_base, kern_base, hyp_offset;

	hyp_base = (unsigned long)panic_info->hyp_stack_base;
	hyp_offset = addr - hyp_base;

	kern_base = (unsigned long)*this_cpu_ptr(&kvm_arm_hyp_stack_page);

	return kern_base + hyp_offset;
}

static unsigned long kvm_nvhe_overflow_stack_kern_va(unsigned long addr)
{
	struct kvm_nvhe_panic_info *panic_info = this_cpu_ptr_nvhe_sym(kvm_panic_info);
	unsigned long hyp_base, kern_base, hyp_offset;

	hyp_base = (unsigned long)panic_info->hyp_overflow_stack_base;
	hyp_offset = addr - hyp_base;

	kern_base = (unsigned long)this_cpu_ptr_nvhe_sym(hyp_overflow_stack);

	return kern_base + hyp_offset;
}

/*
 * Convert KVM nVHE hypervisor stack VA to a kernel VA.
 *
 * The nVHE hypervisor stack is mapped in the flexible 'private' VA range, to allow
 * for guard pages below the stack. Consequently, the fixed offset address
 * translation macros won't work here.
 *
 * The kernel VA is calculated as an offset from the kernel VA of the hypervisor
 * stack base. See: kvm_nvhe_hyp_stack_kern_va(),  kvm_nvhe_overflow_stack_kern_va()
 */
static unsigned long kvm_nvhe_stack_kern_va(unsigned long addr,
					enum stack_type type)
{
	switch (type) {
	case STACK_TYPE_KVM_NVHE_HYP:
		return kvm_nvhe_hyp_stack_kern_va(addr);
	case STACK_TYPE_KVM_NVHE_OVERFLOW:
		return kvm_nvhe_overflow_stack_kern_va(addr);
	default:
		return 0UL;
	}
}

static int notrace kvm_nvhe_unwind_frame(struct task_struct *tsk,
					struct stackframe *frame)
{
	struct stack_info info;

	if (!kvm_nvhe_on_accessible_stack(frame->fp, 16, &info))
		return -EINVAL;

	return  __unwind_frame(frame, &info, kvm_nvhe_stack_kern_va);
}

static bool kvm_nvhe_dump_backtrace_entry(void *arg, unsigned long where)
{
	unsigned long va_mask = GENMASK_ULL(vabits_actual - 1, 0);
	unsigned long hyp_offset = (unsigned long)arg;

	where &= va_mask;	/* Mask tags */
	where += hyp_offset;	/* Convert to kern addr */

	kvm_err("[<%016lx>] %pB\n", where, (void *)where);

	return true;
}

static void notrace kvm_nvhe_walk_stackframe(struct task_struct *tsk,
				    struct stackframe *frame,
				    bool (*fn)(void *, unsigned long), void *data)
{
	__walk_stackframe(tsk, frame, fn, data, kvm_nvhe_unwind_frame);
}

void kvm_nvhe_dump_backtrace(unsigned long hyp_offset)
{
	struct kvm_nvhe_panic_info *panic_info = this_cpu_ptr_nvhe_sym(kvm_panic_info);
	struct stackframe frame;

	start_backtrace(&frame, panic_info->fp, panic_info->pc);
	pr_err("nVHE HYP call trace:\n");
	kvm_nvhe_walk_stackframe(NULL, &frame, kvm_nvhe_dump_backtrace_entry,
					(void *)hyp_offset);
	pr_err("---- end of nVHE HYP call trace ----\n");
}
#endif /* CONFIG_NVHE_EL2_DEBUG */
