// SPDX-License-Identifier: GPL-2.0
/*
 * USB CDC Device Management subdriver
 *
 * Copyright (c) 2012  Bjørn Mork <bjorn@mork.no>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#ifndef __LINUX_USB_CDC_WDM_H
#define __LINUX_USB_CDC_WDM_H

#include <uapi/linux/usb/cdc-wdm.h>

/**
 * enum usb_cdc_wdm_type - CDC WDM endpoint type
 * @USB_CDC_WDM_UNKNOWN: Unknown type
 * @USB_CDC_WDM_MBIM: Mobile Broadband Interface Model control
 * @USB_CDC_WDM_QMI: Qualcomm Modem Interface for modem control
 * @USB_CDC_WDM_AT: AT commands interface
 */
enum usb_cdc_wdm_type {
	USB_CDC_WDM_UNKNOWN,
	USB_CDC_WDM_MBIM,
	USB_CDC_WDM_QMI,
	USB_CDC_WDM_AT
};

extern struct usb_driver *usb_cdc_wdm_register(struct usb_interface *intf,
					struct usb_endpoint_descriptor *ep,
					int bufsize, enum usb_cdc_wdm_type type,
					int (*manage_power)(struct usb_interface *, int));

#endif /* __LINUX_USB_CDC_WDM_H */
