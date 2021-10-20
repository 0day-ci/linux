// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, 0);
	__type(key, int);
	__type(value, int);
} prealloc_map SEC(".maps");
