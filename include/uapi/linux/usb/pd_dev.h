/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _UAPI__LINUX_USB_PDDEV_H
#define _UAPI__LINUX_USB_PDDEV_H

#include <linux/types.h>
#include <linux/usb/pd.h>

/*
 * struct pd_info - USB Power Delivery Device Information
 * @specification_revision: USB Power Delivery Specification Revision
 * @supported_ctrl_msgs: Supported Control Messages
 * @supported_data_msgs: Supported Data Messages
 * @supported_ext_msgs: Supported Extended Messages
 *
 * @specification_revision is in the same format as the Specification Revision
 * Field in the Message Header. @supported_ctrl_msgs, @supported_data_msgs and
 * @supported_ext_msgs list the messages, a bit for each, that can be used with
 * USBPDDEV_SUBMIT_MESSAGE ioctl.
 */
struct pd_info {
	__u8 specification_revision; /* XXX I don't know if this is useful? */
	__u32 ctrl_msgs_supported;
	__u32 data_msgs_supported;
	__u32 ext_msgs_supported;
} __attribute__ ((packed));

/* Example configuration flags for ports. */
#define USBPDDEV_CFPORT_ENTER_MODES	BIT(0) /* Automatic alt mode entry. */

/*
 * For basic communication use USBPDDEV_SUBMIT_MESSAGE ioctl. GoodCRC is not
 * supported. Response will also never be GoodCRC.
 *
 * To check cached objects (if they are cached) use USBPDDEV_GET_MESSAGE ioctl.
 * Useful most likely with RDO and EUDO, but also with Identity etc.
 * USBPDDEV_SET_MESSAGE is primarily meant to be used with ports. If supported,
 * it can be used to assign the values for objects like EUDO that the port should
 * use in future communication.
 *
 * The idea with USBPDDEV_CONFIGURE is that you could modify the behaviour of
 * the underlying TCPM (or what ever interface you have) with some things. So
 * for example, you could disable automatic alternate mode entry with it with
 * that USBPDDEV_CFPORT_ENTER_MODES - It's just an example! - so basically, you
 * could take over some things from TCPM with it.
 */

#define USBPDDEV_INFO		_IOR('P', 0x70, struct pd_info)
#define USBPDDEV_CONFIGURE	_IOW('P', 0x71, __u32)
#define USBPDDEV_PWR_ROLE	_IOR('P', 0x72, int) /* The *current* role! */
#define USBPDDEV_GET_MESSAGE	_IOWR('P', 0x73, struct pd_message)
#define USBPDDEV_SET_MESSAGE	_IOW('P', 0x74, struct pd_message)
#define USBPDDEV_SUBMIT_MESSAGE	_IOWR('P', 0x75, struct pd_message)

#endif /* _UAPI__LINUX_USB_PDDEV_H */
