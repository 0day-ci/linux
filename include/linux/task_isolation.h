/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __LINUX_TASK_ISOL_H
#define __LINUX_TASK_ISOL_H

#ifdef CONFIG_CPU_ISOLATION

struct isol_info {
	/* Which features are active */
	unsigned long active_mask;
	/* Quiesce mask */
	unsigned long quiesce_mask;
};

extern void __tsk_isol_exit(struct task_struct *tsk);

static inline void tsk_isol_exit(struct task_struct *tsk)
{
	if (tsk->isol_info)
		__tsk_isol_exit(tsk);
}


int prctl_task_isolation_feat(unsigned long arg2, unsigned long arg3,
			      unsigned long arg4, unsigned long arg5);
int prctl_task_isolation_get(unsigned long arg2, unsigned long arg3,
			     unsigned long arg4, unsigned long arg5);
int prctl_task_isolation_set(unsigned long arg2, unsigned long arg3,
			     unsigned long arg4, unsigned long arg5);
int prctl_task_isolation_ctrl_get(unsigned long arg2, unsigned long arg3,
				  unsigned long arg4, unsigned long arg5);
int prctl_task_isolation_ctrl_set(unsigned long arg2, unsigned long arg3,
				  unsigned long arg4, unsigned long arg5);

#else

static inline void tsk_isol_exit(struct task_struct *tsk)
{
}

static inline int prctl_task_isolation_feat(unsigned long arg2,
					    unsigned long arg3,
					    unsigned long arg4,
					    unsigned long arg5)
{
	return -EOPNOTSUPP;
}

static inline int prctl_task_isolation_get(unsigned long arg2,
					   unsigned long arg3,
					   unsigned long arg4,
					   unsigned long arg5)
{
	return -EOPNOTSUPP;
}

static inline int prctl_task_isolation_set(unsigned long arg2,
					   unsigned long arg3,
					   unsigned long arg4,
					   unsigned long arg5)
{
	return -EOPNOTSUPP;
}

static inline int prctl_task_isolation_ctrl_get(unsigned long arg2,
						unsigned long arg3,
						unsigned long arg4,
						unsigned long arg5)
{
	return -EOPNOTSUPP;
}

static inline int prctl_task_isolation_ctrl_set(unsigned long arg2,
						unsigned long arg3,
						unsigned long arg4,
						unsigned long arg5)
{
	return -EOPNOTSUPP;
}

#endif /* CONFIG_CPU_ISOLATION */

#endif /* __LINUX_TASK_ISOL_H */
