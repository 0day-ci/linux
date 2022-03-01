/* SPDX-License-Identifier: GPL-2.0 */
/*
 * xhci-plat.h - xHCI host controller driver platform Bus Glue.
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 */

#ifndef _XHCI_PLAT_H
#define _XHCI_PLAT_H

#include <linux/types.h>

struct usb_hcd;

struct xhci_plat_priv {
	const char *firmware_name;
	unsigned long long quirks;
	void (*plat_start)(struct usb_hcd *hcd);
	int (*init_quirk)(struct usb_hcd *hcd);
	int (*suspend_quirk)(struct usb_hcd *hcd);
	int (*resume_quirk)(struct usb_hcd *hcd);
};

#endif	/* _XHCI_PLAT_H */
