/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_INTERNAL_CPUMAP_H
#define __LIBPERF_INTERNAL_CPUMAP_H

#include <linux/refcount.h>

/**
 * A sized, reference counted, sorted array of integers representing CPU
 * numbers. This is commonly used to capture which CPUs a PMU is associated
 * with.
 */
struct perf_cpu_map {
	refcount_t	refcnt;
	/** Length of the map array. */
	int		nr;
	/** The CPU values. */
	int		map[];
};

#ifndef MAX_NR_CPUS
#define MAX_NR_CPUS	2048
#endif

int perf_cpu_map__idx(const struct perf_cpu_map *cpus, int cpu);

#endif /* __LIBPERF_INTERNAL_CPUMAP_H */
