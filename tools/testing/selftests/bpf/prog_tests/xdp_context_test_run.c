// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <network_helpers.h>

void test_xdp_context_test_run(void)
{
	const char *file = "./test_xdp_context_test_run.o";
	struct bpf_object *obj;
	char data[sizeof(pkt_v4) + sizeof(__u32)];
	char buf[128];
	char bad_ctx[sizeof(struct xdp_md)];
	struct xdp_md ctx_in, ctx_out;
	struct bpf_test_run_opts tattr = {
		.sz = sizeof(struct bpf_test_run_opts),
		.data_in = &data,
		.data_out = buf,
		.data_size_in = sizeof(data),
		.data_size_out = sizeof(buf),
		.ctx_out = &ctx_out,
		.ctx_size_out = sizeof(ctx_out),
		.repeat = 1,
	};
	int err, prog_fd;

	err = bpf_prog_load(file, BPF_PROG_TYPE_XDP, &obj, &prog_fd);
	if (CHECK_FAIL(err))
		return;

	*(__u32 *)data = XDP_PASS;
	*(struct ipv4_packet *)(data + sizeof(__u32)) = pkt_v4;

	memset(&ctx_in, 0, sizeof(ctx_in));
	tattr.ctx_in = &ctx_in;
	tattr.ctx_size_in = sizeof(ctx_in);

	tattr.ctx_in = &ctx_in;
	tattr.ctx_size_in = sizeof(ctx_in);
	ctx_in.data_meta = 0;
	ctx_in.data = sizeof(__u32);
	ctx_in.data_end = ctx_in.data + sizeof(pkt_v4);
	err = bpf_prog_test_run_opts(prog_fd, &tattr);
	CHECK_ATTR(err || tattr.retval != XDP_PASS ||
		   tattr.data_size_out != sizeof(pkt_v4) ||
		   tattr.ctx_size_out != tattr.ctx_size_in ||
		   ctx_out.data_meta != 0 ||
		   ctx_out.data != ctx_out.data_meta ||
		   ctx_out.data_end != sizeof(pkt_v4), "xdp_md context",
		   "err %d errno %d retval %d data size out %d context size out %d data_meta %d data %d data_end %d\n",
		   err, errno, tattr.retval, tattr.data_size_out,
		   tattr.ctx_size_out, ctx_out.data_meta, ctx_out.data,
		   ctx_out.data_end);

	/* Data past the end of the kernel's struct xdp_md must be 0 */
	bad_ctx[sizeof(bad_ctx) - 1] = 1;
	tattr.ctx_in = bad_ctx;
	tattr.ctx_size_in = sizeof(bad_ctx);
	err = bpf_prog_test_run_opts(prog_fd, &tattr);
	CHECK_ATTR(!err || errno != 22, "bad context", "err %d errno %d\n",
		   err, errno);

	/* The egress cannot be specified */
	ctx_in.egress_ifindex = 1;
	err = bpf_prog_test_run_opts(prog_fd, &tattr);
	CHECK_ATTR(!err || errno != 22,
		   "nonzero egress index", "err %d errno %d\n", err, errno);

	/* data_meta must reference the start of data */
	ctx_in.data_meta = sizeof(__u32);
	ctx_in.data = ctx_in.data_meta;
	ctx_in.data_end = ctx_in.data + sizeof(pkt_v4);
	ctx_in.egress_ifindex = 0;
	err = bpf_prog_test_run_opts(prog_fd, &tattr);
	CHECK_ATTR(!err || errno != 22, "nonzero data_meta",
		   "err %d errno %d\n", err, errno);

	/* Metadata must be 32 bytes or smaller */
	ctx_in.data_meta = 0;
	ctx_in.data = sizeof(__u32)*9;
	ctx_in.data_end = ctx_in.data + sizeof(pkt_v4);
	err = bpf_prog_test_run_opts(prog_fd, &tattr);
	CHECK_ATTR(!err || errno != 22, "metadata too long",
		   "err %d errno %d\n", err, errno);

	/* Metadata's size must be a multiple of 4 */
	ctx_in.data = 3;
	err = bpf_prog_test_run_opts(prog_fd, &tattr);
	CHECK_ATTR(!err || errno != 22, "multiple of 4",
		   "err %d errno %d\n", err, errno);

	/* Total size of data must match data_end - data_meta */
	ctx_in.data = 0;
	ctx_in.data_end = sizeof(pkt_v4) - 4;
	err = bpf_prog_test_run_opts(prog_fd, &tattr);
	CHECK_ATTR(!err || errno != 22, "data too long", "err %d errno %d\n",
		   err, errno);

	ctx_in.data_end = sizeof(pkt_v4) + 4;
	err = bpf_prog_test_run_opts(prog_fd, &tattr);
	CHECK_ATTR(!err || errno != 22, "data too short", "err %d errno %d\n",
		   err, errno);

	/* RX queue cannot be specified without specifying an ingress */
	ctx_in.data_end = sizeof(pkt_v4);
	ctx_in.ingress_ifindex = 0;
	ctx_in.rx_queue_index = 1;
	err = bpf_prog_test_run_opts(prog_fd, &tattr);
	CHECK_ATTR(!err || errno != 22, "no ingress if",
		   "err %d, rx_queue_index %d\n", err, ctx_out.rx_queue_index);

	ctx_in.ingress_ifindex = 1;
	ctx_in.rx_queue_index = 1;
	err = bpf_prog_test_run_opts(prog_fd, &tattr);
	CHECK_ATTR(!err || errno != 22, "invalid rx queue",
		   "err %d, rx_queue_index %d\n", err, ctx_out.rx_queue_index);

	bpf_object__close(obj);
}
