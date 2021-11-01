/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021, Intel Corporation.
 *
 * Authors: Haijun Lio <haijun.liu@mediatek.com>
 * Contributors: Amir Hanania <amir.hanania@intel.com>
 *               Chiranjeevi Rapolu <chiranjeevi.rapolu@intel.com>
 *               Moises Veleta <moises.veleta@intel.com>
 *               Ricardo Martinez<ricardo.martinez@linux.intel.com>
 *               Sreehari Kancharla <sreehari.kancharla@intel.com>
 */

#ifndef __T7XX_PCI_H__
#define __T7XX_PCI_H__

#include <linux/pci.h>
#include <linux/types.h>

#include "t7xx_reg.h"
#include "t7xx_skb_util.h"

/* struct mtk_addr_base - holds base addresses
 * @pcie_mac_ireg_base: PCIe MAC register base
 * @pcie_ext_reg_base: used to calculate base addresses for CLDMA, DPMA and MHCCIF registers
 * @pcie_dev_reg_trsl_addr: used to calculate the register base address
 * @infracfg_ao_base: base address used in CLDMA reset operations
 * @mhccif_rc_base: host view of MHCCIF rc base addr
 */
struct mtk_addr_base {
	void __iomem *pcie_mac_ireg_base;
	void __iomem *pcie_ext_reg_base;
	u32 pcie_dev_reg_trsl_addr;
	void __iomem *infracfg_ao_base;
	void __iomem *mhccif_rc_base;
};

typedef irqreturn_t (*mtk_intr_callback)(int irq, void *param);

/* struct mtk_pci_dev - MTK device context structure
 * @intr_handler: array of handler function for request_threaded_irq
 * @intr_thread: array of thread_fn for request_threaded_irq
 * @callback_param: array of cookie passed back to interrupt functions
 * @mhccif_bitmask: device to host interrupt mask
 * @pdev: pci device
 * @base_addr: memory base addresses of HW components
 * @md: modem interface
 * @ccmni_ctlb: context structure used to control the network data path
 * @rgu_pci_irq_en: RGU callback isr registered and active
 * @pools: pre allocated skb pools
 */
struct mtk_pci_dev {
	mtk_intr_callback	intr_handler[EXT_INT_NUM];
	mtk_intr_callback	intr_thread[EXT_INT_NUM];
	void			*callback_param[EXT_INT_NUM];
	u32			mhccif_bitmask;
	struct pci_dev		*pdev;
	struct mtk_addr_base	base_addr;
	struct mtk_modem	*md;

	struct ccmni_ctl_block	*ccmni_ctlb;
	bool			rgu_pci_irq_en;
	struct skb_pools	pools;
};

#endif /* __T7XX_PCI_H__ */
