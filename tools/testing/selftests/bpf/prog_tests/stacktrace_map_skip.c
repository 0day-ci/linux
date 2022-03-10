// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "stacktrace_map_skip.skel.h"

#define TEST_STACK_DEPTH  2

void test_stacktrace_map_skip(void)
{
	struct stacktrace_map_skip *skel;
	int control_map_fd, stackid_hmap_fd, stackmap_fd, stack_amap_fd;
	int err, stack_trace_len;
	__u32 key, val, duration = 0;

	skel = stacktrace_map_skip__open_and_load();
	if (CHECK(!skel, "skel_open_and_load", "skeleton open failed\n"))
		return;

	/* find map fds */
	control_map_fd = bpf_map__fd(skel->maps.control_map);
	if (CHECK_FAIL(control_map_fd < 0))
		goto out;

	stackid_hmap_fd = bpf_map__fd(skel->maps.stackid_hmap);
	if (CHECK_FAIL(stackid_hmap_fd < 0))
		goto out;

	stackmap_fd = bpf_map__fd(skel->maps.stackmap);
	if (CHECK_FAIL(stackmap_fd < 0))
		goto out;

	stack_amap_fd = bpf_map__fd(skel->maps.stack_amap);
	if (CHECK_FAIL(stack_amap_fd < 0))
		goto out;

	err = stacktrace_map_skip__attach(skel);
	if (CHECK(err, "skel_attach", "skeleton attach failed\n"))
		goto out;

	/* give some time for bpf program run */
	sleep(1);

	/* disable stack trace collection */
	key = 0;
	val = 1;
	bpf_map_update_elem(control_map_fd, &key, &val, 0);

	/* for every element in stackid_hmap, we can find a corresponding one
	 * in stackmap, and vise versa.
	 */
	err = compare_map_keys(stackid_hmap_fd, stackmap_fd);
	if (CHECK(err, "compare_map_keys stackid_hmap vs. stackmap",
		  "err %d errno %d\n", err, errno))
		goto out;

	err = compare_map_keys(stackmap_fd, stackid_hmap_fd);
	if (CHECK(err, "compare_map_keys stackmap vs. stackid_hmap",
		  "err %d errno %d\n", err, errno))
		goto out;

	stack_trace_len = TEST_STACK_DEPTH * sizeof(__u64);
	err = compare_stack_ips(stackmap_fd, stack_amap_fd, stack_trace_len);
	if (CHECK(err, "compare_stack_ips stackmap vs. stack_amap",
		  "err %d errno %d\n", err, errno))
		goto out;

	if (CHECK(skel->bss->failed, "check skip",
		  "failed to skip some depth: %d", skel->bss->failed))
		goto out;

out:
	stacktrace_map_skip__destroy(skel);
}
