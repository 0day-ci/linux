// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */

#include "ipe.h"
#include "ctx.h"
#include "hooks.h"
#include "eval.h"

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/refcount.h>
#include <linux/rcupdate.h>
#include <linux/binfmts.h>
#include <linux/mman.h>

/**
 * ipe_task_alloc: Assign a new context for an associated task structure.
 * @task: Supplies the task structure to assign a context to.
 * @clone_flags: unused.
 *
 * The context assigned is always the context of the current task.
 * Reference counts are dropped by ipe_task_free
 *
 * Return:
 * 0 - OK
 * -ENOMEM - Out of Memory
 */
int ipe_task_alloc(struct task_struct *task, unsigned long clone_flags)
{
	struct ipe_context __rcu **ctx = NULL;
	struct ipe_context *current_ctx = NULL;

	current_ctx = ipe_current_ctx();
	ctx = ipe_tsk_ctx(task);
	rcu_assign_pointer(*ctx, current_ctx);
	refcount_inc(&current_ctx->refcount);

	ipe_put_ctx(current_ctx);
	return 0;
}

/**
 * ipe_task_free: Drop a reference to an ipe_context associated with @task.
 *		  If there are no tasks remaining, the context is freed.
 * @task: Supplies the task to drop an ipe_context reference to.
 */
void ipe_task_free(struct task_struct *task)
{
	struct ipe_context *ctx;

	/*
	 * This reference was the initial creation, no need to increment
	 * refcount
	 */
	rcu_read_lock();
	ctx = rcu_dereference(*ipe_tsk_ctx(task));
	ipe_put_ctx(ctx);
	rcu_read_unlock();
}

/**
 * ipe_on_exec: LSM hook called when a process is loaded through the exec
 *		family of system calls.
 * @bprm: Supplies a pointer to a linux_binprm structure to source the file
 *	  being evaluated.
 *
 * Return:
 * 0 - OK
 * !0 - Error
 */
int ipe_on_exec(struct linux_binprm *bprm)
{
	return ipe_process_event(bprm->file, ipe_operation_exec, ipe_hook_exec);
}

/**
 * ipe_on_mmap: LSM hook called when a file is loaded through the mmap
 *		family of system calls.
 * @f: File being mmap'd. Can be NULL in the case of anonymous memory.
 * @reqprot: The requested protection on the mmap, passed from usermode.
 * @prot: The effective protection on the mmap, resolved from reqprot and
 *	  system configuration.
 * @flags: Unused.
 *
 * Return:
 * 0 - OK
 * !0 - Error
 */
int ipe_on_mmap(struct file *f, unsigned long reqprot, unsigned long prot,
		unsigned long flags)
{
	if (prot & PROT_EXEC || reqprot & PROT_EXEC)
		return ipe_process_event(f, ipe_operation_exec, ipe_hook_mmap);

	return 0;
}

/**
 * ipe_on_mprotect: LSM hook called when a mmap'd region of memory is changing
 *		    its protections via mprotect.
 * @vma: Existing virtual memory area created by mmap or similar
 * @reqprot: The requested protection on the mmap, passed from usermode.
 * @prot: The effective protection on the mmap, resolved from reqprot and
 *	  system configuration.
 *
 * Return:
 * 0 - OK
 * !0 - Error
 */
int ipe_on_mprotect(struct vm_area_struct *vma, unsigned long reqprot,
		    unsigned long prot)
{
	/* Already Executable */
	if (vma->vm_flags & VM_EXEC)
		return 0;

	if (((prot & PROT_EXEC) || reqprot & PROT_EXEC))
		return ipe_process_event(vma->vm_file, ipe_operation_exec,
					 ipe_hook_mprotect);

	return 0;
}

/**
 * ipe_on_kernel_read: LSM hook called when a file is being read in from
 *		       disk.
 * @file: Supplies a pointer to the file structure being read in from disk
 * @id: Supplies the enumeration identifying the purpose of the read.
 * @contents: Unused.
 *
 * Return:
 * 0 - OK
 * !0 - Error
 */
int ipe_on_kernel_read(struct file *file, enum kernel_read_file_id id,
		       bool contents)
{
	enum ipe_operation op;

	switch (id) {
	case READING_FIRMWARE:
		op = ipe_operation_firmware;
		break;
	case READING_MODULE:
		op = ipe_operation_kernel_module;
		break;
	case READING_KEXEC_INITRAMFS:
		op = ipe_operation_kexec_initramfs;
		break;
	case READING_KEXEC_IMAGE:
		op = ipe_operation_kexec_image;
		break;
	case READING_POLICY:
		op = ipe_operation_ima_policy;
		break;
	case READING_X509_CERTIFICATE:
		op = ipe_operation_ima_x509;
		break;
	default:
		op = ipe_operation_max;
	}

	return ipe_process_event(file, op, ipe_hook_kernel_read);
}

/**
 * ipe_on_kernel_load_data: LSM hook called when a buffer is being read in from
 *			    disk.
 * @id: Supplies the enumeration identifying the purpose of the read.
 * @contents: Unused.
 *
 * Return:
 * 0 - OK
 * !0 - Error
 */
int ipe_on_kernel_load_data(enum kernel_load_data_id id, bool contents)
{
	enum ipe_operation op;

	switch (id) {
	case LOADING_FIRMWARE:
		op = ipe_operation_firmware;
		break;
	case LOADING_MODULE:
		op = ipe_operation_kernel_module;
		break;
	case LOADING_KEXEC_INITRAMFS:
		op = ipe_operation_kexec_initramfs;
		break;
	case LOADING_KEXEC_IMAGE:
		op = ipe_operation_kexec_image;
		break;
	case LOADING_POLICY:
		op = ipe_operation_ima_policy;
		break;
	case LOADING_X509_CERTIFICATE:
		op = ipe_operation_ima_x509;
		break;
	default:
		op = ipe_operation_max;
	}

	return ipe_process_event(NULL, op, ipe_hook_kernel_load);
}
