/* SPDX-License-Identifier: GPL-2.0 */
/**
 * Copyright (C) 2021 Lars Gunnarsson
 */
#ifndef __USBIP_MONITOR_H
#define __USBIP_MONITOR_H

#include <stdbool.h>

typedef struct usbip_monitor usbip_monitor_t;

usbip_monitor_t *usbip_monitor_new(void);
void usbip_monitor_delete(usbip_monitor_t *monitor);

/**
 * Set busid to await events on. If unset, any busid will be matched.
 */
void usbip_monitor_set_busid(usbip_monitor_t *monitor, const char *busid);

/**
 * Set timeout for await calls in milliseconds, default is no timeout (-1).
 */
void usbip_monitor_set_timeout(usbip_monitor_t *monitor, int milliseconds);

/**
 * Functions below is blocking. Returns true if event occurred, or false on
 * timeouts.
 */
bool usbip_monitor_await_usb_add(const usbip_monitor_t *monitor,
				 const char *driver);
bool usbip_monitor_await_usb_bind(const usbip_monitor_t *monitor,
				  const char *driver);
bool usbip_monitor_await_usb_unbind(const usbip_monitor_t *monitor);
bool usbip_monitor_await_usb_delete(const usbip_monitor_t *monitor);

#endif /* __USBIP_MONITOR_H */
