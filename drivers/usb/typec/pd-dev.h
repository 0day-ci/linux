/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __USB_TYPEC_PDDEV__
#define __USB_TYPEC_PDDEV__

#include <linux/kdev_t.h>

#define PD_DEV_MAJOR	MAJOR(usbpd_devt)

extern dev_t usbpd_devt;

int usbpd_dev_init(void);
void usbpd_dev_exit(void);

#endif /* __USB_TYPEC_PDDEV__ */
