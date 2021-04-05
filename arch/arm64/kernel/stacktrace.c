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
#include <asm/exception.h>
#include <asm/pointer_auth.h>
#include <asm/stack_pointer.h>
#include <asm/stacktrace.h>

struct function_range {
	unsigned long	start;
	unsigned long	end;
};

/*
 * Special functions where the stack trace is unreliable.
 *
 * EL1 exceptions
 * ==============
 *
 * EL1 exceptions can happen on any instruction including instructions in
 * the frame pointer prolog or epilog. Depending on where exactly they happen,
 * they could render the stack trace unreliable.
 *
 * If an EL1 exception frame is found on the stack, mark the stack trace as
 * unreliable. Now, the EL1 exception frame is not at any well-known offset
 * on the stack. It can be anywhere on the stack. In order to properly detect
 * an EL1 exception frame, the return address must be checked against all of
 * the possible EL1 exception handlers.
 *
 * Interrupts encountered in kernel code are also EL1 exceptions. At the end
 * of an interrupt, the current task can get preempted. A stack trace taken
 * on the task after the preemption will show the EL1 frame and will be
 * considered unreliable. This is correct behavior as preemption can happen
 * practically at any point in code.
 *
 * Breakpoints encountered in kernel code are also EL1 exceptions. Breakpoints
 * can happen practically on any instruction. Mark the stack trace as
 * unreliable. Breakpoints are used for executing probe code. Stack traces
 * taken while in the probe code will show an EL1 frame and will be considered
 * unreliable. This is correct behavior.
 *
 * FTRACE
 * ======
 *
 * When CONFIG_DYNAMIC_FTRACE_WITH_REGS is enabled, the FTRACE trampoline code
 * is called from a traced function even before the frame pointer prolog.
 * FTRACE sets up two stack frames (one for the traced function and one for
 * its caller) so that the unwinder can provide a sensible stack trace for
 * any tracer function called from the FTRACE trampoline code.
 *
 * There are two cases where the stack trace is not reliable.
 *
 * (1) The task gets preempted before the two frames are set up. Preemption
 *     involves an interrupt which is an EL1 exception. The unwinder already
 *     handles EL1 exceptions.
 *
 * (2) The tracer function that gets called by the FTRACE trampoline code
 *     changes the return PC (e.g., livepatch).
 *
 *     Not all tracer functions do that. But to err on the side of safety,
 *     consider the stack trace as unreliable in all cases.
 *
 * When Function Graph Tracer is used, FTRACE modifies the return address of
 * the traced function in its stack frame to an FTRACE return trampoline
 * (return_to_handler). When the traced function returns, control goes to
 * return_to_handler. return_to_handler calls FTRACE to gather tracing data
 * and to obtain the original return address. Then, return_to_handler returns
 * to the original return address.
 *
 * There are two cases to consider from a stack trace reliability point of
 * view:
 *
 * (1) Stack traces taken within the traced function (and functions that get
 *     called from there) will show return_to_handler instead of the original
 *     return address. The original return address can be obtained from FTRACE.
 *     The unwinder already obtains it and modifies the return PC in its copy
 *     of the stack frame to the original return address. So, this is handled.
 *
 * (2) return_to_handler calls FTRACE as mentioned before. FTRACE discards
 *     the record of the original return address along the way as it does not
 *     need to maintain it anymore. This means that the unwinder cannot get
 *     the original return address beyond that point while the task is still
 *     executing in return_to_handler. So, consider the stack trace unreliable
 *     if return_to_handler is detected on the stack.
 *
 * NOTE: The unwinder must do (1) before (2).
 *
 * KPROBES
 * =======
 *
 * There are two types of kprobes:
 *
 * (1) Regular kprobes that are placed anywhere in a probed function.
 *     This is implemented by replacing the probed instruction with a
 *     breakpoint. When the breakpoint is hit, the kprobe code emulates
 *     the original instruction in-situ and returns to the next
 *     instruction.
 *
 *     Breakpoints are EL1 exceptions. When the unwinder detects them,
 *     the stack trace is marked as unreliable as it does not know where
 *     exactly the exception happened. Detection of EL1 exceptions in
 *     a stack trace will be done separately.
 *
 * (2) Return kprobes that are placed on the return of a probed function.
 *     In this case, Kprobes sets up an initial breakpoint at the
 *     beginning of the probed function. When the breakpoint is hit,
 *     Kprobes replaces the return address in the stack frame with the
 *     kretprobe_trampoline and records the original return address.
 *     When the probed function returns, control goes to the trampoline
 *     which eventually returns to the original return address.
 *
 *     Stack traces taken while in the probed function or while in the
 *     trampoline will show kretprobe_trampoline instead of the original
 *     return address. Detect this and mark the stack trace unreliable.
 *     The detection is done by checking if the return PC falls anywhere
 *     in kretprobe_trampoline.
 */
static struct function_range	special_functions[] = {
	/*
	 * EL1 exception handlers.
	 */
	{ (unsigned long) el1_sync, 0 },
	{ (unsigned long) el1_irq, 0 },
	{ (unsigned long) el1_error, 0 },
	{ (unsigned long) el1_sync_invalid, 0 },
	{ (unsigned long) el1_irq_invalid, 0 },
	{ (unsigned long) el1_fiq_invalid, 0 },
	{ (unsigned long) el1_error_invalid, 0 },

	/*
	 * FTRACE trampolines.
	 *
	 * Tracer function gets patched at the label ftrace_call. Its return
	 * address is the next instruction address.
	 */
#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
	{ (unsigned long) ftrace_call + 4, 0 },
#endif

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	{ (unsigned long) ftrace_graph_caller, 0 },
	{ (unsigned long) return_to_handler, 0 },
#endif

	/*
	 * Kprobe trampolines.
	 */
#ifdef CONFIG_KRETPROBES
	{ (unsigned long) kretprobe_trampoline, 0 },
#endif

	{ /* sentinel */ }
};

static bool is_reliable_function(unsigned long pc)
{
	static bool inited = false;
	struct function_range *func;

	if (!inited) {
		static char sym[KSYM_NAME_LEN];
		unsigned long size, offset;

		for (func = special_functions; func->start; func++) {
			if (kallsyms_lookup(func->start, &size, &offset,
					    NULL, sym)) {
				func->start -= offset;
				func->end = func->start + size;
			} else {
				/*
				 * This is just a label. So, we only need to
				 * consider that particular location. So, size
				 * is the size of one Aarch64 instruction.
				 */
				func->end = func->start + 4;
			}
		}
		inited = true;
	}

	for (func = special_functions; func->start; func++) {
		if (pc >= func->start && pc < func->end)
			return false;
	}
	return true;
}

/*
 * Check for the presence of features and conditions that render the stack
 * trace unreliable.
 *
 * Once all such cases have been addressed, this function can aid live
 * patching (and this comment can be removed).
 */
static void check_reliability(struct stackframe *frame)
{
	/*
	 * If the stack trace has already been marked unreliable, just return.
	 */
	if (!frame->reliable)
		return;

	/*
	 * First, make sure that the return address is a proper kernel text
	 * address. A NULL or invalid return address probably means there's
	 * some generated code which __kernel_text_address() doesn't know
	 * about. Mark the stack trace as not reliable.
	 */
	if (!__kernel_text_address(frame->pc)) {
		frame->reliable = false;
		return;
	}

	/*
	 * Check the reliability of the return PC's function.
	 */
	if (!is_reliable_function(frame->pc))
		frame->reliable = false;
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

	/* Terminal record; nothing to unwind */
	if (!fp)
		return -ENOENT;

	if (fp & 0xf)
		return -EINVAL;

	if (!tsk)
		tsk = current;

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

	check_reliability(frame);

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

noinline void arch_stack_walk(stack_trace_consume_fn consume_entry,
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

#endif
