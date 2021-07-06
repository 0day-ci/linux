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
static void update_local_stats(struct active_stats *ast, ktime_t event_ts);
static void get_stats_snapshot(struct active_stats *ast);

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
		residency = ast->snapshot.result->residency[i];
		if (i == ast->snapshot.result->last_freq_idx && !ast->in_idle && !ast->offline)
			residency += ts - ast->snapshot.result->last_event_ts;

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

static void active_stats_deactivate(struct active_stats *ast)
{
	mutex_lock(&ast->activation_lock);

	ast->num_clients--;
	if (!ast->num_clients)
		WRITE_ONCE(ast->activated, false);

	/* Do similar accouning for shared structure and not deeper */
	if (ast->shared_ast)
		active_stats_deactivate(ast->shared_ast);

	mutex_unlock(&ast->activation_lock);
}

static void active_stats_reinit_snapshots(struct active_stats *ast, int cpu)
{
	int size = ast->states_size;
	unsigned long freq;
	int curr_freq_idx;
	u64 curr_ts;

	freq = cpufreq_quick_get(cpu);
	curr_freq_idx = get_freq_index(ast, freq);
	if (curr_freq_idx < 0)
		curr_freq_idx = 0;

	curr_ts = local_clock();

	/* Only idle stats have the 'curr' and 'prev' */
	if (ast->shared_ast) {
		ast->snapshot.curr->last_event_ts = curr_ts;
		ast->snapshot.curr->last_freq_idx = curr_freq_idx;

		ast->snapshot.prev->last_freq_idx = curr_freq_idx;
		ast->snapshot.prev->last_event_ts = curr_ts;

		memcpy(ast->snapshot.prev->residency,
		       ast->snapshot.curr->residency, size);
	}

	ast->snapshot.result->last_event_ts = curr_ts;
	ast->snapshot.result->last_freq_idx = curr_freq_idx;
}

static void active_stats_activate(struct active_stats *ast, int cpu)
{
	mutex_lock(&ast->activation_lock);

	ast->num_clients++;
	if (!READ_ONCE(ast->activated)) {
		/* For idle tracking stats take snapshot of freq stats */
		if (ast->shared_ast) {
			get_stats_snapshot(ast);
			ast->in_idle = idle_cpu(cpu);
		}

		active_stats_reinit_snapshots(ast, cpu);
		WRITE_ONCE(ast->activated, true);
	}

	mutex_unlock(&ast->activation_lock);
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

	ast_mon->snapshot.result = alloc_state_stats(ast->states_count);
	if (!ast_mon->snapshot.result)
		goto free_ast_mon;

	ast_mon->snapshot.curr = alloc_state_stats(ast->states_count);
	if (!ast_mon->snapshot.curr)
		goto free_local;

	ast_mon->snapshot.prev = alloc_state_stats(ast->states_count);
	if (!ast_mon->snapshot.prev)
		goto free_snapshot_curr;

	ast_mon->tmp_view.result = alloc_state_stats(ast->states_count);
	if (!ast_mon->tmp_view.result)
		goto free_snapshot_prev;

	ast_mon->tmp_view.curr = alloc_state_stats(ast->states_count);
	if (!ast_mon->tmp_view.curr)
		goto free_tmp_view_result;

	ast_mon->tmp_view.prev = alloc_state_stats(ast->states_count);
	if (!ast_mon->tmp_view.prev)
		goto free_tmp_view_snapshot_curr;

	ast_mon->ast = ast;
	ast_mon->local_period = 0;
	ast_mon->states_count = ast->states_count;
	ast_mon->states_size = ast->states_count * sizeof(u64);
	ast_mon->cpu = cpu;

	active_stats_activate(ast->shared_ast, cpu);
	active_stats_activate(ast, cpu);

	mutex_init(&ast_mon->lock);

	return ast_mon;

free_tmp_view_snapshot_curr:
	free_state_stats(ast_mon->tmp_view.curr);
free_tmp_view_result:
	free_state_stats(ast_mon->tmp_view.result);
free_snapshot_prev:
	free_state_stats(ast_mon->snapshot.prev);
free_snapshot_curr:
	free_state_stats(ast_mon->snapshot.curr);
free_local:
	free_state_stats(ast_mon->snapshot.result);
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

	active_stats_deactivate(ast_mon->ast);

	free_state_stats(ast_mon->tmp_view.prev);
	free_state_stats(ast_mon->tmp_view.curr);
	free_state_stats(ast_mon->tmp_view.result);
	free_state_stats(ast_mon->snapshot.prev);
	free_state_stats(ast_mon->snapshot.curr);
	free_state_stats(ast_mon->snapshot.result);
	kfree(ast_mon);
}
EXPORT_SYMBOL_GPL(active_stats_cpu_free_monitor);

static int update_monitor_stats(struct active_stats_monitor *ast_mon)
{
	struct active_stats_state *snapshot_local, *origin_local;
	struct active_stats_state *origin_freq, *snapshot_freq;
	struct active_stats_state *origin_idle, *snapshot_idle;
	unsigned long seq_freq, seq_idle;
	struct active_stats *ast;
	int size, cpu_in_idle;

	ast = ast_mon->ast;
	size = ast->states_size;

	/*
	 * Take a consistent snapshot of the statistics updated from other CPU
	 * which might be changing the frequency for the whole domain.
	 */
	origin_freq = ast->shared_ast->snapshot.result;
	snapshot_freq = ast_mon->tmp_view.curr;
	do {
		seq_freq = read_seqcount_begin(&ast->shared_ast->seqcount);

		/*
		 * Take a consistent snapshot of the statistics updated from other CPU
		 * which might be toggling idle.
		 */
		origin_idle = ast->snapshot.prev;
		snapshot_idle = ast_mon->tmp_view.prev;
		origin_local = ast->snapshot.result;
		snapshot_local = ast_mon->tmp_view.result;
		do {
			seq_idle = read_seqcount_begin(&ast->seqcount);

			memcpy(snapshot_idle->residency, origin_idle->residency, size);
			snapshot_idle->last_event_ts = origin_idle->last_event_ts;
			snapshot_idle->last_freq_idx = origin_idle->last_freq_idx;

			memcpy(snapshot_local->residency, origin_local->residency, size);
			snapshot_local->last_event_ts = origin_local->last_event_ts;
			snapshot_local->last_freq_idx = origin_local->last_freq_idx;

			cpu_in_idle = ast->in_idle || ast->offline;
		} while (read_seqcount_retry(&ast->seqcount, seq_idle));

		/* Now take from frequency, which path is less often used */
		memcpy(snapshot_freq->residency, origin_freq->residency, size);
		snapshot_freq->last_event_ts = origin_freq->last_event_ts;
		snapshot_freq->last_freq_idx = origin_freq->last_freq_idx;

	} while (read_seqcount_retry(&ast->shared_ast->seqcount, seq_freq));

	return cpu_in_idle;
}

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
	struct active_stats_state *origin, *snapshot, *old, *local, *result;
	int last_local_freq_idx, last_new_freq_idx;
	int size, i, cpu_in_idle;
	struct active_stats *ast;
	s64 total_residency = 0;
	u64 local_last_event_ts;
	u64 last_event_ts = 0;
	s64 period = 0;
	s64 diff, acc;
	u64 curr_ts;

	if (IS_ERR_OR_NULL(ast_mon))
		return -EINVAL;

	ast = ast_mon->ast;

	size = ast_mon->states_size;
	origin = ast_mon->ast->snapshot.result;
	local = ast_mon->snapshot.result;

	mutex_lock(&ast_mon->lock);

	curr_ts = local_clock();

	/* last_event_ts = local->last_event_ts; */

	/* Use older buffer for upcoming newest data */
	swap(ast_mon->snapshot.curr, ast_mon->snapshot.prev);

	snapshot = ast_mon->snapshot.curr;
	old = ast_mon->snapshot.prev;
	result = ast_mon->tmp_view.result;


	cpu_in_idle = update_monitor_stats(ast_mon);

	if (!cpu_in_idle) {
		/* Take difference since this freq is set */
		last_event_ts = max(last_event_ts, ast_mon->tmp_view.curr->last_event_ts);
		/* Or take difference since the idle CPU last time accounted it if it was later */
		last_event_ts = max(last_event_ts, result->last_event_ts);
		diff = curr_ts - last_event_ts;

		local_last_event_ts = ast_mon->tmp_view.result->last_event_ts;
		period = curr_ts - local_last_event_ts;

		last_new_freq_idx = ast_mon->tmp_view.curr->last_freq_idx;
		last_local_freq_idx = ast_mon->tmp_view.result->last_freq_idx;

		diff = max(0LL, diff);
		if (diff > 0) {
			result->residency[last_new_freq_idx] += diff;
			total_residency += diff;
		}
		/* Calculate the difference for freq snapshot with idle snapshot */
		for (i = 0; i < ast_mon->states_count; i++) {

			acc = ast_mon->tmp_view.curr->residency[i];
			acc -= ast_mon->tmp_view.prev->residency[i];

			if (last_local_freq_idx != i) {
				result->residency[i] += acc;
				total_residency += acc;
			}
		}

		/* Don't account twice the same running period */
		result->residency[last_local_freq_idx] += period - total_residency;
	}

	memcpy(snapshot->residency, result->residency, size);

	/* Calculate the difference of the running time since last check */
	for (i = 0; i < ast_mon->states_count; i++) {
		diff = snapshot->residency[i] - old->residency[i];
		/* Avoid CPUs local clock differences issue and set 0 */
		local->residency[i] = diff > 0 ? diff : 0;
	}

	snapshot->last_event_ts = curr_ts;
	local->last_event_ts = curr_ts;
	ast_mon->local_period = snapshot->last_event_ts - old->last_event_ts;

	mutex_unlock(&ast_mon->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(active_stats_cpu_update_monitor);

static void get_stats_snapshot(struct active_stats *ast)
{
	struct active_stats_state *origin, *snapshot;
	int size = ast->states_size;
	unsigned long seq;

	origin = ast->shared_ast->snapshot.result;
	snapshot = ast->snapshot.curr;

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

	get_stats_snapshot(ast);

	prev = max(ast->snapshot.result->last_event_ts, ast->snapshot.curr->last_event_ts);
	prev_ts = ns_to_ktime(prev);
	diff = ktime_sub(event_ts, prev_ts);

	prev_ts = ns_to_ktime(ast->snapshot.result->last_event_ts);
	period = ktime_sub(event_ts, prev_ts);

	i = ast->snapshot.curr->last_freq_idx;

	diff = max(0LL, diff);
	if (diff > 0) {
		ast->snapshot.result->residency[i] += diff;
		total_residency += diff;
	}

	for (i = 0; i < ast->states_count; i++) {
		if (ast->snapshot.result->last_freq_idx == i)
			continue;

		acc = ast->snapshot.curr->residency[i];
		acc -= ast->snapshot.prev->residency[i];
		ast->snapshot.result->residency[i] += acc;
		total_residency += acc;
	}

	/* Don't account twice the same running period */
	i = ast->snapshot.result->last_freq_idx;
	ast->snapshot.result->residency[i] += period - total_residency;

	ast->snapshot.result->last_freq_idx = ast->snapshot.curr->last_freq_idx;

	ast->snapshot.prev->last_freq_idx = ast->snapshot.curr->last_freq_idx;
	ast->snapshot.prev->last_event_ts = ast->snapshot.curr->last_event_ts;

	swap(ast->snapshot.curr, ast->snapshot.prev);

	ast->snapshot.result->last_event_ts = event_ts;
}

static inline
void _active_stats_cpu_idle_enter(struct active_stats *ast, ktime_t enter_ts)
{
	write_seqcount_begin(&ast->seqcount);

	update_local_stats(ast, enter_ts);
	ast->in_idle = true;

	write_seqcount_end(&ast->seqcount);
}

static inline
void _active_stats_cpu_idle_exit(struct active_stats *ast, ktime_t time_end)
{
	int size = ast->states_size;

	write_seqcount_begin(&ast->seqcount);

	get_stats_snapshot(ast);

	ast->snapshot.result->last_freq_idx = ast->snapshot.curr->last_freq_idx;

	memcpy(ast->snapshot.prev->residency, ast->snapshot.curr->residency, size);
	ast->snapshot.prev->last_freq_idx = ast->snapshot.curr->last_freq_idx;
	ast->snapshot.prev->last_event_ts = ast->snapshot.curr->last_event_ts;

	swap(ast->snapshot.curr, ast->snapshot.prev);

	ast->snapshot.result->last_event_ts = time_end;
	ast->in_idle = false;

	write_seqcount_end(&ast->seqcount);
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
	if (!ast || !READ_ONCE(ast->activated))
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
	if (!ast || !READ_ONCE(ast->activated))
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
	if (next_idx < 0)
		return;

	write_seqcount_begin(&shared_ast->seqcount);

	/* The value in prev_idx is always a valid array index */
	prev_idx = shared_ast->snapshot.result->last_freq_idx;

	time_diff = ts - shared_ast->snapshot.result->last_event_ts;

	/* Avoid jitter from different CPUs local clock */
	if (time_diff > 0)
		shared_ast->snapshot.result->residency[prev_idx] += time_diff;

	shared_ast->snapshot.result->last_freq_idx = next_idx;
	shared_ast->snapshot.result->last_event_ts = ts;

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
	if (!ast || !READ_ONCE(ast->activated))
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
	if (!ast || !READ_ONCE(ast->activated))
		return;

	shared_ast = ast->shared_ast;
	ts = local_clock();

	spin_lock_irqsave(&shared_ast->lock, flags);
	_active_stats_cpu_freq_change(shared_ast, freq, ts);
	spin_unlock_irqrestore(&shared_ast->lock, flags);
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

	ast->snapshot.result = alloc_state_stats(nr_opp);
	if (!ast->snapshot.result)
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
		ast->snapshot.result->last_freq_idx = nr_opp - 1;
	} else {
		ast->freq = shared_ast->freq;

		ast->snapshot.curr = alloc_state_stats(nr_opp);
		if (!ast->snapshot.curr)
			goto free_ast_local;

		ast->snapshot.prev = alloc_state_stats(nr_opp);
		if (!ast->snapshot.prev)
			goto free_ast_snapshot;

		ast->snapshot.curr->last_freq_idx = nr_opp - 1;
		ast->snapshot.prev->last_freq_idx = nr_opp - 1;

		ast->snapshot.result->last_freq_idx = nr_opp - 1;
	}

	mutex_init(&ast->activation_lock);
	spin_lock_init(&ast->lock);
	seqcount_init(&ast->seqcount);

	return ast;

free_ast_snapshot:
	free_state_stats(ast->snapshot.curr);
free_ast_local:
	free_state_stats(ast->snapshot.result);
free_ast:
	kfree(ast);

	return NULL;
}

static void active_stats_cleanup(struct active_stats *ast)
{
	free_state_stats(ast->snapshot.prev);
	free_state_stats(ast->snapshot.curr);
	free_state_stats(ast->snapshot.result);
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

	ast = per_cpu(ast_local, cpu);
	if (!ast)
		return 0;

	ast->offline = true;

	if (!READ_ONCE(ast->activated))
		return 0;

	_active_stats_cpu_idle_enter(ast, local_clock());

	return 0;
}

static int cpuhp_active_stats_cpu_online(unsigned int cpu)
{
	struct active_stats *ast;

	ast = per_cpu(ast_local, cpu);
	if (!ast)
		return 0;

	ast->offline = false;

	if (!READ_ONCE(ast->activated))
		return 0;

	_active_stats_cpu_idle_exit(ast, local_clock());

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
