/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_CPUFREQ_H
#define _LINUX_SCHED_CPUFREQ_H

#include <linux/types.h>

/*
 * Interface between cpufreq drivers and the scheduler:
 */

#define SCHED_CPUFREQ_IOWAIT	(1U << 0)

#ifdef CONFIG_CPU_FREQ
struct cpufreq_policy;

struct update_util_data {
       void (*func)(struct update_util_data *data, u64 time, unsigned int flags);
};

void cpufreq_add_update_util_hook(int cpu, struct update_util_data *data,
                       void (*func)(struct update_util_data *data, u64 time,
				    unsigned int flags));
void cpufreq_remove_update_util_hook(int cpu);
bool cpufreq_this_cpu_can_update(struct cpufreq_policy *policy);

#ifdef CONFIG_SMP
extern unsigned int sysctl_sched_capacity_margin;

static inline unsigned long map_util_freq(unsigned long util,
					  unsigned long freq, unsigned long cap)
{
	freq = freq * util / cap;
	freq = freq * sysctl_sched_capacity_margin / SCHED_CAPACITY_SCALE;

	return freq;
}

static inline unsigned long map_util_perf(unsigned long util)
{
	return util * sysctl_sched_capacity_margin / SCHED_CAPACITY_SCALE;
}
#else
static inline unsigned long map_util_freq(unsigned long util,
					unsigned long freq, unsigned long cap)
{
	return (freq + (freq >> 2)) * util / cap;
}

static inline unsigned long map_util_perf(unsigned long util)
{
	return util + (util >> 2);
}
#endif

#endif /* CONFIG_CPU_FREQ */

#endif /* _LINUX_SCHED_CPUFREQ_H */
