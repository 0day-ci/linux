/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_CGROUP_H
#define _LINUX_SCHED_CGROUP_H

#include <linux/cgroup-defs.h>
#include <linux/cpumask.h>

#ifdef CONFIG_FAIR_GROUP_SCHED

void cpu_cgroup_remote_begin(struct task_struct *p,
			     struct cgroup_subsys_state *css);
void cpu_cgroup_remote_charge(struct task_struct *p,
			      struct cgroup_subsys_state *css);

#else /* CONFIG_FAIR_GROUP_SCHED */

static inline void cpu_cgroup_remote_begin(struct task_struct *p,
					   struct cgroup_subsys_state *css) {}
static inline void cpu_cgroup_remote_charge(struct task_struct *p,
					    struct cgroup_subsys_state *css) {}

#endif /* CONFIG_FAIR_GROUP_SCHED */

#ifdef CONFIG_CFS_BANDWIDTH

int max_cfs_bandwidth_cpus(struct cgroup_subsys_state *css);

#else /* CONFIG_CFS_BANDWIDTH */

static inline int max_cfs_bandwidth_cpus(struct cgroup_subsys_state *css)
{
	return nr_cpu_ids;
}

#endif /* CONFIG_CFS_BANDWIDTH */

#endif /* _LINUX_SCHED_CGROUP_H */
