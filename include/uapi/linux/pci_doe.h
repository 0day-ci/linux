/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userspace interface for safely accessing a PCI Data Exchange Object
 * mailbox that has been registered by a driver.
 */

#ifndef LINUX_PCI_DOE_UAPI_H
#define LINUX_PCI_DOE_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

struct pci_doe_uexchange {
	__u16 vid;
	__u8 protocol;
	__u8 rsvd;
	__u32 retval;
	struct {
		__s32 size;
		__u32 rsvd;
		__u64 payload;
	} in;
	struct {
		__s32 size;
		__u32 rsvd;
		__u64 payload;
	} out;
};

#define PCI_DOE_EXCHANGE _IOWR(0xDA, 1, struct pci_doe_uexchange)

#endif /* LINUX_PCI_DOE_UAPI_H */
