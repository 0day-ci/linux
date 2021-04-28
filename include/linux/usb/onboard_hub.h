/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __LINUX_USB_ONBOARD_HUB_H
#define __LINUX_USB_ONBOARD_HUB_H

#if defined(CONFIG_USB_ONBOARD_HUB) || defined(CONFIG_COMPILE_TEST)
bool of_is_onboard_usb_hub(const struct device_node *np);
#else
static inline bool of_is_onboard_usb_hub(const struct device_node *np)
{
	return false;
}
#endif

#endif /* __LINUX_USB_ONBOARD_HUB_H */
