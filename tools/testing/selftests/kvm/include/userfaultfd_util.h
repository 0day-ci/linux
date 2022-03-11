// SPDX-License-Identifier: GPL-2.0
/*
 * KVM userfaultfd util
 * Adapted from demand_paging_test.c
 *
 * Copyright (C) 2018, Red Hat, Inc.
 * Copyright (C) 2019, Google, Inc.
 * Copyright (C) 2022, Google, Inc.
 */

#define _GNU_SOURCE /* for pipe2 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <poll.h>
#include <pthread.h>
#include <linux/userfaultfd.h>
#include <sys/syscall.h>

#include "kvm_util.h"
#include "test_util.h"
#include "perf_test_util.h"

typedef int (*uffd_handler_t)(int uffd_mode, int uffd, struct uffd_msg *msg);

struct uffd_desc;

struct uffd_desc *uffd_setup_demand_paging(int uffd_mode,
		useconds_t uffd_delay, void *hva, uint64_t len,
		uffd_handler_t handler);

void uffd_stop_demand_paging(struct uffd_desc *uffd);

#ifdef PRINT_PER_PAGE_UPDATES
#define PER_PAGE_DEBUG(...) printf(__VA_ARGS__)
#else
#define PER_PAGE_DEBUG(...) _no_printf(__VA_ARGS__)
#endif

#ifdef PRINT_PER_VCPU_UPDATES
#define PER_VCPU_DEBUG(...) printf(__VA_ARGS__)
#else
#define PER_VCPU_DEBUG(...) _no_printf(__VA_ARGS__)
#endif
