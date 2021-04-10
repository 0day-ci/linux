/* SPDX-License-Identifier: GPL-2.0 */
/*
 * xhci-plat.h - xHCI host controller driver platform Bus Glue.
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 */

#ifndef __LINUX_USB_XHCI_PLAT_H
#define __LINUX_USB_XHCI_PLAT_H

struct usb_hcd;

struct xhci_plat_priv {
	const char *firmware_name;
	unsigned long long quirks;
	int (*plat_setup)(struct usb_hcd *hcd);
	void (*plat_start)(struct usb_hcd *hcd);
	int (*init_quirk)(struct usb_hcd *hcd);
	int (*suspend_quirk)(struct usb_hcd *hcd);
	int (*resume_quirk)(struct usb_hcd *hcd);
};

#define hcd_to_xhci_priv(h) ((struct xhci_plat_priv *)hcd_to_xhci(h)->priv)
#define xhci_to_priv(x) ((struct xhci_plat_priv *)(x)->priv)
#endif	/* __LINUX_USB_XHCI_PLAT_H */
