// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 * Copyright (C) 2015-2016 Samsung Electronics
 *               Igor Kotrasinski <i.kotrasinsk@samsung.com>
 *               Krzysztof Opasiak <k.opasiak@samsung.com>
 */

#include <sys/stat.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>

#include "vhci_driver.h"
#include "usbip_common.h"
#include "usbip_monitor.h"
#include "usbip_network.h"
#include "usbip.h"

struct attach_options {
	char *busid;
	bool is_persistent;
};

static const char usbip_attach_usage_string[] =
	"usbip attach <args>\n"
	"    -r, --remote=<host>      The machine with exported USB devices\n"
	"    -b, --busid=<busid>      Busid of the device on <host>\n"
	"    -d, --device=<devid>     Id of the virtual UDC on <host>\n"
	"    -p, --persistent         Persistently monitor the given bus and import\n"
	"                             USB devices when available on the remote end\n";


void usbip_attach_usage(void)
{
	printf("usage: %s", usbip_attach_usage_string);
}

#define MAX_BUFF 100
static int record_connection(char *host, char *port, char *busid, int rhport)
{
	int fd;
	char path[PATH_MAX+1];
	char buff[MAX_BUFF+1];
	int ret;

	ret = mkdir(VHCI_STATE_PATH, 0700);
	if (ret < 0) {
		/* if VHCI_STATE_PATH exists, then it better be a directory */
		if (errno == EEXIST) {
			struct stat s;

			ret = stat(VHCI_STATE_PATH, &s);
			if (ret < 0)
				return -1;
			if (!(s.st_mode & S_IFDIR))
				return -1;
		} else
			return -1;
	}

	snprintf(path, PATH_MAX, VHCI_STATE_PATH"/port%d", rhport);

	fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, S_IRWXU);
	if (fd < 0)
		return -1;

	snprintf(buff, MAX_BUFF, "%s %s %s\n",
			host, port, busid);

	ret = write(fd, buff, strlen(buff));
	if (ret != (ssize_t) strlen(buff)) {
		close(fd);
		return -1;
	}

	close(fd);

	return 0;
}

static int import_device(int sockfd, struct usbip_usb_device *udev)
{
	int rc;
	int port;
	uint32_t speed = udev->speed;

	rc = usbip_vhci_driver_open();
	if (rc < 0) {
		err("open vhci_driver");
		goto err_out;
	}

	do {
		port = usbip_vhci_get_free_port(speed);
		if (port < 0) {
			err("no free port");
			goto err_driver_close;
		}

		dbg("got free port %d", port);

		rc = usbip_vhci_attach_device(port, sockfd, udev->busnum,
					      udev->devnum, udev->speed);
		if (rc < 0 && errno != EBUSY) {
			err("import device");
			goto err_driver_close;
		}
	} while (rc < 0);

	usbip_vhci_driver_close();

	return port;

err_driver_close:
	usbip_vhci_driver_close();
err_out:
	return -1;
}

static int query_import_device(int sockfd, char *busid, bool is_persistent)
{
	int rc;
	struct op_import_request request;
	struct op_import_reply   reply;
	uint16_t code = OP_REP_IMPORT;
	int status;

	memset(&request, 0, sizeof(request));
	memset(&reply, 0, sizeof(reply));
	strncpy(request.busid, busid, SYSFS_BUS_ID_SIZE-1);
	if (is_persistent) {
		request.poll_timeout_ms = 5000;
		info("remote device on busid %s: polling", busid);
	}
	PACK_OP_IMPORT_REQUEST(1, &request);

	do {
		/* send a request */
		rc = usbip_net_send_op_common(sockfd, OP_REQ_IMPORT, 0);
		if (rc < 0) {
			err("send op_common");
			return -1;
		}

		rc = usbip_net_send(sockfd, (void *) &request, sizeof(request));
		if (rc < 0) {
			err("send op_import_request");
			return -1;
		}

		/* receive a reply */
		rc = usbip_net_recv_op_common(sockfd, &code, &status);
		if (status != ST_POLL_TIMEOUT && rc < 0) {
			err("Attach Request for %s failed - %s\n",
					busid, usbip_op_common_status_string(status));
			return -1;
		}
	} while (status == ST_POLL_TIMEOUT);

	rc = usbip_net_recv(sockfd, (void *) &reply, sizeof(reply));
	if (rc < 0) {
		err("recv op_import_reply");
		return -1;
	}

	PACK_OP_IMPORT_REPLY(0, &reply);

	/* check the reply */
	if (strncmp(reply.udev.busid, busid, SYSFS_BUS_ID_SIZE)) {
		err("recv different busid %s", reply.udev.busid);
		return -1;
	}

	/* import a device */
	return import_device(sockfd, &reply.udev);
}

static int get_local_busid_from(int port, char *local_busid)
{
	int rc = usbip_vhci_driver_open();

	if (rc == 0)
		rc = usbip_vhci_get_local_busid_from(port, local_busid);
	usbip_vhci_driver_close();
	return rc;
}

static int attach_device(char *host, struct attach_options opt)
{
	int sockfd;
	int rc;
	int rhport;

	sockfd = usbip_net_tcp_connect(host, usbip_port_string);
	if (sockfd < 0) {
		err("tcp connect");
		return -1;
	}

	rhport = query_import_device(sockfd, opt.busid, opt.is_persistent);
	if (rhport < 0)
		return -1;

	close(sockfd);

	rc = record_connection(host, usbip_port_string, opt.busid, rhport);
	if (rc < 0) {
		err("record connection");
		return -1;
	}
	info("remote device on busid %s: attach complete", opt.busid);
	return rhport;
}

static void monitor_disconnect(usbip_monitor_t *monitor, char *busid, int rhport)
{
	// To monitor unbind we must first ensure to be at a bound state. To
	// monitor bound state a local busid is needed, which is unknown at this
	// moment. Local busid is not available until it's already bound to the usbip
	// driver. Thus monitor bind events for any usb device until the busid is
	// available for the port.
	char local_busid[SYSFS_BUS_ID_SIZE] = {};

	while (get_local_busid_from(rhport, local_busid))
		usbip_monitor_await_usb_bind(monitor, USBIP_USB_DRV_NAME);
	info("remote device on busid %s: monitor disconnect", busid);
	usbip_monitor_set_busid(monitor, local_busid);
	usbip_monitor_await_usb_unbind(monitor);
	usbip_monitor_set_busid(monitor, NULL);
}

static int attach_device_persistently(char *host, struct attach_options opt)
{
	int rc = 0;
	usbip_monitor_t *monitor = usbip_monitor_new();

	while (rc == 0) {
		int rhport = attach_device(host, opt);

		if (rhport < 0)
			rc = -1;
		else
			monitor_disconnect(monitor, opt.busid, rhport);
	}
	usbip_monitor_delete(monitor);
	return rc;
}

int usbip_attach(int argc, char *argv[])
{
	static const struct option opts[] = {
		{ "remote", required_argument, NULL, 'r' },
		{ "busid",  required_argument, NULL, 'b' },
		{ "device",  required_argument, NULL, 'd' },
		{ "persistent",  no_argument, NULL, 'p' },
		{ NULL, 0,  NULL, 0 }
	};
	char *host = NULL;
	struct attach_options options = {};
	int opt;
	int ret = -1;

	for (;;) {
		opt = getopt_long(argc, argv, "d:r:b:p", opts, NULL);

		if (opt == -1)
			break;

		switch (opt) {
		case 'r':
			host = optarg;
			break;
		case 'd':
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

	if (!host || !options.busid)
		goto err_out;

	if (options.is_persistent)
		ret = attach_device_persistently(host, options);
	else
		ret = attach_device(host, options);

	goto out;

err_out:
	usbip_attach_usage();
out:
	return ret;
}
