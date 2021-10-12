// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 pureLiFi
 */

#include <linux/kernel.h>
#include <linux/errno.h>

#include "chip.h"
#include "mac.h"
#include "usb.h"

void purelifi_chip_init(struct purelifi_chip *chip,
			struct ieee80211_hw *hw,
			struct usb_interface *intf)
{
	memset(chip, 0, sizeof(*chip));
	mutex_init(&chip->mutex);
	purelifi_usb_init(&chip->usb, hw, intf);
}

void purelifi_chip_release(struct purelifi_chip *chip)
{
	purelifi_usb_release(&chip->usb);
	mutex_destroy(&chip->mutex);
}

int purelifi_set_beacon_interval(struct purelifi_chip *chip, u16 interval,
				 u8 dtim_period, int type)
{
	if (!interval ||
	    (chip->beacon_set &&
	     le16_to_cpu(chip->beacon_interval) == interval))
		return 0;

	chip->beacon_interval = cpu_to_le16(interval);
	chip->beacon_set = true;
	return plf_usb_wreq(&chip->beacon_interval,
			     sizeof(chip->beacon_interval),
			     USB_REQ_BEACON_INTERVAL_WR);
}

int purelifi_chip_init_hw(struct purelifi_chip *chip)
{
	unsigned char *addr = purelifi_mac_get_perm_addr(purelifi_chip_to_mac(chip));
	struct usb_device *udev = interface_to_usbdev(chip->usb.intf);

	pr_info("purelifi chip %04x:%04x v%02x %pM %s\n",
		le16_to_cpu(udev->descriptor.idVendor),
		le16_to_cpu(udev->descriptor.idProduct),
		le16_to_cpu(udev->descriptor.bcdDevice),
		addr,
		purelifi_speed(udev->speed));

	return purelifi_set_beacon_interval(chip, 100, 0, 0);
}

int purelifi_chip_switch_radio(struct purelifi_chip *chip, u16 value)
{
	int r;
	__le16 radio_on = cpu_to_le16(value);

	r = plf_usb_wreq(&radio_on, sizeof(value), USB_REQ_POWER_WR);
	if (r)
		dev_err(purelifi_chip_dev(chip), "POWER_WR failed (%d)\n", r);
	return r;
}

int purelifi_chip_enable_rxtx(struct purelifi_chip *chip)
{
	purelifi_usb_enable_tx(&chip->usb);
	return purelifi_usb_enable_rx(&chip->usb);
}

void purelifi_chip_disable_rxtx(struct purelifi_chip *chip)
{
	u8 value = 0;

	plf_usb_wreq(&value, sizeof(value), USB_REQ_RXTX_WR);
	purelifi_usb_disable_rx(&chip->usb);
	purelifi_usb_disable_tx(&chip->usb);
}

int purelifi_chip_set_rate(struct purelifi_chip *chip, u8 rate)
{
	int r;

	if (!chip)
		return -EINVAL;

	r = plf_usb_wreq(&rate, sizeof(rate), USB_REQ_RATE_WR);
	if (r)
		dev_err(purelifi_chip_dev(chip), "RATE_WR failed (%d)\n", r);
	return r;
}
