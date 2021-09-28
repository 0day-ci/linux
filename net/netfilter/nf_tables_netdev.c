#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <net/net_namespace.h>
#include <net/netfilter/nf_tables.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>
#include <linux/netfilter_bridge.h>
#include <linux/netfilter_arp.h>
#include <net/netfilter/nf_tables_ipv4.h>
#include <net/netfilter/nf_tables_ipv6.h>

static unsigned int nft_do_chain_netdev(void *priv, struct sk_buff *skb,
					const struct nf_hook_state *state)
{
	struct nft_pktinfo pkt;

	nft_set_pktinfo(&pkt, skb, state);

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		nft_set_pktinfo_ipv4_validate(&pkt);
		break;
	case htons(ETH_P_IPV6):
		nft_set_pktinfo_ipv6_validate(&pkt);
		break;
	default:
		nft_set_pktinfo_unspec(&pkt);
		break;
	}

	return nft_do_chain(&pkt, priv);
}

static const struct nft_chain_type nft_chain_filter_netdev = {
	.name		= "filter",
	.type		= NFT_CHAIN_T_DEFAULT,
	.family		= NFPROTO_NETDEV,
	.hook_mask	= (1 << NF_NETDEV_INGRESS),
	.hooks		= {
		[NF_NETDEV_INGRESS]	= nft_do_chain_netdev,
	},
};

static void nft_netdev_event(unsigned long event, struct net_device *dev,
			     struct nft_ctx *ctx)
{
	struct nft_base_chain *basechain = nft_base_chain(ctx->chain);
	struct nft_hook *hook, *found = NULL;
	int n = 0;

	if (event != NETDEV_UNREGISTER)
		return;

	list_for_each_entry(hook, &basechain->hook_list, list) {
		if (hook->ops.dev == dev)
			found = hook;

		n++;
	}
	if (!found)
		return;

	if (n > 1) {
		nf_unregister_net_hook(ctx->net, &found->ops);
		list_del_rcu(&found->list);
		kfree_rcu(found, rcu);
		return;
	}

	/* UNREGISTER events are also happening on netns exit.
	 *
	 * Although nf_tables core releases all tables/chains, only this event
	 * handler provides guarantee that hook->ops.dev is still accessible,
	 * so we cannot skip exiting net namespaces.
	 */
	__nft_release_basechain(ctx);
}

static int nf_tables_netdev_event(struct notifier_block *this,
				  unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct nftables_pernet *nft_net;
	struct nft_table *table;
	struct nft_chain *chain, *nr;
	struct nft_ctx ctx = {
		.net	= dev_net(dev),
	};

	if (event != NETDEV_UNREGISTER &&
	    event != NETDEV_CHANGENAME)
		return NOTIFY_DONE;

	nft_net = nft_pernet(ctx.net);
	mutex_lock(&nft_net->commit_mutex);
	list_for_each_entry(table, &nft_net->tables, list) {
		if (table->family != NFPROTO_NETDEV)
			continue;

		ctx.family = table->family;
		ctx.table = table;
		list_for_each_entry_safe(chain, nr, &table->chains, list) {
			if (!nft_is_base_chain(chain))
				continue;

			ctx.chain = chain;
			nft_netdev_event(event, dev, &ctx);
		}
	}
	mutex_unlock(&nft_net->commit_mutex);

	return NOTIFY_DONE;
}

static struct notifier_block nf_tables_netdev_notifier = {
	.notifier_call	= nf_tables_netdev_event,
};

static int nft_chain_filter_netdev_init(void)
{
	int err;

	nft_register_chain_type(&nft_chain_filter_netdev);

	err = register_netdevice_notifier(&nf_tables_netdev_notifier);
	if (err)
		goto err_register_netdevice_notifier;

	return 0;

err_register_netdevice_notifier:
	nft_unregister_chain_type(&nft_chain_filter_netdev);

	return err;
}

static void nft_chain_filter_netdev_fini(void)
{
	nft_unregister_chain_type(&nft_chain_filter_netdev);
	unregister_netdevice_notifier(&nf_tables_netdev_notifier);
}

module_init(nft_chain_filter_netdev_init);
module_exit(nft_chain_filter_netdev_fini);

MODULE_LICENSE("GPL");
MODULE_ALIAS_NFT_CHAIN(5, "filter");	/* NFPROTO_NETDEV */
