// SPDX-License-Identifier: GPL-2.0
/*
 * Management Component Transport Protocol (MCTP) - device implementation.
 *
 * Copyright (c) 2021 Code Construct
 * Copyright (c) 2021 Google
 */

#include <linux/if_link.h>
#include <linux/mctp.h>
#include <linux/netdevice.h>
#include <linux/rcupdate.h>
#include <linux/rtnetlink.h>

#include <net/addrconf.h>
#include <net/netlink.h>
#include <net/mctp.h>
#include <net/mctpdevice.h>
#include <net/sock.h>

/* unlocked: caller must hold rcu_read_lock */
struct mctp_dev *__mctp_dev_get(const struct net_device *dev)
{
	return rcu_dereference(dev->mctp_ptr);
}

struct mctp_dev *mctp_dev_get_rtnl(const struct net_device *dev)
{
	return rtnl_dereference(dev->mctp_ptr);
}

static void mctp_dev_destroy(struct mctp_dev *mdev)
{
	struct net_device *dev = mdev->dev;

	dev_put(dev);
	kfree_rcu(mdev, rcu);
}

static int mctp_fill_addrinfo(struct sk_buff *skb, struct netlink_callback *cb,
			      struct mctp_dev *mdev, struct mctp_ifaddr *ifa)
{
	struct ifaddrmsg *hdr;
	struct nlmsghdr *nlh;

	nlh = nlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			RTM_NEWADDR, sizeof(*hdr), NLM_F_MULTI);
	if (!nlh)
		return -EMSGSIZE;

	hdr = nlmsg_data(nlh);
	hdr->ifa_family = AF_MCTP;
	hdr->ifa_prefixlen = 0;
	hdr->ifa_flags = 0;
	hdr->ifa_scope = 0;
	hdr->ifa_index = mdev->dev->ifindex;

	if (nla_put_u8(skb, IFA_LOCAL, ifa->eid))
		goto cancel;

	if (nla_put_u8(skb, IFA_ADDRESS, ifa->eid))
		goto cancel;

	nlmsg_end(skb, nlh);

	return 0;

cancel:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int mctp_dump_dev_addrinfo(struct mctp_dev *mdev, struct sk_buff *skb,
				  struct netlink_callback *cb, int s_a_idx)
{
	struct mctp_ifaddr *ifa;
	int a_idx = 0;

	list_for_each_entry(ifa, &mdev->addrs, dev_list) {
		if (a_idx < s_a_idx)
			goto next;

		if (mctp_fill_addrinfo(skb, cb, mdev, ifa) < 0)
			break;

next:
		a_idx++;
	}

	return 0;

	cb->args[2] = a_idx;
}

static int mctp_dump_addrinfo(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net *net = sock_net(skb->sk);
	struct net_device *dev;
	struct mctp_dev *mdev;
	struct hlist_head *head;
	int s_h, s_idx, s_a_idx;
	int h, idx;

	/* todo: change to struct overlay */
	s_h = cb->args[0];
	s_idx = cb->args[1];
	s_a_idx = cb->args[2];

	/* todo: strict check -> filter by dev */

	rcu_read_lock();
	for (h = s_h; h < NETDEV_HASHENTRIES; h++, s_idx = 0) {
		idx = 0;
		head = &net->dev_index_head[h];
		hlist_for_each_entry_rcu(dev, head, index_hlist) {
			if (idx < s_idx)
				goto cont;
			mdev = __mctp_dev_get(dev);
			if (!mdev)
				goto cont;
			if (h > s_h || idx > s_idx)
				s_a_idx = 0;
			if (mctp_dump_dev_addrinfo(mdev, skb, cb, s_a_idx) < 0)
				goto out;
cont:
			idx++;
		}
	}
out:
	rcu_read_unlock();
	cb->args[1] = idx;
	cb->args[0] = h;

	return skb->len;
}

static const struct nla_policy ifa_mctp_policy[IFA_MAX + 1] = {
	[IFA_ADDRESS]		= { .type = NLA_U8 },
	[IFA_LOCAL]		= { .type = NLA_U8 },
};

static int mctp_rtm_newaddr(struct sk_buff *skb, struct nlmsghdr *nlh,
			    struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct nlattr *tb[IFA_MAX + 1];
	struct mctp_ifaddr *ifaddr;
	struct net_device *dev;
	struct mctp_addr *addr;
	struct mctp_dev *mdev;
	struct ifaddrmsg *ifm;
	int rc;

	rc = nlmsg_parse(nlh, sizeof(*ifm), tb, IFA_MAX, ifa_mctp_policy,
			 extack);
	if (rc < 0)
		return rc;

	ifm = nlmsg_data(nlh);

	if (tb[IFA_LOCAL])
		addr = nla_data(tb[IFA_LOCAL]);
	else if (tb[IFA_ADDRESS])
		addr = nla_data(tb[IFA_ADDRESS]);
	else
		return -EINVAL;

	/* find device */
	dev = __dev_get_by_index(net, ifm->ifa_index);
	if (!dev)
		return -ENODEV;

	mdev = mctp_dev_get_rtnl(dev);
	if (!mdev)
		return -ENODEV;

	if (!mctp_address_ok(addr->s_addr))
		return -EINVAL;

	/* Prevent duplicates */
	list_for_each_entry(ifaddr, &mdev->addrs, dev_list) {
		if (ifaddr->eid == addr->s_addr)
			return -EEXIST;
	}

	ifaddr = kzalloc(sizeof(*ifaddr), GFP_KERNEL);
	if (!ifaddr)
		return -ENOMEM;

	ifaddr->eid = addr->s_addr;
	ifaddr->dev = mdev;
	list_add(&ifaddr->dev_list, &mdev->addrs);

	return 0;
}

static struct mctp_dev *mctp_add_dev(struct net_device *dev)
{
	struct mctp_dev *mdev;

	ASSERT_RTNL();

	mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return ERR_PTR(ENOMEM);

	INIT_LIST_HEAD(&mdev->addrs);

	mdev->net = MCTP_INITIAL_DEFAULT_NET;

	/* associate to net_device */
	rcu_assign_pointer(dev->mctp_ptr, mdev);
	dev_hold(dev);
	mdev->dev = dev;

	return mdev;
}

static int mctp_fill_link_af(struct sk_buff *skb,
			     const struct net_device *dev, u32 ext_filter_mask)
{
	struct mctp_dev *mdev;

	mdev = mctp_dev_get_rtnl(dev);
	if (!mdev)
		return -ENODATA;
	if (nla_put_u32(skb, IFLA_MCTP_NET, mdev->net))
		return -EMSGSIZE;
	return 0;
}

static size_t mctp_get_link_af_size(const struct net_device *dev,
				    u32 ext_filter_mask)
{
	struct mctp_dev *mdev;
	unsigned int ret;

	/* caller holds RCU */
	mdev = __mctp_dev_get(dev);
	if (!mdev)
		return 0;
	ret = nla_total_size(4); /* IFLA_MCTP_NET */
	return ret;
}

static const struct nla_policy ifla_af_mctp_policy[IFLA_MCTP_MAX + 1] = {
	[IFLA_MCTP_NET]		= { .type = NLA_U32 },
};

static int mctp_set_link_af(struct net_device *dev, const struct nlattr *attr,
			    struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFLA_MCTP_MAX + 1];
	struct mctp_dev *mdev;
	int rc;

	rc = nla_parse_nested(tb, IFLA_MCTP_MAX, attr, ifla_af_mctp_policy,
			      NULL);
	if (rc)
		return rc;

	/* caller holds RCU */
	mdev = __mctp_dev_get(dev);
	if (!mdev)
		return 0;

	if (tb[IFLA_MCTP_NET])
		WRITE_ONCE(mdev->net, nla_get_u32(tb[IFLA_MCTP_NET]));

	return 0;
}

static void mctp_unregister(struct net_device *dev)
{
	struct mctp_ifaddr *ifa, *tmp;
	struct mctp_dev *mdev;

	mdev = mctp_dev_get_rtnl(dev);

	if (!mdev)
		return;

	RCU_INIT_POINTER(mdev->dev->mctp_ptr, NULL);

	/* TODO: drop routes, to the point where we can dev_put(); requires
	 * coordination with the route layer; each route should hold a dev
	 * reference. We should only need to synchronize_rcu() once for this.
	 */
	list_for_each_entry_safe(ifa, tmp, &mdev->addrs, dev_list) {
		list_del_rcu(&ifa->dev_list);
		kfree_rcu(ifa, rcu);
	}

	mctp_dev_destroy(mdev);
}

static int mctp_register(struct net_device *dev)
{
	struct mctp_dev *mdev;

	/* Already registered? */
	if (rtnl_dereference(dev->mctp_ptr))
		return 0;

	/* only register specific types; MCTP-specific and loopback for now */
	if (dev->type != ARPHRD_MCTP && dev->type != ARPHRD_LOOPBACK)
		return 0;

	mdev = mctp_add_dev(dev);
	if (IS_ERR(mdev))
		return PTR_ERR(mdev);

	return 0;
}

static int mctp_dev_notify(struct notifier_block *this, unsigned long event,
			   void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	int rc;

	switch (event) {
	case NETDEV_REGISTER:
		rc = mctp_register(dev);
		if (rc)
			return notifier_from_errno(rc);
		break;
	case NETDEV_UNREGISTER:
		mctp_unregister(dev);
		break;
	}

	return NOTIFY_OK;
}

static struct rtnl_af_ops mctp_af_ops = {
	.family = AF_MCTP,
	.fill_link_af = mctp_fill_link_af,
	.get_link_af_size = mctp_get_link_af_size,
	.set_link_af = mctp_set_link_af,
};

static struct notifier_block mctp_dev_nb = {
	.notifier_call = mctp_dev_notify,
	.priority = ADDRCONF_NOTIFY_PRIORITY,
};

void __init mctp_device_init(void)
{
	register_netdevice_notifier(&mctp_dev_nb);

	rtnl_register_module(THIS_MODULE, PF_MCTP, RTM_GETADDR,
			     NULL, mctp_dump_addrinfo, 0);
	rtnl_register_module(THIS_MODULE, PF_MCTP, RTM_NEWADDR,
			     mctp_rtm_newaddr, NULL, 0);
	rtnl_af_register(&mctp_af_ops);
}

void __exit mctp_device_exit(void)
{
	rtnl_af_unregister(&mctp_af_ops);
	rtnl_unregister(PF_MCTP, RTM_NEWADDR);
	rtnl_unregister(PF_MCTP, RTM_GETADDR);
	rtnl_unregister(PF_MCTP, RTM_GETLINK);

	unregister_netdevice_notifier(&mctp_dev_nb);
}
