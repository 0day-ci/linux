// SPDX-License-Identifier: GPL-2.0
/*
 * CPU <-> hardware queue mapping helpers
 *
 * Copyright (C) 2013-2014 Jens Axboe
 */
#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/cpu.h>

#include <linux/blk-mq.h>
#include "blk.h"
#include "blk-mq.h"

static int queue_index(struct blk_mq_queue_map *qmap,
		       unsigned int nr_queues, const int q)
{
	return qmap->queue_offset + (q % nr_queues);
}

static int get_first_sibling(unsigned int cpu)
{
	unsigned int ret;

	ret = cpumask_first(topology_sibling_cpumask(cpu));
	if (ret < nr_cpu_ids)
		return ret;

	return cpu;
}

int blk_mq_map_queues(struct blk_mq_queue_map *qmap)
{
	unsigned int *map = qmap->mq_map;
	unsigned int nr_queues = qmap->nr_queues;
	unsigned int cpu, first_sibling, q = 0;

	for_each_possible_cpu(cpu)
		map[cpu] = -1;

	/*
	 * Spread queues among present CPUs first for minimizing
	 * count of dead queues which are mapped by all un-present CPUs
	 */
	for_each_present_cpu(cpu) {
		if (q >= nr_queues)
			break;
		map[cpu] = queue_index(qmap, nr_queues, q++);
	}

	for_each_possible_cpu(cpu) {
		if (map[cpu] != -1)
			continue;
		/*
		 * First do sequential mapping between CPUs and queues.
		 * In case we still have CPUs to map, and we have some number of
		 * threads per cores then map sibling threads to the same queue
		 * for performance optimizations.
		 */
		if (q < nr_queues) {
			map[cpu] = queue_index(qmap, nr_queues, q++);
		} else {
			first_sibling = get_first_sibling(cpu);
			if (first_sibling == cpu)
				map[cpu] = queue_index(qmap, nr_queues, q++);
			else
				map[cpu] = map[first_sibling];
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(blk_mq_map_queues);

/**
 * blk_mq_hw_queue_to_node - Look up the memory node for a hardware queue index
 * @qmap: CPU to hardware queue map.
 * @index: hardware queue index.
 *
 * We have no quick way of doing reverse lookups. This is only used at
 * queue init time, so runtime isn't important.
 */
int blk_mq_hw_queue_to_node(struct blk_mq_queue_map *qmap, unsigned int index)
{
	int i;

	for_each_possible_cpu(i) {
		if (index == qmap->mq_map[i])
			return cpu_to_node(i);
	}

	return NUMA_NO_NODE;
}

/**
 * blk_mq_dev_map_queues - provide generic queue mapping
 * @qmap:	CPU to hardware queue map.
 * @dev_off:	Offset to use for the device
 * @get_queue_affinity:	Callback to retrieve queue affinity
 * @dev_data:	Device data passed to get_queue_affinity()
 * @fallback:	If true, fallback to default blk-mq mapping in case of
 * any failure
 *
 * Generic function to setup each queue mapping in @qmap. It will query
 * each queue's affinity via @get_queue_affinity and built queue mapping
 * that maps a queue to the CPUs in the queue affinity.
 *
 * Driver has to set correct @dev_data, so that the driver callback
 * of @get_queue_affinity can work correctly.
 */
int blk_mq_dev_map_queues(struct blk_mq_queue_map *qmap, void *dev_data,
		int dev_off, get_queue_affinty_fn *get_queue_affinity,
		bool fallback)
{
	const struct cpumask *mask;
	unsigned int queue, cpu;

	/*
	 * fallback to default mapping if driver doesn't provide
	 * get_queue_affinity callback
	 */
	if (!get_queue_affinity) {
		fallback = true;
		goto fallback;
	}

	for (queue = 0; queue < qmap->nr_queues; queue++) {
		mask = get_queue_affinity(dev_data, dev_off, queue);
		if (!mask)
			goto fallback;

		for_each_cpu(cpu, mask)
			qmap->mq_map[cpu] = qmap->queue_offset + queue;
	}

	return 0;

fallback:
	if (!fallback) {
		WARN_ON_ONCE(qmap->nr_queues > 1);
		blk_mq_clear_mq_map(qmap);
		return 0;
	}
	return blk_mq_map_queues(qmap);
}
EXPORT_SYMBOL_GPL(blk_mq_dev_map_queues);
