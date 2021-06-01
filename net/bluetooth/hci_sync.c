// SPDX-License-Identifier: GPL-2.0
/*
 * BlueZ - Bluetooth protocol stack for Linux
 *
 * Copyright (C) 2021 Intel Corporation
 */

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/mgmt.h>

#include "hci_request.h"
#include "hci_sync.h"
#include "smp.h"
#include "eir.h"

struct sk_buff *__hci_cmd_sync_ev(struct hci_dev *hdev, u16 opcode, u32 plen,
				  const void *param, u8 event, u32 timeout)
{
	struct hci_request req;
	struct sk_buff *skb;
	int err = 0;

	bt_dev_dbg(hdev, "");

	hci_req_init(&req, hdev);

	hci_req_add_ev(&req, opcode, plen, param, event);

	hdev->req_status = HCI_REQ_PEND;

	err = hci_req_run_skb(&req, hci_req_sync_complete);
	if (err < 0)
		return ERR_PTR(err);

	err = wait_event_interruptible_timeout(hdev->req_wait_q,
			hdev->req_status != HCI_REQ_PEND, timeout);

	if (err == -ERESTARTSYS)
		return ERR_PTR(-EINTR);

	switch (hdev->req_status) {
	case HCI_REQ_DONE:
		err = -bt_to_errno(hdev->req_result);
		break;

	case HCI_REQ_CANCELED:
		err = -hdev->req_result;
		break;

	default:
		err = -ETIMEDOUT;
		break;
	}

	hdev->req_status = hdev->req_result = 0;
	skb = hdev->req_skb;
	hdev->req_skb = NULL;

	bt_dev_dbg(hdev, "end: err %d", err);

	if (err < 0) {
		kfree_skb(skb);
		return ERR_PTR(err);
	}

	if (!skb)
		return ERR_PTR(-ENODATA);

	return skb;
}
EXPORT_SYMBOL(__hci_cmd_sync_ev);

struct sk_buff *__hci_cmd_sync(struct hci_dev *hdev, u16 opcode, u32 plen,
			       const void *param, u32 timeout)
{
	return __hci_cmd_sync_ev(hdev, opcode, plen, param, 0, timeout);
}
EXPORT_SYMBOL(__hci_cmd_sync);

u8 __hci_cmd_sync_status(struct hci_dev *hdev, u16 opcode, u32 plen,
			  const void *param, u32 timeout)
{
	struct sk_buff *skb;
	uint8_t status;

	skb = __hci_cmd_sync(hdev, opcode, plen, param, timeout);
	if (IS_ERR(skb)) {
		switch (PTR_ERR(skb)) {
		case -ETIMEDOUT:
			return HCI_ERROR_CONNECTION_TIMEOUT;
		default:
			return hdev->req_status;
		}
	}

	status = skb->data[0];

	kfree_skb(skb);

	return status;
}
EXPORT_SYMBOL(__hci_cmd_sync_status);

int hci_update_eir_sync(struct hci_dev *hdev)
{
	struct hci_cp_write_eir cp;

	bt_dev_dbg(hdev, "");

	if (!hdev_is_powered(hdev))
		return 0;

	if (!lmp_ext_inq_capable(hdev))
		return 0;

	if (!hci_dev_test_flag(hdev, HCI_SSP_ENABLED))
		return 0;

	if (hci_dev_test_flag(hdev, HCI_SERVICE_CACHE))
		return 0;

	memset(&cp, 0, sizeof(cp));

	eir_create(hdev, cp.data);

	if (memcmp(cp.data, hdev->eir, sizeof(cp.data)) == 0)
		return 0;

	memcpy(hdev->eir, cp.data, sizeof(cp.data));

	return __hci_cmd_sync_status(hdev, HCI_OP_WRITE_EIR, sizeof(cp), &cp,
				     HCI_CMD_TIMEOUT);
}

static u8 get_service_classes(struct hci_dev *hdev)
{
	struct bt_uuid *uuid;
	u8 val = 0;

	list_for_each_entry(uuid, &hdev->uuids, list)
		val |= uuid->svc_hint;

	return val;
}

int hci_update_class_sync(struct hci_dev *hdev)
{
	u8 cod[3];

	bt_dev_dbg(hdev, "");

	if (!hdev_is_powered(hdev))
		return 0;

	if (!hci_dev_test_flag(hdev, HCI_BREDR_ENABLED))
		return 0;

	if (hci_dev_test_flag(hdev, HCI_SERVICE_CACHE))
		return 0;

	cod[0] = hdev->minor_class;
	cod[1] = hdev->major_class;
	cod[2] = get_service_classes(hdev);

	if (hci_dev_test_flag(hdev, HCI_LIMITED_DISCOVERABLE))
		cod[1] |= 0x20;

	if (memcmp(cod, hdev->dev_class, 3) == 0)
		return 0;

	return __hci_cmd_sync_status(hdev, HCI_OP_WRITE_CLASS_OF_DEV,
				     sizeof(cod), cod, HCI_CMD_TIMEOUT);
}

static bool is_advertising_allowed(struct hci_dev *hdev, bool connectable)
{
	/* If there is no connection we are OK to advertise. */
	if (hci_conn_num(hdev, LE_LINK) == 0)
		return true;

	/* Check le_states if there is any connection in slave role. */
	if (hdev->conn_hash.le_num_slave > 0) {
		/* Slave connection state and non connectable mode bit 20. */
		if (!connectable && !(hdev->le_states[2] & 0x10))
			return false;

		/* Slave connection state and connectable mode bit 38
		 * and scannable bit 21.
		 */
		if (connectable && (!(hdev->le_states[4] & 0x40) ||
				    !(hdev->le_states[2] & 0x20)))
			return false;
	}

	/* Check le_states if there is any connection in master role. */
	if (hci_conn_num(hdev, LE_LINK) != hdev->conn_hash.le_num_slave) {
		/* Master connection state and non connectable mode bit 18. */
		if (!connectable && !(hdev->le_states[2] & 0x02))
			return false;

		/* Master connection state and connectable mode bit 35 and
		 * scannable 19.
		 */
		if (connectable && (!(hdev->le_states[4] & 0x08) ||
				    !(hdev->le_states[2] & 0x08)))
			return false;
	}

	return true;
}

static bool adv_use_rpa(struct hci_dev *hdev, uint32_t flags)
{
	/* If privacy is not enabled don't use RPA */
	if (!hci_dev_test_flag(hdev, HCI_PRIVACY))
		return false;

	/* If basic privacy mode is enabled use RPA */
	if (!hci_dev_test_flag(hdev, HCI_LIMITED_PRIVACY))
		return true;

	/* If limited privacy mode is enabled don't use RPA if we're
	 * both discoverable and bondable.
	 */
	if ((flags & MGMT_ADV_FLAG_DISCOV) &&
	    hci_dev_test_flag(hdev, HCI_BONDABLE))
		return false;

	/* We're neither bondable nor discoverable in the limited
	 * privacy mode, therefore use RPA.
	 */
	return true;
}

static int hci_set_random_addr_sync(struct hci_dev *hdev, bdaddr_t *rpa)
{
	/* If we're advertising or initiating an LE connection we can't
	 * go ahead and change the random address at this time. This is
	 * because the eventual initiator address used for the
	 * subsequently created connection will be undefined (some
	 * controllers use the new address and others the one we had
	 * when the operation started).
	 *
	 * In this kind of scenario skip the update and let the random
	 * address be updated at the next cycle.
	 */
	if (hci_dev_test_flag(hdev, HCI_LE_ADV) ||
	    hci_lookup_le_connect(hdev)) {
		bt_dev_dbg(hdev, "Deferring random address update");
		hci_dev_set_flag(hdev, HCI_RPA_EXPIRED);
		return 0;
	}

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_RANDOM_ADDR,
				     6, rpa, HCI_CMD_TIMEOUT);
}

int hci_update_random_address_sync(struct hci_dev *hdev, bool require_privacy,
				   bool rpa, u8 *own_addr_type)
{
	int err;

	/* If privacy is enabled use a resolvable private address. If
	 * current RPA has expired or there is something else than
	 * the current RPA in use, then generate a new one.
	 */
	if (rpa) {
		int to;

		/* If Controller supports LL Privacy use own address type is
		 * 0x03
		 */
		if (use_ll_privacy(hdev) &&
		    hci_dev_test_flag(hdev, HCI_ENABLE_LL_PRIVACY))
			*own_addr_type = ADDR_LE_DEV_RANDOM_RESOLVED;
		else
			*own_addr_type = ADDR_LE_DEV_RANDOM;

		if (!hci_dev_test_and_clear_flag(hdev, HCI_RPA_EXPIRED) &&
		    !bacmp(&hdev->random_addr, &hdev->rpa))
			return 0;

		err = smp_generate_rpa(hdev, hdev->irk, &hdev->rpa);
		if (err < 0) {
			bt_dev_err(hdev, "failed to generate new RPA");
			return err;
		}

		err = hci_set_random_addr_sync(hdev, &hdev->rpa);
		if (err)
			return err;

		to = msecs_to_jiffies(hdev->rpa_timeout * 1000);
		queue_delayed_work(hdev->workqueue, &hdev->rpa_expired, to);

		return 0;
	}

	/* In case of required privacy without resolvable private address,
	 * use an non-resolvable private address. This is useful for active
	 * scanning and non-connectable advertising.
	 */
	if (require_privacy) {
		bdaddr_t nrpa;

		while (true) {
			/* The non-resolvable private address is generated
			 * from random six bytes with the two most significant
			 * bits cleared.
			 */
			get_random_bytes(&nrpa, 6);
			nrpa.b[5] &= 0x3f;

			/* The non-resolvable private address shall not be
			 * equal to the public address.
			 */
			if (bacmp(&hdev->bdaddr, &nrpa))
				break;
		}

		*own_addr_type = ADDR_LE_DEV_RANDOM;

		return hci_set_random_addr_sync(hdev, &hdev->rpa);
	}

	/* If forcing static address is in use or there is no public
	 * address use the static address as random address (but skip
	 * the HCI command if the current random address is already the
	 * static one.
	 *
	 * In case BR/EDR has been disabled on a dual-mode controller
	 * and a static address has been configured, then use that
	 * address instead of the public BR/EDR address.
	 */
	if (hci_dev_test_flag(hdev, HCI_FORCE_STATIC_ADDR) ||
	    !bacmp(&hdev->bdaddr, BDADDR_ANY) ||
	    (!hci_dev_test_flag(hdev, HCI_BREDR_ENABLED) &&
	     bacmp(&hdev->static_addr, BDADDR_ANY))) {
		*own_addr_type = ADDR_LE_DEV_RANDOM;
		if (bacmp(&hdev->static_addr, &hdev->random_addr))
			return hci_set_random_addr_sync(hdev,
							  &hdev->static_addr);
		return 0;
	}

	/* Neither privacy nor static address is being used so use a
	 * public address.
	 */
	*own_addr_type = ADDR_LE_DEV_PUBLIC;

	return 0;
}

int hci_setup_ext_adv_instance_sync(struct hci_dev *hdev, u8 instance)
{
	struct hci_cp_le_set_ext_adv_params cp;
	bool connectable;
	u32 flags;
	bdaddr_t random_addr;
	u8 own_addr_type;
	int err;
	struct adv_info *adv;
	bool secondary_adv;

	if (instance > 0) {
		adv = hci_find_adv_instance(hdev, instance);
		if (!adv)
			return -EINVAL;
	} else {
		adv = NULL;
	}

	/* Updating parameters of an active instance will return a
	 * Command Disallowed error, so we must first disable the
	 * instance if it is active.
	 */
	if (adv && !adv->pending) {
		err = hci_disable_ext_adv_instance_sync(hdev, instance);
		if (err)
			return err;
	}

	flags = hci_adv_instance_flags(hdev, instance);

	/* If the "connectable" instance flag was not set, then choose between
	 * ADV_IND and ADV_NONCONN_IND based on the global connectable setting.
	 */
	connectable = (flags & MGMT_ADV_FLAG_CONNECTABLE) ||
		      mgmt_get_connectable(hdev);

	if (!is_advertising_allowed(hdev, connectable))
		return -EPERM;

	/* Set require_privacy to true only when non-connectable
	 * advertising is used. In that case it is fine to use a
	 * non-resolvable private address.
	 */
	err = hci_get_random_address(hdev, !connectable,
				     adv_use_rpa(hdev, flags), adv,
				     &own_addr_type, &random_addr);
	if (err < 0)
		return err;

	memset(&cp, 0, sizeof(cp));

	if (adv) {
		hci_cpu_to_le24(adv->min_interval, cp.min_interval);
		hci_cpu_to_le24(adv->max_interval, cp.max_interval);
		cp.tx_power = adv->tx_power;
	} else {
		hci_cpu_to_le24(hdev->le_adv_min_interval, cp.min_interval);
		hci_cpu_to_le24(hdev->le_adv_max_interval, cp.max_interval);
		cp.tx_power = HCI_ADV_TX_POWER_NO_PREFERENCE;
	}

	secondary_adv = (flags & MGMT_ADV_FLAG_SEC_MASK);

	if (connectable) {
		if (secondary_adv)
			cp.evt_properties = cpu_to_le16(LE_EXT_ADV_CONN_IND);
		else
			cp.evt_properties = cpu_to_le16(LE_LEGACY_ADV_IND);
	} else if (hci_adv_instance_is_scannable(hdev, instance) ||
		   (flags & MGMT_ADV_PARAM_SCAN_RSP)) {
		if (secondary_adv)
			cp.evt_properties = cpu_to_le16(LE_EXT_ADV_SCAN_IND);
		else
			cp.evt_properties = cpu_to_le16(LE_LEGACY_ADV_SCAN_IND);
	} else {
		if (secondary_adv)
			cp.evt_properties = cpu_to_le16(LE_EXT_ADV_NON_CONN_IND);
		else
			cp.evt_properties = cpu_to_le16(LE_LEGACY_NONCONN_IND);
	}

	cp.own_addr_type = own_addr_type;
	cp.channel_map = hdev->le_adv_channel_map;
	cp.handle = instance;

	if (flags & MGMT_ADV_FLAG_SEC_2M) {
		cp.primary_phy = HCI_ADV_PHY_1M;
		cp.secondary_phy = HCI_ADV_PHY_2M;
	} else if (flags & MGMT_ADV_FLAG_SEC_CODED) {
		cp.primary_phy = HCI_ADV_PHY_CODED;
		cp.secondary_phy = HCI_ADV_PHY_CODED;
	} else {
		/* In all other cases use 1M */
		cp.primary_phy = HCI_ADV_PHY_1M;
		cp.secondary_phy = HCI_ADV_PHY_1M;
	}

	err = __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_EXT_ADV_PARAMS,
				       sizeof(cp), &cp, HCI_CMD_TIMEOUT);
	if (err)
		return err;

	if (own_addr_type == ADDR_LE_DEV_RANDOM &&
	    bacmp(&random_addr, BDADDR_ANY)) {
		/* Check if random address need to be updated */
		if (adv) {
			if (!bacmp(&random_addr, &adv->random_addr))
				return 0;
		} else {
			if (!bacmp(&random_addr, &hdev->random_addr))
				return 0;
		}

		return hci_set_random_addr_sync(hdev, &random_addr);
	}

	return 0;
}

static int hci_set_ext_scan_rsp_data_sync(struct hci_dev *hdev, u8 instance)
{
	struct hci_cp_le_set_ext_scan_rsp_data cp;
	u8 len;

	memset(&cp, 0, sizeof(cp));

	len = eir_create_scan_rsp(hdev, instance, cp.data);

	if (hdev->scan_rsp_data_len == len &&
	    !memcmp(cp.data, hdev->scan_rsp_data, len))
		return 0;

	memcpy(hdev->scan_rsp_data, cp.data, sizeof(cp.data));
	hdev->scan_rsp_data_len = len;

	cp.handle = instance;
	cp.length = len;
	cp.operation = LE_SET_ADV_DATA_OP_COMPLETE;
	cp.frag_pref = LE_SET_ADV_DATA_NO_FRAG;

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_EXT_SCAN_RSP_DATA,
				     sizeof(cp), &cp, HCI_CMD_TIMEOUT);
}

static int __hci_set_scan_rsp_data_sync(struct hci_dev *hdev, u8 instance)
{
	struct hci_cp_le_set_scan_rsp_data cp;
	u8 len;

	memset(&cp, 0, sizeof(cp));

	len = eir_create_scan_rsp(hdev, instance, cp.data);

	if (hdev->scan_rsp_data_len == len &&
	    !memcmp(cp.data, hdev->scan_rsp_data, len))
		return 0;

	memcpy(hdev->scan_rsp_data, cp.data, sizeof(cp.data));
	hdev->scan_rsp_data_len = len;

	cp.length = len;

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_SCAN_RSP_DATA,
				     sizeof(cp), &cp, HCI_CMD_TIMEOUT);
}

int hci_update_scan_rsp_data_sync(struct hci_dev *hdev, u8 instance)
{
	if (!hci_dev_test_flag(hdev, HCI_LE_ENABLED))
		return 0;

	if (ext_adv_capable(hdev))
		return hci_set_ext_scan_rsp_data_sync(hdev, instance);

	return __hci_set_scan_rsp_data_sync(hdev, instance);
}

int hci_enable_ext_advertising_sync(struct hci_dev *hdev, u8 instance)
{
	struct hci_cp_le_set_ext_adv_enable *cp;
	struct hci_cp_ext_adv_set *set;
	u8 data[sizeof(*cp) + sizeof(*set) * 1];
	struct adv_info *adv;

	if (instance > 0) {
		adv = hci_find_adv_instance(hdev, instance);
		if (!adv)
			return -EINVAL;
	} else {
		adv = NULL;
	}

	cp = (void *) data;
	set = (void *) cp->data;

	memset(cp, 0, sizeof(*cp));

	cp->enable = 0x01;
	cp->num_of_sets = 0x01;

	memset(set, 0, sizeof(*set));

	set->handle = instance;

	/* Set duration per instance since controller is responsible for
	 * scheduling it.
	 */
	if (adv && adv->duration) {
		u16 duration = adv->timeout * MSEC_PER_SEC;

		/* Time = N * 10 ms */
		set->duration = cpu_to_le16(duration / 10);
	}

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_EXT_ADV_ENABLE,
				     sizeof(*cp) +
				     sizeof(*set) * cp->num_of_sets,
				     data, HCI_CMD_TIMEOUT);
}

int hci_start_ext_adv_sync(struct hci_dev *hdev, u8 instance)
{
	int err;

	err = hci_setup_ext_adv_instance_sync(hdev, instance);
	if (err)
		return err;

	err = hci_set_ext_scan_rsp_data_sync(hdev, instance);
	if (err)
		return err;

	return hci_enable_ext_advertising_sync(hdev, instance);
}

int hci_enable_advertising_sync(struct hci_dev *hdev)
{
	struct adv_info *adv_instance;
	struct hci_cp_le_set_adv_param cp;
	u8 own_addr_type, enable = 0x01;
	bool connectable;
	u16 adv_min_interval, adv_max_interval;
	u32 flags;
	u8 status;

	flags = hci_adv_instance_flags(hdev, hdev->cur_adv_instance);
	adv_instance = hci_find_adv_instance(hdev, hdev->cur_adv_instance);

	/* If the "connectable" instance flag was not set, then choose between
	 * ADV_IND and ADV_NONCONN_IND based on the global connectable setting.
	 */
	connectable = (flags & MGMT_ADV_FLAG_CONNECTABLE) ||
		      mgmt_get_connectable(hdev);

	if (!is_advertising_allowed(hdev, connectable))
		return -EINVAL;

	if (hci_dev_test_flag(hdev, HCI_LE_ADV)) {
		status = hci_disable_advertising_sync(hdev);
		if (status)
			return status;
	}

	/* Clear the HCI_LE_ADV bit temporarily so that the
	 * hci_update_random_address knows that it's safe to go ahead
	 * and write a new random address. The flag will be set back on
	 * as soon as the SET_ADV_ENABLE HCI command completes.
	 */
	hci_dev_clear_flag(hdev, HCI_LE_ADV);

	/* Set require_privacy to true only when non-connectable
	 * advertising is used. In that case it is fine to use a
	 * non-resolvable private address.
	 */
	status = hci_update_random_address_sync(hdev, !connectable,
						adv_use_rpa(hdev, flags),
						&own_addr_type);
	if (status)
		return status;

	memset(&cp, 0, sizeof(cp));

	if (adv_instance) {
		adv_min_interval = adv_instance->min_interval;
		adv_max_interval = adv_instance->max_interval;
	} else {
		adv_min_interval = hdev->le_adv_min_interval;
		adv_max_interval = hdev->le_adv_max_interval;
	}

	if (connectable) {
		cp.type = LE_ADV_IND;
	} else {
		if (hci_adv_instance_is_scannable(hdev, hdev->cur_adv_instance))
			cp.type = LE_ADV_SCAN_IND;
		else
			cp.type = LE_ADV_NONCONN_IND;

		if (!hci_dev_test_flag(hdev, HCI_DISCOVERABLE) ||
		    hci_dev_test_flag(hdev, HCI_LIMITED_DISCOVERABLE)) {
			adv_min_interval = DISCOV_LE_FAST_ADV_INT_MIN;
			adv_max_interval = DISCOV_LE_FAST_ADV_INT_MAX;
		}
	}

	cp.min_interval = cpu_to_le16(adv_min_interval);
	cp.max_interval = cpu_to_le16(adv_max_interval);
	cp.own_address_type = own_addr_type;
	cp.channel_map = hdev->le_adv_channel_map;

	status = __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_ADV_PARAM,
				       sizeof(cp), &cp, HCI_CMD_TIMEOUT);
	if (status)
		return status;

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_ADV_ENABLE,
				     sizeof(enable), &enable, HCI_CMD_TIMEOUT);
}

int hci_remove_ext_adv_instance_sync(struct hci_dev *hdev, u8 instance)
{
	/* If request specifies an instance that doesn't exist, fail */
	if (instance > 0 && !hci_find_adv_instance(hdev, instance))
		return -EINVAL;

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_REMOVE_ADV_SET,
				     sizeof(instance), &instance,
				     HCI_CMD_TIMEOUT);
}

int hci_disable_ext_adv_instance_sync(struct hci_dev *hdev, u8 instance)
{
	struct hci_cp_le_set_ext_adv_enable *cp;
	struct hci_cp_ext_adv_set *set;
	u8 data[sizeof(*cp) + sizeof(*set) * 1];
	u8 size;

	/* If request specifies an instance that doesn't exist, fail */
	if (instance > 0 && !hci_find_adv_instance(hdev, instance))
		return -EINVAL;

	memset(data, 0, sizeof(data));

	cp = (void *)data;
	set = (void *)cp->data;

	/* Instance 0x00 indicates all advertising instances will be disabled */
	cp->num_of_sets = !!instance;
	cp->enable = 0x00;

	set->handle = instance;

	size = sizeof(*cp) + sizeof(*set) * cp->num_of_sets;

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_EXT_ADV_ENABLE,
				     size, data, HCI_CMD_TIMEOUT);
}

static void cancel_adv_timeout(struct hci_dev *hdev)
{
	if (hdev->adv_instance_timeout) {
		hdev->adv_instance_timeout = 0;
		cancel_delayed_work(&hdev->adv_instance_expire);
	}
}

static int hci_set_ext_adv_data_sync(struct hci_dev *hdev, u8 instance)
{
	struct hci_cp_le_set_ext_adv_data cp;
	u8 len;

	memset(&cp, 0, sizeof(cp));

	len = eir_create_adv_data(hdev, instance, cp.data);

	/* There's nothing to do if the data hasn't changed */
	if (hdev->adv_data_len == len &&
	    memcmp(cp.data, hdev->adv_data, len) == 0)
		return 0;

	memcpy(hdev->adv_data, cp.data, sizeof(cp.data));
	hdev->adv_data_len = len;

	cp.length = len;
	cp.handle = instance;
	cp.operation = LE_SET_ADV_DATA_OP_COMPLETE;
	cp.frag_pref = LE_SET_ADV_DATA_NO_FRAG;

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_EXT_ADV_DATA,
				     sizeof(cp), &cp, HCI_CMD_TIMEOUT);
}

static int hci_set_adv_data_sync(struct hci_dev *hdev, u8 instance)
{
	struct hci_cp_le_set_adv_data cp;
	u8 len;

	memset(&cp, 0, sizeof(cp));

	len = eir_create_adv_data(hdev, instance, cp.data);

	/* There's nothing to do if the data hasn't changed */
	if (hdev->adv_data_len == len &&
	    memcmp(cp.data, hdev->adv_data, len) == 0)
		return 0;

	memcpy(hdev->adv_data, cp.data, sizeof(cp.data));
	hdev->adv_data_len = len;

	cp.length = len;

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_ADV_DATA,
				     sizeof(cp), &cp, HCI_CMD_TIMEOUT);
}

int hci_update_adv_data_sync(struct hci_dev *hdev, u8 instance)
{
	if (!hci_dev_test_flag(hdev, HCI_LE_ENABLED))
		return 0;

	if (ext_adv_capable(hdev))
		return hci_set_ext_adv_data_sync(hdev, instance);

	return hci_set_adv_data_sync(hdev, instance);
}

int hci_schedule_adv_instance_sync(struct hci_dev *hdev, u8 instance,
				   bool force)
{
	struct adv_info *adv = NULL;
	u16 timeout;

	if (hci_dev_test_flag(hdev, HCI_ADVERTISING) ||
	    list_empty(&hdev->adv_instances))
		return -EPERM;

	if (hdev->adv_instance_timeout)
		return -EBUSY;

	adv = hci_find_adv_instance(hdev, instance);
	if (!adv)
		return -ENOENT;

	/* A zero timeout means unlimited advertising. As long as there is
	 * only one instance, duration should be ignored. We still set a timeout
	 * in case further instances are being added later on.
	 *
	 * If the remaining lifetime of the instance is more than the duration
	 * then the timeout corresponds to the duration, otherwise it will be
	 * reduced to the remaining instance lifetime.
	 */
	if (adv->timeout == 0 || adv->duration <= adv->remaining_time)
		timeout = adv->duration;
	else
		timeout = adv->remaining_time;

	/* The remaining time is being reduced unless the instance is being
	 * advertised without time limit.
	 */
	if (adv->timeout)
		adv->remaining_time = adv->remaining_time - timeout;

	/* Only use work for scheduling instances with legacy advertising */
	if (!ext_adv_capable(hdev)) {
		hdev->adv_instance_timeout = timeout;
		queue_delayed_work(hdev->req_workqueue,
			   &hdev->adv_instance_expire,
			   msecs_to_jiffies(timeout * 1000));
	}

	/* If we're just re-scheduling the same instance again then do not
	 * execute any HCI commands. This happens when a single instance is
	 * being advertised.
	 */
	if (!force && hdev->cur_adv_instance == instance &&
	    hci_dev_test_flag(hdev, HCI_LE_ADV))
		return 0;

	hdev->cur_adv_instance = instance;
	if (ext_adv_capable(hdev)) {
		hci_start_ext_adv_sync(hdev, instance);
	} else {
		hci_update_adv_data_sync(hdev, instance);
		hci_update_scan_rsp_data_sync(hdev, instance);
		hci_enable_advertising_sync(hdev);
	}

	return 0;
}

/* For a single instance:
 * - force == true: The instance will be removed even when its remaining
 *   lifetime is not zero.
 * - force == false: the instance will be deactivated but kept stored unless
 *   the remaining lifetime is zero.
 *
 * For instance == 0x00:
 * - force == true: All instances will be removed regardless of their timeout
 *   setting.
 * - force == false: Only instances that have a timeout will be removed.
 */
void hci_clear_adv_instance_sync(struct hci_dev *hdev, struct sock *sk,
				 u8 instance, bool force)
{
	struct adv_info *adv, *n, *next = NULL;
	int err;

	/* Cancel any timeout concerning the removed instance(s). */
	if (!instance || hdev->cur_adv_instance == instance)
		cancel_adv_timeout(hdev);

	/* Get the next instance to advertise BEFORE we remove
	 * the current one. This can be the same instance again
	 * if there is only one instance.
	 */
	if (instance && hdev->cur_adv_instance == instance)
		next = hci_get_next_instance(hdev, instance);

	if (instance == 0x00) {
		list_for_each_entry_safe(adv, n, &hdev->adv_instances, list) {
			if (!(force || adv->timeout))
				continue;

			instance = adv->instance;
			err = hci_remove_adv_instance(hdev, instance);
			if (!err)
				mgmt_advertising_removed(sk, hdev, instance);
		}
	} else {
		adv = hci_find_adv_instance(hdev, instance);

		if (force || (adv && adv->timeout && !adv->remaining_time)) {
			/* Don't advertise a removed instance. */
			if (next && next->instance == instance)
				next = NULL;

			err = hci_remove_adv_instance(hdev, instance);
			if (!err)
				mgmt_advertising_removed(sk, hdev, instance);
		}
	}

	if (!hdev_is_powered(hdev) || hci_dev_test_flag(hdev, HCI_ADVERTISING))
		return;

	if (next && !ext_adv_capable(hdev))
		hci_schedule_adv_instance_sync(hdev, next->instance, false);
}

int hci_disable_advertising_sync(struct hci_dev *hdev)
{
	u8 enable = 0x00;

	if (ext_adv_capable(hdev))
		return hci_disable_ext_adv_instance_sync(hdev, 0x00);

	return __hci_cmd_sync_status(hdev, HCI_OP_LE_SET_ADV_ENABLE,
				     sizeof(enable), &enable, HCI_CMD_TIMEOUT);
}
