// SPDX-License-Identifier: GPL-2.0-only
/*
 * IEEE 802.15.4 scanning management
 *
 * Copyright (C) Qorvo, 2021
 * Authors:
 *   - David Girault <david.girault@qorvo.com>
 *   - Miquel Raynal <miquel.raynal@bootlin.com>
 */

#include <linux/module.h>
#include <linux/random.h>
#include <linux/rtnetlink.h>
#include <net/mac802154.h>

#include "ieee802154_i.h"
#include "driver-ops.h"
#include "../ieee802154/nl802154.h"

#define IEEE802154_BEACON_MHR_SZ 13
#define IEEE802154_BEACON_PL_SZ 4
#define IEEE802154_BEACON_REQ_MHR_SZ 7
#define IEEE802154_BEACON_REQ_PL_SZ 1
#define IEEE802154_CRC_SZ 2
#define IEEE802154_BEACON_SKB_SZ (IEEE802154_BEACON_MHR_SZ + \
				  IEEE802154_BEACON_PL_SZ + \
				  IEEE802154_CRC_SZ)
#define IEEE802154_BEACON_REQ_SKB_SZ (IEEE802154_BEACON_REQ_MHR_SZ + \
				      IEEE802154_BEACON_REQ_PL_SZ +  \
				      IEEE802154_CRC_SZ)

static bool mac802154_check_promiscuous(struct ieee802154_local *local)
{
	struct ieee802154_sub_if_data *sdata;
	bool promiscuous_on = false;

	/* Check if one subif is already in promiscuous mode. Since the list is
	 * protected by its own mutex, take it here to ensure no modification
	 * occurs during the check.
	 */
	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (ieee802154_sdata_running(sdata) &&
		    sdata->wpan_dev.promiscuous_mode) {
			/* At least one is in promiscuous mode */
			promiscuous_on = true;
			break;
		}
	}
	mutex_unlock(&local->iflist_mtx);
	return promiscuous_on;
}

static int mac802154_set_promiscuous_mode(struct ieee802154_local *local,
					  bool state)
{
	bool promiscuous_on = mac802154_check_promiscuous(local);
	int ret;

	if ((state && promiscuous_on) || (!state && !promiscuous_on))
		return 0;

	ret = drv_set_promiscuous_mode(local, state);
	if (ret)
		pr_err("Failed to %s promiscuous mode for SW scanning",
		       state ? "set" : "reset");

	return ret;
}

static int mac802154_send_scan_done(struct ieee802154_local *local)
{
	struct cfg802154_registered_device *rdev;
	struct cfg802154_scan_request *scan_req;
	struct wpan_dev *wpan_dev;

	scan_req = rcu_dereference_protected(local->scan_req,
					     lockdep_is_held(&local->scan_lock));
	rdev = wpan_phy_to_rdev(scan_req->wpan_phy);
	wpan_dev = scan_req->wpan_dev;

	return nl802154_send_scan_done(rdev, wpan_dev);
}

static int mac802154_end_of_scan(struct ieee802154_local *local)
{
	drv_set_channel(local, local->phy->current_page,
			local->phy->current_channel);
	ieee802154_set_symbol_duration(local->phy);
	atomic_set(&local->scanning, 0);
	mac802154_set_promiscuous_mode(local, false);
	ieee802154_wake_queue(&local->hw);

	return mac802154_send_scan_done(local);
}

int mac802154_abort_scan_locked(struct ieee802154_local *local)
{
	lockdep_assert_held(&local->scan_lock);

	if (!mac802154_scan_is_ongoing(local))
		return -ESRCH;

	cancel_delayed_work(&local->scan_work);

	drv_exit_scan_mode(local);

	return mac802154_end_of_scan(local);
}

static unsigned int mac802154_scan_get_channel_time(u8 duration_order,
						    u8 symbol_duration)
{
	u64 base_super_frame_duration = (u64)symbol_duration *
		IEEE802154_SUPERFRAME_PERIOD * IEEE802154_SLOT_PERIOD;

	return usecs_to_jiffies(base_super_frame_duration *
				(BIT(duration_order) + 1));
}

int mac802154_send_beacons_locked(struct ieee802154_sub_if_data *sdata,
				  struct cfg802154_beacons_request *request)
{
	struct ieee802154_local *local = sdata->local;
	int ret;

	lockdep_assert_held(&local->beacons_lock);

	if (local->ongoing_beacons_request)
		return -EBUSY;

	local->ongoing_beacons_request = true;

	/* Either let the hardware handle the beacons or handle them manually */
	ret = drv_enter_beacons_mode(local, request);
	if (ret) {
		local->ongoing_beacons_request = false;
		return ret;
	}

	memset(&local->beacon, 0, sizeof(local->beacon));
	local->beacon.mhr.fc.type = IEEE802154_BEACON_FRAME;
	local->beacon.mhr.fc.security_enabled = 0;
	local->beacon.mhr.fc.frame_pending = 0;
	local->beacon.mhr.fc.ack_request = 0;
	local->beacon.mhr.fc.intra_pan = 0;
	local->beacon.mhr.fc.dest_addr_mode = IEEE802154_NO_ADDRESSING;
	local->beacon.mhr.fc.version = IEEE802154_2003_STD;
	local->beacon.mhr.fc.source_addr_mode = IEEE802154_EXTENDED_ADDRESSING;
	atomic_set(&request->wpan_dev->bsn, -1);
	local->beacon.mhr.source.mode = IEEE802154_ADDR_LONG;
	local->beacon.mhr.source.pan_id = cpu_to_le16(request->wpan_dev->pan_id);
	local->beacon.mhr.source.extended_addr = cpu_to_le64(request->wpan_dev->extended_addr);
	local->beacon.mac_pl.beacon_order = request->interval;
	if (request->interval <= IEEE802154_MAX_SCAN_DURATION)
		local->beacon.mac_pl.superframe_order = request->interval;
	local->beacon.mac_pl.final_cap_slot = 0xf;
	local->beacon.mac_pl.battery_life_ext = 0;
	local->beacon.mac_pl.pan_coordinator = 1;
	local->beacon.mac_pl.assoc_permit = 1;

	rcu_assign_pointer(local->beacons_sdata, sdata);

	if (request->interval == IEEE802154_ACTIVE_SCAN_DURATION) {
		local->beacons_interval = -1;
		return 0;
	}

	/* Start the beacon work */
	local->beacons_interval =
		mac802154_scan_get_channel_time(request->interval,
						request->wpan_phy->symbol_duration);
	ieee802154_queue_delayed_work(&local->hw, &local->beacons_work, 0);

	return 0;
}

int mac802154_stop_beacons_locked(struct ieee802154_local *local)
{
	lockdep_assert_held(&local->beacons_lock);

	if (!local->ongoing_beacons_request)
		return -ESRCH;

	local->ongoing_beacons_request = false;

	if (local->beacons_interval >= 0)
		cancel_delayed_work(&local->beacons_work);

	drv_exit_beacons_mode(local);

	return 0;
}

static int mac802154_scan_prepare_beacon_req(struct ieee802154_local *local)
{
	memset(&local->beacon_req, 0, sizeof(local->beacon_req));
	local->beacon_req.mhr.fc.type = IEEE802154_FC_TYPE_MAC_CMD;
	local->beacon_req.mhr.fc.dest_addr_mode = IEEE802154_SHORT_ADDRESSING;
	local->beacon_req.mhr.fc.version = IEEE802154_2003_STD;
	local->beacon_req.mhr.fc.source_addr_mode = IEEE802154_NO_ADDRESSING;
	local->beacon_req.mhr.dest.mode = IEEE802154_ADDR_SHORT;
	local->beacon_req.mhr.dest.pan_id = cpu_to_le16(IEEE802154_PANID_BROADCAST);
	local->beacon_req.mhr.dest.short_addr = cpu_to_le16(IEEE802154_ADDR_BROADCAST);
	local->beacon_req.mac_pl.cmd_id = IEEE802154_CMD_BEACON_REQ;

	return 0;
}

static int mac802154_scan_send_beacon_req_locked(struct ieee802154_local *local)
{
	struct sk_buff *skb;
	int ret;

	lockdep_assert_held(&local->scan_lock);

	skb = alloc_skb(IEEE802154_BEACON_REQ_SKB_SZ, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	ret = ieee802154_beacon_req_push(skb, &local->beacon_req);
	if (ret) {
		kfree_skb(skb);
		return ret;
	}

	return drv_xmit_async(local, skb);
}

static int mac802154_scan_send_beacon_locked(struct ieee802154_local *local,
					     struct wpan_dev *wpan_dev)
{
	struct sk_buff *skb;
	int ret;

	lockdep_assert_held(&local->beacons_lock);

	/* Update the sequence number */
	local->beacon.mhr.seq = atomic_inc_return(&wpan_dev->bsn);

	skb = alloc_skb(IEEE802154_BEACON_SKB_SZ, GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	ret = ieee802154_beacon_push(skb, &local->beacon);
	if (ret) {
		kfree_skb(skb);
		return ret;
	}

	return drv_xmit_async(local, skb);
}

void mac802154_scan_work(struct work_struct *work)
{
	struct ieee802154_local *local =
		container_of(work, struct ieee802154_local, scan_work.work);
	struct cfg802154_scan_request *scan_req;
	struct ieee802154_sub_if_data *sdata;
	unsigned int scan_duration;
	bool end_of_scan = false;
	unsigned long chan;
	int ret;

	mutex_lock(&local->scan_lock);

	if (!mac802154_scan_is_ongoing(local))
		goto unlock_mutex;

	sdata = rcu_dereference_protected(local->scan_sdata,
					  lockdep_is_held(&local->scan_lock));
	scan_req = rcu_dereference_protected(local->scan_req,
					     lockdep_is_held(&local->scan_lock));

	if (local->suspended || !ieee802154_sdata_running(sdata))
		goto queue_work;

	do {
		chan = find_next_bit((const unsigned long *)&scan_req->channels,
				     IEEE802154_MAX_CHANNEL + 1,
				     local->scan_channel_idx + 1);

		/* If there are no more channels left, complete the scan */
		if (chan > IEEE802154_MAX_CHANNEL) {
			end_of_scan = true;
			goto unlock_mutex;
		}

		/* Channel switch cannot be made atomic so hide the chan number
		 * in order to prevent beacon processing during this timeframe.
		 */
		local->scan_channel_idx = -1;
		/* Bypass the stack on purpose */
		ret = drv_set_channel(local, scan_req->page, chan);
		local->scan_channel_idx = chan;
		ieee802154_set_symbol_duration(local->phy);
	} while (ret);

	if (scan_req->type == NL802154_SCAN_ACTIVE)
		mac802154_scan_send_beacon_req_locked(local);

queue_work:
	scan_duration = mac802154_scan_get_channel_time(scan_req->duration,
							local->phy->symbol_duration);
	pr_debug("Scan channel %lu of page %u for %ums\n",
		 chan, scan_req->page, jiffies_to_msecs(scan_duration));
	ieee802154_queue_delayed_work(&local->hw, &local->scan_work,
				      scan_duration);

unlock_mutex:
	if (end_of_scan)
		mac802154_end_of_scan(local);

	mutex_unlock(&local->scan_lock);
}

int mac802154_trigger_scan_locked(struct ieee802154_sub_if_data *sdata,
				  struct cfg802154_scan_request *request)
{
	struct ieee802154_local *local = sdata->local;
	int ret;

	lockdep_assert_held(&local->scan_lock);

	if (mac802154_scan_is_ongoing(local))
		return -EBUSY;

	if (request->type != NL802154_SCAN_PASSIVE &&
	    request->type != NL802154_SCAN_ACTIVE)
		return -EOPNOTSUPP;

	/* Store scanning parameters */
	rcu_assign_pointer(local->scan_req, request);
	rcu_assign_pointer(local->scan_sdata, sdata);

	/* Configure scan_addr to use net_device addr or random */
	if (request->flags & NL802154_SCAN_FLAG_RANDOM_ADDR)
		get_random_bytes(&local->scan_addr, sizeof(local->scan_addr));
	else
		local->scan_addr = cpu_to_le64(get_unaligned_be64(sdata->dev->dev_addr));

	/* Let the drivers know  about the starting scanning operation */
	ret = drv_enter_scan_mode(local, request);
	if (ret)
		return ret;

	if (request->type == NL802154_SCAN_ACTIVE)
		mac802154_scan_prepare_beacon_req(local);

	local->scan_channel_idx = -1;
	atomic_set(&local->scanning, 1);

	/* Software scanning requires to set promiscuous mode, so we need to
	 * pause the Tx queue
	 */
	ieee802154_stop_queue(&local->hw);
	ret = mac802154_set_promiscuous_mode(local, true);
	if (ret)
		return mac802154_end_of_scan(local);

	ieee802154_queue_delayed_work(&local->hw, &local->scan_work, 0);

	return 0;
}

void mac802154_beacons_work(struct work_struct *work)
{
	struct ieee802154_local *local =
		container_of(work, struct ieee802154_local, beacons_work.work);
	struct ieee802154_sub_if_data *sdata;
	struct wpan_dev *wpan_dev;
	int ret;

	mutex_lock(&local->beacons_lock);

	if (!local->ongoing_beacons_request)
		goto unlock_mutex;

	if (local->suspended)
		goto queue_work;

	sdata = rcu_dereference_protected(local->beacons_sdata,
					  lockdep_is_held(&local->beacons_lock));
	wpan_dev = &sdata->wpan_dev;

	ret = mac802154_scan_send_beacon_locked(local, wpan_dev);
	if (ret)
		pr_err("Error when transmitting beacon (%d)\n", ret);

queue_work:
	if (local->beacons_interval >= 0)
		ieee802154_queue_delayed_work(&local->hw, &local->beacons_work,
					      local->beacons_interval);

unlock_mutex:
	mutex_unlock(&local->beacons_lock);
}

int mac802154_scan_process_beacon(struct ieee802154_local *local,
				  struct sk_buff *skb)
{
	struct ieee802154_beacon_hdr *bh = (void *)skb->data;
	struct ieee802154_addr *src = &mac_cb(skb)->source;
	struct cfg802154_scan_request *scan_req;
	struct ieee802154_pan_desc desc = {};
	int ret;

	/* Check the validity of the frame length */
	if (skb->len < sizeof(*bh))
		return -EINVAL;

	if (unlikely(src->mode == IEEE802154_ADDR_NONE))
		return -EINVAL;

	if (unlikely(!bh->pan_coordinator))
		return -ENODEV;

	scan_req = rcu_dereference(local->scan_req);
	if (unlikely(!scan_req))
		return -EINVAL;

	if (unlikely(local->scan_channel_idx < 0)) {
		pr_info("Dropping beacon received during channel change\n");
		return 0;
	}

	pr_debug("Beacon received on channel %d of page %d\n",
		 local->scan_channel_idx, scan_req->page);

	/* Parse beacon and create PAN information */
	desc.coord = src;
	desc.page = scan_req->page;
	desc.channel = local->scan_channel_idx;
	desc.link_quality = mac_cb(skb)->lqi;
	desc.superframe_spec = get_unaligned_le16(skb->data);
	desc.gts_permit = bh->gts_permit;

	/* Create or update the PAN entry in the management layer */
	ret = cfg802154_record_pan(local->phy, &desc);
	if (ret) {
		pr_err("Failed to save PAN descriptor\n");
		return ret;
	}

	return 0;
}
