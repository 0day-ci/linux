/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file holds the definitions of quirks found in xHCI USB hosts.
 */

#ifndef __LINUX_USB_XHCI_QUIRKS_H
#define __LINUX_USB_XHCI_QUIRKS_H

#define	XHCI_LINK_TRB_QUIRK		BIT_ULL(0)
#define XHCI_RESET_EP_QUIRK		BIT_ULL(1)
#define XHCI_NEC_HOST			BIT_ULL(2)
#define XHCI_AMD_PLL_FIX		BIT_ULL(3)
#define XHCI_SPURIOUS_SUCCESS		BIT_ULL(4)
/*
 * Certain Intel host controllers have a limit to the number of endpoint
 * contexts they can handle.  Ideally, they would signal that they can't handle
 * anymore endpoint contexts by returning a Resource Error for the Configure
 * Endpoint command, but they don't.  Instead they expect software to keep track
 * of the number of active endpoints for them, across configure endpoint
 * commands, reset device commands, disable slot commands, and address device
 * commands.
 */
#define XHCI_EP_LIMIT_QUIRK		BIT_ULL(5)
#define XHCI_BROKEN_MSI			BIT_ULL(6)
#define XHCI_RESET_ON_RESUME		BIT_ULL(7)
#define	XHCI_SW_BW_CHECKING		BIT_ULL(8)
#define XHCI_AMD_0x96_HOST		BIT_ULL(9)
#define XHCI_TRUST_TX_LENGTH		BIT_ULL(10)
#define XHCI_LPM_SUPPORT		BIT_ULL(11)
#define XHCI_INTEL_HOST			BIT_ULL(12)
#define XHCI_SPURIOUS_REBOOT		BIT_ULL(13)
#define XHCI_COMP_MODE_QUIRK		BIT_ULL(14)
#define XHCI_AVOID_BEI			BIT_ULL(15)
#define XHCI_PLAT			BIT_ULL(16)
#define XHCI_SLOW_SUSPEND		BIT_ULL(17)
#define XHCI_SPURIOUS_WAKEUP		BIT_ULL(18)
/* For controllers with a broken beyond repair streams implementation */
#define XHCI_BROKEN_STREAMS		BIT_ULL(19)
#define XHCI_PME_STUCK_QUIRK		BIT_ULL(20)
#define XHCI_MTK_HOST			BIT_ULL(21)
#define XHCI_SSIC_PORT_UNUSED		BIT_ULL(22)
#define XHCI_NO_64BIT_SUPPORT		BIT_ULL(23)
#define XHCI_MISSING_CAS		BIT_ULL(24)
/* For controller with a broken Port Disable implementation */
#define XHCI_BROKEN_PORT_PED		BIT_ULL(25)
#define XHCI_LIMIT_ENDPOINT_INTERVAL_7	BIT_ULL(26)
#define XHCI_U2_DISABLE_WAKE		BIT_ULL(27)
#define XHCI_ASMEDIA_MODIFY_FLOWCONTROL	BIT_ULL(28)
#define XHCI_HW_LPM_DISABLE		BIT_ULL(29)
#define XHCI_SUSPEND_DELAY		BIT_ULL(30)
#define XHCI_INTEL_USB_ROLE_SW		BIT_ULL(31)
#define XHCI_ZERO_64B_REGS		BIT_ULL(32)
#define XHCI_DEFAULT_PM_RUNTIME_ALLOW	BIT_ULL(33)
#define XHCI_RESET_PLL_ON_DISCONNECT	BIT_ULL(34)
#define XHCI_SNPS_BROKEN_SUSPEND	BIT_ULL(35)
#define XHCI_RENESAS_FW_QUIRK		BIT_ULL(36)
#define XHCI_SKIP_PHY_INIT		BIT_ULL(37)
#define XHCI_DISABLE_SPARSE		BIT_ULL(38)
#define XHCI_SG_TRB_CACHE_SIZE_QUIRK	BIT_ULL(39)
#define XHCI_NO_SOFT_RETRY		BIT_ULL(40)
#define XHCI_ISOC_BLOCKED_DISCONNECT	BIT_ULL(41)
#define XHCI_LIMIT_FS_BI_INTR_EP	BIT_ULL(42)
#define XHCI_LOST_DISCONNECT_QUIRK	BIT_ULL(43)

struct usb_hcd;

struct xhci_plat_priv {
	const char *firmware_name;
	unsigned long long quirks;
	int (*plat_setup)(struct usb_hcd *hcd);
	void (*plat_start)(struct usb_hcd *hcd);
	int (*init_quirk)(struct usb_hcd *hcd);
	int (*suspend_quirk)(struct usb_hcd *hcd);
	int (*resume_quirk)(struct usb_hcd *hcd);
};

#define hcd_to_xhci_priv(h) ((struct xhci_plat_priv *)hcd_to_xhci(h)->priv)
#define xhci_to_priv(x) ((struct xhci_plat_priv *)(x)->priv)

#endif /* __LINUX_USB_XHCI_QUIRKS_H */
