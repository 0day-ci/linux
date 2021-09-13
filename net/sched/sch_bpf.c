// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Programmable Qdisc with eBPF
 *
 * Copyright (C) 2021, Bytedance, Cong Wang <cong.wang@bytedance.com>
 */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/filter.h>
#include <linux/bpf.h>
#include <linux/priority_queue.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <net/pkt_cls.h>

#define ACT_BPF_NAME_LEN	256

struct sch_bpf_prog {
	struct bpf_prog *prog;
	const char *name;
};

struct sch_bpf_class {
	struct Qdisc_class_common common;
	struct Qdisc *qdisc;
	struct pq_node node;
	struct pq_root pq;

	u32 rank;
	unsigned int drops;
	unsigned int overlimits;
	struct gnet_stats_basic_packed bstats;
};

struct sch_bpf_qdisc {
	struct tcf_proto __rcu *filter_list; /* optional external classifier */
	struct tcf_block *block;
	struct Qdisc_class_hash clhash;
	struct sch_bpf_prog enqueue_prog;
	struct sch_bpf_prog dequeue_prog;

	struct pq_root flows;
	struct qdisc_watchdog watchdog;
};

struct sch_bpf_skb_cb {
	u64 rank;
};

static struct sch_bpf_skb_cb *sch_bpf_skb_cb(struct sk_buff *skb)
{
	qdisc_cb_private_validate(skb, sizeof(struct sch_bpf_skb_cb));
	return (struct sch_bpf_skb_cb *)qdisc_skb_cb(skb)->data;
}

static int sch_bpf_dump_prog(const struct sch_bpf_prog *prog, struct sk_buff *skb,
			     int name, int id, int tag)
{
	struct nlattr *nla;

	if (prog->name &&
	    nla_put_string(skb, name, prog->name))
		return -EMSGSIZE;

	if (nla_put_u32(skb, id, prog->prog->aux->id))
		return -EMSGSIZE;

	nla = nla_reserve(skb, tag, sizeof(prog->prog->tag));
	if (!nla)
		return -EMSGSIZE;

	memcpy(nla_data(nla), prog->prog->tag, nla_len(nla));
	return 0;
}

static int sch_bpf_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct sch_bpf_qdisc *q = qdisc_priv(sch);
	struct nlattr *opts;

	opts = nla_nest_start_noflag(skb, TCA_OPTIONS);
	if (!opts)
		goto nla_put_failure;

	if (sch_bpf_dump_prog(&q->enqueue_prog, skb, TCA_SCH_BPF_ENQUEUE_PROG_NAME,
			      TCA_SCH_BPF_ENQUEUE_PROG_ID, TCA_SCH_BPF_ENQUEUE_PROG_TAG))
		goto nla_put_failure;
	if (sch_bpf_dump_prog(&q->dequeue_prog, skb, TCA_SCH_BPF_DEQUEUE_PROG_NAME,
			      TCA_SCH_BPF_DEQUEUE_PROG_ID, TCA_SCH_BPF_DEQUEUE_PROG_TAG))
		goto nla_put_failure;

	return nla_nest_end(skb, opts);

nla_put_failure:
	return -1;
}

static int sch_bpf_dump_stats(struct Qdisc *sch, struct gnet_dump *d)
{
	return 0;
}

static struct sch_bpf_class *sch_bpf_find(struct Qdisc *sch, u32 classid)
{
	struct sch_bpf_qdisc *q = qdisc_priv(sch);
	struct Qdisc_class_common *clc;

	clc = qdisc_class_find(&q->clhash, classid);
	if (!clc)
		return NULL;
	return container_of(clc, struct sch_bpf_class, common);
}

static struct sch_bpf_class *sch_bpf_classify(struct sk_buff *skb,
					      struct Qdisc *sch, int *qerr)
{
	struct sch_bpf_qdisc *q = qdisc_priv(sch);
	struct sch_bpf_class *cl = NULL;
	struct tcf_proto *tcf;
	struct tcf_result res;
	int result;

	tcf = rcu_dereference_bh(q->filter_list);
	if (!tcf)
		return NULL;
	*qerr = NET_XMIT_SUCCESS | __NET_XMIT_BYPASS;
	result = tcf_classify(skb, NULL, tcf, &res, false);
	if (result  >= 0) {
#ifdef CONFIG_NET_CLS_ACT
		switch (result) {
		case TC_ACT_QUEUED:
		case TC_ACT_STOLEN:
		case TC_ACT_TRAP:
			*qerr = NET_XMIT_SUCCESS | __NET_XMIT_STOLEN;
			fallthrough;
		case TC_ACT_SHOT:
			return NULL;
		}
#endif
		cl = (void *)res.class;
		if (!cl) {
			cl = sch_bpf_find(sch, res.classid);
			if (!cl)
				return NULL;
		}
	}

	return cl;
}

static int sch_bpf_enqueue(struct sk_buff *skb, struct Qdisc *sch,
			   struct sk_buff **to_free)
{
	struct sch_bpf_qdisc *q = qdisc_priv(sch);
	unsigned int len = qdisc_pkt_len(skb);
	struct sch_bpf_ctx ctx = {};
	struct sch_bpf_class *cl;
	int res = NET_XMIT_SUCCESS;

	cl = sch_bpf_classify(skb, sch, &res);
	if (!cl) {
		struct bpf_prog *enqueue;

		enqueue = rcu_dereference(q->enqueue_prog.prog);
		bpf_compute_data_pointers(skb);

		ctx.skb = (struct __sk_buff *)skb;
		ctx.nr_flows = q->clhash.hashelems;
		ctx.handle = sch->handle;
		res = bpf_prog_run(enqueue, &ctx);
		switch (res) {
		case SCH_BPF_DROP:
			__qdisc_drop(skb, to_free);
			return NET_XMIT_DROP;
		}
		cl = sch_bpf_find(sch, ctx.classid);
		if (!cl) {
			if (res & __NET_XMIT_BYPASS)
				qdisc_qstats_drop(sch);
			__qdisc_drop(skb, to_free);
			return res;
		}
	}

	if (cl->qdisc) {
		res = qdisc_enqueue(skb, cl->qdisc, to_free);
		if (res != NET_XMIT_SUCCESS) {
			if (net_xmit_drop_count(res)) {
				qdisc_qstats_drop(sch);
				cl->drops++;
			}
			return res;
		}
	} else {
		sch_bpf_skb_cb(skb)->rank = ctx.rank;
		pq_push(&cl->pq, &skb->pqnode);
	}

	sch->qstats.backlog += len;
	sch->q.qlen++;
	return res;
}

static struct sk_buff *sch_bpf_dequeue(struct Qdisc *sch)
{
	struct sch_bpf_qdisc *q = qdisc_priv(sch);
	struct sk_buff *skb, *ret = NULL;
	struct sch_bpf_ctx ctx = {};
	struct bpf_prog *dequeue;
	struct sch_bpf_class *cl;
	struct pq_node *flow;
	s64 now;
	int res;

requeue:
	flow = pq_pop(&q->flows);
	if (!flow)
		return NULL;

	cl = container_of(flow, struct sch_bpf_class, node);
	if (cl->qdisc) {
		skb = cl->qdisc->dequeue(cl->qdisc);
		ctx.classid = cl->common.classid;
	} else {
		struct pq_node *p = pq_pop(&cl->pq);

		if (!p)
			return NULL;
		skb = container_of(p, struct sk_buff, pqnode);
		ctx.classid = cl->rank;
	}
	ctx.skb = (struct __sk_buff *) skb;
	ctx.handle = sch->handle;
	ctx.nr_flows = q->clhash.hashelems;

	dequeue = rcu_dereference(q->dequeue_prog.prog);
	bpf_compute_data_pointers(skb);
	res = bpf_prog_run(dequeue, &ctx);
	switch (res) {
	case SCH_BPF_OK:
		ret = skb;
		break;
	case SCH_BPF_REQUEUE:
		sch_bpf_skb_cb(skb)->rank = ctx.rank;
		cl->rank = ctx.classid;
		pq_push(&cl->pq, &skb->pqnode);
		bstats_update(&cl->bstats, skb);
		pq_push(&q->flows, &cl->node);
		goto requeue;
	case SCH_BPF_THROTTLE:
		now = ktime_get_ns();
		qdisc_watchdog_schedule_ns(&q->watchdog, now + ctx.delay);
		qdisc_qstats_overlimit(sch);
		cl->overlimits++;
		return NULL;
	default:
		kfree_skb(skb);
		ret = NULL;
	}

	if (pq_top(&cl->pq))
		pq_push(&q->flows, &cl->node);
	return ret;
}

static struct sk_buff *sch_bpf_peek(struct Qdisc *sch)
{
	struct sch_bpf_qdisc *q = qdisc_priv(sch);
	struct pq_node *node = pq_top(&q->flows);
	struct sch_bpf_class *cl;
	struct sk_buff *skb;

	if (!node)
		return NULL;
	cl = container_of(node, struct sch_bpf_class, node);
	node = pq_top(&cl->pq);
	if (!node)
		return NULL;
	skb = container_of(node, struct sk_buff, pqnode);
	return skb;
}

static struct Qdisc *sch_bpf_leaf(struct Qdisc *sch, unsigned long arg)
{
	struct sch_bpf_class *cl = (struct sch_bpf_class *)arg;

	return cl->qdisc;
}

static int sch_bpf_graft(struct Qdisc *sch, unsigned long arg, struct Qdisc *new,
			 struct Qdisc **old, struct netlink_ext_ack *extack)
{
	struct sch_bpf_class *cl = (struct sch_bpf_class *)arg;

	if (new)
		*old = qdisc_replace(sch, new, &cl->qdisc);
	return 0;
}

static unsigned long sch_bpf_bind(struct Qdisc *sch, unsigned long parent,
				  u32 classid)
{
	return 0;
}

static void sch_bpf_unbind(struct Qdisc *q, unsigned long cl)
{
}

static unsigned long sch_bpf_search(struct Qdisc *sch, u32 handle)
{
	return (unsigned long)sch_bpf_find(sch, handle);
}

static struct tcf_block *sch_bpf_tcf_block(struct Qdisc *sch, unsigned long cl,
					   struct netlink_ext_ack *extack)
{
	struct sch_bpf_qdisc *q = qdisc_priv(sch);

	if (cl)
		return NULL;
	return q->block;
}

static const struct nla_policy sch_bpf_policy[TCA_SCH_BPF_MAX + 1] = {
	[TCA_SCH_BPF_ENQUEUE_PROG_FD]	= { .type = NLA_U32 },
	[TCA_SCH_BPF_ENQUEUE_PROG_NAME]	= { .type = NLA_NUL_STRING,
					    .len = ACT_BPF_NAME_LEN },
	[TCA_SCH_BPF_DEQUEUE_PROG_FD]	= { .type = NLA_U32 },
	[TCA_SCH_BPF_DEQUEUE_PROG_NAME]	= { .type = NLA_NUL_STRING,
					    .len = ACT_BPF_NAME_LEN },
};

static int bpf_init_prog(struct nlattr *fd, struct nlattr *name, struct sch_bpf_prog *prog)
{
	char *prog_name = NULL;
	struct bpf_prog *fp;
	u32 bpf_fd;

	if (!fd)
		return -EINVAL;
	bpf_fd = nla_get_u32(fd);

	fp = bpf_prog_get_type(bpf_fd, BPF_PROG_TYPE_SCHED_QDISC);
	if (IS_ERR(fp))
		return PTR_ERR(fp);

	if (name) {
		prog_name = nla_memdup(name, GFP_KERNEL);
		if (!prog_name) {
			bpf_prog_put(fp);
			return -ENOMEM;
		}
	}

	prog->name = prog_name;
	prog->prog = fp;
	return 0;
}

static void bpf_cleanup_prog(struct sch_bpf_prog *prog)
{
	if (prog->prog)
		bpf_prog_put(prog->prog);
	kfree(prog->name);
}

static int sch_bpf_change(struct Qdisc *sch, struct nlattr *opt,
			  struct netlink_ext_ack *extack)
{
	struct sch_bpf_qdisc *q = qdisc_priv(sch);
	struct nlattr *tb[TCA_SCH_BPF_MAX + 1];
	int err;

	if (!opt)
		return -EINVAL;

	err = nla_parse_nested_deprecated(tb, TCA_SCH_BPF_MAX, opt,
					  sch_bpf_policy, NULL);
	if (err < 0)
		return err;

	err = bpf_init_prog(tb[TCA_SCH_BPF_ENQUEUE_PROG_FD],
			    tb[TCA_SCH_BPF_ENQUEUE_PROG_NAME], &q->enqueue_prog);
	if (err)
		return err;
	err = bpf_init_prog(tb[TCA_SCH_BPF_DEQUEUE_PROG_FD],
			    tb[TCA_SCH_BPF_DEQUEUE_PROG_NAME], &q->dequeue_prog);
	return err;
}

static bool skb_rank(struct pq_node *l, struct pq_node *r)
{
	struct sk_buff *lskb, *rskb;

	lskb = container_of(l, struct sk_buff, pqnode);
	rskb = container_of(r, struct sk_buff, pqnode);

	return sch_bpf_skb_cb(lskb)->rank < sch_bpf_skb_cb(rskb)->rank;
}

static void skb_flush(struct pq_node *n)
{
	struct sk_buff *skb = container_of(n, struct sk_buff, pqnode);

	kfree_skb(skb);
}

static bool flow_rank(struct pq_node *l, struct pq_node *r)
{
	struct sch_bpf_class *lflow, *rflow;

	lflow = container_of(l, struct sch_bpf_class, node);
	rflow = container_of(r, struct sch_bpf_class, node);

	return lflow->rank < rflow->rank;
}

static void flow_flush(struct pq_node *n)
{
	struct sch_bpf_class *cl = container_of(n, struct sch_bpf_class, node);

	pq_flush(&cl->pq, skb_flush);
}

static int sch_bpf_init(struct Qdisc *sch, struct nlattr *opt,
			struct netlink_ext_ack *extack)
{
	struct sch_bpf_qdisc *q = qdisc_priv(sch);
	int err;

	qdisc_watchdog_init(&q->watchdog, sch);
	if (opt) {
		err = sch_bpf_change(sch, opt, extack);
		if (err)
			return err;
	}

	err = tcf_block_get(&q->block, &q->filter_list, sch, extack);
	if (err)
		return err;

	pq_root_init(&q->flows, flow_rank);
	return qdisc_class_hash_init(&q->clhash);
}

static void sch_bpf_reset(struct Qdisc *sch)
{
	struct sch_bpf_qdisc *q = qdisc_priv(sch);

	qdisc_watchdog_cancel(&q->watchdog);
	pq_flush(&q->flows, flow_flush);
}

static void sch_bpf_destroy(struct Qdisc *sch)
{
	struct sch_bpf_qdisc *q = qdisc_priv(sch);

	qdisc_watchdog_cancel(&q->watchdog);
	tcf_block_put(q->block);
	qdisc_class_hash_destroy(&q->clhash);
	sch_bpf_reset(sch);
	bpf_cleanup_prog(&q->enqueue_prog);
	bpf_cleanup_prog(&q->dequeue_prog);
}

static int sch_bpf_change_class(struct Qdisc *sch, u32 classid,
				u32 parentid, struct nlattr **tca,
				unsigned long *arg,
				struct netlink_ext_ack *extack)
{
	struct sch_bpf_class *cl = (struct sch_bpf_class *)*arg;
	struct sch_bpf_qdisc *q = qdisc_priv(sch);

	if (!cl) {
		cl = kzalloc(sizeof(*cl), GFP_KERNEL);
		if (!cl)
			return -ENOBUFS;
		cl->rank = classid;
		pq_root_init(&cl->pq, skb_rank);
		qdisc_class_hash_insert(&q->clhash, &cl->common);
	}

	qdisc_class_hash_grow(sch, &q->clhash);
	*arg = (unsigned long)cl;
	return 0;
}

static int sch_bpf_delete(struct Qdisc *sch, unsigned long arg,
			  struct netlink_ext_ack *extack)
{
	struct sch_bpf_class *cl = (struct sch_bpf_class *)arg;
	struct sch_bpf_qdisc *q = qdisc_priv(sch);

	qdisc_class_hash_remove(&q->clhash, &cl->common);
	if (cl->qdisc)
		qdisc_put(cl->qdisc);
	else
		pq_flush(&cl->pq, skb_flush);
	return 0;
}

static int sch_bpf_dump_class(struct Qdisc *sch, unsigned long arg,
			      struct sk_buff *skb, struct tcmsg *tcm)
{
	struct sch_bpf_class *cl = (struct sch_bpf_class *)arg;

	tcm->tcm_handle |= TC_H_MIN(cl->rank);
	return 0;
}

static int
sch_bpf_dump_class_stats(struct Qdisc *sch, unsigned long arg, struct gnet_dump *d)
{
	struct sch_bpf_class *cl = (struct sch_bpf_class *)arg;
	struct gnet_stats_queue qs = {
		.drops = cl->drops,
		.overlimits = cl->overlimits,
	};
	__u32 qlen = 0;

	if (cl->qdisc)
		qdisc_qstats_qlen_backlog(cl->qdisc, &qlen, &qs.backlog);
	else
		qlen = 0;

	if (gnet_stats_copy_basic(qdisc_root_sleeping_running(sch),
				  d, NULL, &cl->bstats) < 0 ||
	    gnet_stats_copy_queue(d, NULL, &qs, qlen) < 0)
		return -1;
	return 0;
}

static void sch_bpf_walk(struct Qdisc *sch, struct qdisc_walker *arg)
{
	struct sch_bpf_qdisc *q = qdisc_priv(sch);
	struct sch_bpf_class *cl;
	unsigned int i;

	if (arg->stop)
		return;

	for (i = 0; i < q->clhash.hashsize; i++) {
		hlist_for_each_entry(cl, &q->clhash.hash[i], common.hnode) {
			if (arg->count < arg->skip) {
				arg->count++;
				continue;
			}
			if (arg->fn(sch, (unsigned long)cl, arg) < 0) {
				arg->stop = 1;
				return;
			}
			arg->count++;
		}
	}
}

static const struct Qdisc_class_ops sch_bpf_class_ops = {
	.graft		=	sch_bpf_graft,
	.leaf		=	sch_bpf_leaf,
	.find		=	sch_bpf_search,
	.change		=	sch_bpf_change_class,
	.delete		=	sch_bpf_delete,
	.tcf_block	=	sch_bpf_tcf_block,
	.bind_tcf	=	sch_bpf_bind,
	.unbind_tcf	=	sch_bpf_unbind,
	.dump		=	sch_bpf_dump_class,
	.dump_stats	=	sch_bpf_dump_class_stats,
	.walk		=	sch_bpf_walk,
};

static struct Qdisc_ops sch_bpf_qdisc_ops __read_mostly = {
	.cl_ops		=	&sch_bpf_class_ops,
	.id		=	"bpf",
	.priv_size	=	sizeof(struct sch_bpf_qdisc),
	.enqueue	=	sch_bpf_enqueue,
	.dequeue	=	sch_bpf_dequeue,
	.peek		=	sch_bpf_peek,
	.init		=	sch_bpf_init,
	.reset		=	sch_bpf_reset,
	.destroy	=	sch_bpf_destroy,
	.change		=	sch_bpf_change,
	.dump		=	sch_bpf_dump,
	.dump_stats	=	sch_bpf_dump_stats,
	.owner		=	THIS_MODULE,
};

static int __init sch_bpf_mod_init(void)
{
	return register_qdisc(&sch_bpf_qdisc_ops);
}

static void __exit sch_bpf_mod_exit(void)
{
	unregister_qdisc(&sch_bpf_qdisc_ops);
}

module_init(sch_bpf_mod_init)
module_exit(sch_bpf_mod_exit)
MODULE_AUTHOR("Cong Wang");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("eBPF queue discipline");
