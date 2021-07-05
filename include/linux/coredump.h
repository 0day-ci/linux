/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_COREDUMP_H
#define _LINUX_COREDUMP_H

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <asm/siginfo.h>

#ifdef CONFIG_COREDUMP
struct core_vma_metadata {
	unsigned long start, end;
	unsigned long flags;
	unsigned long dump_size;
};

extern int core_uses_pid;
extern char core_pattern[];
extern unsigned int core_pipe_limit;

/**
 * cdh_ptrace_allowed() - Checks whether ptrace of the task being core-dumped,
 *                        is allowed to the caller.
 * @task: Tracee task being core-dumped,
 *        which the core dump user-space helper wants to ptrace.
 *
 * Called by ptrace when a process attempts to ptrace a task being core-dumped.
 * If the caller is the core dump user-space helper process,
 * it will be allowed to do so, after instructing the task being core-dumped to
 * wait for the ptrace operation to complete,
 * and waiting for that task to become inactive in waiting for ptrace to complete.
 * Ptrace operation is considered complete when the tracer issues the PTRACE_CONT
 * ptrace request to the tracee.
 *
 * Context: Takes and releases the cdh_mutex.
 *          Sleeps waiting for the current task to become inactive
 *          (due to waiting for ptrace to be done).
 * Return: true if caller is core dump user-space helper, false otherwise.
 */
bool cdh_ptrace_allowed(struct task_struct *task);

/**
 * cdh_signal_continue() - Lets the specified task being core dumped know that
 *                         ptrace operation for it is done and it can continue.
 * @task: Tracee task being core-dumped, the caller wants to signal to continue.
 *
 * Called by ptrace when the tracer of the task being core dumped signals
 * that ptrace operation for it is complete,
 * by means of issuing a PTRACE_CONT request to the tracee.
 * This makes the core dump of the tracee task continue.
 *
 * Context: Takes and releases the cdh_mutex.
 */
void cdh_signal_continue(struct task_struct *task);

/*
 * These are the only things you should do on a core-file: use only these
 * functions to write out all the necessary info.
 */
struct coredump_params;
extern void dump_skip_to(struct coredump_params *cprm, unsigned long to);
extern void dump_skip(struct coredump_params *cprm, size_t nr);
extern int dump_emit(struct coredump_params *cprm, const void *addr, int nr);
extern int dump_align(struct coredump_params *cprm, int align);
int dump_user_range(struct coredump_params *cprm, unsigned long start,
		    unsigned long len);
int dump_vma_snapshot(struct coredump_params *cprm, int *vma_count,
		      struct core_vma_metadata **vma_meta,
		      size_t *vma_data_size_ptr);
extern void do_coredump(const kernel_siginfo_t *siginfo);
#else
static inline void do_coredump(const kernel_siginfo_t *siginfo) {}
#endif

#endif /* _LINUX_COREDUMP_H */
