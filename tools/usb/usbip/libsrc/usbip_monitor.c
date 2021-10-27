// SPDX-License-Identifier: GPL-2.0
/**
 * Copyright (C) 2021 Lars Gunnarsson
 */
#include <libudev.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>

#include "usbip_monitor.h"

struct usbip_monitor {
	const char *busid;
	int timeout_ms;
	struct udev *udev;
	struct udev_monitor *udev_monitor;
};

usbip_monitor_t *usbip_monitor_new(void)
{
	usbip_monitor_t *monitor = NULL;
	struct udev *udev = udev_new();

	if (udev) {
		struct udev_monitor *udev_monitor =
			udev_monitor_new_from_netlink(udev, "udev");
		udev_monitor_filter_add_match_subsystem_devtype(
			udev_monitor,
			/*subsystem=*/"usb",
			/*devtype=*/"usb_device");
		udev_monitor_enable_receiving(udev_monitor);
		monitor = malloc(sizeof(struct usbip_monitor));
		monitor->busid = NULL;
		monitor->timeout_ms = -1;
		monitor->udev = udev;
		monitor->udev_monitor = udev_monitor;
	}
	return monitor;
}

void usbip_monitor_delete(usbip_monitor_t *monitor)
{
	if (monitor) {
		udev_monitor_unref(monitor->udev_monitor);
		udev_unref(monitor->udev);
		free(monitor);
	}
}

void usbip_monitor_set_busid(usbip_monitor_t *monitor, const char *busid)
{
	monitor->busid = busid;
}

void usbip_monitor_set_timeout(usbip_monitor_t *monitor, int milliseconds)
{
	monitor->timeout_ms = milliseconds;
}

static struct udev_device *await_udev_event(const usbip_monitor_t *monitor)
{
	struct udev_device *dev = NULL;
	int fd = udev_monitor_get_fd(monitor->udev_monitor);
	const int nfds = 1;
	struct pollfd pollfd[] = { { fd, POLLIN, 0 } };
	int nfd = poll(pollfd, nfds, monitor->timeout_ms);

	if (nfd)
		dev = udev_monitor_receive_device(monitor->udev_monitor);
	return dev;
}

static int optional_filter_busid(const char *busid, const char *udev_busid)
{
	int filter_match = 0;

	if (busid) {
		if (strcmp(busid, udev_busid) == 0)
			filter_match = 1;
	} else {
		filter_match = 1;
	}
	return filter_match;
}

static bool await_usb_with_driver(const usbip_monitor_t *monitor,
				  const char *driver, const char *action)
{
	bool event_occured = false;

	while (!event_occured) {
		struct udev_device *dev = await_udev_event(monitor);

		if (dev) {
			const char *udev_action = udev_device_get_action(dev);
			const char *udev_driver = udev_device_get_driver(dev);
			const char *udev_busid = udev_device_get_sysname(dev);

			if (strcmp(udev_action, action) == 0 &&
			    strcmp(udev_driver, driver) == 0) {
				if (optional_filter_busid(monitor->busid,
							  udev_busid)) {
					event_occured = true;
				}
			}
			udev_device_unref(dev);
		} else {
			break;
		}
	}
	return event_occured;
}

bool usbip_monitor_await_usb_add(const usbip_monitor_t *monitor,
				 const char *driver)
{
	return await_usb_with_driver(monitor, driver, "add");
}

bool usbip_monitor_await_usb_bind(const usbip_monitor_t *monitor,
				  const char *driver)
{
	return await_usb_with_driver(monitor, driver, "bind");
}

static bool await_usb(const usbip_monitor_t *monitor, const char *action)
{
	bool event_occured = false;

	while (!event_occured) {
		struct udev_device *dev = await_udev_event(monitor);

		if (dev) {
			const char *udev_action = udev_device_get_action(dev);
			const char *udev_busid = udev_device_get_sysname(dev);

			if (strcmp(udev_action, action) == 0) {
				if (optional_filter_busid(monitor->busid,
							  udev_busid)) {
					event_occured = true;
				}
			}
			udev_device_unref(dev);
		} else {
			break;
		}
	}
	return event_occured;
}

bool usbip_monitor_await_usb_unbind(const usbip_monitor_t *monitor)
{
	return await_usb(monitor, "unbind");
}

bool usbip_monitor_await_usb_delete(const usbip_monitor_t *monitor)
{
	return await_usb(monitor, "delete");
}
