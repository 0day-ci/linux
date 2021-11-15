/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (C) 2014 Intel Corporation */

#include <net/bluetooth/l2cap.h>

void hci_read_supported_codecs(struct hci_dev *hdev);
void hci_read_supported_codecs_v2(struct hci_dev *hdev);
void hci_codec_list_clear(struct list_head *codec_list);
int hci_get_supported_codecs(struct hci_dev *hdev, u8 type, char __user *optval,
			     int __user *optlen, int len);
int hci_configure_msft_avdtp_open(struct hci_dev *hdev, struct l2cap_chan *chan,
				  sockptr_t optval, int optlen, struct sock *sk);
