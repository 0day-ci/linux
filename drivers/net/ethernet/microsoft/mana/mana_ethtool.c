// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2021, Microsoft Corporation. */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>

#include "mana.h"

static const struct {
	char name[ETH_GSTRING_LEN];
	u16 offset;
} ana_eth_stats[] = {
	{"stop_queue", offsetof(struct ana_ethtool_stats, stop_queue)},
	{"wake_queue", offsetof(struct ana_ethtool_stats, wake_queue)},
};

static int ana_get_sset_count(struct net_device *ndev, int stringset)
{
	struct ana_context *ac = netdev_priv(ndev);
	unsigned int num_queues = ac->num_queues;

	if (stringset != ETH_SS_STATS)
		return -EINVAL;

	return ARRAY_SIZE(ana_eth_stats) + num_queues * 4;
}

static void ana_get_strings(struct net_device *ndev, u32 stringset, u8 *data)
{
	struct ana_context *ac = netdev_priv(ndev);
	unsigned int num_queues = ac->num_queues;
	u8 *p = data;
	int i;

	if (stringset != ETH_SS_STATS)
		return;

	for (i = 0; i < ARRAY_SIZE(ana_eth_stats); i++) {
		memcpy(p, ana_eth_stats[i].name, ETH_GSTRING_LEN);
		p += ETH_GSTRING_LEN;
	}

	for (i = 0; i < num_queues; i++) {
		sprintf(p, "rx_%d_packets", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "rx_%d_bytes", i);
		p += ETH_GSTRING_LEN;
	}

	for (i = 0; i < num_queues; i++) {
		sprintf(p, "tx_%d_packets", i);
		p += ETH_GSTRING_LEN;
		sprintf(p, "tx_%d_bytes", i);
		p += ETH_GSTRING_LEN;
	}
}

static void ana_get_ethtool_stats(struct net_device *ndev,
				  struct ethtool_stats *e_stats, u64 *data)
{
	struct ana_context *ac = netdev_priv(ndev);
	unsigned int num_queues = ac->num_queues;
	void *eth_stats = &ac->eth_stats;
	struct ana_stats *stats;
	unsigned int start;
	u64 packets, bytes;
	int q, i = 0;

	for (q = 0; q < ARRAY_SIZE(ana_eth_stats); q++)
		data[i++] = *(u64 *)(eth_stats + ana_eth_stats[q].offset);

	for (q = 0; q < num_queues; q++) {
		stats = &ac->rxqs[q]->stats;

		do {
			start = u64_stats_fetch_begin_irq(&stats->syncp);
			packets = stats->packets;
			bytes = stats->bytes;
		} while (u64_stats_fetch_retry_irq(&stats->syncp, start));

		data[i++] = packets;
		data[i++] = bytes;
	}

	for (q = 0; q < num_queues; q++) {
		stats = &ac->tx_qp[q].txq.stats;

		do {
			start = u64_stats_fetch_begin_irq(&stats->syncp);
			packets = stats->packets;
			bytes = stats->bytes;
		} while (u64_stats_fetch_retry_irq(&stats->syncp, start));

		data[i++] = packets;
		data[i++] = bytes;
	}
}

static int ana_get_rxnfc(struct net_device *ndev, struct ethtool_rxnfc *cmd,
			 u32 *rules)
{
	struct ana_context *ac = netdev_priv(ndev);

	switch (cmd->cmd) {
	case ETHTOOL_GRXRINGS:
		cmd->data = ac->num_queues;
		return 0;
	}

	return -EOPNOTSUPP;
}

static u32 ana_get_rxfh_key_size(struct net_device *ndev)
{
	return ANA_HASH_KEY_SIZE;
}

static u32 ana_rss_indir_size(struct net_device *ndev)
{
	return ANA_INDIRECT_TABLE_SIZE;
}

static int ana_get_rxfh(struct net_device *ndev, u32 *indir, u8 *key, u8 *hfunc)
{
	struct ana_context *ac = netdev_priv(ndev);
	int i;

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP; /* Toeplitz */

	if (indir) {
		for (i = 0; i < ANA_INDIRECT_TABLE_SIZE; i++)
			indir[i] = ac->ind_table[i];
	}

	if (key)
		memcpy(key, ac->hashkey, ANA_HASH_KEY_SIZE);

	return 0;
}

static int ana_set_rxfh(struct net_device *ndev, const u32 *indir,
			const u8 *key, const u8 hfunc)
{
	struct ana_context *ac = netdev_priv(ndev);
	bool update_hash = false, update_table = false;
	u32 save_table[ANA_INDIRECT_TABLE_SIZE];
	u8 save_key[ANA_HASH_KEY_SIZE];
	int i, err;

	if (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP)
		return -EOPNOTSUPP;

	if (indir) {
		for (i = 0; i < ANA_INDIRECT_TABLE_SIZE; i++)
			if (indir[i] >= ac->num_queues)
				return -EINVAL;

		update_table = true;
		for (i = 0; i < ANA_INDIRECT_TABLE_SIZE; i++) {
			save_table[i] = ac->ind_table[i];
			ac->ind_table[i] = indir[i];
		}
	}

	if (key) {
		update_hash = true;
		memcpy(save_key, ac->hashkey, ANA_HASH_KEY_SIZE);
		memcpy(ac->hashkey, key, ANA_HASH_KEY_SIZE);
	}

	err = ana_config_rss(ac, TRI_STATE_TRUE, update_hash, update_table);

	if (err) { /* recover to original values */
		if (update_table) {
			for (i = 0; i < ANA_INDIRECT_TABLE_SIZE; i++)
				ac->ind_table[i] = save_table[i];
		}

		if (update_hash)
			memcpy(ac->hashkey, save_key, ANA_HASH_KEY_SIZE);

		ana_config_rss(ac, TRI_STATE_TRUE, update_hash, update_table);
	}

	return err;
}

static int ana_attach(struct net_device *ndev)
{
	struct ana_context *ac = netdev_priv(ndev);
	int err;

	ASSERT_RTNL();

	err = ana_do_attach(ndev, false);
	if (err)
		return err;

	netif_device_attach(ndev);

	ac->port_is_up = ac->port_st_save;
	ac->start_remove = false;

	/* Ensure port state updated before txq state */
	smp_wmb();

	if (ac->port_is_up) {
		netif_carrier_on(ndev);
		netif_tx_wake_all_queues(ndev);
	}

	return 0;
}

static void ana_get_channels(struct net_device *ndev,
			     struct ethtool_channels *channel)
{
	struct ana_context *ac = netdev_priv(ndev);

	channel->max_combined = ac->max_queues;
	channel->combined_count = ac->num_queues;
}

static int ana_set_channels(struct net_device *ndev,
			    struct ethtool_channels *channels)
{
	struct ana_context *ac = netdev_priv(ndev);
	unsigned int count = channels->combined_count;
	unsigned int orig = ac->num_queues;
	int err;

	if (count < 1 || count > ac->max_queues || channels->rx_count ||
	    channels->tx_count || channels->other_count)
		return -EINVAL;

	err = ana_detach(ndev);

	if (err) {
		pr_err("ana_detach failed: %d\n", err);
		return err;
	}

	/* change #queues */
	ac->num_queues = count;

	err = ana_attach(ndev);

	if (err) {
		pr_err("ana_attach failed: %d\n", err);

		ac->num_queues = orig;
		err = ana_attach(ndev);

		if (err)
			pr_err("Set channel recovery failed: %d\n", err);
	}

	return err;
}

const struct ethtool_ops ana_ethtool_ops = {
	.get_ethtool_stats = ana_get_ethtool_stats,
	.get_sset_count = ana_get_sset_count,
	.get_strings = ana_get_strings,
	.get_rxnfc = ana_get_rxnfc,
	.get_rxfh_key_size = ana_get_rxfh_key_size,
	.get_rxfh_indir_size = ana_rss_indir_size,
	.get_rxfh = ana_get_rxfh,
	.set_rxfh = ana_set_rxfh,
	.get_channels = ana_get_channels,
	.set_channels = ana_set_channels,
};
