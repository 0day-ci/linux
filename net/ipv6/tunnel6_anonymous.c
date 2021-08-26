// SPDX-License-Identifier: GPL-2.0+
/*
 *  Anonymous tunnels for IPv6
 *
 *  Handle the decapsulation process of anonymous tunnels (i.e., not
 *  explicitly configured). This behavior is needed for architectures
 *  where a lot of ingresses and egresses must be linked altogether,
 *  leading to a solution to avoid configuring all possible tunnels.
 *
 *  Author:
 *  Justin Iurman <justin.iurman@uliege.be>
 */

#include <linux/export.h>
#include <linux/icmpv6.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <net/addrconf.h>
#include <net/protocol.h>
#include <net/tunnel6_anonymous.h>
#include <uapi/linux/in.h>

/* called with rcu_read_lock() */
int anonymous66_rcv(struct sk_buff *skb)
{
	if (!pskb_may_pull(skb, sizeof(struct ipv6hdr)))
		goto drop;

	if (anonymous66_enabled(skb))
		return anonymous66_decap(skb);

	icmpv6_send(skb, ICMPV6_PARAMPROB, ICMPV6_UNK_NEXTHDR, 0);
drop:
	kfree_skb(skb);
	return 0;
}

static const struct inet6_protocol anonymous66_protocol = {
	.handler	=	anonymous66_rcv,
	.flags		=	INET6_PROTO_NOPOLICY|INET6_PROTO_FINAL,
};

bool anonymous66_enabled(struct sk_buff *skb)
{
	return __in6_dev_get(skb->dev)->cnf.tunnel66_decap_enabled;
}
EXPORT_SYMBOL(anonymous66_enabled);

int anonymous66_decap(struct sk_buff *skb)
{
	skb_reset_network_header(skb);
	skb_reset_transport_header(skb);
	skb->encapsulation = 0;

	__skb_tunnel_rx(skb, skb->dev, dev_net(skb->dev));
	netif_rx(skb);

	return 0;
}
EXPORT_SYMBOL(anonymous66_decap);

int tunnel6_anonymous_register(void)
{
	return inet6_add_protocol(&anonymous66_protocol, IPPROTO_IPV6);
}
EXPORT_SYMBOL(tunnel6_anonymous_register);

int tunnel6_anonymous_unregister(void)
{
	return inet6_del_protocol(&anonymous66_protocol, IPPROTO_IPV6);
}
EXPORT_SYMBOL(tunnel6_anonymous_unregister);

int __init tunnel6_anonymous_init(void)
{
	tunnel6_anonymous_register();
	return 0;
}

void tunnel6_anonymous_exit(void)
{
	tunnel6_anonymous_unregister();
}
