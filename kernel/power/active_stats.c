// SPDX-License-Identifier: GPL-2.0
/*
 * Active Stats - CPU performance statistics tracking mechanism, which
 * provides handy and combined information about how long a CPU was running
 * at each frequency - excluding idle period. It is more detailed information
 * than just time accounted in CPUFreq when that frequency was set.
 *
 * Copyright (C) 2021, ARM Ltd.
 * Written by: Lukasz Luba, ARM Ltd.
 */

#include <linux/active_stats.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/ktime.h>
#include <linux/minmax.h>
#include <linux/percpu.h>
#include <linux/pm_opp.h>
#include <linux/sched/clock.h>
#include <linux/slab.h>

static cpumask_var_t cpus_to_visit;
static void processing_done_fn(struct work_struct *work);
static DECLARE_WORK(processing_done_work, processing_done_fn);

static DEFINE_PER_CPU(struct active_stats *, ast_local);

static struct active_stats_state *alloc_state_stats(int count);
static void update_local_stats(struct active_stats *ast,
			       ktime_t event_ts);

#ifdef CONFIG_DEBUG_FS
static struct dentry *rootdir;

static int active_stats_debug_residency_show(struct seq_file *s, void *unused)
{
	struct active_stats *ast = s->private;
	u64 ts, residency;
	int i;

	ts = local_clock();

	/*
	 * Print statistics for each performance state and related residency
	 * time [ns].
	 */
	for (i = 0; i < ast->states_count; i++) {
		residency = ast->local->residency[i];
		if (i == ast->local->last_freq_idx && !ast->in_idle)
			residency += ts - ast->local->last_event_ts;

		seq_printf(s, "%u:\t%llu\n", ast->freq[i], residency);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(active_stats_debug_residency);

static void active_stats_debug_init(int cpu)
{
	struct device *dev;
	struct dentry *d;

	if (!rootdir)
		rootdir = debugfs_create_dir("active_stats", NULL);

	dev = get_cpu_device(cpu);
	if (!dev)
		return;

	d = debugfs_create_dir(dev_name(dev), rootdir);
	debugfs_create_file("time_in_state", 0444, d,
			    per_cpu(ast_local, cpu),
			    &active_stats_debug_residency_fops);
}

static void active_stats_debug_remove(int cpu)
{
	struct dentry *debug_dir;
	struct device *dev;

	dev = get_cpu_device(cpu);
	if (!dev || !rootdir)
		return;

	debug_dir = debugfs_lookup(dev_name(dev), rootdir);
	debugfs_remove_recursive(debug_dir);
}
#else /* CONFIG_DEBUG_FS */
static void active_stats_debug_init(int cpu) {}
static void active_stats_debug_remove(int cpu) {}
#endif

static int get_freq_index(struct active_stats *ast, unsigned int freq)
{
	int i;

	for (i = 0; i < ast->states_count; i++)
		if (ast->freq[i] == freq)
			return i;
	return -EINVAL;
}

static void free_state_stats(struct active_stats_state *stats)
{
	if (!stats)
		return;

	kfree(stats->residency);
	kfree(stats);
}

/**
 * active_stats_cpu_setup_monitor - setup Active Stats Monitor structures
 * @cpu:	CPU id for which the update is done
 *
 * Setup Active Stats Monitor statistics for a given @cpu. It allocates the
 * needed structures for tracking the CPU performance levels residency.
 * Return a valid pointer to struct active_stats_monitor or corresponding
 * ERR_PTR().
 */
struct active_stats_monitor *active_stats_cpu_setup_monitor(int cpu)
{
	struct active_stats_monitor *ast_mon;
	struct active_stats *ast;

	ast = per_cpu(ast_local, cpu);
	if (!ast)
		return ERR_PTR(-EINVAL);

	ast_mon = kzalloc(sizeof(struct active_stats_monitor), GFP_KERNEL);
	if (!ast_mon)
		return ERR_PTR(-ENOMEM);

	ast_mon->local = alloc_state_stats(ast->states_count);
	if (!ast_mon->local)
		goto free_ast_mon;

	ast_mon->snapshot_new = alloc_state_stats(ast->states_count);
	if (!ast_mon->snapshot_new)
		goto free_local;

	ast_mon->snapshot_old = alloc_state_stats(ast->states_count);
	if (!ast_mon->snapshot_old)
		goto free_snapshot_new;

	ast_mon->ast = ast;
	ast_mon->local_period = 0;
	ast_mon->states_count = ast->states_count;
	ast_mon->states_size = ast->states_count * sizeof(u64);

	mutex_init(&ast_mon->lock);

	return ast_mon;

free_snapshot_new:
	free_state_stats(ast_mon->snapshot_new);
free_local:
	free_state_stats(ast_mon->local);
free_ast_mon:
	kfree(ast_mon);

	return ERR_PTR(-ENOMEM);
}
EXPORT_SYMBOL_GPL(active_stats_cpu_setup_monitor);

/**
 * active_stats_cpu_free_monitor - free Active Stats Monitor structures
 * @ast_mon:	Active Stats Monitor pointer
 *
 * Free the Active Stats Monitor data structures. No return value.
 */
void active_stats_cpu_free_monitor(struct active_stats_monitor *ast_mon)
{
	if (IS_ERR_OR_NULL(ast_mon))
		return;

	free_state_stats(ast_mon->snapshot_old);
	free_state_stats(ast_mon->snapshot_new);
	free_state_stats(ast_mon->local);
	kfree(ast_mon);
}
EXPORT_SYMBOL_GPL(active_stats_cpu_free_monitor);

/**
 * active_stats_cpu_update_monitor - update Active Stats Monitor structures
 * @ast_mon:	Active Stats Monitor pointer
 *
 * Update Active Stats Monitor statistics for a given @ast_mon. It calculates
 * residency time for all supported performance levels when CPU was running.
 * Return 0 for success or -EINVAL on error.
 */
int active_stats_cpu_update_monitor(struct active_stats_monitor *ast_mon)
{
	struct active_stats_state *origin, *snapshot, *old, *local;
	struct active_stats *ast;
	unsigned long flags;
	u64 event_ts;
	int size, i;
	s64 diff;

	if (IS_ERR_OR_NULL(ast_mon))
		return -EINVAL;

	ast = ast_mon->ast;

	size = ast_mon->states_size;
	origin = ast_mon->ast->local;
	local = ast_mon->local;

	mutex_lock(&ast_mon->lock);

	event_ts = local_clock();

	/* Protect from concurrent access with currently toggling idle CPU */
	raw_spin_lock_irqsave(&ast->lock, flags);

	/* If the CPU is offline, then exit immediately */
	if (ast->offline) {
		raw_spin_unlock_irqrestore(&ast->lock, flags);
		goto unlock;
	}

	/* Use older buffer for upcoming newest data */
	swap(ast_mon->snapshot_new, ast_mon->snapshot_old);

	snapshot = ast_mon->snapshot_new;
	old = ast_mon->snapshot_old;

	update_local_stats(ast, ns_to_ktime(event_ts));

	/* Take snapshot of main stats into local buffer and process locally */
	memcpy(snapshot->residency, origin->residency, size);

	raw_spin_unlock_irqrestore(&ast->lock, flags);

	/* Calculate the difference of the running time since last check */
	for (i = 0; i < ast_mon->states_count; i++) {
		diff = snapshot->residency[i] - old->residency[i];
		/* Avoid CPU local clock differences issue and set 0 */
		local->residency[i] = diff > 0 ? diff : 0;
	}

	snapshot->last_event_ts = event_ts;
	ast_mon->local_period = snapshot->last_event_ts - old->last_event_ts;

unlock:
	mutex_unlock(&ast_mon->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(active_stats_cpu_update_monitor);

static void get_stats_snapshot(struct active_stats *ast)
{
	struct active_stats_state *origin, *snapshot;
	int size = ast->states_size;
	unsigned long seq;

	origin = ast->shared_ast->local;
	snapshot = ast->snapshot_new;

	/*
	 * Take a consistent snapshot of the statistics updated from other CPU
	 * which might be changing the frequency for the whole domain.
	 */
	do {
		seq = read_seqcount_begin(&ast->shared_ast->seqcount);

		memcpy(snapshot->residency, origin->residency, size);

		snapshot->last_event_ts = origin->last_event_ts;
		snapshot->last_freq_idx = origin->last_freq_idx;

	} while (read_seqcount_retry(&ast->shared_ast->seqcount, seq));
}

static void
update_local_stats(struct active_stats *ast, ktime_t event_ts)
{
	s64 total_residency = 0;
	s64 diff, acc, period;
	ktime_t prev_ts;
	u64 prev;
	int i;

	if (ast->in_idle)
		return;

	get_stats_snapshot(ast);

	prev = max(ast->local->last_event_ts, ast->snapshot_new->last_event_ts);
	prev_ts = ns_to_ktime(prev);
	diff = ktime_sub(event_ts, prev_ts);

	prev_ts = ns_to_ktime(ast->local->last_event_ts);
	period = ktime_sub(event_ts, prev_ts);

	i = ast->snapshot_new->last_freq_idx;

	diff = max(0LL, diff);
	if (diff > 0) {
		ast->local->residency[i] += diff;
		total_residency += diff;
	}

	for (i = 0; i < ast->states_count; i++) {
		acc = ast->snapshot_new->residency[i];
		acc -= ast->snapshot_old->residency[i];

		/* Don't account twice the same running period */
		if (ast->local->last_freq_idx != i) {
			ast->local->residency[i] += acc;
			total_residency += acc;
		}
	}

	i = ast->local->last_freq_idx;
	ast->local->residency[i] += period - total_residency;

	ast->local->last_freq_idx = ast->snapshot_new->last_freq_idx;

	/* Swap the buffer pointers */
	swap(ast->snapshot_new, ast->snapshot_old);

	ast->local->last_event_ts = event_ts;
}

static inline
void _active_stats_cpu_idle_enter(struct active_stats *ast, ktime_t enter_ts)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&ast->lock, flags);
	update_local_stats(ast, enter_ts);
	ast->in_idle = true;
	raw_spin_unlock_irqrestore(&ast->lock, flags);
}

static inline
void _active_stats_cpu_idle_exit(struct active_stats *ast, ktime_t time_end)
{
	unsigned long flags;
	int size = ast->states_size;

	raw_spin_lock_irqsave(&ast->lock, flags);

	get_stats_snapshot(ast);

	ast->local->last_freq_idx = ast->snapshot_new->last_freq_idx;

	memcpy(ast->snapshot_old->residency, ast->snapshot_new->residency, size);

	/* Swap the buffer pointers */
	swap(ast->snapshot_new, ast->snapshot_old);

	ast->local->last_event_ts = time_end;
	ast->in_idle = false;

	raw_spin_unlock_irqrestore(&ast->lock, flags);
}

/**
 * active_stats_cpu_idle_enter - update Active Stats idle tracking data
 * @enter_ts:	the time stamp with idle enter value
 *
 * Update the maintained statistics for entering idle for a given CPU.
 * No return value.
 */
void active_stats_cpu_idle_enter(ktime_t enter_ts)
{
	struct active_stats *ast;

	ast = per_cpu(ast_local, raw_smp_processor_id());
	if (!ast)
		return;

	_active_stats_cpu_idle_enter(ast, enter_ts);
}
EXPORT_SYMBOL_GPL(active_stats_cpu_idle_enter);

/**
 * active_stats_cpu_idle_exit - update Active Stats idle tracking data
 * @time_end:	the time stamp with idle exit value
 *
 * Update the maintained statistics for exiting idle for a given CPU.
 * No return value.
 */
void active_stats_cpu_idle_exit(ktime_t time_end)
{
	struct active_stats *ast;

	ast = per_cpu(ast_local, raw_smp_processor_id());
	if (!ast)
		return;

	_active_stats_cpu_idle_exit(ast, time_end);
}
EXPORT_SYMBOL_GPL(active_stats_cpu_idle_exit);

static void _active_stats_cpu_freq_change(struct active_stats *shared_ast,
					  unsigned int freq, u64 ts)
{
	int prev_idx, next_idx;
	s64 time_diff;

	next_idx = get_freq_index(shared_ast, freq);
	/*
	 * It's very unlikely that the freq wasn't found,
	 * but play safe with the array index.
	 */
	if (next_idx < 0)
		return;

	write_seqcount_begin(&shared_ast->seqcount);

	/* The value in prev_idx is always a valid array index */
	prev_idx = shared_ast->local->last_freq_idx;

	time_diff = ts - shared_ast->local->last_event_ts;

	/* Avoid jitter from different CPUs local clock */
	if (time_diff > 0)
		shared_ast->local->residency[prev_idx] += time_diff;

	shared_ast->local->last_freq_idx = next_idx;
	shared_ast->local->last_event_ts = ts;

	write_seqcount_end(&shared_ast->seqcount);
}

/**
 * active_stats_cpu_freq_fast_change - update Active Stats tracking data
 * @cpu:	the CPU id belonging to frequency domain which is updated
 * @freq:	new frequency value
 *
 * Update the maintained statistics for frequency change for a given CPU's
 * frequency domain. This function must be used only in the fast switch code
 * path. No return value.
 */
void active_stats_cpu_freq_fast_change(int cpu, unsigned int freq)
{
	struct active_stats *ast;
	u64 ts;

	ast = per_cpu(ast_local, cpu);
	if (!ast)
		return;

	ts = local_clock();
	_active_stats_cpu_freq_change(ast->shared_ast, freq, ts);
}
EXPORT_SYMBOL_GPL(active_stats_cpu_freq_fast_change);

/**
 * active_stats_cpu_freq_change - update Active Stats tracking data
 * @cpu:	the CPU id belonging to frequency domain which is updated
 * @freq:	new frequency value
 *
 * Update the maintained statistics for frequency change for a given CPU's
 * frequency domain. This function must not be used in the fast switch code
 * path. No return value.
 */
void active_stats_cpu_freq_change(int cpu, unsigned int freq)
{
	struct active_stats *ast, *shared_ast;
	unsigned long flags;
	u64 ts;

	ast = per_cpu(ast_local, cpu);
	if (!ast)
		return;

	shared_ast = ast->shared_ast;
	ts = local_clock();

	raw_spin_lock_irqsave(&shared_ast->lock, flags);
	_active_stats_cpu_freq_change(ast->shared_ast, freq, ts);
	raw_spin_unlock_irqrestore(&shared_ast->lock, flags);
}
EXPORT_SYMBOL_GPL(active_stats_cpu_freq_change);

static struct active_stats_state *alloc_state_stats(int count)
{
	struct active_stats_state *stats;

	stats = kzalloc(sizeof(*stats), GFP_KERNEL);
	if (!stats)
		return NULL;

	stats->residency = kcalloc(count, sizeof(u64), GFP_KERNEL);
	if (!stats->residency)
		goto free_stats;

	return stats;

free_stats:
	kfree(stats);

	return NULL;
}

static struct active_stats *
active_stats_setup(int cpu, int nr_opp, struct active_stats *shared_ast)
{
	struct active_stats *ast;
	struct device *cpu_dev;
	struct dev_pm_opp *opp;
	unsigned long rate;
	int i;

	cpu_dev = get_cpu_device(cpu);
	if (!cpu_dev) {
		pr_err("%s: too early to get CPU%d device!\n",
		       __func__, cpu);
		return NULL;
	}

	ast = kzalloc(sizeof(*ast), GFP_KERNEL);
	if (!ast)
		return NULL;

	ast->states_count = nr_opp;
	ast->states_size = ast->states_count * sizeof(u64);
	ast->in_idle = true;

	ast->local = alloc_state_stats(nr_opp);
	if (!ast->local)
		goto free_ast;

	if (!shared_ast) {
		ast->freq = kcalloc(nr_opp, sizeof(unsigned long), GFP_KERNEL);
		if (!ast->freq)
			goto free_ast_local;

		for (i = 0, rate = 0; i < nr_opp; i++, rate++) {
			opp = dev_pm_opp_find_freq_ceil(cpu_dev, &rate);
			if (IS_ERR(opp)) {
				dev_warn(cpu_dev, "reading an OPP failed\n");
				kfree(ast->freq);
				goto free_ast_local;
			}
			dev_pm_opp_put(opp);

			ast->freq[i] = rate / 1000;
		}

		/* Frequency isn't known at this point */
		ast->local->last_freq_idx = nr_opp - 1;
	} else {
		ast->freq = shared_ast->freq;

		ast->snapshot_new = alloc_state_stats(nr_opp);
		if (!ast->snapshot_new)
			goto free_ast_local;

		ast->snapshot_old = alloc_state_stats(nr_opp);
		if (!ast->snapshot_old)
			goto free_ast_snapshot;

		ast->snapshot_new->last_freq_idx = nr_opp - 1;
		ast->snapshot_old->last_freq_idx = nr_opp - 1;

		ast->local->last_freq_idx = nr_opp - 1;
	}

	raw_spin_lock_init(&ast->lock);
	seqcount_init(&ast->seqcount);

	return ast;

free_ast_snapshot:
	free_state_stats(ast->snapshot_new);
free_ast_local:
	free_state_stats(ast->local);
free_ast:
	kfree(ast);

	return NULL;
}

static void active_stats_cleanup(struct active_stats *ast)
{
	free_state_stats(ast->snapshot_old);
	free_state_stats(ast->snapshot_new);
	free_state_stats(ast->local);
	kfree(ast);
}

static void active_stats_init(struct cpufreq_policy *policy)
{
	struct active_stats *ast, *shared_ast;
	cpumask_var_t setup_cpus;
	struct device *cpu_dev;
	int nr_opp, cpu;

	cpu = policy->cpu;
	cpu_dev = get_cpu_device(cpu);
	if (!cpu_dev) {
		pr_err("%s: too early to get CPU%d device!\n",
		       __func__, cpu);
		return;
	}

	nr_opp = dev_pm_opp_get_opp_count(cpu_dev);
	if (nr_opp <= 0) {
		dev_warn(cpu_dev, "OPP table is not ready\n");
		return;
	}

	if (!alloc_cpumask_var(&setup_cpus, GFP_KERNEL)) {
		dev_warn(cpu_dev, "cpumask alloc failed\n");
		return;
	}

	shared_ast = active_stats_setup(cpu, nr_opp, NULL);
	if (!shared_ast) {
		free_cpumask_var(setup_cpus);
		dev_warn(cpu_dev, "failed to setup shared_ast properly\n");
		return;
	}

	for_each_cpu(cpu, policy->related_cpus) {
		ast = active_stats_setup(cpu, nr_opp, shared_ast);
		if (!ast) {
			dev_warn(cpu_dev, "failed to setup stats properly\n");
			goto cpus_cleanup;
		}

		ast->shared_ast = shared_ast;
		per_cpu(ast_local, cpu) = ast;

		active_stats_debug_init(cpu);

		cpumask_set_cpu(cpu, setup_cpus);
	}

	free_cpumask_var(setup_cpus);

	dev_info(cpu_dev, "Active Stats created\n");
	return;

cpus_cleanup:
	for_each_cpu(cpu, setup_cpus) {
		active_stats_debug_remove(cpu);

		ast = per_cpu(ast_local, cpu);
		per_cpu(ast_local, cpu) = NULL;

		active_stats_cleanup(ast);
	}

	free_cpumask_var(setup_cpus);
	kfree(ast->freq);

	active_stats_cleanup(shared_ast);
}

static int
active_stats_init_callback(struct notifier_block *nb,
			   unsigned long val, void *data)
{
	struct cpufreq_policy *policy = data;

	if (val != CPUFREQ_CREATE_POLICY)
		return 0;

	cpumask_andnot(cpus_to_visit, cpus_to_visit, policy->related_cpus);

	active_stats_init(policy);

	if (cpumask_empty(cpus_to_visit))
		schedule_work(&processing_done_work);

	return 0;
}

static struct notifier_block active_stats_init_notifier = {
	.notifier_call = active_stats_init_callback,
};

static void processing_done_fn(struct work_struct *work)
{
	cpufreq_unregister_notifier(&active_stats_init_notifier,
				    CPUFREQ_POLICY_NOTIFIER);
	free_cpumask_var(cpus_to_visit);
}

static int cpuhp_active_stats_cpu_offline(unsigned int cpu)
{
	struct active_stats *ast;
	unsigned long flags;

	ast = per_cpu(ast_local, cpu);
	if (!ast)
		return 0;

	_active_stats_cpu_idle_enter(ast, local_clock());

	raw_spin_lock_irqsave(&ast->lock, flags);
	ast->offline = true;
	raw_spin_unlock_irqrestore(&ast->lock, flags);

	return 0;
}

static int cpuhp_active_stats_cpu_online(unsigned int cpu)
{
	struct active_stats *ast;
	unsigned long flags;

	ast = per_cpu(ast_local, cpu);
	if (!ast)
		return 0;

	_active_stats_cpu_idle_exit(ast, local_clock());

	raw_spin_lock_irqsave(&ast->lock, flags);
	ast->offline = false;
	raw_spin_unlock_irqrestore(&ast->lock, flags);

	return 0;
}

static int __init active_stats_register_notifier(void)
{
	int ret;

	if (!alloc_cpumask_var(&cpus_to_visit, GFP_KERNEL))
		return -ENOMEM;

	cpumask_copy(cpus_to_visit, cpu_possible_mask);

	ret = cpufreq_register_notifier(&active_stats_init_notifier,
					CPUFREQ_POLICY_NOTIFIER);

	if (ret)
		free_cpumask_var(cpus_to_visit);

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
				"active_stats_cpu:online",
				cpuhp_active_stats_cpu_online,
				cpuhp_active_stats_cpu_offline);

	return ret;
}
fs_initcall(active_stats_register_notifier);
