/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BPF_CGROUP_TYPES_H
#define _BPF_CGROUP_TYPES_H

#include <uapi/linux/bpf.h>

#include <linux/workqueue.h>

struct bpf_prog;
struct bpf_link_ops;

struct bpf_link {
	atomic64_t refcnt;
	u32 id;
	enum bpf_link_type type;
	const struct bpf_link_ops *ops;
	struct bpf_prog *prog;
	struct work_struct work;
};

enum bpf_cgroup_storage_type {
	BPF_CGROUP_STORAGE_SHARED,
	BPF_CGROUP_STORAGE_PERCPU,
	__BPF_CGROUP_STORAGE_MAX
};

#define MAX_BPF_CGROUP_STORAGE_TYPE __BPF_CGROUP_STORAGE_MAX

#endif
