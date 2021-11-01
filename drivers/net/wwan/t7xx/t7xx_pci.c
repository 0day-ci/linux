// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021, Intel Corporation.
 *
 * Authors: Haijun Lio <haijun.liu@mediatek.com>
 * Contributors: Amir Hanania <amir.hanania@intel.com>
 *               Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *               Chiranjeevi Rapolu <chiranjeevi.rapolu@intel.com>
 *               Eliot Lee <eliot.lee@intel.com>
 *               Moises Veleta <moises.veleta@intel.com>
 *               Ricardo Martinez<ricardo.martinez@linux.intel.com>
 *               Sreehari Kancharla <sreehari.kancharla@intel.com>
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>

#include "t7xx_mhccif.h"
#include "t7xx_modem_ops.h"
#include "t7xx_pci.h"
#include "t7xx_pcie_mac.h"
#include "t7xx_reg.h"
#include "t7xx_skb_util.h"

#define PCI_IREG_BASE			0
#define PCI_EREG_BASE			2

static int mtk_request_irq(struct pci_dev *pdev)
{
	struct mtk_pci_dev *mtk_dev;
	int ret, i;

	mtk_dev = pci_get_drvdata(pdev);

	for (i = 0; i < EXT_INT_NUM; i++) {
		const char *irq_descr;
		int irq_vec;

		if (!mtk_dev->intr_handler[i])
			continue;

		irq_descr = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s_%d",
					   dev_driver_string(&pdev->dev), i);
		if (!irq_descr)
			return -ENOMEM;

		irq_vec = pci_irq_vector(pdev, i);
		ret = request_threaded_irq(irq_vec, mtk_dev->intr_handler[i],
					   mtk_dev->intr_thread[i], 0, irq_descr,
					   mtk_dev->callback_param[i]);
		if (ret) {
			dev_err(&pdev->dev, "Failed to request_irq: %d, int: %d, ret: %d\n",
				irq_vec, i, ret);
			while (i--) {
				if (!mtk_dev->intr_handler[i])
					continue;

				free_irq(pci_irq_vector(pdev, i), mtk_dev->callback_param[i]);
			}

			return ret;
		}
	}

	return 0;
}

static int mtk_setup_msix(struct mtk_pci_dev *mtk_dev)
{
	int ret;

	/* We are interested only in 6 interrupts, but HW-design requires power-of-2
	 * IRQs allocation.
	 */
	ret = pci_alloc_irq_vectors(mtk_dev->pdev, EXT_INT_NUM, EXT_INT_NUM, PCI_IRQ_MSIX);
	if (ret < 0) {
		dev_err(&mtk_dev->pdev->dev, "Failed to allocate MSI-X entry, errno: %d\n", ret);
		return ret;
	}

	ret = mtk_request_irq(mtk_dev->pdev);
	if (ret) {
		pci_free_irq_vectors(mtk_dev->pdev);
		return ret;
	}

	/* Set MSIX merge config */
	mtk_pcie_mac_msix_cfg(mtk_dev, EXT_INT_NUM);
	return 0;
}

static int mtk_interrupt_init(struct mtk_pci_dev *mtk_dev)
{
	int ret, i;

	if (!mtk_dev->pdev->msix_cap)
		return -EINVAL;

	ret = mtk_setup_msix(mtk_dev);
	if (ret)
		return ret;

	/* let the IPs enable interrupts when they are ready */
	for (i = EXT_INT_START; i < EXT_INT_START + EXT_INT_NUM; i++)
		PCIE_MAC_MSIX_MSK_SET(mtk_dev, i);

	return 0;
}

static inline void mtk_pci_infracfg_ao_calc(struct mtk_pci_dev *mtk_dev)
{
	mtk_dev->base_addr.infracfg_ao_base = mtk_dev->base_addr.pcie_ext_reg_base +
					      INFRACFG_AO_DEV_CHIP -
					      mtk_dev->base_addr.pcie_dev_reg_trsl_addr;
}

static int mtk_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct mtk_pci_dev *mtk_dev;
	int ret;

	mtk_dev = devm_kzalloc(&pdev->dev, sizeof(*mtk_dev), GFP_KERNEL);
	if (!mtk_dev)
		return -ENOMEM;

	pci_set_drvdata(pdev, mtk_dev);
	mtk_dev->pdev = pdev;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = pcim_iomap_regions(pdev, BIT(PCI_IREG_BASE) | BIT(PCI_EREG_BASE), pci_name(pdev));
	if (ret) {
		dev_err(&pdev->dev, "PCIm iomap regions fail %d\n", ret);
		return -ENOMEM;
	}

	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (ret) {
		ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (ret) {
			dev_err(&pdev->dev, "Could not set PCI DMA mask, err: %d\n", ret);
			return ret;
		}
	}

	ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	if (ret) {
		ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		if (ret) {
			dev_err(&pdev->dev, "Could not set consistent PCI DMA mask, err: %d\n",
				ret);
			return ret;
		}
	}

	IREG_BASE(mtk_dev) = pcim_iomap_table(pdev)[PCI_IREG_BASE];
	mtk_dev->base_addr.pcie_ext_reg_base = pcim_iomap_table(pdev)[PCI_EREG_BASE];

	ret = ccci_skb_pool_alloc(&mtk_dev->pools);
	if (ret)
		return ret;

	mtk_pcie_mac_atr_init(mtk_dev);
	mtk_pci_infracfg_ao_calc(mtk_dev);
	mhccif_init(mtk_dev);

	ret = mtk_md_init(mtk_dev);
	if (ret)
		goto err;

	mtk_pcie_mac_interrupts_dis(mtk_dev);
	ret = mtk_interrupt_init(mtk_dev);
	if (ret)
		goto err;

	mtk_pcie_mac_set_int(mtk_dev, MHCCIF_INT);
	mtk_pcie_mac_interrupts_en(mtk_dev);
	pci_set_master(pdev);

	return 0;

err:
	ccci_skb_pool_free(&mtk_dev->pools);
	return ret;
}

static void mtk_pci_remove(struct pci_dev *pdev)
{
	struct mtk_pci_dev *mtk_dev;
	int i;

	mtk_dev = pci_get_drvdata(pdev);
	mtk_md_exit(mtk_dev);

	for (i = 0; i < EXT_INT_NUM; i++) {
		if (!mtk_dev->intr_handler[i])
			continue;

		free_irq(pci_irq_vector(pdev, i), mtk_dev->callback_param[i]);
	}

	pci_free_irq_vectors(mtk_dev->pdev);
	ccci_skb_pool_free(&mtk_dev->pools);
}

static const struct pci_device_id t7xx_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x4d75) },
	{ }
};
MODULE_DEVICE_TABLE(pci, t7xx_pci_table);

static struct pci_driver mtk_pci_driver = {
	.name = "mtk_t7xx",
	.id_table = t7xx_pci_table,
	.probe = mtk_pci_probe,
	.remove = mtk_pci_remove,
};

static int __init mtk_pci_init(void)
{
	return pci_register_driver(&mtk_pci_driver);
}
module_init(mtk_pci_init);

static void __exit mtk_pci_cleanup(void)
{
	pci_unregister_driver(&mtk_pci_driver);
}
module_exit(mtk_pci_cleanup);

MODULE_AUTHOR("MediaTek Inc");
MODULE_DESCRIPTION("MediaTek PCIe 5G WWAN modem t7xx driver");
MODULE_LICENSE("GPL");
