// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright 2021 NXP */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <net/act_api.h>
#include <net/netlink.h>
#include <net/pkt_cls.h>
#include <net/tc_act/tc_frer.h>

#define FRER_SEQ_SPACE		16
#define FRER_RCVY_RESET_MSEC	100
#define FRER_RCVY_INVALID_SEQ	0x100
#define FRER_RCVY_PASSED	0
#define FRER_RCVY_DISCARDED	-1

static unsigned int frer_net_id;
static struct tc_action_ops act_frer_ops;

struct r_tag {
	__be16 reserved;
	__be16 sequence_nr;
	__be16 encap_proto;
} __packed;

struct rtag_ethhdr {
	struct ethhdr		ethhdr;
	struct r_tag		h_rtag;
} __packed;

struct rtag_vlan_ethhdr {
	struct vlan_ethhdr	vlanhdr;
	struct r_tag		h_rtag;
} __packed;

static const struct nla_policy frer_policy[TCA_FRER_MAX + 1] = {
	[TCA_FRER_PARMS]		=
		NLA_POLICY_EXACT_LEN(sizeof(struct tc_frer)),
	[TCA_FRER_TAG_TYPE]		= { .type = NLA_U8 },
	[TCA_FRER_TAG_ACTION]		= { .type = NLA_U8 },
	[TCA_FRER_RECOVER]		= { .type = NLA_U8 },
	[TCA_FRER_RECOVER_ALG]		= { .type = NLA_U8 },
	[TCA_FRER_RECOVER_HISTORY_LEN]	= { .type = NLA_U8 },
	[TCA_FRER_RECOVER_RESET_TM]	= { .type = NLA_U64 },
};

static void frer_seq_recovery_reset(struct tcf_frer *frer_act);

static enum hrtimer_restart frer_hrtimer_func(struct hrtimer *timer)
{
	struct tcf_frer *frer_act = container_of(timer, struct tcf_frer,
						 hrtimer);
	ktime_t remaining_tm;

	frer_seq_recovery_reset(frer_act);

	remaining_tm = (ktime_t)(frer_act->rcvy_reset_msec * 1000000);

	hrtimer_forward(timer, timer->base->get_time(), remaining_tm);

	return HRTIMER_RESTART;
}

static int frer_rtag_decode(struct sk_buff *skb)
{
	struct rtag_vlan_ethhdr *rtag_vlan_hdr;
	struct rtag_ethhdr *rtag_hdr;
	struct vlan_ethhdr *vlanhdr;
	struct ethhdr *ethhdr;
	struct r_tag *rtag;
	bool is_vlan;
	u16 sequence;
	u16 proto;

	ethhdr = (struct ethhdr *)skb_mac_header(skb);
	proto = ethhdr->h_proto;
	is_vlan = false;

	if (proto == htons(ETH_P_8021Q)) {
		vlanhdr = (struct vlan_ethhdr *)ethhdr;
		proto = vlanhdr->h_vlan_encapsulated_proto;
		is_vlan = true;
	}

	if (proto != htons(ETH_P_RTAG))
		return FRER_RCVY_INVALID_SEQ;

	if (is_vlan) {
		rtag_vlan_hdr = (struct rtag_vlan_ethhdr *)ethhdr;
		rtag = &rtag_vlan_hdr->h_rtag;
	} else {
		rtag_hdr = (struct rtag_ethhdr *)ethhdr;
		rtag = &rtag_hdr->h_rtag;
	}

	sequence = ntohs(rtag->sequence_nr);

	return sequence;
}

static int frer_seq_generation_alg(struct tcf_frer *frer_act)
{
	u32 gen_seq_max = frer_act->seq_space - 1;
	u32 gen_seq_num = frer_act->gen_seq_num;
	int sequence_number;

	sequence_number = gen_seq_num;

	if (gen_seq_num >= gen_seq_max)
		gen_seq_num = 0;
	else
		gen_seq_num++;

	frer_act->gen_seq_num = gen_seq_num;

	return sequence_number;
}

static int frer_rtag_encode(struct sk_buff *skb, struct tcf_frer *frer_act)
{
	struct vlan_ethhdr *vlanhdr;
	struct ethhdr *ethhdr;
	struct r_tag *rtag;
	int rtag_len, head_len;
	unsigned char *dst, *src, *p;
	__be16 *proto, proto_val;

	ethhdr = (struct ethhdr *)skb_mac_header(skb);
	if (ethhdr->h_proto == htons(ETH_P_8021Q)) {
		vlanhdr = (struct vlan_ethhdr *)ethhdr;
		p = (unsigned char *)(vlanhdr + 1);
		proto = &vlanhdr->h_vlan_encapsulated_proto;
	} else {
		p = (unsigned char *)(ethhdr + 1);
		proto = &ethhdr->h_proto;
	}

	proto_val = *proto;
	*proto = htons(ETH_P_RTAG);

	src = skb_mac_header(skb);
	head_len = p - src;

	rtag_len = sizeof(struct r_tag);
	if (skb_cow_head(skb, rtag_len) < 0)
		return -ENOMEM;

	skb_push(skb, rtag_len);
	skb->mac_header -= rtag_len;

	dst = skb_mac_header(skb);
	memmove(dst, src, head_len);

	rtag = (struct r_tag *)(dst + head_len);
	rtag->encap_proto = proto_val;
	rtag->sequence_nr = htons(frer_act->gen_seq_num);
	rtag->reserved = 0;

	return 0;
}

static void frer_rtag_pop(struct sk_buff *skb, struct tcf_frer *frer_act)
{
	struct vlan_ethhdr *vlanhdr;
	struct ethhdr *ethhdr;
	struct r_tag *rtag;
	int rtag_len, head_len;
	unsigned char *dst, *src, *p;
	__be16 *proto;

	ethhdr = (struct ethhdr *)skb_mac_header(skb);

	if (ethhdr->h_proto == htons(ETH_P_8021Q)) {
		vlanhdr = (struct vlan_ethhdr *)ethhdr;
		p = (unsigned char *)(vlanhdr + 1);
		proto = &vlanhdr->h_vlan_encapsulated_proto;
	} else {
		p = (unsigned char *)(ethhdr + 1);
		proto = &ethhdr->h_proto;
	}

	if (*proto != htons(ETH_P_RTAG))
		return;

	rtag = (struct r_tag *)p;
	rtag_len = sizeof(struct r_tag);
	*proto = rtag->encap_proto;

	src = skb_mac_header(skb);
	head_len = p - src;

	skb->data = skb_mac_header(skb);
	skb_pull(skb, rtag_len);

	skb_reset_mac_header(skb);

	if (skb->ip_summed == CHECKSUM_PARTIAL)
		skb->csum_start += rtag_len;

	dst = skb_mac_header(skb);
	memmove(dst, src, head_len);
}

static const struct tcf_frer_proto_ops rtag_ops = {
	.encode = frer_rtag_encode,
	.decode = frer_rtag_decode,
	.tag_pop = frer_rtag_pop,
};

static int tcf_frer_init(struct net *net, struct nlattr *nla,
			 struct nlattr *est, struct tc_action **a,
			 int ovr, int bind, bool rtnl_held,
			 struct tcf_proto *tp, u32 flags,
			 struct netlink_ext_ack *extack)
{
	struct tc_action_net *tn = net_generic(net, frer_net_id);
	struct nlattr *tb[TCA_FRER_MAX + 1];
	struct tcf_chain *goto_ch = NULL;
	struct tcf_frer *frer_act;
	struct tc_frer *parm;
	int ret = 0, err, index;
	ktime_t remaining_tm;

	if (!nla)
		return -EINVAL;

	err = nla_parse_nested(tb, TCA_FRER_MAX, nla, frer_policy, extack);
	if (err < 0)
		return err;

	if (!tb[TCA_FRER_PARMS])
		return -EINVAL;

	parm = nla_data(tb[TCA_FRER_PARMS]);
	index = parm->index;

	err = tcf_idr_check_alloc(tn, &index, a, bind);
	if (err < 0)
		return err;

	if (err && bind)
		return 0;

	if (!err) {
		ret = tcf_idr_create(tn, index, est, a,
				     &act_frer_ops, bind, false, 0);

		if (ret) {
			tcf_idr_cleanup(tn, index);
			return ret;
		}
	} else if (!ovr) {
		tcf_idr_release(*a, bind);
		return -EEXIST;
	}

	err = tcf_action_check_ctrlact(parm->action, tp, &goto_ch, extack);
	if (err < 0)
		goto release_idr;

	frer_act = to_frer(*a);

	spin_lock_bh(&frer_act->tcf_lock);
	goto_ch = tcf_action_set_ctrlact(*a, parm->action, goto_ch);

	frer_act->tag_type = nla_get_u8(tb[TCA_FRER_TAG_TYPE]);
	frer_act->tag_action = nla_get_u8(tb[TCA_FRER_TAG_ACTION]);
	frer_act->recover = nla_get_u8(tb[TCA_FRER_RECOVER]);
	frer_act->rcvy_alg = nla_get_u8(tb[TCA_FRER_RECOVER_ALG]);
	frer_act->rcvy_history_len = nla_get_u8(tb[TCA_FRER_RECOVER_HISTORY_LEN]);
	frer_act->rcvy_reset_msec = nla_get_u64(tb[TCA_FRER_RECOVER_RESET_TM]);

	frer_act->gen_seq_num = 0;
	frer_act->seq_space = 1 << FRER_SEQ_SPACE;
	frer_act->rcvy_seq_num = 0;
	frer_act->seq_history = 0xFFFFFFFF;
	frer_act->rcvy_take_noseq = true;

	switch (frer_act->tag_type) {
	case TCA_FRER_TAG_RTAG:
		frer_act->proto_ops = &rtag_ops;
		break;
	case TCA_FRER_TAG_HSR:
	case TCA_FRER_TAG_PRP:
	default:
		spin_unlock_bh(&frer_act->tcf_lock);
		return -EOPNOTSUPP;
	}

	if (frer_act->recover && frer_act->rcvy_reset_msec) {
		hrtimer_init(&frer_act->hrtimer, CLOCK_TAI,
			     HRTIMER_MODE_REL_SOFT);
		frer_act->hrtimer.function = frer_hrtimer_func;

		remaining_tm = (ktime_t)(frer_act->rcvy_reset_msec * 1000000);
		hrtimer_start(&frer_act->hrtimer, remaining_tm,
			      HRTIMER_MODE_REL_SOFT);
	}

	spin_unlock_bh(&frer_act->tcf_lock);

	if (goto_ch)
		tcf_chain_put_by_act(goto_ch);

	return ret;

release_idr:
	tcf_idr_release(*a, bind);
	return err;
}

static void frer_seq_recovery_reset(struct tcf_frer *frer_act)
{
	spin_lock(&frer_act->tcf_lock);
	if (frer_act->rcvy_alg == TCA_FRER_RCVY_VECTOR_ALG) {
		frer_act->rcvy_seq_num = frer_act->seq_space - 1;
		frer_act->seq_history = 0;
	}
	frer_act->cps_seq_rcvy_resets++;
	frer_act->take_any = true;
	spin_unlock(&frer_act->tcf_lock);
}

static void frer_shift_seq_history(int value, struct tcf_frer *frer_act)
{
	int history_len = frer_act->rcvy_history_len;

	if ((frer_act->seq_history & BIT(history_len - 1)) == 0)
		frer_act->cps_seq_rcvy_lost_pkts++;

	frer_act->seq_history <<= 1;

	if (value)
		frer_act->seq_history |= BIT(0);
}

static int frer_vector_rcvy_alg(struct tcf_frer *frer_act, int sequence,
				bool individual)
{
	struct hrtimer *timer = &frer_act->hrtimer;
	bool reset_timer = false;
	ktime_t remaining_tm;
	int delta, ret;

	if (sequence == FRER_RCVY_INVALID_SEQ) {
		frer_act->cps_seq_rcvy_tagless_pkts++;
		if (frer_act->rcvy_take_noseq) {
			reset_timer = true;
			ret = FRER_RCVY_PASSED;
			goto out;
		} else {
			return FRER_RCVY_DISCARDED;
		}
	}

	delta = (sequence - frer_act->rcvy_seq_num) & (frer_act->seq_space - 1);
	/* -(RecovSeqSpace/2) <= delta <= ((RecovSeqSpace/2)-1) */
	if (delta & (frer_act->seq_space / 2))
		delta -= frer_act->seq_space;

	if (frer_act->take_any) {
		frer_act->take_any = false;
		frer_act->seq_history |= BIT(0);
		frer_act->rcvy_seq_num = sequence;

		reset_timer = true;
		ret = FRER_RCVY_PASSED;
		goto out;
	}

	if (delta >= frer_act->rcvy_history_len ||
	    delta <= -frer_act->rcvy_history_len) {
		/* Packet is out-of-range. */
		frer_act->cps_seq_rcvy_rogue_pkts++;

		if (individual)
			reset_timer = true;

		ret = FRER_RCVY_DISCARDED;
		goto out;
	} else if (delta <= 0) {
		/* Packet is old and in SequenceHistory. */
		if (frer_act->seq_history & BIT(-delta)) {
			if (individual)
				reset_timer = true;

			/* Packet has been seen. */
			ret = FRER_RCVY_DISCARDED;
			goto out;
		} else {
			/* Packet has not been seen. */
			frer_act->seq_history |= BIT(-delta);
			frer_act->cps_seq_rcvy_out_of_order_pkts++;

			reset_timer = true;
			ret = FRER_RCVY_PASSED;
			goto out;
		}
	} else {
		/* Packet is not too far ahead of the one we want. */
		if (delta != 1)
			frer_act->cps_seq_rcvy_out_of_order_pkts++;

		while (--delta)
			frer_shift_seq_history(0, frer_act);
		frer_shift_seq_history(1, frer_act);
		frer_act->rcvy_seq_num = sequence;

		reset_timer = true;
		ret = FRER_RCVY_PASSED;
		goto out;
	}
out:
	if (reset_timer && frer_act->rcvy_reset_msec) {
		remaining_tm =
			(ktime_t)(frer_act->rcvy_reset_msec * 1000000);
		hrtimer_start(timer, remaining_tm, HRTIMER_MODE_REL_SOFT);
	}

	return ret;
}

static int frer_match_rcvy_alg(struct tcf_frer *frer_act, int sequence,
			       bool individual)
{
	struct hrtimer *timer = &frer_act->hrtimer;
	bool reset_timer = false;
	ktime_t remaining_tm;
	int delta, ret;

	if (sequence == FRER_RCVY_INVALID_SEQ) {
		frer_act->cps_seq_rcvy_tagless_pkts++;

		return FRER_RCVY_PASSED;
	}

	if (frer_act->take_any) {
		frer_act->take_any = false;
		frer_act->rcvy_seq_num = sequence;

		reset_timer = true;
		ret = FRER_RCVY_PASSED;
		goto out;
	}

	delta = sequence - frer_act->rcvy_seq_num;
	if (delta) {
		/* Packet has not been seen, accept it. */
		if (delta != 1)
			frer_act->cps_seq_rcvy_out_of_order_pkts++;

		frer_act->rcvy_seq_num = sequence;

		reset_timer = true;
		ret = FRER_RCVY_PASSED;
		goto out;
	} else {
		if (individual)
			reset_timer = true;

		/* Packet has been seen. Do not forward. */
		ret = FRER_RCVY_DISCARDED;
		goto out;
	}

out:
	if (reset_timer && frer_act->rcvy_reset_msec) {
		remaining_tm = (ktime_t)(frer_act->rcvy_reset_msec * 1000000);
		hrtimer_start(timer, remaining_tm, HRTIMER_MODE_REL_SOFT);
	}

	return ret;
}

static int tcf_frer_act(struct sk_buff *skb, const struct tc_action *a,
			struct tcf_result *res)
{
	struct tcf_frer *frer_act = to_frer(a);
	bool ingress, individual;
	int ret, retval;
	int sequence;

	tcf_lastuse_update(&frer_act->tcf_tm);
	tcf_action_update_bstats(&frer_act->common, skb);

	retval = READ_ONCE(frer_act->tcf_action);

	sequence = frer_act->proto_ops->decode(skb);

	ingress = skb_at_tc_ingress(skb);
	individual = ingress;

	if (frer_act->recover) {
		spin_lock(&frer_act->tcf_lock);

		if (frer_act->rcvy_alg == TCA_FRER_RCVY_VECTOR_ALG)
			ret = frer_vector_rcvy_alg(frer_act, sequence,
						   individual);
		else
			ret = frer_match_rcvy_alg(frer_act, sequence,
						  individual);
		if (ret) {
			frer_act->tcf_qstats.drops++;
			retval = TC_ACT_SHOT;
		}

		if (frer_act->tag_action == TCA_FRER_TAG_POP)
			frer_act->proto_ops->tag_pop(skb, frer_act);

		spin_unlock(&frer_act->tcf_lock);

		return retval;
	}

	if (frer_act->tag_action == TCA_FRER_TAG_PUSH &&
	    sequence == FRER_RCVY_INVALID_SEQ) {
		spin_lock(&frer_act->tcf_lock);

		frer_seq_generation_alg(frer_act);

		frer_act->proto_ops->encode(skb, frer_act);

		spin_unlock(&frer_act->tcf_lock);
	}

	return retval;
}

static int tcf_frer_dump(struct sk_buff *skb, struct tc_action *a,
			 int bind, int ref)
{
	unsigned char *b = skb_tail_pointer(skb);
	struct tcf_frer *frer_act = to_frer(a);
	struct tc_frer opt = {
		.index	= frer_act->tcf_index,
		.refcnt	= refcount_read(&frer_act->tcf_refcnt) - ref,
		.bindcnt = atomic_read(&frer_act->tcf_bindcnt) - bind,
	};
	struct tcf_t t;

	spin_lock_bh(&frer_act->tcf_lock);
	opt.action = frer_act->tcf_action;

	if (nla_put(skb, TCA_FRER_PARMS, sizeof(opt), &opt))
		goto nla_put_failure;

	if (nla_put_u8(skb, TCA_FRER_TAG_TYPE, frer_act->tag_type))
		goto nla_put_failure;

	if (nla_put_u8(skb, TCA_FRER_TAG_ACTION, frer_act->tag_action))
		goto nla_put_failure;

	if (nla_put_u8(skb, TCA_FRER_RECOVER, frer_act->recover))
		goto nla_put_failure;

	if (nla_put_u8(skb, TCA_FRER_RECOVER_ALG, frer_act->rcvy_alg))
		goto nla_put_failure;

	if (nla_put_u8(skb, TCA_FRER_RECOVER_HISTORY_LEN,
		       frer_act->rcvy_history_len))
		goto nla_put_failure;

	if (nla_put_u64_64bit(skb, TCA_FRER_RECOVER_RESET_TM,
			      frer_act->rcvy_reset_msec, TCA_FRER_PAD))
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_FRER_RECOVER_TAGLESS_PKTS,
			frer_act->cps_seq_rcvy_tagless_pkts))
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_FRER_RECOVER_OUT_OF_ORDER_PKTS,
			frer_act->cps_seq_rcvy_out_of_order_pkts))
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_FRER_RECOVER_ROGUE_PKTS,
			frer_act->cps_seq_rcvy_rogue_pkts))
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_FRER_RECOVER_LOST_PKTS,
			frer_act->cps_seq_rcvy_lost_pkts))
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_FRER_RECOVER_RESETS,
			frer_act->cps_seq_rcvy_resets))
		goto nla_put_failure;

	tcf_tm_dump(&t, &frer_act->tcf_tm);
	if (nla_put_64bit(skb, TCA_FRER_TM, sizeof(t),
			  &t, TCA_FRER_PAD))
		goto nla_put_failure;
	spin_unlock_bh(&frer_act->tcf_lock);

	return skb->len;

nla_put_failure:
	spin_unlock_bh(&frer_act->tcf_lock);
	nlmsg_trim(skb, b);

	return -1;
}

static int tcf_frer_walker(struct net *net, struct sk_buff *skb,
			   struct netlink_callback *cb, int type,
			   const struct tc_action_ops *ops,
			   struct netlink_ext_ack *extack)
{
	struct tc_action_net *tn = net_generic(net, frer_net_id);

	return tcf_generic_walker(tn, skb, cb, type, ops, extack);
}

static int tcf_frer_search(struct net *net, struct tc_action **a, u32 index)
{
	struct tc_action_net *tn = net_generic(net, frer_net_id);

	return tcf_idr_search(tn, a, index);
}

static void tcf_frer_stats_update(struct tc_action *a, u64 bytes, u64 packets,
				  u64 drops, u64 lastuse, bool hw)
{
	struct tcf_frer *frer_act = to_frer(a);
	struct tcf_t *tm = &frer_act->tcf_tm;

	tcf_action_update_stats(a, bytes, packets, drops, hw);
	tm->lastuse = max_t(u64, tm->lastuse, lastuse);
}

static void tcf_frer_cleanup(struct tc_action *a)
{
	struct tcf_frer *frer_act = to_frer(a);

	if (frer_act->rcvy_reset_msec)
		hrtimer_cancel(&frer_act->hrtimer);
}

static size_t tcf_frer_get_fill_size(const struct tc_action *act)
{
	return nla_total_size(sizeof(struct tc_frer));
}

static struct tc_action_ops act_frer_ops = {
	.kind		=	"frer",
	.id		=	TCA_ID_FRER,
	.owner		=	THIS_MODULE,
	.act		=	tcf_frer_act,
	.init		=	tcf_frer_init,
	.cleanup	=	tcf_frer_cleanup,
	.dump		=	tcf_frer_dump,
	.walk		=	tcf_frer_walker,
	.stats_update	=	tcf_frer_stats_update,
	.get_fill_size	=	tcf_frer_get_fill_size,
	.lookup		=	tcf_frer_search,
	.size		=	sizeof(struct tcf_frer),
};

static __net_init int frer_init_net(struct net *net)
{
	struct tc_action_net *tn = net_generic(net, frer_net_id);

	return tc_action_net_init(net, tn, &act_frer_ops);
}

static void __net_exit frer_exit_net(struct list_head *net_list)
{
	tc_action_net_exit(net_list, frer_net_id);
};

static struct pernet_operations frer_net_ops = {
	.init = frer_init_net,
	.exit_batch = frer_exit_net,
	.id   = &frer_net_id,
	.size = sizeof(struct tc_action_net),
};

static int __init frer_init_module(void)
{
	return tcf_register_action(&act_frer_ops, &frer_net_ops);
}

static void __exit frer_cleanup_module(void)
{
	tcf_unregister_action(&act_frer_ops, &frer_net_ops);
}

module_init(frer_init_module);
module_exit(frer_cleanup_module);
MODULE_LICENSE("GPL v2");
