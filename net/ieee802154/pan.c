// SPDX-License-Identifier: GPL-2.0
/*
 * IEEE 802.15.4 PAN management
 *
 * Copyright (C) Qorvo, 2021
 * Authors:
 *   - David Girault <david.girault@qorvo.com>
 *   - Miquel Raynal <miquel.raynal@bootlin.com>
 */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>

#include <net/cfg802154.h>
#include <net/af_ieee802154.h>

#include "ieee802154.h"
#include "core.h"
#include "trace.h"

static struct cfg802154_internal_pan *
cfg802154_alloc_pan(struct ieee802154_pan_desc *desc)
{
	struct cfg802154_internal_pan *new;
	struct ieee802154_addr *coord;

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return ERR_PTR(-ENOMEM);

	coord = kzalloc(sizeof(*coord), GFP_KERNEL);
	if (!coord) {
		kfree(new);
		return ERR_PTR(-ENOMEM);
	}

	new->discovery_ts = jiffies;
	new->desc = *desc;

	*coord = *desc->coord;
	new->desc.coord = coord;

	return new;
}

static void cfg802154_free_pan(struct cfg802154_internal_pan *pan)
{
	kfree(pan->desc.coord);
	kfree(pan);
}

static void cfg802154_unlink_pan(struct cfg802154_registered_device *rdev,
				 struct cfg802154_internal_pan *pan)
{
	lockdep_assert_held(&rdev->pan_lock);

	list_del(&pan->list);
	cfg802154_free_pan(pan);
	rdev->pan_entries--;
	rdev->pan_generation++;
}

static void cfg802154_link_pan(struct cfg802154_registered_device *rdev,
			       struct cfg802154_internal_pan *pan)
{
	lockdep_assert_held(&rdev->pan_lock);

	list_add_tail(&pan->list, &rdev->pan_list);
	rdev->pan_entries++;
	rdev->pan_generation++;
}

void cfg802154_set_max_pan_entries(struct cfg802154_registered_device *rdev,
				   unsigned int max)
{
	lockdep_assert_held(&rdev->pan_lock);

	rdev->max_pan_entries = max;
}
EXPORT_SYMBOL(cfg802154_set_max_pan_entries);

static bool
cfg802154_need_to_expire_pans(struct cfg802154_registered_device *rdev)
{
	if (!rdev->max_pan_entries)
		return false;

	if (rdev->pan_entries > rdev->max_pan_entries)
		return true;

	return false;
}

void cfg802154_set_pans_expiration(struct cfg802154_registered_device *rdev,
				   unsigned int exp_time_s)
{
	lockdep_assert_held(&rdev->pan_lock);

	rdev->pan_expiration = exp_time_s * HZ;
}
EXPORT_SYMBOL(cfg802154_set_pans_expiration);

void cfg802154_expire_pans(struct cfg802154_registered_device *rdev)
{
	struct cfg802154_internal_pan *pan, *tmp;
	unsigned long expiration_time;

	lockdep_assert_held(&rdev->pan_lock);

	if (!rdev->pan_expiration)
		return;

	expiration_time = jiffies - rdev->pan_expiration;
	list_for_each_entry_safe(pan, tmp, &rdev->pan_list, list) {
		if (!time_after(expiration_time, pan->discovery_ts))
			continue;

		cfg802154_unlink_pan(rdev, pan);
	}
}
EXPORT_SYMBOL(cfg802154_expire_pans);

static void cfg802154_expire_oldest_pan(struct cfg802154_registered_device *rdev)
{
	struct cfg802154_internal_pan *pan, *oldest;

	lockdep_assert_held(&rdev->pan_lock);

	if (WARN_ON(list_empty(&rdev->pan_list)))
		return;

	oldest = list_first_entry(&rdev->pan_list,
				  struct cfg802154_internal_pan, list);

	list_for_each_entry(pan, &rdev->pan_list, list) {
		if (!time_before(oldest->discovery_ts, pan->discovery_ts))
			oldest = pan;
	}

	cfg802154_unlink_pan(rdev, oldest);
}

void cfg802154_flush_pans(struct cfg802154_registered_device *rdev)
{
	struct cfg802154_internal_pan *pan, *tmp;

	lockdep_assert_held(&rdev->pan_lock);

	list_for_each_entry_safe(pan, tmp, &rdev->pan_list, list)
		cfg802154_unlink_pan(rdev, pan);
}
EXPORT_SYMBOL(cfg802154_flush_pans);

static bool cfg802154_same_pan(struct ieee802154_pan_desc *a,
			       struct ieee802154_pan_desc *b)
{
	int ret;

	if (a->page != b->page)
		return false;

	if (a->channel != b->channel)
		return false;

	ret = memcmp(&a->coord->pan_id, &b->coord->pan_id,
		     sizeof(a->coord->pan_id));
	if (ret)
		return false;

	if (a->coord->mode != b->coord->mode)
		return false;

	if (a->coord->mode == IEEE802154_ADDR_SHORT)
		ret = memcmp(&a->coord->short_addr, &b->coord->short_addr,
			     IEEE802154_SHORT_ADDR_LEN);
	else
		ret = memcmp(&a->coord->extended_addr, &b->coord->extended_addr,
			     IEEE802154_EXTENDED_ADDR_LEN);

	return true;
}

static struct cfg802154_internal_pan *
cfg802154_find_matching_pan(struct cfg802154_registered_device *rdev,
			    struct cfg802154_internal_pan *tmp)
{
	struct cfg802154_internal_pan *pan;

	list_for_each_entry(pan, &rdev->pan_list, list) {
		if (cfg802154_same_pan(&pan->desc, &tmp->desc))
			return pan;
	}

	return NULL;
}

static void cfg802154_pan_update(struct cfg802154_registered_device *rdev,
				 struct cfg802154_internal_pan *new)
{
	struct cfg802154_internal_pan *found;

	spin_lock_bh(&rdev->pan_lock);

	found = cfg802154_find_matching_pan(rdev, new);
	if (found)
		cfg802154_unlink_pan(rdev, found);
	else
		trace_802154_new_pan(&new->desc);

	if (unlikely(cfg802154_need_to_expire_pans(rdev)))
		cfg802154_expire_oldest_pan(rdev);

	cfg802154_link_pan(rdev, new);

	spin_unlock_bh(&rdev->pan_lock);
}

int cfg802154_record_pan(struct wpan_phy *wpan_phy,
			 struct ieee802154_pan_desc *desc)
{
	struct cfg802154_registered_device *rdev = wpan_phy_to_rdev(wpan_phy);
	struct cfg802154_internal_pan *new;

	new = cfg802154_alloc_pan(desc);
	if (IS_ERR(new))
		return (PTR_ERR(new));

	cfg802154_pan_update(rdev, new);

	return 0;
}
EXPORT_SYMBOL(cfg802154_record_pan);
