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

static int test_tc_internal(int fd, __u32 parent_id)
{
	DECLARE_LIBBPF_OPTS(bpf_tc_opts, opts, .handle = 1, .priority = 10,
			    .class_id = TC_H_MAKE(1UL << 16, 1));
	struct bpf_tc_attach_id id = {};
	struct bpf_tc_info info = {};
	int ret;

	ret = bpf_tc_attach(fd, LO_IFINDEX, parent_id, &opts, &id);
	if (!ASSERT_EQ(ret, 0, "bpf_tc_attach"))
		return ret;

	ret = bpf_tc_get_info(LO_IFINDEX, parent_id, &id, &info);
	if (!ASSERT_EQ(ret, 0, "bpf_tc_get_info"))
		goto end;

	if (!ASSERT_EQ(info.id.handle, id.handle, "handle mismatch") ||
	    !ASSERT_EQ(info.id.priority, id.priority, "priority mismatch") ||
	    !ASSERT_EQ(info.id.handle, 1, "handle incorrect") ||
	    !ASSERT_EQ(info.chain_index, 0, "chain_index incorrect") ||
	    !ASSERT_EQ(info.id.priority, 10, "priority incorrect") ||
	    !ASSERT_EQ(info.class_id, TC_H_MAKE(1UL << 16, 1),
		       "class_id incorrect") ||
	    !ASSERT_EQ(info.protocol, ETH_P_ALL, "protocol incorrect"))
		goto end;

	opts.replace = true;
	ret = bpf_tc_attach(fd, LO_IFINDEX, parent_id, &opts, &id);
	if (!ASSERT_EQ(ret, 0, "bpf_tc_attach in replace mode"))
		return ret;

	/* Demonstrate changing attributes */
	opts.class_id = TC_H_MAKE(1UL << 16, 2);

	ret = bpf_tc_attach(fd, LO_IFINDEX, parent_id, &opts, &id);
	if (!ASSERT_EQ(ret, 0, "bpf_tc attach in replace mode"))
		goto end;

	ret = bpf_tc_get_info(LO_IFINDEX, parent_id, &id, &info);
	if (!ASSERT_EQ(ret, 0, "bpf_tc_get_info"))
		goto end;

	if (!ASSERT_EQ(info.class_id, TC_H_MAKE(1UL << 16, 2),
		       "class_id incorrect after replace"))
		goto end;
	if (!ASSERT_EQ(info.bpf_flags & TCA_BPF_FLAG_ACT_DIRECT, 1,
		       "direct action mode not set"))
		goto end;

end:
	ret = bpf_tc_detach(LO_IFINDEX, parent_id, &id);
	ASSERT_EQ(ret, 0, "detach failed");
	return ret;
}

int test_tc_info(int fd)
{
	DECLARE_LIBBPF_OPTS(bpf_tc_opts, opts, .handle = 1, .priority = 10,
			    .class_id = TC_H_MAKE(1UL << 16, 1));
	struct bpf_tc_attach_id id = {}, old;
	struct bpf_tc_info info = {};
	int ret;

	ret = bpf_tc_attach(fd, LO_IFINDEX, BPF_TC_CLSACT_INGRESS, &opts, &id);
	if (!ASSERT_EQ(ret, 0, "bpf_tc_attach"))
		return ret;
	old = id;

	ret = bpf_tc_get_info(LO_IFINDEX, BPF_TC_CLSACT_INGRESS, &id, &info);
	if (!ASSERT_EQ(ret, 0, "bpf_tc_get_info"))
		goto end_old;

	if (!ASSERT_EQ(info.id.handle, id.handle, "handle mismatch") ||
	    !ASSERT_EQ(info.id.priority, id.priority, "priority mismatch") ||
	    !ASSERT_EQ(info.id.handle, 1, "handle incorrect") ||
	    !ASSERT_EQ(info.chain_index, 0, "chain_index incorrect") ||
	    !ASSERT_EQ(info.id.priority, 10, "priority incorrect") ||
	    !ASSERT_EQ(info.class_id, TC_H_MAKE(1UL << 16, 1),
		       "class_id incorrect") ||
	    !ASSERT_EQ(info.protocol, ETH_P_ALL, "protocol incorrect"))
		goto end_old;

	/* choose a priority */
	opts.priority = 0;
	ret = bpf_tc_attach(fd, LO_IFINDEX, BPF_TC_CLSACT_INGRESS, &opts, &id);
	if (!ASSERT_EQ(ret, 0, "bpf_tc_attach"))
		goto end_old;

	ret = bpf_tc_get_info(LO_IFINDEX, BPF_TC_CLSACT_INGRESS, &id, &info);
	if (!ASSERT_EQ(ret, 0, "bpf_tc_get_info"))
		goto end;

	if (!ASSERT_NEQ(id.priority, old.priority, "filter priority mismatch"))
		goto end;
	if (!ASSERT_EQ(info.id.priority, id.priority, "priority mismatch"))
		goto end;

end:
	ret = bpf_tc_detach(LO_IFINDEX, BPF_TC_CLSACT_INGRESS, &id);
	ASSERT_EQ(ret, 0, "detach failed");
end_old:
	ret = bpf_tc_detach(LO_IFINDEX, BPF_TC_CLSACT_INGRESS, &old);
	ASSERT_EQ(ret, 0, "detach failed");
	return ret;
}

void test_test_tc_bpf(void)
{
	const char *file = "./test_tc_bpf_kern.o";
	struct bpf_program *clsp;
	struct bpf_object *obj;
	int cls_fd, ret;

	obj = bpf_object__open(file);
	if (!ASSERT_OK_PTR(obj, "bpf_object__open"))
		return;

	clsp = bpf_object__find_program_by_title(obj, "classifier");
	if (!ASSERT_OK_PTR(clsp, "bpf_object__find_program_by_title"))
		goto end;

	ret = bpf_object__load(obj);
	if (!ASSERT_EQ(ret, 0, "bpf_object__load"))
		goto end;

	cls_fd = bpf_program__fd(clsp);

	system("tc qdisc del dev lo clsact");

	ret = test_tc_internal(cls_fd, BPF_TC_CLSACT_INGRESS);
	if (!ASSERT_EQ(ret, 0, "test_tc_internal INGRESS"))
		goto end;

	if (!ASSERT_EQ(system("tc qdisc del dev lo clsact"), 0,
		       "clsact qdisc delete failed"))
		goto end;

	ret = test_tc_info(cls_fd);
	if (!ASSERT_EQ(ret, 0, "test_tc_info"))
		goto end;

	if (!ASSERT_EQ(system("tc qdisc del dev lo clsact"), 0,
		       "clsact qdisc delete failed"))
		goto end;

	ret = test_tc_internal(cls_fd, BPF_TC_CLSACT_EGRESS);
	if (!ASSERT_EQ(ret, 0, "test_tc_internal EGRESS"))
		goto end;

	ASSERT_EQ(system("tc qdisc del dev lo clsact"), 0,
		  "clsact qdisc delete failed");

end:
	bpf_object__close(obj);
}
