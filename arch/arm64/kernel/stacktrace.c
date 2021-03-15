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
#include <asm/stack_pointer.h>
#include <asm/stacktrace.h>

#ifdef CONFIG_KRETPROBES
static bool kretprobe_detected(struct stackframe *frame)
{
	static char kretprobe_name[KSYM_NAME_LEN];
	static unsigned long kretprobe_pc, kretprobe_end_pc;
	unsigned long pc, offset, size;

	if (!kretprobe_pc) {
		pc = (unsigned long) kretprobe_trampoline;
		if (!kallsyms_lookup(pc, &size, &offset, NULL, kretprobe_name))
			return false;

		kretprobe_pc = pc - offset;
		kretprobe_end_pc = kretprobe_pc + size;
	}

	return frame->pc >= kretprobe_pc && frame->pc < kretprobe_end_pc;
}
#endif

static void check_if_reliable(unsigned long fp, struct stackframe *frame,
			      struct stack_info *info)
{
	struct pt_regs *regs;
	unsigned long regs_start, regs_end;
	unsigned long caller_fp;

	/*
	 * If the stack trace has already been marked unreliable, just
	 * return.
	 */
	if (!frame->reliable)
		return;

	/*
	 * Assume that this is an intermediate marker frame inside a pt_regs
	 * structure created on the stack and get the pt_regs pointer. Other
	 * checks will be done below to make sure that this is a marker
	 * frame.
	 */
	regs_start = fp - offsetof(struct pt_regs, stackframe);
	if (regs_start < info->low)
		return;
	regs_end = regs_start + sizeof(*regs);
	if (regs_end > info->high)
		return;
	regs = (struct pt_regs *) regs_start;

	/*
	 * When an EL1 exception happens, a pt_regs structure is created
	 * on the stack and the register state is recorded. Part of the
	 * state is the FP and PC at the time of the exception.
	 *
	 * In addition, the FP and PC are also stored in pt_regs->stackframe
	 * and pt_regs->stackframe is chained with other frames on the stack.
	 * This is so that the interrupted function shows up in the stack
	 * trace.
	 *
	 * The exception could have happened during the frame pointer
	 * prolog or epilog. This could result in a missing frame in
	 * the stack trace so that the caller of the interrupted
	 * function does not show up in the stack trace.
	 *
	 * So, mark the stack trace as unreliable if an EL1 frame is
	 * detected.
	 */
	if (regs->frame_type == EL1_FRAME && regs->pc == frame->pc &&
	    regs->regs[29] == frame->fp) {
		frame->reliable = false;
		return;
	}

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
	/*
	 * When tracing is active for a function, the ftrace code is called
	 * from the function even before the frame pointer prolog and
	 * epilog. ftrace creates a pt_regs structure on the stack to save
	 * register state.
	 *
	 * In addition, ftrace sets up two stack frames and chains them
	 * with other frames on the stack. One frame is pt_regs->stackframe
	 * that is for the traced function. The other frame is set up right
	 * after the pt_regs structure and it is for the caller of the
	 * traced function. This is done to ensure a proper stack trace.
	 *
	 * If the ftrace code returns to the traced function, then all is
	 * fine. But if it transfers control to a different function (like
	 * in livepatch), then a stack walk performed while still in the
	 * ftrace code will not find the target function.
	 *
	 * So, mark the stack trace as unreliable if an ftrace frame is
	 * detected.
	 */
	if (regs->frame_type == FTRACE_FRAME && frame->fp == regs_end &&
	    frame->fp < info->high) {
		/* Check the traced function's caller's frame. */
		caller_fp = READ_ONCE_NOCHECK(*(unsigned long *)(frame->fp));
		if (caller_fp == regs->regs[29]) {
			frame->reliable = false;
			return;
		}
	}
#endif

	/*
	 * A NULL or invalid return address probably means there's some
	 * generated code which __kernel_text_address() doesn't know about.
	 * Mark the stack trace as not reliable.
	 */
	if (!__kernel_text_address(frame->pc)) {
		frame->reliable = false;
		return;
	}

#ifdef CONFIG_KRETPROBES
	/*
	 * The return address of a function that has an active kretprobe
	 * is modified in the stack frame to point to a trampoline. So,
	 * the original return address is not available on the stack.
	 *
	 * A stack trace taken while executing the function (and its
	 * descendants) will not show the original caller. So, mark the
	 * stack trace as unreliable if the trampoline shows up in the
	 * stack trace. (Obtaining the original return address from
	 * task->kretprobe_instances seems problematic and not worth the
	 * effort).
	 *
	 * The stack trace taken while inside the trampoline and functions
	 * called by the trampoline have the same problem as above. This
	 * is also covered by kretprobe_detected() using a range check.
	 */
	if (kretprobe_detected(frame)) {
		frame->reliable = false;
		return;
	}
#endif
}

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

/*
 * Unwind from one frame record (A) to the next frame record (B).
 *
 * We terminate early if the location of B indicates a malformed chain of frame
 * records (e.g. a cycle), determined based on the location and fp value of A
 * and the location (but not the fp value) of B.
 */
int notrace unwind_frame(struct task_struct *tsk, struct stackframe *frame)
{
	unsigned long fp = frame->fp;
	struct stack_info info;
	struct pt_regs *regs;

	if (!tsk)
		tsk = current;
	regs = task_pt_regs(tsk);

	/* Terminal record, nothing to unwind */
	if (fp == (unsigned long) regs->stackframe) {
		if (regs->frame_type == TASK_FRAME ||
		    regs->frame_type == EL0_FRAME)
			return -ENOENT;
		return -EINVAL;
	}

	if (!fp || fp & 0xf)
		return -EINVAL;

	if (!on_accessible_stack(tsk, fp, &info))
		return -EINVAL;

	if (test_bit(info.type, frame->stacks_done))
		return -EINVAL;

	/*
	 * As stacks grow downward, any valid record on the same stack must be
	 * at a strictly higher address than the prior record.
	 *
	 * Stacks can nest in several valid orders, e.g.
	 *
	 * TASK -> IRQ -> OVERFLOW -> SDEI_NORMAL
	 * TASK -> SDEI_NORMAL -> SDEI_CRITICAL -> OVERFLOW
	 *
	 * ... but the nesting itself is strict. Once we transition from one
	 * stack to another, it's never valid to unwind back to that first
	 * stack.
	 */
	if (info.type == frame->prev_type) {
		if (fp <= frame->prev_fp)
			return -EINVAL;
	} else {
		set_bit(frame->prev_type, frame->stacks_done);
	}

	/*
	 * Record this frame record's values and location. The prev_fp and
	 * prev_type are only meaningful to the next unwind_frame() invocation.
	 */
	frame->fp = READ_ONCE_NOCHECK(*(unsigned long *)(fp));
	frame->pc = READ_ONCE_NOCHECK(*(unsigned long *)(fp + 8));
	frame->prev_fp = fp;
	frame->prev_type = info.type;

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	if (tsk->ret_stack &&
		(ptrauth_strip_insn_pac(frame->pc) == (unsigned long)return_to_handler)) {
		struct ftrace_ret_stack *ret_stack;
		/*
		 * This is a case where function graph tracer has
		 * modified a return address (LR) in a stack frame
		 * to hook a function return.
		 * So replace it to an original value.
		 */
		ret_stack = ftrace_graph_get_ret_stack(tsk, frame->graph++);
		if (WARN_ON_ONCE(!ret_stack))
			return -EINVAL;
		frame->pc = ret_stack->ret;
	}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

	frame->pc = ptrauth_strip_insn_pac(frame->pc);

	/*
	 * Check for features that render the stack trace unreliable.
	 */
	check_if_reliable(fp, frame, &info);

	return 0;
}
NOKPROBE_SYMBOL(unwind_frame);

void notrace walk_stackframe(struct task_struct *tsk, struct stackframe *frame,
			     bool (*fn)(void *, unsigned long), void *data)
{
	while (1) {
		int ret;

		if (!fn(data, frame->pc))
			break;
		ret = unwind_frame(tsk, frame);
		if (ret < 0)
			break;
	}
}
NOKPROBE_SYMBOL(walk_stackframe);

static void dump_backtrace_entry(unsigned long where, const char *loglvl)
{
	printk("%s %pS\n", loglvl, (void *)where);
}

void dump_backtrace(struct pt_regs *regs, struct task_struct *tsk,
		    const char *loglvl)
{
	struct stackframe frame;
	int skip = 0;

	pr_debug("%s(regs = %p tsk = %p)\n", __func__, regs, tsk);

	if (regs) {
		if (user_mode(regs))
			return;
		skip = 1;
	}

	if (!tsk)
		tsk = current;

	if (!try_get_task_stack(tsk))
		return;

	if (tsk == current) {
		start_backtrace(&frame,
				(unsigned long)__builtin_frame_address(0),
				(unsigned long)dump_backtrace);
	} else {
		/*
		 * task blocked in __switch_to
		 */
		start_backtrace(&frame,
				thread_saved_fp(tsk),
				thread_saved_pc(tsk));
	}

	printk("%sCall trace:\n", loglvl);
	do {
		/* skip until specified stack frame */
		if (!skip) {
			dump_backtrace_entry(frame.pc, loglvl);
		} else if (frame.fp == regs->regs[29]) {
			skip = 0;
			/*
			 * Mostly, this is the case where this function is
			 * called in panic/abort. As exception handler's
			 * stack frame does not contain the corresponding pc
			 * at which an exception has taken place, use regs->pc
			 * instead.
			 */
			dump_backtrace_entry(regs->pc, loglvl);
		}
	} while (!unwind_frame(tsk, &frame));

	put_task_stack(tsk);
}

void show_stack(struct task_struct *tsk, unsigned long *sp, const char *loglvl)
{
	dump_backtrace(NULL, tsk, loglvl);
	barrier();
}

#ifdef CONFIG_STACKTRACE

void arch_stack_walk(stack_trace_consume_fn consume_entry, void *cookie,
		     struct task_struct *task, struct pt_regs *regs)
{
	struct stackframe frame;

	if (regs)
		start_backtrace(&frame, regs->regs[29], regs->pc);
	else if (task == current)
		start_backtrace(&frame,
				(unsigned long)__builtin_frame_address(0),
				(unsigned long)arch_stack_walk);
	else
		start_backtrace(&frame, thread_saved_fp(task),
				thread_saved_pc(task));

	walk_stackframe(task, &frame, consume_entry, cookie);
}

/*
 * Walk the stack like arch_stack_walk() but stop the walk as soon as
 * some unreliability is detected in the stack.
 */
int arch_stack_walk_reliable(stack_trace_consume_fn consume_entry,
			      void *cookie, struct task_struct *task)
{
	struct stackframe frame;
	int ret = 0;

	if (task == current) {
		start_backtrace(&frame,
				(unsigned long)__builtin_frame_address(0),
				(unsigned long)arch_stack_walk_reliable);
	} else {
		/*
		 * The task must not be running anywhere for the duration of
		 * arch_stack_walk_reliable(). The caller must guarantee
		 * this.
		 */
		start_backtrace(&frame, thread_saved_fp(task),
				thread_saved_pc(task));
	}

	while (!ret) {
		if (!frame.reliable)
			return -EINVAL;
		if (!consume_entry(cookie, frame.pc))
			return -EINVAL;
		ret = unwind_frame(task, &frame);
	}

	return ret == -ENOENT ? 0 : -EINVAL;
}

#endif
