/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */
#ifndef _ASM_SWITCH_TO_H
#define _ASM_SWITCH_TO_H

#include <asm/cpu-features.h>
#include <asm/fpu.h>

struct task_struct;

/**
 * resume - resume execution of a task
 * @prev:	The task previously executed.
 * @next:	The task to begin executing.
 * @next_ti:	task_thread_info(next).
 *
 * This function is used whilst scheduling to save the context of prev & load
 * the context of next. Returns prev.
 */
extern asmlinkage struct task_struct *resume(struct task_struct *prev,
		struct task_struct *next, struct thread_info *next_ti);

/*
 * For newly created kernel threads switch_to() will return to
 * ret_from_kernel_thread, newly created user threads to ret_from_fork.
 * That is, everything following resume() will be skipped for new threads.
 * So everything that matters to new threads should be placed before resume().
 */
#define switch_to(prev, next, last)					\
do {									\
	lose_fpu_inatomic(1, prev);					\
	(last) = resume(prev, next, task_thread_info(next));		\
} while (0)

#endif /* _ASM_SWITCH_TO_H */
