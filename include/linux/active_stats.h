/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ACTIVE_STATS_H
#define _LINUX_ACTIVE_STATS_H

#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/seqlock.h>
#include <linux/spinlock.h>

/**
 * active_stats_state - State statistics associated with performance level
 * @last_event_ts:	Timestamp of the last event in nanoseconds
 * @last_freq_idx:	Last used frequency index
 * @residency:		Array which holds total time (in nanoseconds) that
 *			each frequency has been used when CPU was
 *			running
 */
struct active_stats_state {
	u64 last_event_ts;
	int last_freq_idx;
	u64 *residency;
};

/**
 * active_stats - Active Stats main structure
 * @in_ilde:	Set when CPU is in idle
 * @offline:	Set when CPU was hotplug out and is offline
 * @states_count:	Number of state entries in the statistics
 * @states_size:	Size of the state stats entries in bytes
 * @freq:	Frequency table
 * @local:	Statistics of running time which are calculated for this CPU
 * @snapshot_new:	Snapshot of statistics from a shared structure which
 *			accounts the domain frequencies residency
 * @snapshot_old:	Old snapshot of the shared structure statistics for
 *			the whole domain against which new snapshot is compared
 * @shared_ast:		Pointer to a common structure which tracks all CPUs in
 *			frequency domain
 * @lock:	Lock protecting: idle path, hotplug, Active Stats Monitor
 * @seqcount:	Used for making consistent state for the reader side
 *		of the shared statistics structure
 */
struct active_stats {
	bool in_idle;
	bool offline;
	unsigned int states_count;
	unsigned int states_size;
	unsigned int *freq;
	struct active_stats_state *local;
	struct active_stats_state *snapshot_new;
	struct active_stats_state *snapshot_old;
	struct active_stats *shared_ast;
	/* protect idle enter and hotplug */
	raw_spinlock_t lock;
	/* Seqcount to create consistent state in the read side */
	seqcount_t seqcount;
};

/**
 * active_stats_monitor - Active Stats Monitor structure
 * @local_period:	Local period for which the statistics are provided
 * @states_count:	Number of state entries in the statistics
 * @states_size:	Size of the state stats entries in bytes
 * @ast:	Active Stats structure for the associated CPU, which is used
 *		for taking the snapshot
 * @local:	Statistics of running time for each performance state which
 *		are calculated for this CPU for a specific period from last
 *		sampling (@local_period)
 * @snapshot_new:	Snapshot of statistics from Active Stats main structure
 *			which accounts this CPU performance states residency
 * @snapshot_old:	Old snapshot of the Active Stats main structure
 *			structure, against which new snapshot is compared
 * @lock:	Lock which is used to serialize access to Active Stats
 *		Monitor structure which might be used concurrently.
 */
struct active_stats_monitor {
	u64 local_period;
	unsigned int states_count;
	unsigned int states_size;
	struct active_stats *ast;
	struct active_stats_state *local;
	struct active_stats_state *snapshot_new;
	struct active_stats_state *snapshot_old;
	struct mutex lock;
};

#if defined(CONFIG_ACTIVE_STATS)
void active_stats_cpu_idle_enter(ktime_t time_start);
void active_stats_cpu_idle_exit(ktime_t time_start);
void active_stats_cpu_freq_fast_change(int cpu, unsigned int freq);
void active_stats_cpu_freq_change(int cpu, unsigned int freq);
struct active_stats_monitor *active_stats_cpu_setup_monitor(int cpu);
void active_stats_cpu_free_monitor(struct active_stats_monitor *ast_mon);
int active_stats_cpu_update_monitor(struct active_stats_monitor *ast_mon);
#else
static inline
void active_stats_cpu_freq_fast_change(int cpu, unsigned int freq) {}
static inline
void active_stats_cpu_freq_change(int cpu, unsigned int freq) {}
static inline
void active_stats_cpu_idle_enter(ktime_t time_start) {}
static inline
void active_stats_cpu_idle_exit(ktime_t time_start) {}
static inline
struct active_stats_monitor *active_stats_cpu_setup_monitor(int cpu)
{
	return ERR_PTR(-EINVAL);
}
static inline
void active_stats_cpu_free_monitor(struct active_stats_monitor *ast_mon)
{
}
static inline
int active_stats_cpu_update_monitor(struct active_stats_monitor *ast_mon)
{
	return -EINVAL;
}
#endif

#endif /* _LINUX_ACTIVE_STATS_H */
