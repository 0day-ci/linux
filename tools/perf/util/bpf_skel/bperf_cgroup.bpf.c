// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
// Copyright (c) 2021 Facebook
// Copyright (c) 2021 Google
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

#define MAX_EVENTS  32  // max events per cgroup: arbitrary

// NOTE: many of map and global data will be modified before loading
//       from the userspace (perf tool) using the skeleton helpers.

// single set of global perf events to measure
struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(int));
	__uint(max_entries, 1);
} events SEC(".maps");

// from logical cpu number to event index
// useful when user wants to count subset of cpus
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
	__uint(max_entries, 1);
} cpu_idx SEC(".maps");

// from cgroup id to event index
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(key_size, sizeof(__u64));
	__uint(value_size, sizeof(__u32));
	__uint(max_entries, 1);
} cgrp_idx SEC(".maps");

// per-cpu event snapshots to calculate delta
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(struct bpf_perf_event_value));
} prev_readings SEC(".maps");

// aggregated event values for each cgroup
// will be read from the user-space
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(struct bpf_perf_event_value));
} cgrp_readings SEC(".maps");

const volatile __u32 num_events = 1;
const volatile __u32 num_cpus = 1;

int enabled = 0;
int use_cgroup_v2 = 0;

static inline get_current_cgroup_v1_id(void)
{
	struct task_struct *p = (void *)bpf_get_current_task();

	return BPF_CORE_READ(p, cgroups, subsys[perf_event_cgrp_id], cgroup, kn, id);
}

// This will be attached to cgroup-switches event for each cpu
SEC("perf_events")
int BPF_PROG(on_switch)
{
	register __u32 idx = 0;  // to have it in a register to pass BPF verifier
	struct bpf_perf_event_value val, *prev_val, *cgrp_val;
	__u32 cpu = bpf_get_smp_processor_id();
	__u64 cgrp;
	__u32 evt_idx, key;
	__u32 *elem;
	long err;

	// map the current CPU to a CPU index, particularly necessary if there
	// are fewer CPUs profiled on than all CPUs.
	elem = bpf_map_lookup_elem(&cpu_idx, &cpu);
	if (!elem)
		return 0;
	cpu = *elem;

	if (use_cgroup_v2)
		cgrp = bpf_get_current_cgroup_id();
	else
		cgrp = get_current_cgroup_v1_id();

	elem = bpf_map_lookup_elem(&cgrp_idx, &cgrp);
	if (elem)
		cgrp = *elem;
	else
		cgrp = ~0ULL;

	for ( ; idx < MAX_EVENTS; idx++) {
		if (idx == num_events)
			break;

		// XXX: do not pass idx directly (for verifier)
		key = idx;
		// this is per-cpu array for diff
		prev_val = bpf_map_lookup_elem(&prev_readings, &key);
		if (!prev_val) {
			val.counter = val.enabled = val.running = 0;
			bpf_map_update_elem(&prev_readings, &key, val, BPF_ANY);

			prev_val = bpf_map_lookup_elem(&prev_readings, &key);
			if (!prev_val)
				continue;
		}

		// read from global event array
		evt_idx = idx * num_cpus + cpu;
		err = bpf_perf_event_read_value(&events, evt_idx, &val, sizeof(val));
		if (err)
			continue;

		if (enabled && cgrp != ~0ULL) {
			// aggregate the result by cgroup
			evt_idx += cgrp * num_cpus * num_events;
			cgrp_val = bpf_map_lookup_elem(&cgrp_readings, &evt_idx);
			if (cgrp_val) {
				cgrp_val->counter += val.counter - prev_val->counter;
				cgrp_val->enabled += val.enabled - prev_val->enabled;
				cgrp_val->running += val.running - prev_val->running;
			} else {
				val->counter -= prev_val->counter;
				val->enabled -= prev_val->enabled;
				val->running -= prev_val->running;

				bpf_map_update_elem(&cgrp_readings, &evt_idx, &val, BPF_ANY);

				val->counter += prev_val->counter;
				val->enabled += prev_val->enabled;
				val->running += prev_val->running;
			}
		}

		*prev_val = val;
	}
	return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
