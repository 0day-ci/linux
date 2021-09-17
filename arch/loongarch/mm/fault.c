// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 *
 * Derived from MIPS:
 * Copyright (C) 1995 - 2000 by Ralf Baechle
 */
#include <linux/context_tracking.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/entry-common.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/ratelimit.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/kdebug.h>
#include <linux/kprobes.h>
#include <linux/perf_event.h>
#include <linux/uaccess.h>

#include <asm/branch.h>
#include <asm/mmu_context.h>
#include <asm/ptrace.h>

int show_unhandled_signals = 1;

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 */
static void __kprobes __do_page_fault(struct pt_regs *regs, unsigned long write,
	unsigned long address)
{
	int si_code;
	const int field = sizeof(unsigned long) * 2;
	unsigned int flags = FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_KILLABLE;
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->mm;
	struct vm_area_struct *vma = NULL;
	vm_fault_t fault;

	static DEFINE_RATELIMIT_STATE(ratelimit_state, 5 * HZ, 10);

	si_code = SEGV_MAPERR;

	if (user_mode(regs) && (address & __UA_LIMIT))
		goto bad_area_nosemaphore;

	/*
	 * We fault-in kernel-space virtual memory on-demand. The
	 * 'reference' page table is init_mm.pgd.
	 *
	 * NOTE! We MUST NOT take any locks for this case. We may
	 * be in an interrupt or a critical region, and should
	 * only copy the information from the master page table,
	 * nothing more.
	 */
	if (unlikely(address >= VMALLOC_START && address <= VMALLOC_END))
		goto no_context;

	/*
	 * If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */
	if (faulthandler_disabled() || !mm)
		goto bad_area_nosemaphore;

	if (user_mode(regs))
		flags |= FAULT_FLAG_USER;
retry:
	mmap_read_lock(mm);
	vma = find_vma(mm, address);
	if (!vma)
		goto bad_area;
	if (vma->vm_start <= address)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (expand_stack(vma, address))
		goto bad_area;
/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
	si_code = SEGV_ACCERR;

	if (write) {
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
		flags |= FAULT_FLAG_WRITE;
	} else {
		if (address == regs->csr_epc && !(vma->vm_flags & VM_EXEC))
			goto bad_area;
		if (!(vma->vm_flags & VM_READ) &&
		    exception_epc(regs) != address)
			goto bad_area;
	}

	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	fault = handle_mm_fault(vma, address, flags, regs);

	if ((fault & VM_FAULT_RETRY) && fatal_signal_pending(current))
		return;

	perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS, 1, regs, address);
	if (unlikely(fault & VM_FAULT_ERROR)) {
		if (fault & VM_FAULT_OOM)
			goto out_of_memory;
		else if (fault & VM_FAULT_SIGSEGV)
			goto bad_area;
		else if (fault & VM_FAULT_SIGBUS)
			goto do_sigbus;
		BUG();
	}
	if (flags & FAULT_FLAG_ALLOW_RETRY) {
		if (fault & VM_FAULT_MAJOR) {
			perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS_MAJ, 1,
						  regs, address);
			tsk->maj_flt++;
		} else {
			perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS_MIN, 1,
						  regs, address);
			tsk->min_flt++;
		}
		if (fault & VM_FAULT_RETRY) {
			flags &= ~FAULT_FLAG_ALLOW_RETRY;
			flags |= FAULT_FLAG_TRIED;

			/*
			 * No need to mmap_read_unlock(mm) as we would
			 * have already released it in __lock_page_or_retry
			 * in mm/filemap.c.
			 */

			goto retry;
		}
	}

	mmap_read_unlock(mm);
	return;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
	mmap_read_unlock(mm);

bad_area_nosemaphore:
	/* User mode accesses just cause a SIGSEGV */
	if (user_mode(regs)) {
		tsk->thread.csr_badvaddr = address;
		tsk->thread.error_code = write;
		if (show_unhandled_signals &&
		    unhandled_signal(tsk, SIGSEGV) &&
		    __ratelimit(&ratelimit_state)) {
			pr_info("do_page_fault(): sending SIGSEGV to %s for invalid %s %0*lx\n",
				tsk->comm,
				write ? "write access to" : "read access from",
				field, address);
			pr_info("epc = %0*lx in", field,
				(unsigned long) regs->csr_epc);
			print_vma_addr(KERN_CONT " ", regs->csr_epc);
			pr_cont("\n");
			pr_info("ra  = %0*lx in", field,
				(unsigned long) regs->regs[1]);
			print_vma_addr(KERN_CONT " ", regs->regs[1]);
			pr_cont("\n");
		}
		current->thread.trap_nr = read_csr_excode();
		force_sig_fault(SIGSEGV, si_code, (void __user *)address);
		return;
	}

no_context:
	/* Are we prepared to handle this kernel fault?	 */
	if (fixup_exception(regs))
		return;

	/*
	 * Oops. The kernel tried to access some bad page. We'll have to
	 * terminate things with extreme prejudice.
	 */
	bust_spinlocks(1);

	pr_alert("CPU %d Unable to handle kernel paging request at "
	       "virtual address %0*lx, epc == %0*lx, ra == %0*lx\n",
	       raw_smp_processor_id(), field, address, field, regs->csr_epc,
	       field,  regs->regs[1]);
	die("Oops", regs);

out_of_memory:
	/*
	 * We ran out of memory, call the OOM killer, and return the userspace
	 * (which will retry the fault, or kill us if we got oom-killed).
	 */
	mmap_read_unlock(mm);
	if (!user_mode(regs))
		goto no_context;
	pagefault_out_of_memory();

	return;

do_sigbus:
	mmap_read_unlock(mm);

	/* Kernel mode? Handle exceptions or die */
	if (!user_mode(regs))
		goto no_context;

	/*
	 * Send a sigbus, regardless of whether we were in kernel
	 * or user mode.
	 */
	current->thread.trap_nr = read_csr_excode();
	tsk->thread.csr_badvaddr = address;
	force_sig_fault(SIGBUS, BUS_ADRERR, (void __user *)address);

	return;
}

asmlinkage void __kprobes do_page_fault(struct pt_regs *regs,
	unsigned long write, unsigned long address)
{
	irqentry_state_t state = irqentry_enter(regs);

	/* Enable interrupt if enabled in parent context */
	if (likely(regs->csr_prmd & CSR_PRMD_PIE))
		local_irq_enable();

	__do_page_fault(regs, write, address);

	local_irq_disable();

	irqentry_exit(regs, state);
}
