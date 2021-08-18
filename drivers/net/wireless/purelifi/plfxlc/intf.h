/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 pureLiFi
 */

#define PURELIFI_BYTE_NUM_ALIGNMENT 4
#define ETH_ALEN 6
#define AP_USER_LIMIT 8

struct rx_status {
	__be16 rssi;
	u8     rate_idx;
	u8     pad;
	__be64 crc_error_count;
} __packed;

enum usb_req_enum_t {
	USB_REQ_TEST_WR            = 0,
	USB_REQ_MAC_WR             = 1,
	USB_REQ_POWER_WR           = 2,
	USB_REQ_RXTX_WR            = 3,
	USB_REQ_BEACON_WR          = 4,
	USB_REQ_BEACON_INTERVAL_WR = 5,
	USB_REQ_RTS_CTS_RATE_WR    = 6,
	USB_REQ_HASH_WR            = 7,
	USB_REQ_DATA_TX            = 8,
	USB_REQ_RATE_WR            = 9,
	USB_REQ_SET_FREQ           = 15
};

struct usb_req_t {
	__be32         id; /* should be usb_req_enum_t */
	__be32	       len;
	u8             buf[512];
};

