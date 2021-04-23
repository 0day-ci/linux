// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include <linux/pkt_cls.h>

#include "test_tc_bpf.skel.h"

#define LO_IFINDEX 1

static const __u32 tcm_parent[2] = {
	[BPF_TC_INGRESS] = TC_H_MAKE(TC_H_CLSACT, TC_H_MIN_INGRESS),
	[BPF_TC_EGRESS] = TC_H_MAKE(TC_H_CLSACT, TC_H_MIN_EGRESS),
};

static int test_tc_internal(struct bpf_tc_ctx *ctx, int fd,
			    enum bpf_tc_attach_point parent)
{
	DECLARE_LIBBPF_OPTS(bpf_tc_opts, opts, .handle = 1, .priority = 10);
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	int ret;

	ret = bpf_obj_get_info_by_fd(fd, &info, &info_len);
	if (!ASSERT_EQ(ret, 0, "bpf_obj_get_info_by_fd"))
		return ret;

	ret = bpf_tc_attach(ctx, fd, &opts);
	if (!ASSERT_EQ(ret, 0, "bpf_tc_attach"))
		return ret;

	if (!ASSERT_EQ(opts.handle, 1, "handle set") ||
	    !ASSERT_EQ(opts.priority, 10, "priority set") ||
	    !ASSERT_EQ(opts.parent, tcm_parent[parent], "parent set") ||
	    !ASSERT_NEQ(opts.prog_id, 0, "prog_id set"))
		goto end;

	opts.prog_id = 0;
	ret = bpf_tc_query(ctx, &opts);
	if (!ASSERT_EQ(ret, 0, "bpf_tc_query"))
		goto end;

	if (!ASSERT_NEQ(opts.prog_id, 0, "prog_id set") ||
	    !ASSERT_EQ(info.id, opts.prog_id, "prog_id matching"))
		goto end;

	/* Atomic replace */
	opts.replace = true;
	opts.parent = opts.prog_id = 0;
	ret = bpf_tc_attach(ctx, fd, &opts);
	if (!ASSERT_EQ(ret, 0, "bpf_tc_attach replace mode"))
		return ret;
	opts.replace = false;

end:
	opts.prog_id = 0;
	ret = bpf_tc_detach(ctx, &opts);
	ASSERT_EQ(ret, 0, "bpf_tc_detach");
	return ret;
}

int test_tc_invalid(struct bpf_tc_ctx *ctx, int fd)
{
	DECLARE_LIBBPF_OPTS(bpf_tc_opts, opts, .handle = 1, .priority = 10,
			    .parent = tcm_parent[BPF_TC_INGRESS]);
	struct bpf_tc_ctx *inv_ctx;
	int ret, saved_errno;

	inv_ctx = bpf_tc_ctx_init(0, BPF_TC_INGRESS, NULL);
	saved_errno = errno;
	if (!ASSERT_EQ(inv_ctx, NULL, "bpf_tc_ctx_init invalid ifindex = 0"))
		return -EINVAL;

	ASSERT_EQ(saved_errno, EINVAL, "errno");

	inv_ctx = bpf_tc_ctx_init(LO_IFINDEX, 0xdeadc0de, NULL);
	saved_errno = errno;
	if (!ASSERT_EQ(inv_ctx, NULL,
		       "bpf_tc_ctx_init invalid parent >= _BPF_TC_PARENT_MAX"))
		return -EINVAL;

	ASSERT_EQ(saved_errno, EINVAL, "errno");

	ret = bpf_tc_ctx_destroy(NULL);
	if (!ASSERT_EQ(ret, 0, "bpf_tc_ctx_destroy ctx = NULL"))
		return -EINVAL;

	ret = bpf_tc_detach(NULL, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_detach invalid ctx = NULL"))
		return -EINVAL;

	ret = bpf_tc_detach(ctx, NULL);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_detach invalid opts = NULL"))
		return -EINVAL;

	ret = bpf_tc_query(NULL, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_query invalid ctx = NULL"))
		return -EINVAL;

	ret = bpf_tc_query(ctx, NULL);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_query invalid opts = NULL"))
		return -EINVAL;

	opts.replace = true;
	ret = bpf_tc_detach(ctx, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_detach invalid replace set"))
		return -EINVAL;
	ret = bpf_tc_query(ctx, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_query invalid replace set"))
		return -EINVAL;
	opts.replace = false;

	opts.prog_id = 42;
	ret = bpf_tc_detach(ctx, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_detach invalid prog_id set"))
		return -EINVAL;
	ret = bpf_tc_query(ctx, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_query invalid prog_id set"))
		return -EINVAL;
	opts.prog_id = 0;

	opts.handle = 0;
	ret = bpf_tc_detach(ctx, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_detach invalid handle unset"))
		return -EINVAL;
	ret = bpf_tc_query(ctx, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_query invalid handle unset"))
		return -EINVAL;
	opts.handle = 1;

	opts.priority = 0;
	ret = bpf_tc_detach(ctx, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_detach invalid priority unset"))
		return -EINVAL;
	ret = bpf_tc_query(ctx, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_query invalid priority unset"))
		return -EINVAL;
	opts.priority = 10;

	opts.parent = 0;
	ret = bpf_tc_detach(ctx, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_detach invalid parent unset"))
		return -EINVAL;
	ret = bpf_tc_query(ctx, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_query invalid parent unset"))
		return -EINVAL;

	ret = bpf_tc_attach(NULL, fd, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_attach invalid ctx = NULL"))
		return -EINVAL;

	ret = bpf_tc_attach(ctx, -1, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_attach invalid fd < 0"))
		return -EINVAL;

	ret = bpf_tc_attach(ctx, fd, NULL);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_attach invalid opts = NULL"))
		return -EINVAL;

	opts.prog_id = 42;
	ret = bpf_tc_attach(ctx, fd, &opts);
	if (!ASSERT_EQ(ret, -EINVAL, "bpf_tc_attach invalid prog_id set"))
		return -EINVAL;
	opts.prog_id = 0;

	return 0;
}

void test_tc_bpf(void)
{
	struct bpf_tc_ctx *ctx_ing = NULL, *ctx_eg = NULL;
	struct test_tc_bpf *skel = NULL;
	int cls_fd, ret;

	skel = test_tc_bpf__open_and_load();
	if (!ASSERT_NEQ(skel, NULL, "test_tc_bpf skeleton"))
		goto end;

	cls_fd = bpf_program__fd(skel->progs.cls);

	ctx_ing = bpf_tc_ctx_init(LO_IFINDEX, BPF_TC_INGRESS, NULL);
	if (!ASSERT_NEQ(ctx_ing, NULL, "bpf_tc_ctx_init(BPF_TC_INGRESS)"))
		goto end;

	ctx_eg = bpf_tc_ctx_init(LO_IFINDEX, BPF_TC_EGRESS, NULL);
	if (!ASSERT_NEQ(ctx_eg, NULL, "bpf_tc_ctx_init(BPF_TC_EGRESS)"))
		goto end;

	ret = test_tc_internal(ctx_ing, cls_fd, BPF_TC_INGRESS);
	if (!ASSERT_EQ(ret, 0, "test_tc_internal ingress"))
		goto end;

	ret = test_tc_internal(ctx_eg, cls_fd, BPF_TC_EGRESS);
	if (!ASSERT_EQ(ret, 0, "test_tc_internal egress"))
		goto end;

	ret = test_tc_invalid(ctx_ing, cls_fd);
	if (!ASSERT_EQ(ret, 0, "test_tc_invalid"))
		goto end;

end:
	bpf_tc_ctx_destroy(ctx_eg);
	bpf_tc_ctx_destroy(ctx_ing);
	test_tc_bpf__destroy(skel);
}
