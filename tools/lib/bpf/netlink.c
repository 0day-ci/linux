// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
/* Copyright (c) 2018 Facebook */

#include <stdlib.h>
#include <memory.h>
#include <unistd.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/pkt_cls.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <errno.h>
#include <time.h>

#include "bpf.h"
#include "libbpf.h"
#include "libbpf_internal.h"
#include "nlattr.h"

#ifndef SOL_NETLINK
#define SOL_NETLINK 270
#endif

typedef int (*libbpf_dump_nlmsg_t)(void *cookie, void *msg, struct nlattr **tb);

typedef int (*__dump_nlmsg_t)(struct nlmsghdr *nlmsg, libbpf_dump_nlmsg_t,
			      void *cookie);

struct xdp_id_md {
	int ifindex;
	__u32 flags;
	struct xdp_link_info info;
};

static int libbpf_netlink_open(__u32 *nl_pid)
{
	struct sockaddr_nl sa;
	socklen_t addrlen;
	int one = 1, ret;
	int sock;

	memset(&sa, 0, sizeof(sa));
	sa.nl_family = AF_NETLINK;

	sock = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
	if (sock < 0)
		return -errno;

	if (setsockopt(sock, SOL_NETLINK, NETLINK_EXT_ACK,
		       &one, sizeof(one)) < 0) {
		pr_warn("Netlink error reporting not supported\n");
	}

	if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		ret = -errno;
		goto cleanup;
	}

	addrlen = sizeof(sa);
	if (getsockname(sock, (struct sockaddr *)&sa, &addrlen) < 0) {
		ret = -errno;
		goto cleanup;
	}

	if (addrlen != sizeof(sa)) {
		ret = -LIBBPF_ERRNO__INTERNAL;
		goto cleanup;
	}

	*nl_pid = sa.nl_pid;
	return sock;

cleanup:
	close(sock);
	return ret;
}

enum {
	BPF_NL_CONT,
	BPF_NL_NEXT,
};

static int bpf_netlink_recv(int sock, __u32 nl_pid, int seq,
			    __dump_nlmsg_t _fn, libbpf_dump_nlmsg_t fn,
			    void *cookie)
{
	bool multipart = true;
	struct nlmsgerr *err;
	struct nlmsghdr *nh;
	char buf[4096];
	int len, ret;

	while (multipart) {
start:
		multipart = false;
		len = recv(sock, buf, sizeof(buf), 0);
		if (len < 0) {
			ret = -errno;
			goto done;
		}

		if (len == 0)
			break;

		for (nh = (struct nlmsghdr *)buf; NLMSG_OK(nh, len);
		     nh = NLMSG_NEXT(nh, len)) {
			if (nh->nlmsg_pid != nl_pid) {
				ret = -LIBBPF_ERRNO__WRNGPID;
				goto done;
			}
			if (nh->nlmsg_seq != seq) {
				ret = -LIBBPF_ERRNO__INVSEQ;
				goto done;
			}
			if (nh->nlmsg_flags & NLM_F_MULTI)
				multipart = true;
			switch (nh->nlmsg_type) {
			case NLMSG_ERROR:
				err = (struct nlmsgerr *)NLMSG_DATA(nh);
				if (!err->error)
					continue;
				ret = err->error;
				libbpf_nla_dump_errormsg(nh);
				goto done;
			case NLMSG_DONE:
				return 0;
			default:
				break;
			}
			if (_fn) {
				ret = _fn(nh, fn, cookie);
				if (ret < 0)
					return ret;
				switch (ret) {
				case BPF_NL_CONT:
					break;
				case BPF_NL_NEXT:
					goto start;
				default:
					return ret;
				}
			}
		}
	}
	ret = 0;
done:
	return ret;
}

/* In TC-BPF we use seqnum to form causal order of operations on shared ctx
 * socket, so we want to skip messages older than the one we are looking for,
 * in case they are left in socket buffer for some reason (e.g. errors). */
static int bpf_netlink_recv_skip(int sock, __u32 nl_pid, int seq, __dump_nlmsg_t fn,
				 void *cookie)
{
	int ret;

restart:
	ret = bpf_netlink_recv(sock, nl_pid, seq, fn, NULL, cookie);
	if (ret < 0 && ret == -LIBBPF_ERRNO__INVSEQ)
		goto restart;
	return ret;
}

static int __bpf_set_link_xdp_fd_replace(int ifindex, int fd, int old_fd,
					 __u32 flags)
{
	int sock, seq = 0, ret;
	struct nlattr *nla;
	struct {
		struct nlmsghdr  nh;
		struct ifinfomsg ifinfo;
		char             attrbuf[64];
	} req;
	__u32 nl_pid = 0;

	sock = libbpf_netlink_open(&nl_pid);
	if (sock < 0)
		return sock;

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.nh.nlmsg_type = RTM_SETLINK;
	req.nh.nlmsg_pid = 0;
	req.nh.nlmsg_seq = ++seq;
	req.ifinfo.ifi_family = AF_UNSPEC;
	req.ifinfo.ifi_index = ifindex;

	/* started nested attribute for XDP */
	nla = nlattr_begin_nested(&req.nh, sizeof(req), IFLA_XDP);
	if (!nla) {
		ret = -EMSGSIZE;
		goto cleanup;
	}

	/* add XDP fd */
	ret = nlattr_add(&req.nh, sizeof(req), IFLA_XDP_FD, &fd, sizeof(fd));
	if (ret < 0)
		goto cleanup;

	/* if user passed in any flags, add those too */
	if (flags) {
		ret = nlattr_add(&req.nh, sizeof(req), IFLA_XDP_FLAGS, &flags, sizeof(flags));
		if (ret < 0)
			goto cleanup;
	}

	if (flags & XDP_FLAGS_REPLACE) {
		ret = nlattr_add(&req.nh, sizeof(req), IFLA_XDP_EXPECTED_FD, &flags, sizeof(flags));
		if (ret < 0)
			goto cleanup;
	}

	nlattr_end_nested(&req.nh, nla);

	if (send(sock, &req, req.nh.nlmsg_len, 0) < 0) {
		ret = -errno;
		goto cleanup;
	}
	ret = bpf_netlink_recv(sock, nl_pid, seq, NULL, NULL, NULL);

cleanup:
	close(sock);
	return ret;
}

int bpf_set_link_xdp_fd_opts(int ifindex, int fd, __u32 flags,
			     const struct bpf_xdp_set_link_opts *opts)
{
	int old_fd = -1;

	if (!OPTS_VALID(opts, bpf_xdp_set_link_opts))
		return -EINVAL;

	if (OPTS_HAS(opts, old_fd)) {
		old_fd = OPTS_GET(opts, old_fd, -1);
		flags |= XDP_FLAGS_REPLACE;
	}

	return __bpf_set_link_xdp_fd_replace(ifindex, fd,
					     old_fd,
					     flags);
}

int bpf_set_link_xdp_fd(int ifindex, int fd, __u32 flags)
{
	return __bpf_set_link_xdp_fd_replace(ifindex, fd, 0, flags);
}

static int __dump_link_nlmsg(struct nlmsghdr *nlh,
			     libbpf_dump_nlmsg_t dump_link_nlmsg, void *cookie)
{
	struct nlattr *tb[IFLA_MAX + 1], *attr;
	struct ifinfomsg *ifi = NLMSG_DATA(nlh);
	int len;

	len = nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*ifi));
	attr = (struct nlattr *) ((void *) ifi + NLMSG_ALIGN(sizeof(*ifi)));
	if (libbpf_nla_parse(tb, IFLA_MAX, attr, len, NULL) != 0)
		return -LIBBPF_ERRNO__NLPARSE;

	return dump_link_nlmsg(cookie, ifi, tb);
}

static int get_xdp_info(void *cookie, void *msg, struct nlattr **tb)
{
	struct nlattr *xdp_tb[IFLA_XDP_MAX + 1];
	struct xdp_id_md *xdp_id = cookie;
	struct ifinfomsg *ifinfo = msg;
	int ret;

	if (xdp_id->ifindex && xdp_id->ifindex != ifinfo->ifi_index)
		return 0;

	if (!tb[IFLA_XDP])
		return 0;

	ret = libbpf_nla_parse_nested(xdp_tb, IFLA_XDP_MAX, tb[IFLA_XDP], NULL);
	if (ret)
		return ret;

	if (!xdp_tb[IFLA_XDP_ATTACHED])
		return 0;

	xdp_id->info.attach_mode = libbpf_nla_getattr_u8(
		xdp_tb[IFLA_XDP_ATTACHED]);

	if (xdp_id->info.attach_mode == XDP_ATTACHED_NONE)
		return 0;

	if (xdp_tb[IFLA_XDP_PROG_ID])
		xdp_id->info.prog_id = libbpf_nla_getattr_u32(
			xdp_tb[IFLA_XDP_PROG_ID]);

	if (xdp_tb[IFLA_XDP_SKB_PROG_ID])
		xdp_id->info.skb_prog_id = libbpf_nla_getattr_u32(
			xdp_tb[IFLA_XDP_SKB_PROG_ID]);

	if (xdp_tb[IFLA_XDP_DRV_PROG_ID])
		xdp_id->info.drv_prog_id = libbpf_nla_getattr_u32(
			xdp_tb[IFLA_XDP_DRV_PROG_ID]);

	if (xdp_tb[IFLA_XDP_HW_PROG_ID])
		xdp_id->info.hw_prog_id = libbpf_nla_getattr_u32(
			xdp_tb[IFLA_XDP_HW_PROG_ID]);

	return 0;
}

static int libbpf_nl_get_link(int sock, unsigned int nl_pid,
			      libbpf_dump_nlmsg_t dump_link_nlmsg, void *cookie);

int bpf_get_link_xdp_info(int ifindex, struct xdp_link_info *info,
			  size_t info_size, __u32 flags)
{
	struct xdp_id_md xdp_id = {};
	int sock, ret;
	__u32 nl_pid = 0;
	__u32 mask;

	if (flags & ~XDP_FLAGS_MASK || !info_size)
		return -EINVAL;

	/* Check whether the single {HW,DRV,SKB} mode is set */
	flags &= (XDP_FLAGS_SKB_MODE | XDP_FLAGS_DRV_MODE | XDP_FLAGS_HW_MODE);
	mask = flags - 1;
	if (flags && flags & mask)
		return -EINVAL;

	sock = libbpf_netlink_open(&nl_pid);
	if (sock < 0)
		return sock;

	xdp_id.ifindex = ifindex;
	xdp_id.flags = flags;

	ret = libbpf_nl_get_link(sock, nl_pid, get_xdp_info, &xdp_id);
	if (!ret) {
		size_t sz = min(info_size, sizeof(xdp_id.info));

		memcpy(info, &xdp_id.info, sz);
		memset((void *) info + sz, 0, info_size - sz);
	}

	close(sock);
	return ret;
}

static __u32 get_xdp_id(struct xdp_link_info *info, __u32 flags)
{
	flags &= XDP_FLAGS_MODES;

	if (info->attach_mode != XDP_ATTACHED_MULTI && !flags)
		return info->prog_id;
	if (flags & XDP_FLAGS_DRV_MODE)
		return info->drv_prog_id;
	if (flags & XDP_FLAGS_HW_MODE)
		return info->hw_prog_id;
	if (flags & XDP_FLAGS_SKB_MODE)
		return info->skb_prog_id;

	return 0;
}

int bpf_get_link_xdp_id(int ifindex, __u32 *prog_id, __u32 flags)
{
	struct xdp_link_info info;
	int ret;

	ret = bpf_get_link_xdp_info(ifindex, &info, sizeof(info), flags);
	if (!ret)
		*prog_id = get_xdp_id(&info, flags);

	return ret;
}

int libbpf_nl_get_link(int sock, unsigned int nl_pid,
		       libbpf_dump_nlmsg_t dump_link_nlmsg, void *cookie)
{
	struct {
		struct nlmsghdr nlh;
		struct ifinfomsg ifm;
	} req = {
		.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg)),
		.nlh.nlmsg_type = RTM_GETLINK,
		.nlh.nlmsg_flags = NLM_F_DUMP | NLM_F_REQUEST,
		.ifm.ifi_family = AF_PACKET,
	};
	int seq = time(NULL);

	req.nlh.nlmsg_seq = seq;
	if (send(sock, &req, req.nlh.nlmsg_len, 0) < 0)
		return -errno;

	return bpf_netlink_recv(sock, nl_pid, seq, __dump_link_nlmsg,
				dump_link_nlmsg, cookie);
}

/* TC-CTX */

struct bpf_tc_ctx {
	__u32 ifindex;
	enum bpf_tc_attach_point parent;
	int sock;
	__u32 nl_pid;
	__u32 seq;
	bool created_qdisc;
};

typedef int (*qdisc_config_t)(struct nlmsghdr *nh, struct tcmsg *t,
			      size_t maxsz);

static int clsact_config(struct nlmsghdr *nh, struct tcmsg *t, size_t maxsz)
{
	int ret;

	t->tcm_parent = TC_H_CLSACT;
	t->tcm_handle = TC_H_MAKE(TC_H_CLSACT, 0);

	ret = nlattr_add(nh, maxsz, TCA_KIND, "clsact", sizeof("clsact"));
	if (ret < 0)
		return ret;

	return 0;
}

static const qdisc_config_t parent_to_qdisc[_BPF_TC_PARENT_MAX] = {
	[BPF_TC_INGRESS] = &clsact_config,
	[BPF_TC_EGRESS] = &clsact_config,
	[BPF_TC_CUSTOM_PARENT] = NULL,
};

static int tc_qdisc_modify(struct bpf_tc_ctx *ctx, int cmd, int flags,
			   qdisc_config_t config)
{
	int ret = 0;
	struct {
		struct nlmsghdr nh;
		struct tcmsg t;
		char buf[256];
	} req;

	if (!ctx || !config)
		return -EINVAL;

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct tcmsg));
	req.nh.nlmsg_flags =
		NLM_F_REQUEST | NLM_F_ACK | flags;
	req.nh.nlmsg_type = cmd;
	req.nh.nlmsg_pid = 0;
	req.nh.nlmsg_seq = ++ctx->seq;
	req.t.tcm_family = AF_UNSPEC;
	req.t.tcm_ifindex = ctx->ifindex;

	ret = config(&req.nh, &req.t, sizeof(req));
	if (ret < 0)
		return ret;

	ret = send(ctx->sock, &req.nh, req.nh.nlmsg_len, 0);
	if (ret < 0)
		return ret;

	return bpf_netlink_recv_skip(ctx->sock, ctx->nl_pid, ctx->seq, NULL, NULL);
}

static int tc_qdisc_create_excl(struct bpf_tc_ctx *ctx, qdisc_config_t config)
{
	return tc_qdisc_modify(ctx, RTM_NEWQDISC, NLM_F_CREATE | NLM_F_EXCL, config);
}

static int tc_qdisc_delete(struct bpf_tc_ctx *ctx, qdisc_config_t config)
{
	return tc_qdisc_modify(ctx, RTM_DELQDISC, 0, config);
}

struct bpf_tc_ctx *bpf_tc_ctx_init(__u32 ifindex, enum bpf_tc_attach_point parent,
				   struct bpf_tc_ctx_opts *opts)
{
	struct bpf_tc_ctx *ctx = NULL;
	qdisc_config_t config;
	int ret, sock;
	__u32 nl_pid;

	if (!ifindex || parent >= _BPF_TC_PARENT_MAX ||
	    !OPTS_VALID(opts, bpf_tc_ctx_opts)) {
		errno = EINVAL;
		return NULL;
	}

	sock = libbpf_netlink_open(&nl_pid);
	if (sock < 0) {
		errno = -sock;
		return NULL;
	}

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx) {
		errno = ENOMEM;
		goto end_sock;
	}

	ctx->ifindex = ifindex;
	ctx->parent = parent;
	ctx->seq = time(NULL);
	ctx->nl_pid = nl_pid;
	ctx->sock = sock;

	config = parent_to_qdisc[parent];
	if (config) {
		ret = tc_qdisc_create_excl(ctx, config);
		if (ret < 0 && ret != -EEXIST) {
			errno = -ret;
			goto end_ctx;
		}
		ctx->created_qdisc = ret == 0;
	}

	return ctx;

end_ctx:
	free(ctx);
end_sock:
	close(sock);
	return NULL;
}

struct pass_info {
	struct bpf_tc_opts *opts;
	bool processed;
};

static int __tc_query(struct bpf_tc_ctx *ctx,
	              struct bpf_tc_opts *opts);

int bpf_tc_ctx_destroy(struct bpf_tc_ctx *ctx)
{
	qdisc_config_t config;
	int ret = 0;

	if (!ctx)
		return 0;

	config = parent_to_qdisc[ctx->parent];
	if (ctx->created_qdisc && config) {
		/* ctx->parent cannot be BPF_TC_CUSTOM_PARENT, as this doesn't
		 * map to a qdisc that can be created, so opts being NULL won't
		 * be an error (e.g. in tc_ctx_get_tcm_parent).
		 */
		if (__tc_query(ctx, NULL) == -ENOENT)
			ret = tc_qdisc_delete(ctx, config);
	}

	close(ctx->sock);
	free(ctx);
	return ret;
}

static long long int tc_ctx_get_tcm_parent(enum bpf_tc_attach_point type,
					   __u32 parent)
{
	long long int ret;

	switch (type) {
	case BPF_TC_INGRESS:
		ret = TC_H_MAKE(TC_H_CLSACT, TC_H_MIN_INGRESS);
		if (parent && parent != ret)
			return -EINVAL;
		break;
	case BPF_TC_EGRESS:
		ret = TC_H_MAKE(TC_H_CLSACT, TC_H_MIN_EGRESS);
		if (parent && parent != ret)
			return -EINVAL;
		break;
	case BPF_TC_CUSTOM_PARENT:
		if (!parent)
			return -EINVAL;
		ret = parent;
		break;
	default:
		return -ERANGE;
	}

	return ret;
}

/* TC-BPF */

static int tc_bpf_add_fd_and_name(struct nlmsghdr *nh, size_t maxsz, int fd)
{
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	char name[256] = {};
	int len, ret;

	ret = bpf_obj_get_info_by_fd(fd, &info, &info_len);
	if (ret < 0)
		return ret;

	ret = nlattr_add(nh, maxsz, TCA_BPF_FD, &fd, sizeof(fd));
	if (ret < 0)
		return ret;

	len = snprintf(name, sizeof(name), "%s:[%" PRIu32 "]", info.name,
		       info.id);
	if (len < 0 || len >= sizeof(name))
		return len < 0 ? -EINVAL : -ENAMETOOLONG;

	return nlattr_add(nh, maxsz, TCA_BPF_NAME, name, len + 1);
}

static int tc_cls_bpf_modify(struct bpf_tc_ctx *ctx, int fd, int cmd, int flags,
			     struct bpf_tc_opts *opts, __dump_nlmsg_t fn)
{
	unsigned int bpf_flags = 0;
	struct pass_info info = {};
	__u32 protocol, priority;
	long long int tcm_parent;
	struct nlattr *nla;
	int ret;
	struct {
		struct nlmsghdr nh;
		struct tcmsg t;
		char buf[256];
	} req;

	if (cmd == RTM_NEWTFILTER)
		flags |= OPTS_GET(opts, replace, false) ? NLM_F_REPLACE :
								NLM_F_EXCL;
	priority = OPTS_GET(opts, priority, 0);
	protocol = ETH_P_ALL;

	memset(&req, 0, sizeof(req));
	req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct tcmsg));
	req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | flags;
	req.nh.nlmsg_type = cmd;
	req.nh.nlmsg_pid = 0;
	req.nh.nlmsg_seq = ++ctx->seq;
	req.t.tcm_family = AF_UNSPEC;
	req.t.tcm_handle = OPTS_GET(opts, handle, 0);
	req.t.tcm_ifindex = ctx->ifindex;
	req.t.tcm_info = TC_H_MAKE(priority << 16, htons(protocol));

	tcm_parent = tc_ctx_get_tcm_parent(ctx->parent, OPTS_GET(opts, parent, 0));
	if (tcm_parent < 0)
		return tcm_parent;
	req.t.tcm_parent = tcm_parent;

	ret = nlattr_add(&req.nh, sizeof(req), TCA_KIND, "bpf", sizeof("bpf"));
	if (ret < 0)
		return ret;

	nla = nlattr_begin_nested(&req.nh, sizeof(req), TCA_OPTIONS);
	if (!nla)
		return -EMSGSIZE;

	if (cmd != RTM_DELTFILTER) {
		ret = tc_bpf_add_fd_and_name(&req.nh, sizeof(req), fd);
		if (ret < 0)
			return ret;

		/* direct action mode is always enabled */
		bpf_flags |= TCA_BPF_FLAG_ACT_DIRECT;
		ret = nlattr_add(&req.nh, sizeof(req), TCA_BPF_FLAGS,
				 &bpf_flags, sizeof(bpf_flags));
		if (ret < 0)
			return ret;
	}

	nlattr_end_nested(&req.nh, nla);

	ret = send(ctx->sock, &req.nh, req.nh.nlmsg_len, 0);
	if (ret < 0)
		return ret;

	info.opts = opts;
	ret = bpf_netlink_recv_skip(ctx->sock, ctx->nl_pid, ctx->seq, fn,
				    &info);
	if (ret < 0)
		return ret;

	/* Failed to process unicast response */
	if (fn && !info.processed)
		ret = -ENOENT;

	return ret;
}

static int cls_get_info(struct nlmsghdr *nh, libbpf_dump_nlmsg_t fn,
			void *cookie);

int bpf_tc_attach(struct bpf_tc_ctx *ctx, int fd,
		  struct bpf_tc_opts *opts)
{
	if (!ctx || fd < 0 || !opts)
		return -EINVAL;

	if (!OPTS_VALID(opts, bpf_tc_opts) || OPTS_GET(opts, prog_id, 0))
		return -EINVAL;

	if (OPTS_GET(opts, parent, 0) && ctx->parent < BPF_TC_CUSTOM_PARENT)
		return -EINVAL;

	return tc_cls_bpf_modify(ctx, fd, RTM_NEWTFILTER,
				 NLM_F_ECHO | NLM_F_CREATE,
				 opts, cls_get_info);
}

int bpf_tc_detach(struct bpf_tc_ctx *ctx,
		  const struct bpf_tc_opts *opts)
{
	if (!ctx || !opts)
		return -EINVAL;

	if (!OPTS_VALID(opts, bpf_tc_opts) || !OPTS_GET(opts, handle, 0) ||
	    !OPTS_GET(opts, priority, 0) || !OPTS_GET(opts, parent, 0) ||
	    OPTS_GET(opts, replace, false) || OPTS_GET(opts, prog_id, 0))
		return -EINVAL;

	/* Won't write to opts when fn is NULL */
	return tc_cls_bpf_modify(ctx, 0, RTM_DELTFILTER, 0,
				 (struct bpf_tc_opts *)opts, NULL);
}

static int __cls_get_info(void *cookie, void *msg, struct nlattr **tb,
			  bool unicast)
{
	struct nlattr *tbb[TCA_BPF_MAX + 1];
	struct pass_info *info = cookie;
	struct tcmsg *t = msg;

	if (!info)
		return -EINVAL;
	if (unicast && info->processed)
		return -EINVAL;
	/* We use BPF_NL_CONT even after finding the filter to consume all
	 * remaining multipart messages.
	 */
	if (info->processed || !tb[TCA_OPTIONS])
		return BPF_NL_CONT;

	libbpf_nla_parse_nested(tbb, TCA_BPF_MAX, tb[TCA_OPTIONS], NULL);
	if (!tbb[TCA_BPF_ID])
		return BPF_NL_CONT;

	OPTS_SET(info->opts, handle, t->tcm_handle);
	OPTS_SET(info->opts, parent, t->tcm_parent);
	OPTS_SET(info->opts, priority, TC_H_MAJ(t->tcm_info) >> 16);
	OPTS_SET(info->opts, prog_id, libbpf_nla_getattr_u32(tbb[TCA_BPF_ID]));

	info->processed = true;
	return unicast ? BPF_NL_NEXT : BPF_NL_CONT;
}

static int cls_get_info(struct nlmsghdr *nh, libbpf_dump_nlmsg_t fn,
			void *cookie)
{
	struct tcmsg *t = NLMSG_DATA(nh);
	struct nlattr *tb[TCA_MAX + 1];

	libbpf_nla_parse(tb, TCA_MAX,
			 (struct nlattr *)((char *)t + NLMSG_ALIGN(sizeof(*t))),
			 NLMSG_PAYLOAD(nh, sizeof(*t)), NULL);
	if (!tb[TCA_KIND])
		return BPF_NL_CONT;

	return __cls_get_info(cookie, t, tb, nh->nlmsg_flags & NLM_F_ECHO);
}

/* This is the less strict internal helper used to get dump for more than one
 * filter, used to determine if there are any filters attach for a bpf_tc_ctx.
 *
 */
static int __tc_query(struct bpf_tc_ctx *ctx,
	              struct bpf_tc_opts *opts)
{
	struct pass_info pinfo = {};
	__u32 priority, protocol;
	long long int tcm_parent;
	int ret;
	struct {
		struct nlmsghdr nh;
		struct tcmsg t;
		char buf[256];
	} req = {
		.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct tcmsg)),
		.nh.nlmsg_type = RTM_GETTFILTER,
		.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP,
		.t.tcm_family = AF_UNSPEC,
	};

	if (!ctx)
		return -EINVAL;

	priority = OPTS_GET(opts, priority, 0);
	protocol = ETH_P_ALL;

	req.nh.nlmsg_seq = ++ctx->seq;
	req.t.tcm_ifindex = ctx->ifindex;
	req.t.tcm_handle = OPTS_GET(opts, handle, 0);
	req.t.tcm_info = TC_H_MAKE(priority << 16, htons(protocol));

	tcm_parent = tc_ctx_get_tcm_parent(ctx->parent, OPTS_GET(opts, parent, 0));
	if (tcm_parent < 0)
		return tcm_parent;
	req.t.tcm_parent = tcm_parent;

	ret = nlattr_add(&req.nh, sizeof(req), TCA_KIND, "bpf", sizeof("bpf"));
	if (ret < 0)
		return ret;

	ret = send(ctx->sock, &req.nh, req.nh.nlmsg_len, 0);
	if (ret < 0)
		return ret;

	pinfo.opts = opts;
	ret = bpf_netlink_recv_skip(ctx->sock, ctx->nl_pid, ctx->seq,
				    cls_get_info, &pinfo);
	if (ret < 0)
		return ret;

	if (!pinfo.processed)
		ret = -ENOENT;

	return ret;
}

int bpf_tc_query(struct bpf_tc_ctx *ctx,
		 struct bpf_tc_opts *opts)
{
	if (!ctx || !opts)
		return -EINVAL;

	if (!OPTS_VALID(opts, bpf_tc_opts) || !OPTS_GET(opts, handle, 0) ||
	    !OPTS_GET(opts, priority, 0) || !OPTS_GET(opts, parent, 0) ||
	    OPTS_GET(opts, replace, false) || OPTS_GET(opts, prog_id, 0))
		return -EINVAL;

	return __tc_query(ctx, opts);
}
