// SPDX-License-Identifier: GPL-2.0+
/*
 *  IPv6 IOAM Lightweight Tunnel implementation
 *
 *  Author:
 *  Justin Iurman <justin.iurman@uliege.be>
 */

#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/net.h>
#include <linux/netlink.h>
#include <linux/in6.h>
#include <linux/ioam6.h>
#include <linux/ioam6_iptunnel.h>
#include <net/dst.h>
#include <net/sock.h>
#include <net/lwtunnel.h>
#include <net/ioam6.h>
#include <net/ipv6.h>
#include <net/dst_cache.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>

#define IOAM6_MASK_SHORT_FIELDS 0xff100000
#define IOAM6_MASK_WIDE_FIELDS 0xe00000

struct ioam6_lwt_encap {
	struct ipv6_hopopt_hdr eh;
	u8 pad[2];			/* 2-octet padding for 4n-alignment */
	struct ioam6_hdr ioamh;
	struct ioam6_trace_hdr traceh;
} __packed;

struct ioam6_lwt {
	u8 mode;
	struct in6_addr tundst;
	struct dst_cache cache;
	struct ioam6_lwt_encap	tuninfo;
};

static struct ioam6_lwt *ioam6_lwt_state(struct lwtunnel_state *lwt)
{
	return (struct ioam6_lwt *)lwt->data;
}

static struct ioam6_lwt_encap *ioam6_lwt_info(struct lwtunnel_state *lwt)
{
	return &ioam6_lwt_state(lwt)->tuninfo;
}

static struct ioam6_trace_hdr *ioam6_lwt_trace(struct lwtunnel_state *lwt)
{
	return &(ioam6_lwt_state(lwt)->tuninfo.traceh);
}

static const struct nla_policy ioam6_iptunnel_policy[IOAM6_IPTUNNEL_MAX + 1] = {
	[IOAM6_IPTUNNEL_TRACE]	= NLA_POLICY_EXACT_LEN(sizeof(struct ioam6_iptunnel_trace)),
};

static bool ioam6_validate_trace_hdr(struct ioam6_trace_hdr *trace)
{
	u32 fields;

	if (!trace->type_be32 || !trace->remlen ||
	    trace->remlen > IOAM6_TRACE_DATA_SIZE_MAX / 4)
		return false;

	trace->nodelen = 0;
	fields = be32_to_cpu(trace->type_be32);

	trace->nodelen += hweight32(fields & IOAM6_MASK_SHORT_FIELDS)
				* (sizeof(__be32) / 4);
	trace->nodelen += hweight32(fields & IOAM6_MASK_WIDE_FIELDS)
				* (sizeof(__be64) / 4);

	return true;
}

static int ioam6_build_state(struct net *net, struct nlattr *nla,
			     unsigned int family, const void *cfg,
			     struct lwtunnel_state **ts,
			     struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IOAM6_IPTUNNEL_MAX + 1];
	struct ioam6_iptunnel_trace *data;
	struct ioam6_trace_hdr *trace;
	struct ioam6_lwt_encap *info;
	struct lwtunnel_state *s;
	struct ioam6_lwt *lwt;
	int len, aligned, err;

	if (family != AF_INET6)
		return -EINVAL;

	err = nla_parse_nested(tb, IOAM6_IPTUNNEL_MAX, nla,
			       ioam6_iptunnel_policy, extack);
	if (err < 0)
		return err;

	if (!tb[IOAM6_IPTUNNEL_TRACE]) {
		NL_SET_ERR_MSG(extack, "missing trace");
		return -EINVAL;
	}

	data = nla_data(tb[IOAM6_IPTUNNEL_TRACE]);
	if (!ioam6_validate_trace_hdr(&data->trace)) {
		NL_SET_ERR_MSG_ATTR(extack, tb[IOAM6_IPTUNNEL_TRACE],
				    "invalid trace validation");
		return -EINVAL;
	}

	switch (data->mode) {
	case IOAM6_IPTUNNEL_MODE_INLINE:
	case IOAM6_IPTUNNEL_MODE_ENCAP:
	case IOAM6_IPTUNNEL_MODE_AUTO:
		break;
	default:
		NL_SET_ERR_MSG_ATTR(extack, tb[IOAM6_IPTUNNEL_TRACE],
				    "invalid mode");
		return -EINVAL;
	}

	len = sizeof(*info) + data->trace.remlen * 4;
	aligned = ALIGN(len, 8);

	s = lwtunnel_state_alloc(aligned + sizeof(*lwt) - sizeof(*info));
	if (!s)
		return -ENOMEM;

	lwt = ioam6_lwt_state(s);
	lwt->mode = data->mode;
	if (lwt->mode != IOAM6_IPTUNNEL_MODE_INLINE)
		memcpy(&lwt->tundst, &data->tundst, sizeof(lwt->tundst));

	err = dst_cache_init(&lwt->cache, GFP_ATOMIC);
	if (err) {
		kfree(s);
		return err;
	}

	trace = ioam6_lwt_trace(s);
	memcpy(trace, &data->trace, sizeof(*trace));

	info = ioam6_lwt_info(s);
	info->eh.hdrlen = (aligned >> 3) - 1;
	info->pad[0] = IPV6_TLV_PADN;
	info->ioamh.type = IOAM6_TYPE_PREALLOC;
	info->ioamh.opt_type = IPV6_TLV_IOAM;
	info->ioamh.opt_len = sizeof(info->ioamh) - 2;
	info->ioamh.opt_len += sizeof(*trace) + trace->remlen * 4;

	len = aligned - len;
	if (len == 1) {
		trace->data[trace->remlen * 4] = IPV6_TLV_PAD1;
	} else if (len > 0) {
		trace->data[trace->remlen * 4] = IPV6_TLV_PADN;
		trace->data[trace->remlen * 4 + 1] = len - 2;
	}

	s->type = LWTUNNEL_ENCAP_IOAM6;
	s->flags |= LWTUNNEL_STATE_OUTPUT_REDIRECT;

	*ts = s;

	return 0;
}

static int ioam6_do_fill(struct net *net, struct sk_buff *skb)
{
	struct ioam6_trace_hdr *trace;
	struct ioam6_namespace *ns;

	trace = (struct ioam6_trace_hdr *)(skb_transport_header(skb)
					   + sizeof(struct ipv6_hopopt_hdr) + 2
					   + sizeof(struct ioam6_hdr));

	ns = ioam6_namespace(net, trace->namespace_id);
	if (ns)
		ioam6_fill_trace_data(skb, ns, trace, false);

	return 0;
}

static int ioam6_do_inline(struct net *net, struct sk_buff *skb,
			   struct ioam6_lwt_encap *tuninfo)
{
	struct ipv6hdr *oldhdr, *hdr;
	int hdrlen, err;

	hdrlen = (tuninfo->eh.hdrlen + 1) << 3;

	err = skb_cow_head(skb, hdrlen + skb->mac_len);
	if (unlikely(err))
		return err;

	oldhdr = ipv6_hdr(skb);
	skb_pull(skb, sizeof(*oldhdr));
	skb_postpull_rcsum(skb, skb_network_header(skb), sizeof(*oldhdr));

	skb_push(skb, sizeof(*oldhdr) + hdrlen);
	skb_reset_network_header(skb);
	skb_mac_header_rebuild(skb);

	hdr = ipv6_hdr(skb);
	memmove(hdr, oldhdr, sizeof(*oldhdr));
	tuninfo->eh.nexthdr = hdr->nexthdr;

	skb_set_transport_header(skb, sizeof(*hdr));
	skb_postpush_rcsum(skb, hdr, sizeof(*hdr) + hdrlen);

	memcpy(skb_transport_header(skb), (u8 *)tuninfo, hdrlen);

	hdr->nexthdr = NEXTHDR_HOP;
	hdr->payload_len = cpu_to_be16(skb->len - sizeof(*hdr));

	return ioam6_do_fill(net, skb);
}

static int ioam6_do_encap(struct net *net, struct sk_buff *skb,
			  struct ioam6_lwt_encap *tuninfo,
			  struct in6_addr *tundst)
{
	struct dst_entry *dst = skb_dst(skb);
	struct ipv6hdr *hdr, *inner_hdr;
	int hdrlen, len, err;

	hdrlen = (tuninfo->eh.hdrlen + 1) << 3;
	len = sizeof(*hdr) + hdrlen;

	err = skb_cow_head(skb, len + skb->mac_len);
	if (unlikely(err))
		return err;

	inner_hdr = ipv6_hdr(skb);

	skb_push(skb, len);
	skb_reset_network_header(skb);
	skb_mac_header_rebuild(skb);
	skb_set_transport_header(skb, sizeof(*hdr));

	tuninfo->eh.nexthdr = NEXTHDR_IPV6;
	memcpy(skb_transport_header(skb), (u8 *)tuninfo, hdrlen);

	hdr = ipv6_hdr(skb);
	memcpy(hdr, inner_hdr, sizeof(*hdr));

	hdr->nexthdr = NEXTHDR_HOP;
	hdr->payload_len = cpu_to_be16(skb->len - sizeof(*hdr));
	hdr->daddr = *tundst;
	ipv6_dev_get_saddr(net, dst->dev, &hdr->daddr,
			   IPV6_PREFER_SRC_PUBLIC, &hdr->saddr);

	skb_postpush_rcsum(skb, hdr, len);

	return ioam6_do_fill(net, skb);
}

static int ioam6_output(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	struct in6_addr daddr_prev;
	struct ioam6_lwt *lwt;
	int err = -EINVAL;

	lwt = ioam6_lwt_state(dst->lwtstate);

	if (skb->protocol != htons(ETH_P_IPV6))
		goto drop;

	daddr_prev = ipv6_hdr(skb)->daddr;

	switch (lwt->mode) {
	case IOAM6_IPTUNNEL_MODE_INLINE:
do_inline:
		/* Direct insertion - if there is no Hop-by-Hop yet */
		if (ipv6_hdr(skb)->nexthdr == NEXTHDR_HOP)
			goto out;

		err = ioam6_do_inline(net, skb, &lwt->tuninfo);
		if (unlikely(err))
			goto drop;

		break;
	case IOAM6_IPTUNNEL_MODE_ENCAP:
do_encap:
		/* Encapsulation (ip6ip6) */
		err = ioam6_do_encap(net, skb, &lwt->tuninfo, &lwt->tundst);
		if (unlikely(err))
			goto drop;

		break;
	case IOAM6_IPTUNNEL_MODE_AUTO:
		/* Automatic (RFC8200 compliant):
		 *  - local packet -> INLINE mode
		 *  - forwarded packet -> ENCAP mode
		 */
		if (!skb->dev)
			goto do_inline;

		goto do_encap;
	default:
		goto drop;
	}

	err = skb_cow_head(skb, LL_RESERVED_SPACE(dst->dev));
	if (unlikely(err))
		goto drop;

	if (!ipv6_addr_equal(&daddr_prev, &ipv6_hdr(skb)->daddr)) {
		preempt_disable();
		dst = dst_cache_get(&lwt->cache);
		preempt_enable();

		if (unlikely(!dst)) {
			struct ipv6hdr *hdr = ipv6_hdr(skb);
			struct flowi6 fl6;

			memset(&fl6, 0, sizeof(fl6));
			fl6.daddr = hdr->daddr;
			fl6.saddr = hdr->saddr;
			fl6.flowlabel = ip6_flowinfo(hdr);
			fl6.flowi6_mark = skb->mark;
			fl6.flowi6_proto = hdr->nexthdr;

			dst = ip6_route_output(net, NULL, &fl6);
			if (dst->error) {
				err = dst->error;
				dst_release(dst);
				goto drop;
			}

			preempt_disable();
			dst_cache_set_ip6(&lwt->cache, dst, &fl6.saddr);
			preempt_enable();
		}

		skb_dst_drop(skb);
		skb_dst_set(skb, dst);

		return dst_output(net, sk, skb);
	}
out:
	return dst->lwtstate->orig_output(net, sk, skb);
drop:
	kfree_skb(skb);
	return err;
}

static void ioam6_destroy_state(struct lwtunnel_state *lwt)
{
	dst_cache_destroy(&ioam6_lwt_state(lwt)->cache);
}

static int ioam6_fill_encap_info(struct sk_buff *skb,
				 struct lwtunnel_state *lwtstate)
{
	struct ioam6_iptunnel_trace *info;
	struct ioam6_trace_hdr *trace;
	struct ioam6_lwt *lwt;
	struct nlattr *nla;

	nla = nla_reserve(skb, IOAM6_IPTUNNEL_TRACE, sizeof(*info));
	if (!nla)
		return -EMSGSIZE;

	lwt = ioam6_lwt_state(lwtstate);
	trace = ioam6_lwt_trace(lwtstate);

	info = nla_data(nla);
	info->mode = lwt->mode;
	memcpy(&info->trace, trace, sizeof(*trace));
	if (info->mode != IOAM6_IPTUNNEL_MODE_INLINE)
		memcpy(&info->tundst, &lwt->tundst, sizeof(lwt->tundst));

	return 0;
}

static int ioam6_encap_nlsize(struct lwtunnel_state *lwtstate)
{
	return nla_total_size(sizeof(struct ioam6_iptunnel_trace));
}

static int ioam6_encap_cmp(struct lwtunnel_state *a, struct lwtunnel_state *b)
{
	struct ioam6_trace_hdr *trace_a = ioam6_lwt_trace(a);
	struct ioam6_trace_hdr *trace_b = ioam6_lwt_trace(b);
	struct ioam6_lwt *lwt_a = ioam6_lwt_state(a);
	struct ioam6_lwt *lwt_b = ioam6_lwt_state(b);

	return (lwt_a->mode != lwt_b->mode ||
		(lwt_a->mode != IOAM6_IPTUNNEL_MODE_INLINE &&
		 !ipv6_addr_equal(&lwt_a->tundst, &lwt_b->tundst)) ||
		trace_a->namespace_id != trace_b->namespace_id);
}

static const struct lwtunnel_encap_ops ioam6_iptun_ops = {
	.build_state		= ioam6_build_state,
	.destroy_state		= ioam6_destroy_state,
	.output		= ioam6_output,
	.fill_encap		= ioam6_fill_encap_info,
	.get_encap_size	= ioam6_encap_nlsize,
	.cmp_encap		= ioam6_encap_cmp,
	.owner			= THIS_MODULE,
};

int __init ioam6_iptunnel_init(void)
{
	return lwtunnel_encap_add_ops(&ioam6_iptun_ops, LWTUNNEL_ENCAP_IOAM6);
}

void ioam6_iptunnel_exit(void)
{
	lwtunnel_encap_del_ops(&ioam6_iptun_ops, LWTUNNEL_ENCAP_IOAM6);
}
