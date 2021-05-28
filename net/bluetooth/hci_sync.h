/* SPDX-License-Identifier: GPL-2.0 */
/*
 * BlueZ - Bluetooth protocol stack for Linux
 *
 * Copyright (C) 2021 Intel Corporation
 */

int hci_update_eir_sync(struct hci_dev *hdev);
int hci_update_class_sync(struct hci_dev *hdev);

int hci_update_eir_sync(struct hci_dev *hdev);
int hci_update_class_sync(struct hci_dev *hdev);

int hci_update_random_address_sync(struct hci_dev *hdev, bool require_privacy,
				   bool rpa, u8 *own_addr_type);

int hci_update_scan_rsp_data_sync(struct hci_dev *hdev, u8 instance);
int hci_update_adv_data_sync(struct hci_dev *hdev, u8 instance);
int hci_schedule_adv_instance_sync(struct hci_dev *hdev, u8 instance,
				   bool force);

int hci_setup_ext_adv_instance_sync(struct hci_dev *hdev, u8 instance);
int hci_start_ext_adv_sync(struct hci_dev *hdev, u8 instance);
int hci_enable_ext_advertising_sync(struct hci_dev *hdev, u8 instance);
int hci_enable_advertising_sync(struct hci_dev *hdev);

int hci_disable_ext_adv_instance_sync(struct hci_dev *hdev, u8 instance);
int hci_remove_ext_adv_instance_sync(struct hci_dev *hdev, u8 instance);
void hci_clear_adv_instance_sync(struct hci_dev *hdev, struct sock *sk,
				 u8 instance, bool force);
int hci_disable_advertising_sync(struct hci_dev *hdev);

int hci_update_background_scan_sync(struct hci_dev *hdev);
