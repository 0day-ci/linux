/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2021 NXP */

#ifndef __NET_TC_FRER_H
#define __NET_TC_FRER_H

#include <net/act_api.h>
#include <linux/tc_act/tc_frer.h>

struct tcf_frer;

struct tcf_frer_proto_ops {
	int (*encode)(struct sk_buff *skb, struct tcf_frer *frer_act);
	int (*decode)(struct sk_buff *skb);
	void (*tag_pop)(struct sk_buff *skb, struct tcf_frer *frer_act);
};

struct tcf_frer {
	struct tc_action		common;
	u8				tag_type;
	u8				tag_action;
	u8				recover;
	u8				rcvy_alg;
	u8				rcvy_history_len;
	u64				rcvy_reset_msec;
	u32				gen_seq_num;
	u32				rcvy_seq_num;
	u64				seq_space;
	u32				seq_history;
	bool				take_any;
	bool				rcvy_take_noseq;
	u32				cps_seq_rcvy_lost_pkts;
	u32				cps_seq_rcvy_tagless_pkts;
	u32				cps_seq_rcvy_out_of_order_pkts;
	u32				cps_seq_rcvy_rogue_pkts;
	u32				cps_seq_rcvy_resets;
	struct hrtimer			hrtimer;
	const struct tcf_frer_proto_ops	*proto_ops;
};

#define to_frer(a) ((struct tcf_frer *)a)

static inline bool is_tcf_frer(const struct tc_action *a)
{
#ifdef CONFIG_NET_CLS_ACT
	if (a->ops && a->ops->id == TCA_ID_FRER)
		return true;
#endif
	return false;
}

#endif
