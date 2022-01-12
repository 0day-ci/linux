/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __IEEE802154_CORE_H
#define __IEEE802154_CORE_H

#include <net/cfg802154.h>

struct cfg802154_registered_device {
	const struct cfg802154_ops *ops;
	struct list_head list;

	/* wpan_phy index, internal only */
	int wpan_phy_idx;

	/* also protected by devlist_mtx */
	int opencount;
	wait_queue_head_t dev_wait;

	/* protected by RTNL only */
	int num_running_ifaces;

	/* associated wpan interfaces, protected by rtnl or RCU */
	struct list_head wpan_dev_list;
	int devlist_generation, wpan_dev_id;

	/* pan management */
	spinlock_t pan_lock;
	struct list_head pan_list;
	unsigned int max_pan_entries;
	unsigned int pan_expiration;
	unsigned int pan_entries;
	unsigned int pan_generation;

	/* scanning */
	struct cfg802154_scan_request *scan_req;

	/* must be last because of the way we do wpan_phy_priv(),
	 * and it should at least be aligned to NETDEV_ALIGN
	 */
	struct wpan_phy wpan_phy __aligned(NETDEV_ALIGN);
};

static inline struct cfg802154_registered_device *
wpan_phy_to_rdev(struct wpan_phy *wpan_phy)
{
	BUG_ON(!wpan_phy);
	return container_of(wpan_phy, struct cfg802154_registered_device,
			    wpan_phy);
}

extern struct list_head cfg802154_rdev_list;
extern int cfg802154_rdev_list_generation;

struct cfg802154_internal_pan {
	struct list_head list;
	unsigned long discovery_ts;
	struct ieee802154_pan_desc desc;
};

/* Always update the list by dropping the expired PANs before iterating */
#define ieee802154_for_each_pan(pan, rdev)				\
	cfg802154_expire_pans(rdev);					\
	list_for_each_entry((pan), &(rdev)->pan_list, list)

int cfg802154_switch_netns(struct cfg802154_registered_device *rdev,
			   struct net *net);
/* free object */
void cfg802154_dev_free(struct cfg802154_registered_device *rdev);
struct cfg802154_registered_device *
cfg802154_rdev_by_wpan_phy_idx(int wpan_phy_idx);
struct wpan_phy *wpan_phy_idx_to_wpan_phy(int wpan_phy_idx);

u32 cfg802154_get_supported_chans(struct wpan_phy *phy, unsigned int page);

void cfg802154_set_max_pan_entries(struct cfg802154_registered_device *rdev,
				   unsigned int max);
void cfg802154_set_pans_expiration(struct cfg802154_registered_device *rdev,
				   unsigned int exp_time_s);
void cfg802154_expire_pans(struct cfg802154_registered_device *rdev);
void cfg802154_flush_pans(struct cfg802154_registered_device *rdev);

#endif /* __IEEE802154_CORE_H */
