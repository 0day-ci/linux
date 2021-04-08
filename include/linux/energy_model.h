/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ENERGY_MODEL_H
#define _LINUX_ENERGY_MODEL_H
#include <linux/cpumask.h>
#include <linux/device.h>
#include <linux/jump_label.h>
#include <linux/kobject.h>
#include <linux/rcupdate.h>
#include <linux/sched/cpufreq.h>
#include <linux/sched/topology.h>
#include <linux/types.h>

/**
 * em_perf_state - Performance state of a performance domain
 * @frequency:	The frequency in KHz, for consistency with CPUFreq
 * @power:	The power consumed at this level (by 1 CPU or by a registered
 *		device). It can be a total power: static and dynamic.
 * @cost:	The cost coefficient associated with this level, used during
 *		energy calculation. Equal to: power * max_frequency / frequency
 * @flags:	see "em_perf_state flags" description below.
 */
struct em_perf_state {
	unsigned long frequency;
	unsigned long power;
	unsigned long cost;
	unsigned long flags;
};

/*
 * em_perf_state flags:
 *
 * EM_PERF_STATE_INEFFICIENT: The performance state is inefficient. There is
 * in this em_perf_domain, another performance state with a higher frequency
 * but a lower or equal power cost. Such inefficient states are ignored when
 * using em_pd_get_efficient_*() functions.
 */
#define EM_PERF_STATE_INEFFICIENT BIT(0)

/**
 * em_efficient_table - Efficient em_perf_state lookup table.
 * @table:	Lookup table for the efficient em_perf_state
 * @min_state:  Minimum efficient state for the perf_domain
 * @max_state:  Maximum state for the perf_domain
 * @min_freq:	Minimum efficient frequency for the perf_domain
 * @max_freq:	Maximum frequency for the perf_domain
 * @shift:	Shift value used to resolve the lookup table
 *
 * Resolving a frequency to an efficient em_perf_state is as follows:
 *
 *   1. Check frequency against min_freq and max_freq
 *   2. idx = (frequency - min_freq) >> shift;
 *   3. idx = table[idx].frequency < frequency ? idx + 1 : idx;
 *   4. table[idx]
 *
 *   3. Intends to resolve undershoot, when an OPP is in the middle of the
 *   lookup table bin.
 */
struct em_efficient_table {
	struct em_perf_state **table;
	struct em_perf_state *min_state;
	struct em_perf_state *max_state;
	unsigned long min_freq;
	unsigned long max_freq;
	int shift;
};

/**
 * em_perf_domain - Performance domain
 * @table:		List of performance states, in ascending order
 * @efficient_table:	List of efficient performance states, in a lookup
 *			table. This is filled only for CPU devices.
 * @nr_perf_states:	Number of performance states
 * @flags:		See "em_perf_domain flags"
 * @cpus:		Cpumask covering the CPUs of the domain. It's here
 *			for performance reasons to avoid potential cache
 *			misses during energy calculations in the scheduler
 *			and simplifies allocating/freeing that memory region.
 *
 * In case of CPU device, a "performance domain" represents a group of CPUs
 * whose performance is scaled together. All CPUs of a performance domain
 * must have the same micro-architecture. Performance domains often have
 * a 1-to-1 mapping with CPUFreq policies. In case of other devices the @cpus
 * field is unused.
 */
struct em_perf_domain {
	struct em_perf_state *table;
	struct em_efficient_table efficient_table;
	int nr_perf_states;
	int flags;
	unsigned long cpus[];
};

/*
 *  em_perf_domain flags:
 *
 *  EM_PERF_DOMAIN_MILLIWATTS: The power values are in milli-Watts or some
 *  other scale.
 *
 *  EM_PERF_DOMAIN_INEFFICIENCIES: This perf domain contains inefficient perf
 *  states.
 */
#define EM_PERF_DOMAIN_MILLIWATTS BIT(0)
#define EM_PERF_DOMAIN_INEFFICIENCIES BIT(1)

#define em_span_cpus(em) (to_cpumask((em)->cpus))

#ifdef CONFIG_ENERGY_MODEL
#define EM_MAX_POWER 0xFFFF

struct em_data_callback {
	/**
	 * active_power() - Provide power at the next performance state of
	 *		a device
	 * @power	: Active power at the performance state
	 *		(modified)
	 * @freq	: Frequency at the performance state in kHz
	 *		(modified)
	 * @dev		: Device for which we do this operation (can be a CPU)
	 *
	 * active_power() must find the lowest performance state of 'dev' above
	 * 'freq' and update 'power' and 'freq' to the matching active power
	 * and frequency.
	 *
	 * In case of CPUs, the power is the one of a single CPU in the domain,
	 * expressed in milli-Watts or an abstract scale. It is expected to
	 * fit in the [0, EM_MAX_POWER] range.
	 *
	 * Return 0 on success.
	 */
	int (*active_power)(unsigned long *power, unsigned long *freq,
			    struct device *dev);
};
#define EM_DATA_CB(_active_power_cb) { .active_power = &_active_power_cb }

struct em_perf_domain *em_cpu_get(int cpu);
struct em_perf_domain *em_pd_get(struct device *dev);
int em_dev_register_perf_domain(struct device *dev, unsigned int nr_states,
				struct em_data_callback *cb, cpumask_t *span,
				bool milliwatts);
void em_dev_unregister_perf_domain(struct device *dev);

/**
 * em_pd_get_efficient_state() - Get an efficient performance state from the EM
 * @pd   : Performance domain for which we want an efficient frequency
 * @freq : Frequency to map with the EM
 *
 * This function must be used only for CPU devices. It is called from the
 * scheduler code quite frequently and as a consequence doesn't implement any
 * check.
 *
 * Return: An efficient performance state, high enough to meet @freq
 * requirement.
 */
static inline
struct em_perf_state *em_pd_get_efficient_state(struct em_perf_domain *pd,
						unsigned long freq)
{
	struct em_efficient_table *efficients = &pd->efficient_table;
	int idx;

	if (freq <= efficients->min_freq)
		return efficients->min_state;

	if (freq >= efficients->max_freq)
		return efficients->max_state;

	idx = (freq - efficients->min_freq) >> efficients->shift;

	/* Undershoot due to the bin size. Use the higher perf_state */
	if (efficients->table[idx]->frequency < freq)
		idx++;

	return efficients->table[idx];
}

/**
 * em_pd_get_efficient_freq() - Get the efficient frequency from the EM
 * @pd	 : Performance domain for which we want an efficient frequency
 * @freq : Frequency to map with the EM
 *
 * This function will return @freq if no inefficiencies have been found for
 * that @pd. This is to avoid a useless lookup table resolution.
 *
 * Return: An efficient frequency, high enough to meet @freq requirement.
 */
static inline unsigned long em_pd_get_efficient_freq(struct em_perf_domain *pd,
						     unsigned long freq)
{
	struct em_perf_state *ps;

	if (!pd || !(pd->flags & EM_PERF_DOMAIN_INEFFICIENCIES))
		return freq;

	ps = em_pd_get_efficient_state(pd, freq);

	return ps->frequency;
}

/**
 * em_cpu_energy() - Estimates the energy consumed by the CPUs of a
		performance domain
 * @pd		: performance domain for which energy has to be estimated
 * @max_util	: highest utilization among CPUs of the domain
 * @sum_util	: sum of the utilization of all CPUs in the domain
 *
 * This function must be used only for CPU devices. There is no validation,
 * i.e. if the EM is a CPU type and has cpumask allocated. It is called from
 * the scheduler code quite frequently and that is why there is not checks.
 *
 * Return: the sum of the energy consumed by the CPUs of the domain assuming
 * a capacity state satisfying the max utilization of the domain.
 */
static inline unsigned long em_cpu_energy(struct em_perf_domain *pd,
				unsigned long max_util, unsigned long sum_util)
{
	unsigned long freq, scale_cpu;
	struct em_perf_state *ps;
	int cpu;

	if (!sum_util)
		return 0;

	/*
	 * In order to predict the performance state, map the utilization of
	 * the most utilized CPU of the performance domain to a requested
	 * frequency, like schedutil.
	 */
	cpu = cpumask_first(to_cpumask(pd->cpus));
	scale_cpu = arch_scale_cpu_capacity(cpu);
	ps = &pd->table[pd->nr_perf_states - 1];
	freq = map_util_freq(max_util, ps->frequency, scale_cpu);

	/*
	 * Find the lowest performance state of the Energy Model above the
	 * requested frequency.
	 */
	ps = em_pd_get_efficient_state(pd, freq);

	/*
	 * The capacity of a CPU in the domain at the performance state (ps)
	 * can be computed as:
	 *
	 *             ps->freq * scale_cpu
	 *   ps->cap = --------------------                          (1)
	 *                 cpu_max_freq
	 *
	 * So, ignoring the costs of idle states (which are not available in
	 * the EM), the energy consumed by this CPU at that performance state
	 * is estimated as:
	 *
	 *             ps->power * cpu_util
	 *   cpu_nrg = --------------------                          (2)
	 *                   ps->cap
	 *
	 * since 'cpu_util / ps->cap' represents its percentage of busy time.
	 *
	 *   NOTE: Although the result of this computation actually is in
	 *         units of power, it can be manipulated as an energy value
	 *         over a scheduling period, since it is assumed to be
	 *         constant during that interval.
	 *
	 * By injecting (1) in (2), 'cpu_nrg' can be re-expressed as a product
	 * of two terms:
	 *
	 *             ps->power * cpu_max_freq   cpu_util
	 *   cpu_nrg = ------------------------ * ---------          (3)
	 *                    ps->freq            scale_cpu
	 *
	 * The first term is static, and is stored in the em_perf_state struct
	 * as 'ps->cost'.
	 *
	 * Since all CPUs of the domain have the same micro-architecture, they
	 * share the same 'ps->cost', and the same CPU capacity. Hence, the
	 * total energy of the domain (which is the simple sum of the energy of
	 * all of its CPUs) can be factorized as:
	 *
	 *            ps->cost * \Sum cpu_util
	 *   pd_nrg = ------------------------                       (4)
	 *                  scale_cpu
	 */
	return ps->cost * sum_util / scale_cpu;
}

/**
 * em_pd_nr_perf_states() - Get the number of performance states of a perf.
 *				domain
 * @pd		: performance domain for which this must be done
 *
 * Return: the number of performance states in the performance domain table
 */
static inline int em_pd_nr_perf_states(struct em_perf_domain *pd)
{
	return pd->nr_perf_states;
}

#else
struct em_data_callback {};
#define EM_DATA_CB(_active_power_cb) { }

static inline
int em_dev_register_perf_domain(struct device *dev, unsigned int nr_states,
				struct em_data_callback *cb, cpumask_t *span,
				bool milliwatts)
{
	return -EINVAL;
}
static inline void em_dev_unregister_perf_domain(struct device *dev)
{
}
static inline struct em_perf_domain *em_cpu_get(int cpu)
{
	return NULL;
}
static inline struct em_perf_domain *em_pd_get(struct device *dev)
{
	return NULL;
}
static inline unsigned long em_cpu_energy(struct em_perf_domain *pd,
			unsigned long max_util, unsigned long sum_util)
{
	return 0;
}
static inline int em_pd_nr_perf_states(struct em_perf_domain *pd)
{
	return 0;
}

static inline unsigned long
em_pd_get_efficient_freq(struct em_perf_domain *pd, unsigned long freq)
{
	return freq;
}
#endif

#endif
