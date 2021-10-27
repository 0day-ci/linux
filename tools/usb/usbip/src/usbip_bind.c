// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 */

#include <libudev.h>

#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <getopt.h>

#include "usbip_common.h"
#include "usbip_monitor.h"
#include "utils.h"
#include "usbip.h"
#include "sysfs_utils.h"

enum unbind_status {
	UNBIND_ST_OK,
	UNBIND_ST_USBIP_HOST,
	UNBIND_ST_FAILED
};

struct bind_options {
	char *busid;
	bool is_persistent;
};

static const char usbip_bind_usage_string[] =
	"usbip bind <args>\n"
	"    -b, --busid=<busid>        Bind " USBIP_HOST_DRV_NAME ".ko to device\n"
	"                               on <busid>\n"
	"    -p, --persistent           Persistently monitor the given bus and\n"
	"                               export USB devices plugged in\n";

void usbip_bind_usage(void)
{
	printf("usage: %s", usbip_bind_usage_string);
}

/* call at unbound state */
static int bind_usbip(char *busid)
{
	char attr_name[] = "bind";
	char bind_attr_path[SYSFS_PATH_MAX];
	int rc = -1;

	snprintf(bind_attr_path, sizeof(bind_attr_path), "%s/%s/%s/%s/%s/%s",
		 SYSFS_MNT_PATH, SYSFS_BUS_NAME, SYSFS_BUS_TYPE,
		 SYSFS_DRIVERS_NAME, USBIP_HOST_DRV_NAME, attr_name);

	rc = write_sysfs_attribute(bind_attr_path, busid, strlen(busid));
	if (rc < 0) {
		err("error binding device %s to driver: %s", busid,
		    strerror(errno));
		return -1;
	}

	return 0;
}

/* buggy driver may cause dead lock */
static int unbind_other(char *busid)
{
	enum unbind_status status = UNBIND_ST_OK;

	char attr_name[] = "unbind";
	char unbind_attr_path[SYSFS_PATH_MAX];
	int rc = -1;

	struct udev *udev;
	struct udev_device *dev;
	const char *driver;
	const char *bDevClass;

	/* Create libudev context. */
	udev = udev_new();

	/* Get the device. */
	dev = udev_device_new_from_subsystem_sysname(udev, "usb", busid);
	if (!dev) {
		dbg("unable to find device with bus ID %s", busid);
		goto err_close_busid_dev;
	}

	/* Check what kind of device it is. */
	bDevClass  = udev_device_get_sysattr_value(dev, "bDeviceClass");
	if (!bDevClass) {
		dbg("unable to get bDevClass device attribute");
		goto err_close_busid_dev;
	}

	if (!strncmp(bDevClass, "09", strlen(bDevClass))) {
		dbg("skip unbinding of hub");
		goto err_close_busid_dev;
	}

	/* Get the device driver. */
	driver = udev_device_get_driver(dev);
	if (!driver) {
		/* No driver bound to this device. */
		goto out;
	}

	if (!strncmp(USBIP_HOST_DRV_NAME, driver,
				strlen(USBIP_HOST_DRV_NAME))) {
		/* Already bound to usbip-host. */
		status = UNBIND_ST_USBIP_HOST;
		goto out;
	}

	/* Unbind device from driver. */
	snprintf(unbind_attr_path, sizeof(unbind_attr_path), "%s/%s/%s/%s/%s/%s",
		 SYSFS_MNT_PATH, SYSFS_BUS_NAME, SYSFS_BUS_TYPE,
		 SYSFS_DRIVERS_NAME, driver, attr_name);

	rc = write_sysfs_attribute(unbind_attr_path, busid, strlen(busid));
	if (rc < 0) {
		err("error unbinding device %s from driver", busid);
		goto err_close_busid_dev;
	}

	goto out;

err_close_busid_dev:
	status = UNBIND_ST_FAILED;
out:
	udev_device_unref(dev);
	udev_unref(udev);

	return status;
}

static const char *get_device_devpath(char *busid)
{
	struct udev *udev = udev_new();
	const char *devpath = NULL;
	struct udev_device *dev = udev_device_new_from_subsystem_sysname(udev, "usb", busid);

	if (dev)
		devpath = udev_device_get_devpath(dev);
	udev_unref(udev);
	return devpath;
}

static bool is_usb_connected(char *busid)
{
	return get_device_devpath(busid) != NULL;
}

static int bind_available_device(char *busid)
{
	int rc;

	rc = unbind_other(busid);
	if (rc == UNBIND_ST_FAILED) {
		err("could not unbind driver from device on busid %s", busid);
		return -1;
	} else if (rc == UNBIND_ST_USBIP_HOST) {
		err("device on busid %s is already bound to %s", busid,
		    USBIP_HOST_DRV_NAME);
		return -1;
	}

	rc = modify_match_busid(busid, 1);
	if (rc < 0) {
		err("unable to bind device on %s", busid);
		return -1;
	}

	rc = bind_usbip(busid);
	if (rc < 0) {
		err("could not bind device to %s", USBIP_HOST_DRV_NAME);
		modify_match_busid(busid, 0);
		return -1;
	}

	info("device on busid %s: bind complete", busid);

	return 0;
}

static int bind_device(char *busid)
{
	const char *devpath = get_device_devpath(busid);

	/* Check whether the device with this bus ID exists. */
	if (!devpath) {
		err("device with the specified bus ID does not exist");
		return -1;
	}

	/* If the device is already attached to vhci_hcd - bail out */
	if (strstr(devpath, USBIP_VHCI_DRV_NAME)) {
		err("bind loop detected: device: %s is attached to %s\n",
		    devpath, USBIP_VHCI_DRV_NAME);
		return -1;
	}

	return bind_available_device(busid);
}

static int bind_device_persistently(char *busid)
{
	int rc = 0;
	bool already_connected = is_usb_connected(busid);
	usbip_monitor_t *monitor = monitor = usbip_monitor_new();

	usbip_monitor_set_busid(monitor, busid);
	while (rc == 0) {
		if (!already_connected) {
			info("device on busid %s: monitor connect", busid);
			usbip_monitor_await_usb_bind(monitor, USBIP_USB_DRV_NAME);
		}
		rc = bind_available_device(busid);
		if (rc == 0) {
			info("device on busid %s: monitor disconnect", busid);
			usbip_monitor_await_usb_bind(monitor, USBIP_HOST_DRV_NAME);
			usbip_monitor_await_usb_unbind(monitor);
		}
		already_connected = false;
	}
	usbip_monitor_delete(monitor);
	return rc;
}

int usbip_bind(int argc, char *argv[])
{
	static const struct option opts[] = {
		{ "busid", required_argument, NULL, 'b' },
		{ "persistent",  no_argument, NULL, 'p' },
		{ NULL,    0,                 NULL,  0  }
	};

	struct bind_options options = {};
	int opt;
	int ret = -1;

	for (;;) {
		opt = getopt_long(argc, argv, "b:p", opts, NULL);

		if (opt == -1)
			break;

		switch (opt) {
		case 'b':
			options.busid = optarg;
			break;
		case 'p':
			options.is_persistent = true;
			break;
		default:
			goto err_out;
		}
	}

	if (!options.busid)
		goto err_out;

	if (options.is_persistent)
		ret = bind_device_persistently(options.busid);
	else
		ret = bind_device(options.busid);
	goto out;

err_out:
	usbip_bind_usage();
out:
	return ret;
}
