/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 pureLiFi
 */

#ifndef _PURELIFI_MAC_H
#define _PURELIFI_MAC_H

#include <linux/kernel.h>
#include <net/mac80211.h>

#include "chip.h"

#define PURELIFI_CCK                  0x00
#define PURELIFI_OFDM                 0x10
#define PURELIFI_CCK_PREA_SHORT       0x20

#define PURELIFI_OFDM_PLCP_RATE_6M	0xb
#define PURELIFI_OFDM_PLCP_RATE_9M	0xf
#define PURELIFI_OFDM_PLCP_RATE_12M	0xa
#define PURELIFI_OFDM_PLCP_RATE_18M	0xe
#define PURELIFI_OFDM_PLCP_RATE_24M	0x9
#define PURELIFI_OFDM_PLCP_RATE_36M	0xd
#define PURELIFI_OFDM_PLCP_RATE_48M	0x8
#define PURELIFI_OFDM_PLCP_RATE_54M	0xc

#define PURELIFI_CCK_RATE_1M	(PURELIFI_CCK | 0x00)
#define PURELIFI_CCK_RATE_2M	(PURELIFI_CCK | 0x01)
#define PURELIFI_CCK_RATE_5_5M	(PURELIFI_CCK | 0x02)
#define PURELIFI_CCK_RATE_11M	(PURELIFI_CCK | 0x03)
#define PURELIFI_OFDM_RATE_6M	(PURELIFI_OFDM | PURELIFI_OFDM_PLCP_RATE_6M)
#define PURELIFI_OFDM_RATE_9M	(PURELIFI_OFDM | PURELIFI_OFDM_PLCP_RATE_9M)
#define PURELIFI_OFDM_RATE_12M	(PURELIFI_OFDM | PURELIFI_OFDM_PLCP_RATE_12M)
#define PURELIFI_OFDM_RATE_18M	(PURELIFI_OFDM | PURELIFI_OFDM_PLCP_RATE_18M)
#define PURELIFI_OFDM_RATE_24M	(PURELIFI_OFDM | PURELIFI_OFDM_PLCP_RATE_24M)
#define PURELIFI_OFDM_RATE_36M	(PURELIFI_OFDM | PURELIFI_OFDM_PLCP_RATE_36M)
#define PURELIFI_OFDM_RATE_48M	(PURELIFI_OFDM | PURELIFI_OFDM_PLCP_RATE_48M)
#define PURELIFI_OFDM_RATE_54M	(PURELIFI_OFDM | PURELIFI_OFDM_PLCP_RATE_54M)

#define PURELIFI_RX_ERROR		0x80
#define PURELIFI_RX_CRC32_ERROR		0x10

#define PLF_REGDOMAIN_FCC	0x10
#define PLF_REGDOMAIN_IC	0x20
#define PLF_REGDOMAIN_ETSI	0x30
#define PLF_REGDOMAIN_SPAIN	0x31
#define PLF_REGDOMAIN_FRANCE	0x32
#define PLF_REGDOMAIN_JAPAN_2	0x40
#define PLF_REGDOMAIN_JAPAN	0x41
#define PLF_REGDOMAIN_JAPAN_3	0x49

enum {
	MODULATION_RATE_BPSK_1_2 = 0,
	MODULATION_RATE_BPSK_3_4,
	MODULATION_RATE_QPSK_1_2,
	MODULATION_RATE_QPSK_3_4,
	MODULATION_RATE_QAM16_1_2,
	MODULATION_RATE_QAM16_3_4,
	MODULATION_RATE_QAM64_1_2,
	MODULATION_RATE_QAM64_3_4,
	MODULATION_RATE_AUTO,
	MODULATION_RATE_NUM
};

#define purelifi_mac_dev(mac) purelifi_chip_dev(&(mac)->chip)

#define PURELIFI_MAC_STATS_BUFFER_SIZE 16
#define PURELIFI_MAC_MAX_ACK_WAITERS 50

struct purelifi_ctrlset {
	/* id should be plf_usb_req_enum */
	__be32		id;
	__be32		len;
	u8              modulation;
	u8              control;
	u8              service;
	u8		pad;
	__le16		packet_length;
	__le16		current_length;
	__le16		next_frame_length;
	__le16		tx_length;
	__be32		payload_len_nw;
} __packed;

/* overlay */
struct purelifi_header {
	struct purelifi_ctrlset plf_ctrl;
	u32    frametype;
	u8    *dmac;
} __packed;

struct tx_status {
	u8 type;
	u8 id;
	u8 rate;
	u8 pad;
	u8 mac[ETH_ALEN];
	u8 retry;
	u8 failure;
} __packed;

struct beacon {
	struct delayed_work watchdog_work;
	struct sk_buff *cur_beacon;
	unsigned long last_update;
	u16 interval;
	u8 period;
};

enum purelifi_device_flags {
	PURELIFI_DEVICE_RUNNING,
};

struct purelifi_mac {
	struct purelifi_chip chip;
	spinlock_t lock; /* lock for mac data */
	struct ieee80211_hw *hw;
	struct ieee80211_vif *vif;
	struct beacon beacon;
	struct work_struct set_rts_cts_work;
	struct work_struct process_intr;
	struct purelifi_mc_hash multicast_hash;
	u8 intr_buffer[USB_MAX_EP_INT_BUFFER];
	u8 regdomain;
	u8 default_regdomain;
	u8 channel;
	int type;
	int associated;
	unsigned long flags;
	struct sk_buff_head ack_wait_queue;
	struct ieee80211_channel channels[14];
	struct ieee80211_rate rates[12];
	struct ieee80211_supported_band band;

	/* whether to pass frames with CRC errors to stack */
	bool pass_failed_fcs;

	/* whether to pass control frames to stack */
	bool pass_ctrl;

	/* whether we have received a 802.11 ACK that is pending */
	bool ack_pending;

	/* signal strength of the last 802.11 ACK received */
	int ack_signal;

	unsigned char hw_address[ETH_ALEN];
	char serial_number[PURELIFI_SERIAL_LEN];
	u64 crc_errors;
	u64 rssi;
};

static inline struct purelifi_mac *
purelifi_hw_mac(struct ieee80211_hw *hw)
{
	return hw->priv;
}

static inline struct purelifi_mac *
purelifi_chip_to_mac(struct purelifi_chip *chip)
{
	return container_of(chip, struct purelifi_mac, chip);
}

static inline struct purelifi_mac *
purelifi_usb_to_mac(struct purelifi_usb *usb)
{
	return purelifi_chip_to_mac(purelifi_usb_to_chip(usb));
}

static inline u8 *purelifi_mac_get_perm_addr(struct purelifi_mac *mac)
{
	return mac->hw->wiphy->perm_addr;
}

struct ieee80211_hw *purelifi_mac_alloc_hw(struct usb_interface *intf);
void purelifi_mac_release(struct purelifi_mac *mac);

int purelifi_mac_preinit_hw(struct ieee80211_hw *hw, const u8 *hw_address);
int purelifi_mac_init_hw(struct ieee80211_hw *hw);

int purelifi_mac_rx(struct ieee80211_hw *hw, const u8 *buffer,
		    unsigned int length);
void purelifi_mac_tx_failed(struct urb *urb);
void purelifi_mac_tx_to_dev(struct sk_buff *skb, int error);
int plfxlc_op_start(struct ieee80211_hw *hw);
void plfxlc_op_stop(struct ieee80211_hw *hw);
int purelifi_restore_settings(struct purelifi_mac *mac);

#endif
