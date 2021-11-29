/* SPDX-License-Identifier: GPL-2.0-only */

#define CMAC_BUF_SIZE		2048

enum rtl_dash_type {
	RTL_DASH_NONE,
	RTL_DASH_DP,
	RTL_DASH_EP,
	RTL_DASH_FP,
};

struct rtl_dash;

void rtl_release_dash(struct rtl_dash *dash);
void rtl_dash_up(struct rtl_dash *dash);
void rtl_dash_down(struct rtl_dash *dash);
void rtl_dash_cmac_reset_indicate(struct rtl_dash *dash);
void rtl_dash_interrupt(struct rtl_dash *dash);
void rtl_dash_set_ap_ready(struct rtl_dash *dash, bool enable);

int rtl_dash_to_fw(struct rtl_dash *dash, u8 *src, int size);
int rtl_dash_from_fw(struct rtl_dash *dash, u8 *src, int size);

bool rtl_dash_get_ap_ready(struct rtl_dash *dash);

ssize_t rtl_dash_info(struct rtl_dash *dash, char *buf);

struct rtl_dash *rtl_request_dash(struct rtl8169_private *tp,
				  struct pci_dev *pci_dev, enum mac_version ver,
				  void __iomem *mmio_addr);
