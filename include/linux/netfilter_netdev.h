/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NETFILTER_NETDEV_H_
#define _NETFILTER_NETDEV_H_

#include <linux/netfilter.h>
#include <linux/netdevice.h>

#ifdef CONFIG_NETFILTER_INGRESS
static inline bool nf_hook_ingress_active(const struct sk_buff *skb)
{
#ifdef CONFIG_JUMP_LABEL
	if (!static_key_false(&nf_hooks_needed[NFPROTO_NETDEV][NF_NETDEV_INGRESS]))
		return false;
#endif
	return rcu_access_pointer(skb->dev->nf_hooks_ingress);
}

/* caller must hold rcu_read_lock */
static inline int nf_hook_ingress(struct sk_buff *skb)
{
	struct nf_hook_entries *e = rcu_dereference(skb->dev->nf_hooks_ingress);
	struct nf_hook_state state;
	int ret;

	/* Must recheck the ingress hook head, in the event it became NULL
	 * after the check in nf_hook_ingress_active evaluated to true.
	 */
	if (unlikely(!e))
		return 0;

	nf_hook_state_init(&state, NF_NETDEV_INGRESS,
			   NFPROTO_NETDEV, skb->dev, NULL, NULL,
			   dev_net(skb->dev), NULL);
	ret = nf_hook_slow(skb, &state, e, 0);
	if (ret == 0)
		return -1;

	return ret;
}

#else /* CONFIG_NETFILTER_INGRESS */
static inline int nf_hook_ingress_active(struct sk_buff *skb)
{
	return 0;
}

static inline int nf_hook_ingress(struct sk_buff *skb)
{
	return 0;
}
#endif /* CONFIG_NETFILTER_INGRESS */

#ifdef CONFIG_NETFILTER_EGRESS
static inline bool nf_hook_egress_active(void)
{
#ifdef CONFIG_JUMP_LABEL
	if (!static_key_false(&nf_hooks_needed[NFPROTO_NETDEV][NF_NETDEV_EGRESS]))
		return false;
#endif
	return true;
}

/* caller must hold rcu_read_lock */
static inline struct sk_buff *nf_hook_egress(struct sk_buff *skb, int *rc,
					     struct net_device *dev)
{
	struct nf_hook_entries *e = rcu_dereference(dev->nf_hooks_egress);
	struct nf_hook_state state;
	int ret;

	if (!e)
		return skb;

	nf_hook_state_init(&state, NF_NETDEV_EGRESS,
			   NFPROTO_NETDEV, dev, NULL, NULL,
			   dev_net(dev), NULL);
	ret = nf_hook_slow(skb, &state, e, 0);

	if (ret == 1) {
		return skb;
	} else if (ret < 0) {
		*rc = NET_XMIT_DROP;
		return NULL;
	} else { /* ret == 0 */
		*rc = NET_XMIT_SUCCESS;
		return NULL;
	}
}
#else /* CONFIG_NETFILTER_EGRESS */
static inline bool nf_hook_egress_active(void)
{
	return false;
}

static inline struct sk_buff *nf_hook_egress(struct sk_buff *skb, int *rc,
					     struct net_device *dev)
{
	return skb;
}
#endif /* CONFIG_NETFILTER_EGRESS */

static inline void nf_hook_netdev_init(struct net_device *dev)
{
#ifdef CONFIG_NETFILTER_INGRESS
	RCU_INIT_POINTER(dev->nf_hooks_ingress, NULL);
#endif
#ifdef CONFIG_NETFILTER_EGRESS
	RCU_INIT_POINTER(dev->nf_hooks_egress, NULL);
#endif
}

#endif /* _NETFILTER_NETDEV_H_ */
