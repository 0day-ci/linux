// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2019 Facebook */
/* Copyright (c) 2021 Google */

#include <assert.h>
#include <limits.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <linux/err.h>
#include <linux/zalloc.h>
#include <linux/perf_event.h>
#include <bpf/bpf.h>
#include <bpf/btf.h>
#include <bpf/libbpf.h>
#include <api/fs/fs.h>
#include <perf/bpf_perf.h>

#include "affinity.h"
#include "bpf_counter.h"
#include "cgroup.h"
#include "counts.h"
#include "debug.h"
#include "evsel.h"
#include "evlist.h"
#include "target.h"
#include "cpumap.h"
#include "thread_map.h"

#include "bpf_skel/bperf_cgroup.skel.h"

static struct perf_event_attr cgrp_switch_attr = {
	.type = PERF_TYPE_SOFTWARE,
	.config = PERF_COUNT_SW_CGROUP_SWITCHES,
	.size = sizeof(cgrp_switch_attr),
	.sample_period = 1,
	.disabled = 1,
};

static struct evsel *cgrp_switch;
static struct xyarray *cgrp_prog_fds;
static struct bperf_cgroup_bpf *skel;

#define FD(evt, cpu) (*(int *)xyarray__entry(evt->core.fd, cpu, 0))
#define PROG(cpu)    (*(int *)xyarray__entry(cgrp_prog_fds, cpu, 0))

static void set_max_rlimit(void)
{
	struct rlimit rinf = { RLIM_INFINITY, RLIM_INFINITY };

	setrlimit(RLIMIT_MEMLOCK, &rinf);
}

static __u32 bpf_link_get_prog_id(int fd)
{
	struct bpf_link_info link_info = {0};
	__u32 link_info_len = sizeof(link_info);

	bpf_obj_get_info_by_fd(fd, &link_info, &link_info_len);
	return link_info.prog_id;
}

static int bperf_load_program(struct evlist *evlist)
{
	struct bpf_link *link;
	struct evsel *evsel;
	struct cgroup *cgrp, *leader_cgrp;
	__u32 i, cpu, prog_id;
	int nr_cpus = evlist->core.all_cpus->nr;
	int map_size, map_fd, err;

	skel = bperf_cgroup_bpf__open();
	if (!skel) {
		pr_err("Failed to open cgroup skeleton\n");
		return -1;
	}

	skel->rodata->num_cpus = nr_cpus;
	skel->rodata->num_events = evlist->core.nr_entries / nr_cgroups;

	BUG_ON(evlist->core.nr_entries % nr_cgroups != 0);

	/* we need one copy of events per cpu for reading */
	map_size = nr_cpus * evlist->core.nr_entries / nr_cgroups;
	bpf_map__resize(skel->maps.events, map_size);
	bpf_map__resize(skel->maps.cpu_idx, nr_cpus);
	bpf_map__resize(skel->maps.cgrp_idx, nr_cgroups);
	/* previous result is saved in a per-cpu array */
	map_size = evlist->core.nr_entries / nr_cgroups;
	bpf_map__resize(skel->maps.prev_readings, map_size);
	/* cgroup result needs all events */
	map_size = nr_cpus * evlist->core.nr_entries;
	bpf_map__resize(skel->maps.cgrp_readings, map_size);

	set_max_rlimit();

	err = bperf_cgroup_bpf__load(skel);
	if (err) {
		pr_err("Failed to load cgroup skeleton\n");
		goto out;
	}

	if (cgroup_is_v2("perf_event") > 0)
		skel->bss->use_cgroup_v2 = 1;

	err = -1;

	cgrp_switch = evsel__new(&cgrp_switch_attr);
	if (evsel__open_per_cpu(cgrp_switch, evlist->core.all_cpus, -1) < 0) {
		pr_err("Failed to open cgroup switches event\n");
		goto out;
	}

	map_fd = bpf_map__fd(skel->maps.cpu_idx);
	if (map_fd < 0) {
		pr_err("cannot get cpu idx map\n");
		goto out;
	}

	cgrp_prog_fds = xyarray__new(nr_cpus, 1, sizeof(int));
	if (!cgrp_prog_fds) {
		pr_err("Failed to allocate cgroup switch prog fd\n");
		goto out;
	}

	for (i = 0; i < nr_cpus; i++) {
		link = bpf_program__attach_perf_event(skel->progs.on_switch,
						      FD(cgrp_switch, i));
		if (IS_ERR(link)) {
			pr_err("Failed to attach cgroup program\n");
			err = PTR_ERR(link);
			goto out;
		}

		/* update cpu index in case there are missing cpus */
		cpu = evlist->core.all_cpus->map[i];
		bpf_map_update_elem(map_fd, &cpu, &i, BPF_ANY);

		prog_id = bpf_link_get_prog_id(bpf_link__fd(link));
		PROG(i) = bpf_prog_get_fd_by_id(prog_id);
	}

	/*
	 * Update cgrp_idx map from cgroup-id to event index.
	 */
	cgrp = NULL;
	i = 0;

	evlist__for_each_entry(evlist, evsel) {
		if (cgrp == NULL || evsel->cgrp == leader_cgrp) {
			leader_cgrp = evsel->cgrp;
			evsel->cgrp = NULL;

			/* open single copy of the events w/o cgroup */
			err = evsel__open_per_cpu(evsel, evlist->core.all_cpus, -1);
			if (err) {
				pr_err("Failed to open first cgroup events\n");
				goto out;
			}

			map_fd = bpf_map__fd(skel->maps.events);
			for (cpu = 0; cpu < nr_cpus; cpu++) {
				__u32 idx = evsel->idx * nr_cpus + cpu;
				int fd = FD(evsel, cpu);

				bpf_map_update_elem(map_fd, &idx, &fd, BPF_ANY);
			}

			evsel->cgrp = leader_cgrp;
		}
		evsel->supported = true;

		if (evsel->cgrp == cgrp)
			continue;

		cgrp = evsel->cgrp;

		if (read_cgroup_id(cgrp) < 0) {
			pr_debug("Failed to get cgroup id\n");
			err = -1;
			goto out;
		}

		map_fd = bpf_map__fd(skel->maps.cgrp_idx);
		bpf_map_update_elem(map_fd, &cgrp->id, &i, BPF_ANY);

		i++;
	}

	pr_debug("The kernel does not support test_run for perf_event BPF programs.\n"
		 "Therefore, --for-each-cgroup might show inaccurate readings\n");
	err = 0;

out:
	return err;
}

static int bperf_cgrp__load(struct evsel *evsel, struct target *target)
{
	static bool bperf_loaded = false;

	evsel->bperf_leader_prog_fd = -1;
	evsel->bperf_leader_link_fd = -1;

	if (!bperf_loaded && bperf_load_program(evsel->evlist))
		return -1;

	bperf_loaded = true;
	/* just to bypass bpf_counter_skip() */
	evsel->follower_skel = (struct bperf_follower_bpf *)skel;

	return 0;
}

static int bperf_cgrp__install_pe(struct evsel *evsel, int cpu, int fd)
{
	/* nothing to do */
	return 0;
}

/*
 * trigger the leader prog on each cpu, so the cgrp_reading map could get
 * the latest results.
 */
static int bperf_sync_counters(struct evlist *evlist)
{
	struct affinity affinity;
	int i, cpu;

	/* change affinity to rotate all cpus to trigger cgroup-switches (hopefully) */
	if (affinity__setup(&affinity) < 0)
		return -1;

	evlist__for_each_cpu(evlist, i, cpu)
		affinity__set(&affinity, cpu);

	affinity__cleanup(&affinity);

	return 0;
}

static int bperf_cgrp__enable(struct evsel *evsel)
{
	skel->bss->enabled = 1;
	return 0;
}

static int bperf_cgrp__disable(struct evsel *evsel)
{
	if (evsel->idx)
		return 0;

	bperf_sync_counters(evsel->evlist);

	skel->bss->enabled = 0;
	return 0;
}

static int bperf_cgrp__read(struct evsel *evsel)
{
	struct evlist *evlist = evsel->evlist;
	int i, nr_cpus = evlist->core.all_cpus->nr;
	struct perf_counts_values *counts;
	struct bpf_perf_event_value values;
	struct cgroup *cgrp = NULL;
	int cgrp_idx = -1;
	int reading_map_fd, err = 0;
	__u32 idx;

	if (evsel->idx)
		return 0;

	reading_map_fd = bpf_map__fd(skel->maps.cgrp_readings);

	evlist__for_each_entry(evlist, evsel) {
		if (cgrp != evsel->cgrp) {
			cgrp = evsel->cgrp;
			cgrp_idx++;
		}

		for (i = 0; i < nr_cpus; i++) {
			idx = evsel->idx * nr_cpus + i;
			err = bpf_map_lookup_elem(reading_map_fd, &idx, &values);
			if (err)
				goto out;

			counts = perf_counts(evsel->counts, i, 0);
			counts->val = values.counter;
			counts->ena = values.enabled;
			counts->run = values.running;
		}
	}

out:
	return err;
}

static int bperf_cgrp__destroy(struct evsel *evsel)
{
	if (evsel->idx)
		return 0;

	bperf_cgroup_bpf__destroy(skel);
	evsel__delete(cgrp_switch);  // it'll destroy on_switch progs too
	free(cgrp_prog_fds);

	return 0;
}

struct bpf_counter_ops bperf_cgrp_ops = {
	.load       = bperf_cgrp__load,
	.enable     = bperf_cgrp__enable,
	.disable    = bperf_cgrp__disable,
	.read       = bperf_cgrp__read,
	.install_pe = bperf_cgrp__install_pe,
	.destroy    = bperf_cgrp__destroy,
};
