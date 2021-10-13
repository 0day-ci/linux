/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Microsoft Corporation. All rights reserved.
 */
#ifndef IPE_HOOKS_H
#define IPE_HOOKS_H

#include <linux/types.h>
#include <linux/sched.h>

int ipe_task_alloc(struct task_struct *task,
		   unsigned long clone_flags);

void ipe_task_free(struct task_struct *task);

#endif /* IPE_HOOKS_H */
