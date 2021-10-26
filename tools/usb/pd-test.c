// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * USB Power Delivery device tester.
 *
 * Copyright (C) 2021 Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/types.h>

struct pd_message {
	__le16 header;
	__le32 payload[7];
} __attribute__((packed));

struct pd_info {
	__u8 specification_revision;
	__u32 ctrl_msgs_supported;
	__u32 data_msgs_supported;
	__u32 ext_msgs_supported;
} __attribute__((packed));

#define USBPDDEV_INFO		_IOR('P', 0x70, struct pd_info)
#define USBPDDEV_CONFIGURE	_IOW('P', 0x71, __u32)
#define USBPDDEV_PWR_ROLE	_IOR('P', 0x72, int)
#define USBPDDEV_GET_MESSAGE	_IOWR('P', 0x73, struct pd_message)
#define USBPDDEV_SET_MESSAGE	_IOW('P', 0x74, struct pd_message)
#define USBPDDEV_SUBMIT_MESSAGE	_IOWR('P', 0x75, struct pd_message)

enum pd_data_msg_type {
	/* 0 Reserved */
	PD_DATA_SOURCE_CAP = 1,
	PD_DATA_REQUEST = 2,
	PD_DATA_BIST = 3,
	PD_DATA_SINK_CAP = 4,
	PD_DATA_BATT_STATUS = 5,
	PD_DATA_ALERT = 6,
	PD_DATA_GET_COUNTRY_INFO = 7,
	PD_DATA_ENTER_USB = 8,
	/* 9-14 Reserved */
	PD_DATA_VENDOR_DEF = 15,
	/* 16-31 Reserved */
};

int dump_source_pdos(int fd)
{
	struct pd_message msg = {};
	int ret;
	int i;

	msg.header = PD_DATA_SOURCE_CAP;
	ret = ioctl(fd, USBPDDEV_GET_MESSAGE, &msg);
	if (ret < 0) {
		printf("No cached Source Capabilities %d\n", ret);
		return ret;
	}

	printf("Source Capabilities:\n");

	for (i = 0; i < (msg.header >> 12 & 7); i++)
		printf("  PDO%d: 0x%08x\n", i + 1, msg.payload[i]);

	return 0;
}

int dump_sink_pdos(int fd)
{
	struct pd_message msg = {};
	int ret;
	int i;

	msg.header = PD_DATA_SINK_CAP;
	ret = ioctl(fd, USBPDDEV_GET_MESSAGE, &msg);
	if (ret < 0) {
		printf("No cached Sink Capabilities %d\n", ret);
		return ret;
	}

	printf("Sink Capabilities:\n");

	for (i = 0; i < (msg.header >> 12 & 7); i++)
		printf("  PDO%d: 0x%08x\n", i + 1, msg.payload[i]);

	return 0;
}

int main(int argc, char **argv)
{
	unsigned int role;
	int ret;
	int fd;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s [DEV]\n"
				"       %% %s /dev/pd0/port\n\n",
				argv[0], argv[0]);
		return -1;
	}

	fd = open(argv[1], O_RDWR);
	if (fd < 0)
		return fd;

	ret = ioctl(fd, USBPDDEV_PWR_ROLE, &role);
	if (ret < 0) {
		printf("USBPDDEV_PWR_ROLE failed %d\n", ret);
		goto err;
	}

	if (role)
		ret = dump_source_pdos(fd);
	else
		ret = dump_sink_pdos(fd);
err:
	close(fd);

	return ret;
}
