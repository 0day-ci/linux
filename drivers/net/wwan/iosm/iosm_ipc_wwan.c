// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-21 Intel Corporation.
 */

#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/if_link.h>
#include <net/rtnetlink.h>

#include "iosm_ipc_chnl_cfg.h"
#include "iosm_ipc_imem_ops.h"
#include "iosm_ipc_wwan.h"

#define IOSM_IP_TYPE_MASK 0xF0
#define IOSM_IP_TYPE_IPV4 0x40
#define IOSM_IP_TYPE_IPV6 0x60

#define IOSM_IF_ID_PAYLOAD 2

static struct device_type wwan_type = { .name = "wwan" };

static const struct nla_policy ipc_wwan_policy[IFLA_IOSM_MAX + 1] = {
	[IFLA_IOSM_IF_ID]	= { .type = NLA_U16 },
};

/**
 * struct iosm_net_link - This structure includes information about interface
 *			  dev.
 * @if_id:	Interface id for device.
 * @ch_id:	IPC channel number for which interface device is created.
 * @netdev:	Pointer to network interface device structure
 * @ipc_wwan:	Pointer to iosm_wwan struct
 */

struct iosm_net_link {
	int if_id;
	int ch_id;
	struct net_device *netdev;
	struct iosm_wwan *ipc_wwan;
};

/**
 * struct iosm_wwan - This structure contains information about WWAN root device
 *		     and interface to the IPC layer.
 * @netdev:		Pointer to network interface device structure.
 * @sub_netlist:	List of netlink interfaces
 * @ipc_imem:		Pointer to imem data-struct
 * @dev:		Pointer device structure
 * @if_mutex:		Mutex used for add and remove interface id
 * @is_registered:	Registration status with netdev
 */
struct iosm_wwan {
	struct net_device *netdev;
	struct iosm_net_link __rcu *sub_netlist[MAX_IP_MUX_SESSION];
	struct iosm_imem *ipc_imem;
	struct device *dev;
	struct mutex if_mutex; /* Mutex used for add and remove interface id */
	u8 is_registered:1;
};

/* Bring-up the wwan net link */
static int ipc_wwan_link_open(struct net_device *netdev)
{
	struct iosm_net_link *netlink = netdev_priv(netdev);
	struct iosm_wwan *ipc_wwan = netlink->ipc_wwan;
	int if_id = netlink->if_id;
	int ret = -EINVAL;

	if (if_id < IP_MUX_SESSION_START || if_id > IP_MUX_SESSION_END)
		return ret;

	mutex_lock(&ipc_wwan->if_mutex);

	/* get channel id */
	netlink->ch_id =
		ipc_imem_sys_wwan_open(ipc_wwan->ipc_imem, if_id);

	if (netlink->ch_id < 0) {
		dev_err(ipc_wwan->dev,
			"cannot connect wwan0 & id %d to the IPC mem layer",
			if_id);
		ret = -ENODEV;
		goto out;
	}

	/* enable tx path, DL data may follow */
	netif_start_queue(netdev);

	dev_dbg(ipc_wwan->dev, "Channel id %d allocated to if_id %d",
		netlink->ch_id, netlink->if_id);

	ret = 0;
out:
	mutex_unlock(&ipc_wwan->if_mutex);
	return ret;
}

/* Bring-down the wwan net link */
static int ipc_wwan_link_stop(struct net_device *netdev)
{
	struct iosm_net_link *netlink = netdev_priv(netdev);

	netif_stop_queue(netdev);

	mutex_lock(&netlink->ipc_wwan->if_mutex);
	ipc_imem_sys_wwan_close(netlink->ipc_wwan->ipc_imem, netlink->if_id,
				netlink->ch_id);
	mutex_unlock(&netlink->ipc_wwan->if_mutex);

	return 0;
}

/* Transmit a packet */
static int ipc_wwan_link_transmit(struct sk_buff *skb,
				  struct net_device *netdev)
{
	struct iosm_net_link *netlink = netdev_priv(netdev);
	struct iosm_wwan *ipc_wwan = netlink->ipc_wwan;
	int if_id = netlink->if_id;
	int ret = -EINVAL;

	/* Interface IDs from 1 to 8 are for IP data
	 * & from 257 to 261 are for non-IP data
	 */
	if (if_id < IP_MUX_SESSION_START || if_id > IP_MUX_SESSION_END)
		goto exit;

	/* Send the SKB to device for transmission */
	ret = ipc_imem_sys_wwan_transmit(ipc_wwan->ipc_imem,
					 if_id, netlink->ch_id, skb);

	/* Return code of zero is success */
	if (ret == 0) {
		ret = NETDEV_TX_OK;
	} else if (ret == -EBUSY) {
		ret = NETDEV_TX_BUSY;
		dev_err(ipc_wwan->dev, "unable to push packets");
	} else {
		goto exit;
	}

	return ret;

exit:
	/* Log any skb drop */
	if (if_id)
		dev_dbg(ipc_wwan->dev, "skb dropped. IF_ID: %d, ret: %d", if_id,
			ret);

	dev_kfree_skb_any(skb);
	return ret;
}

/* Ops structure for wwan net link */
static const struct net_device_ops ipc_inm_ops = {
	.ndo_open = ipc_wwan_link_open,
	.ndo_stop = ipc_wwan_link_stop,
	.ndo_start_xmit = ipc_wwan_link_transmit,
};

/* Setup function for creating new net link */
static void ipc_wwan_setup(struct net_device *iosm_dev)
{
	iosm_dev->header_ops = NULL;
	iosm_dev->hard_header_len = 0;
	iosm_dev->priv_flags |= IFF_NO_QUEUE;

	iosm_dev->type = ARPHRD_NONE;
	iosm_dev->min_mtu = ETH_MIN_MTU;
	iosm_dev->max_mtu = ETH_MAX_MTU;

	iosm_dev->flags = IFF_POINTOPOINT | IFF_NOARP;

	iosm_dev->netdev_ops = &ipc_inm_ops;
}

static struct device_type inm_type = { .name = "iosmdev" };

/* Create new wwan net link */
static int ipc_wwan_newlink(struct net *src_net, struct net_device *dev,
			    struct nlattr *tb[], struct nlattr *data[],
			    struct netlink_ext_ack *extack)
{
	struct iosm_net_link *netlink = netdev_priv(dev);
	struct iosm_wwan *ipc_wwan;
	struct net_device *netdev;
	int err = -EINVAL;
	int index;

	if (!data[IFLA_IOSM_IF_ID]) {
		pr_err("Interface ID not specified");
		goto out;
	}

	if (!tb[IFLA_LINK]) {
		pr_err("Link not specified");
		goto out;
	}

	netlink->netdev = dev;

	netdev = __dev_get_by_index(src_net, nla_get_u32(tb[IFLA_LINK]));

	netlink->ipc_wwan = netdev_priv(netdev);

	ipc_wwan = netlink->ipc_wwan;

	if (ipc_wwan->netdev != netdev)
		goto out;

	netlink->if_id = nla_get_u16(data[IFLA_IOSM_IF_ID]);
	index = netlink->if_id;

	/* Complete all memory stores before this point */
	smp_mb();
	if (index < IP_MUX_SESSION_START || index > IP_MUX_SESSION_END)
		goto out;

	rcu_read_lock();

	if (rcu_access_pointer(ipc_wwan->sub_netlist[index - 1])) {
		pr_err("IOSM interface ID already in use");
		goto out_free_lock;
	}

	SET_NETDEV_DEVTYPE(dev, &inm_type);

	eth_hw_addr_random(dev);
	err = register_netdevice(dev);
	if (err) {
		dev_err(ipc_wwan->dev, "register netlink failed.\n");
		goto out_free_lock;
	}

	err = netdev_upper_dev_link(ipc_wwan->netdev, dev, extack);

	if (err) {
		dev_err(ipc_wwan->dev, "netdev linking with parent failed.\n");
		goto netlink_err;
	}

	rcu_assign_pointer(ipc_wwan->sub_netlist[index - 1], netlink);
	netif_device_attach(dev);
	rcu_read_unlock();

	return 0;

netlink_err:
	unregister_netdevice(dev);
out_free_lock:
	rcu_read_unlock();
out:
	return err;
}

/* Delete new wwan net link */
static void ipc_wwan_dellink(struct net_device *dev, struct list_head *head)
{
	struct iosm_net_link *netlink = netdev_priv(dev);
	u16 index = netlink->if_id;

	netdev_upper_dev_unlink(netlink->ipc_wwan->netdev, dev);
	unregister_netdevice(dev);

	mutex_lock(&netlink->ipc_wwan->if_mutex);
	rcu_assign_pointer(netlink->ipc_wwan->sub_netlist[index - 1], NULL);
	mutex_unlock(&netlink->ipc_wwan->if_mutex);
}

/* Get size for iosm net link payload*/
static size_t ipc_wwan_get_size(const struct net_device *dev)
{
	return nla_total_size(IOSM_IF_ID_PAYLOAD);
}

/* Validate the input parameters for wwan net link */
static int ipc_wwan_validate(struct nlattr *tb[], struct nlattr *data[],
			     struct netlink_ext_ack *extack)
{
	u16 if_id;

	if (!data || !data[IFLA_IOSM_IF_ID]) {
		NL_SET_ERR_MSG_MOD(extack, "IF ID not specified");
		return -EINVAL;
	}

	if_id = nla_get_u16(data[IFLA_IOSM_IF_ID]);

	if (if_id < IP_MUX_SESSION_START || if_id > IP_MUX_SESSION_END) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid Interface");
		return -ERANGE;
	}

	return 0;
}

/* RT Net link ops structure for new wwan net link */
struct rtnl_link_ops iosm_netlink __read_mostly = {
	.kind           = "iosm",
	.maxtype        = __IFLA_IOSM_MAX,
	.priv_size      = sizeof(struct iosm_net_link),
	.setup          = ipc_wwan_setup,
	.validate       = ipc_wwan_validate,
	.newlink        = ipc_wwan_newlink,
	.dellink        = ipc_wwan_dellink,
	.get_size       = ipc_wwan_get_size,
	.policy         = ipc_wwan_policy,
};

int ipc_wwan_receive(struct iosm_wwan *ipc_wwan, struct sk_buff *skb_arg,
		     bool dss, int if_id)
{
	struct sk_buff *skb = skb_arg;
	struct net_device_stats stats;
	int ret;

	if ((skb->data[0] & IOSM_IP_TYPE_MASK) == IOSM_IP_TYPE_IPV4)
		skb->protocol = htons(ETH_P_IP);
	else if ((skb->data[0] & IOSM_IP_TYPE_MASK) ==
		 IOSM_IP_TYPE_IPV6)
		skb->protocol = htons(ETH_P_IPV6);

	skb->pkt_type = PACKET_HOST;

	if (if_id < (IP_MUX_SESSION_START - 1) ||
	    if_id > (IP_MUX_SESSION_END - 1)) {
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	rcu_read_lock();
	skb->dev = rcu_dereference(ipc_wwan->sub_netlist[if_id])->netdev;
	stats = rcu_dereference(ipc_wwan->sub_netlist[if_id])->netdev->stats;
	stats.rx_packets++;
	stats.rx_bytes += skb->len;

	ret = netif_rx(skb);
	rcu_read_unlock();

	return ret;
}

void ipc_wwan_tx_flowctrl(struct iosm_wwan *ipc_wwan, int if_id, bool on)
{
	struct net_device *netdev;
	bool is_tx_blk;

	rcu_read_lock();
	netdev = rcu_dereference(ipc_wwan->sub_netlist[if_id])->netdev;

	is_tx_blk = netif_queue_stopped(netdev);

	if (on)
		dev_dbg(ipc_wwan->dev, "session id[%d]: flowctrl enable",
			if_id);

	if (on && !is_tx_blk)
		netif_stop_queue(netdev);
	else if (!on && is_tx_blk)
		netif_wake_queue(netdev);
	rcu_read_unlock();
}

static void ipc_netdev_setup(struct net_device *dev) {}

struct iosm_wwan *ipc_wwan_init(struct iosm_imem *ipc_imem, struct device *dev)
{
	static const struct net_device_ops iosm_wwandev_ops = {};
	struct iosm_wwan *ipc_wwan;
	struct net_device *netdev;

	netdev = alloc_netdev(sizeof(*ipc_wwan), "wwan%d", NET_NAME_ENUM,
			      ipc_netdev_setup);

	if (!netdev)
		return NULL;

	ipc_wwan = netdev_priv(netdev);

	ipc_wwan->dev = dev;
	ipc_wwan->netdev = netdev;
	ipc_wwan->is_registered = false;

	ipc_wwan->ipc_imem = ipc_imem;

	mutex_init(&ipc_wwan->if_mutex);

	/* allocate random ethernet address */
	eth_random_addr(netdev->dev_addr);
	netdev->addr_assign_type = NET_ADDR_RANDOM;

	netdev->netdev_ops = &iosm_wwandev_ops;
	netdev->flags |= IFF_NOARP;

	SET_NETDEV_DEVTYPE(netdev, &wwan_type);

	if (register_netdev(netdev)) {
		dev_err(ipc_wwan->dev, "register_netdev failed");
		goto reg_fail;
	}

	ipc_wwan->is_registered = true;

	netif_device_attach(netdev);

	return ipc_wwan;

reg_fail:
	free_netdev(netdev);
	return NULL;
}

void ipc_wwan_deinit(struct iosm_wwan *ipc_wwan)
{
	struct iosm_net_link *netlink;
	int i;

	if (ipc_wwan->is_registered) {
		rcu_read_lock();
		for (i = IP_MUX_SESSION_START; i <= IP_MUX_SESSION_END; i++) {
			if (rcu_access_pointer(ipc_wwan->sub_netlist[i - 1])) {
				netlink =
				rcu_dereference(ipc_wwan->sub_netlist[i - 1]);
				rtnl_lock();
				netdev_upper_dev_unlink(ipc_wwan->netdev,
							netlink->netdev);
				unregister_netdevice(netlink->netdev);
				rtnl_unlock();
				rcu_assign_pointer(ipc_wwan->sub_netlist[i - 1],
						   NULL);
			}
		}
		rcu_read_unlock();

		unregister_netdev(ipc_wwan->netdev);
		free_netdev(ipc_wwan->netdev);
	}
}
