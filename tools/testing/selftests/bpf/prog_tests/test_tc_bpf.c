// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <linux/err.h>
#include <linux/limits.h>
#include <bpf/libbpf.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <test_progs.h>
#include <linux/if_ether.h>

#define LO_IFINDEX 1

static int test_tc_cls_internal(int fd, __u32 parent_id)
{
	DECLARE_LIBBPF_OPTS(bpf_tc_cls_opts, opts, .handle = 1, .priority = 10,
			    .class_id = TC_H_MAKE(1UL << 16, 1),
			    .chain_index = 5);
	struct bpf_tc_cls_attach_id id = {};
	struct bpf_tc_cls_info info = {};
	int ret;

	ret = bpf_tc_cls_attach(fd, LO_IFINDEX, parent_id, &opts, &id);
	if (CHECK_FAIL(ret < 0))
		return ret;

	ret = bpf_tc_cls_get_info(fd, LO_IFINDEX, parent_id, NULL, &info);
	if (CHECK_FAIL(ret < 0))
		goto end;

	ret = -1;

	if (CHECK_FAIL(info.id.handle != id.handle) ||
	    CHECK_FAIL(info.id.chain_index != id.chain_index) ||
	    CHECK_FAIL(info.id.priority != id.priority) ||
	    CHECK_FAIL(info.id.handle != 1) ||
	    CHECK_FAIL(info.id.priority != 10) ||
	    CHECK_FAIL(info.class_id != TC_H_MAKE(1UL << 16, 1)) ||
	    CHECK_FAIL(info.id.chain_index != 5))
		goto end;

	ret = bpf_tc_cls_replace(fd, LO_IFINDEX, parent_id, &opts, &id);
	if (CHECK_FAIL(ret < 0))
		return ret;

	if (CHECK_FAIL(info.id.handle != 1) ||
	    CHECK_FAIL(info.id.priority != 10) ||
	    CHECK_FAIL(info.class_id != TC_H_MAKE(1UL << 16, 1)))
		goto end;

	/* Demonstrate changing attributes */
	opts.class_id = TC_H_MAKE(1UL << 16, 2);

	ret = bpf_tc_cls_change(fd, LO_IFINDEX, parent_id, &opts, &info.id);
	if (CHECK_FAIL(ret < 0))
		goto end;

	ret = bpf_tc_cls_get_info(fd, LO_IFINDEX, parent_id, NULL, &info);
	if (CHECK_FAIL(ret < 0))
		goto end;

	if (CHECK_FAIL(info.class_id != TC_H_MAKE(1UL << 16, 2)))
		goto end;
	if (CHECK_FAIL((info.bpf_flags & TCA_BPF_FLAG_ACT_DIRECT) != 1))
		goto end;

end:
	ret = bpf_tc_cls_detach(LO_IFINDEX, parent_id, &id);
	CHECK_FAIL(ret < 0);
	return ret;
}

void test_test_tc_bpf(void)
{
	const char *file = "./test_tc_bpf_kern.o";
	struct bpf_program *clsp;
	struct bpf_object *obj;
	int cls_fd, ret;

	obj = bpf_object__open(file);
	if (CHECK_FAIL(IS_ERR_OR_NULL(obj)))
		return;

	clsp = bpf_object__find_program_by_title(obj, "classifier");
	if (CHECK_FAIL(IS_ERR_OR_NULL(clsp)))
		goto end;

	ret = bpf_object__load(obj);
	if (CHECK_FAIL(ret < 0))
		goto end;

	cls_fd = bpf_program__fd(clsp);

	system("tc qdisc del dev lo clsact");

	ret = test_tc_cls_internal(cls_fd, BPF_TC_CLSACT_INGRESS);
	if (CHECK_FAIL(ret < 0))
		goto end;

	if (CHECK_FAIL(system("tc qdisc del dev lo clsact")))
		goto end;

	ret = test_tc_cls_internal(cls_fd, BPF_TC_CLSACT_EGRESS);
	if (CHECK_FAIL(ret < 0))
		goto end;

	CHECK_FAIL(system("tc qdisc del dev lo clsact"));

end:
	bpf_object__close(obj);
}
