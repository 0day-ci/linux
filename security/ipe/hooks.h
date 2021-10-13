/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */
#ifndef IPE_HOOKS_H
#define IPE_HOOKS_H

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/binfmts.h>
#include <linux/security.h>

enum ipe_hook {
	ipe_hook_exec = 0,
	ipe_hook_mmap,
	ipe_hook_mprotect,
	ipe_hook_kernel_read,
	ipe_hook_kernel_load,
	ipe_hook_max
};

int ipe_task_alloc(struct task_struct *task,
		   unsigned long clone_flags);

void ipe_task_free(struct task_struct *task);

int ipe_on_exec(struct linux_binprm *bprm);

int ipe_on_mmap(struct file *f, unsigned long reqprot, unsigned long prot,
		unsigned long flags);

int ipe_on_mprotect(struct vm_area_struct *vma, unsigned long reqprot,
		    unsigned long prot);

int ipe_on_kernel_read(struct file *file, enum kernel_read_file_id id,
		       bool contents);

int ipe_on_kernel_load_data(enum kernel_load_data_id id, bool contents);

void ipe_sb_free_security(struct super_block *mnt_sb);

#endif /* IPE_HOOKS_H */
