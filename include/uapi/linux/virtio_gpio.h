/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef _LINUX_VIRTIO_GPIO_H
#define _LINUX_VIRTIO_GPIO_H

#include <linux/types.h>

enum virtio_gpio_msg_type {
	// requests from cpu to device
	VIRTIO_GPIO_MSG_CPU_REQUEST		= 0x01,
	VIRTIO_GPIO_MSG_CPU_DIRECTION_INPUT	= 0x02,
	VIRTIO_GPIO_MSG_CPU_DIRECTION_OUTPUT	= 0x03,
	VIRTIO_GPIO_MSG_CPU_GET_DIRECTION	= 0x04,
	VIRTIO_GPIO_MSG_CPU_GET_LEVEL		= 0x05,
	VIRTIO_GPIO_MSG_CPU_SET_LEVEL		= 0x06,

	// messages from host to guest
	VIRTIO_GPIO_MSG_DEVICE_LEVEL		= 0x11,	// gpio state changed

	/* mask bit set on host->guest reply */
	VIRTIO_GPIO_MSG_REPLY			= 0x8000,
};

struct virtio_gpio_config {
	__u8    version;
	__u8    reserved0;
	__u16   num_gpios;
	__u32   names_size;
	__u8    reserved1[24];
	__u8    name[32];
};

struct virtio_gpio_msg {
	__le16 type;
	__le16 pin;
	__le32 value;
};

#endif /* _LINUX_VIRTIO_GPIO_H */
