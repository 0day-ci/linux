/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021 pureLiFi
 */

#ifndef _LF_X_CHIP_H
#define _LF_X_CHIP_H

#include <net/mac80211.h>

#include "usb.h"

enum unit_type {
	STA = 0,
	AP = 1,
};

enum {
	PLFXLC_RADIO_OFF = 0,
	PLFXLC_RADIO_ON = 1,
};

struct purelifi_chip {
	struct purelifi_usb usb;
	struct mutex mutex; /* lock to protect chip data */
	enum unit_type unit_type;
	u16 link_led;
	u8 beacon_set;
	__le16 beacon_interval;
};

struct purelifi_mc_hash {
	u32 low;
	u32 high;
};

#define purelifi_chip_dev(chip) (&(chip)->usb.intf->dev)

void purelifi_chip_init(struct purelifi_chip *chip,
			struct ieee80211_hw *hw,
			struct usb_interface *intf);

void purelifi_chip_release(struct purelifi_chip *chip);

void purelifi_chip_disable_rxtx(struct purelifi_chip *chip);

int purelifi_chip_init_hw(struct purelifi_chip *chip);

int purelifi_chip_enable_rxtx(struct purelifi_chip *chip);

int purelifi_chip_set_rate(struct purelifi_chip *chip, u8 rate);

int purelifi_set_beacon_interval(struct purelifi_chip *chip, u16 interval,
				 u8 dtim_period, int type);

int purelifi_chip_switch_radio(struct purelifi_chip *chip, u16 value);

static inline struct purelifi_chip *purelifi_usb_to_chip(struct purelifi_usb
							 *usb)
{
	return container_of(usb, struct purelifi_chip, usb);
}

static inline void purelifi_mc_clear(struct purelifi_mc_hash *hash)
{
	hash->low = 0;
	/* The interfaces must always received broadcasts.
	 * The hash of the broadcast address ff:ff:ff:ff:ff:ff is 63.
	 */
	hash->high = 0x80000000;
}

static inline void purelifi_mc_add_all(struct purelifi_mc_hash *hash)
{
	hash->low  = 0xffffffff;
	hash->high = 0xffffffff;
}

static inline void purelifi_mc_add_addr(struct purelifi_mc_hash *hash,
					u8 *addr)
{
	unsigned int i = addr[5] >> 2;

	if (i < 32)
		hash->low |= 1U << i;
	else
		hash->high |= 1 << (i - 32);
}
#endif /* _LF_X_CHIP_H */
