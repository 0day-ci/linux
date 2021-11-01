// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021, Intel Corporation.
 *
 * Authors: Haijun Lio <haijun.liu@mediatek.com>
 * Contributors: Amir Hanania <amir.hanania@intel.com>
 *               Chiranjeevi Rapolu <chiranjeevi.rapolu@intel.com>
 *               Eliot Lee <eliot.lee@intel.com>
 *               Moises Veleta <moises.veleta@intel.com>
 *               Ricardo Martinez<ricardo.martinez@linux.intel.com>
 *               Sreehari Kancharla <sreehari.kancharla@intel.com>
 */

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/device.h>

#include "t7xx_hif_cldma.h"
#include "t7xx_mhccif.h"
#include "t7xx_modem_ops.h"
#include "t7xx_monitor.h"
#include "t7xx_pci.h"
#include "t7xx_pcie_mac.h"

#define RGU_RESET_DELAY_US	20
#define PORT_RESET_DELAY_US	2000

enum mtk_feature_support_type {
	MTK_FEATURE_DOES_NOT_EXIST,
	MTK_FEATURE_NOT_SUPPORTED,
	MTK_FEATURE_MUST_BE_SUPPORTED,
};

static inline unsigned int get_interrupt_status(struct mtk_pci_dev *mtk_dev)
{
	return mhccif_read_sw_int_sts(mtk_dev) & D2H_SW_INT_MASK;
}

/**
 * mtk_pci_mhccif_isr() - Process MHCCIF interrupts
 * @mtk_dev: MTK device
 *
 * Check the interrupt status, and queue commands accordingly
 *
 * Returns: 0 on success or -EINVAL on failure
 */
int mtk_pci_mhccif_isr(struct mtk_pci_dev *mtk_dev)
{
	struct md_sys_info *md_info;
	struct ccci_fsm_ctl *ctl;
	struct mtk_modem *md;
	unsigned int int_sta;
	unsigned long flags;
	u32 mask;

	md = mtk_dev->md;
	ctl = fsm_get_entry();
	if (!ctl) {
		dev_err(&mtk_dev->pdev->dev,
			"process MHCCIF interrupt before modem monitor was initialized\n");
		return -EINVAL;
	}

	md_info = md->md_info;
	spin_lock_irqsave(&md_info->exp_spinlock, flags);
	int_sta = get_interrupt_status(mtk_dev);
	md_info->exp_id |= int_sta;

	if (md_info->exp_id & D2H_INT_PORT_ENUM) {
		md_info->exp_id &= ~D2H_INT_PORT_ENUM;
		if (ctl->curr_state == CCCI_FSM_INIT ||
		    ctl->curr_state == CCCI_FSM_PRE_START ||
		    ctl->curr_state == CCCI_FSM_STOPPED)
			ccci_fsm_recv_md_interrupt(MD_IRQ_PORT_ENUM);
	}

	if (md_info->exp_id & D2H_INT_EXCEPTION_INIT) {
		if (ctl->md_state == MD_STATE_INVALID ||
		    ctl->md_state == MD_STATE_WAITING_FOR_HS1 ||
		    ctl->md_state == MD_STATE_WAITING_FOR_HS2 ||
		    ctl->md_state == MD_STATE_READY) {
			md_info->exp_id &= ~D2H_INT_EXCEPTION_INIT;
			ccci_fsm_recv_md_interrupt(MD_IRQ_CCIF_EX);
		}
	} else if (ctl->md_state == MD_STATE_WAITING_FOR_HS1) {
		/* start handshake if MD not assert */
		mask = mhccif_mask_get(mtk_dev);
		if ((md_info->exp_id & D2H_INT_ASYNC_MD_HK) && !(mask & D2H_INT_ASYNC_MD_HK)) {
			md_info->exp_id &= ~D2H_INT_ASYNC_MD_HK;
			queue_work(md->handshake_wq, &md->handshake_work);
		}
	}

	spin_unlock_irqrestore(&md_info->exp_spinlock, flags);

	return 0;
}

static void clr_device_irq_via_pcie(struct mtk_pci_dev *mtk_dev)
{
	struct mtk_addr_base *pbase_addr;
	void __iomem *rgu_pciesta_reg;

	pbase_addr = &mtk_dev->base_addr;
	rgu_pciesta_reg = pbase_addr->pcie_ext_reg_base + TOPRGU_CH_PCIE_IRQ_STA -
			  pbase_addr->pcie_dev_reg_trsl_addr;

	/* clear RGU PCIe IRQ state */
	iowrite32(ioread32(rgu_pciesta_reg), rgu_pciesta_reg);
}

void mtk_clear_rgu_irq(struct mtk_pci_dev *mtk_dev)
{
	/* clear L2 */
	clr_device_irq_via_pcie(mtk_dev);
	/* clear L1 */
	mtk_pcie_mac_clear_int_status(mtk_dev, SAP_RGU_INT);
}

static int mtk_acpi_reset(struct mtk_pci_dev *mtk_dev, char *fn_name)
{
#ifdef CONFIG_ACPI
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_status acpi_ret;
	struct device *dev;
	acpi_handle handle;

	dev = &mtk_dev->pdev->dev;

	if (acpi_disabled) {
		dev_err(dev, "acpi function isn't enabled\n");
		return -EFAULT;
	}

	handle = ACPI_HANDLE(dev);
	if (!handle) {
		dev_err(dev, "acpi handle isn't found\n");
		return -EFAULT;
	}

	if (!acpi_has_method(handle, fn_name)) {
		dev_err(dev, "%s method isn't found\n", fn_name);
		return -EFAULT;
	}

	acpi_ret = acpi_evaluate_object(handle, fn_name, NULL, &buffer);
	if (ACPI_FAILURE(acpi_ret)) {
		dev_err(dev, "%s method fail: %s\n", fn_name, acpi_format_exception(acpi_ret));
		return -EFAULT;
	}
#endif
	return 0;
}

int mtk_acpi_fldr_func(struct mtk_pci_dev *mtk_dev)
{
	return mtk_acpi_reset(mtk_dev, "_RST");
}

static void reset_device_via_pmic(struct mtk_pci_dev *mtk_dev)
{
	unsigned int val;

	val = ioread32(IREG_BASE(mtk_dev) + PCIE_MISC_DEV_STATUS);

	if (val & MISC_RESET_TYPE_PLDR)
		mtk_acpi_reset(mtk_dev, "MRST._RST");
	else if (val & MISC_RESET_TYPE_FLDR)
		mtk_acpi_fldr_func(mtk_dev);
}

static irqreturn_t rgu_isr_thread(int irq, void *data)
{
	struct mtk_pci_dev *mtk_dev;
	struct mtk_modem *modem;

	mtk_dev = data;
	modem = mtk_dev->md;

	msleep(RGU_RESET_DELAY_US);
	reset_device_via_pmic(modem->mtk_dev);
	return IRQ_HANDLED;
}

static irqreturn_t rgu_isr_handler(int irq, void *data)
{
	struct mtk_pci_dev *mtk_dev;
	struct mtk_modem *modem;

	mtk_dev = data;
	modem = mtk_dev->md;

	mtk_clear_rgu_irq(mtk_dev);

	if (!mtk_dev->rgu_pci_irq_en)
		return IRQ_HANDLED;

	atomic_set(&modem->rgu_irq_asserted, 1);
	mtk_pcie_mac_clear_int(mtk_dev, SAP_RGU_INT);
	return IRQ_WAKE_THREAD;
}

static void mtk_pcie_register_rgu_isr(struct mtk_pci_dev *mtk_dev)
{
	/* registers RGU callback isr with PCIe driver */
	mtk_pcie_mac_clear_int(mtk_dev, SAP_RGU_INT);
	mtk_pcie_mac_clear_int_status(mtk_dev, SAP_RGU_INT);

	mtk_dev->intr_handler[SAP_RGU_INT] = rgu_isr_handler;
	mtk_dev->intr_thread[SAP_RGU_INT] = rgu_isr_thread;
	mtk_dev->callback_param[SAP_RGU_INT] = mtk_dev;
	mtk_pcie_mac_set_int(mtk_dev, SAP_RGU_INT);
}

static void md_exception(struct mtk_modem *md, enum hif_ex_stage stage)
{
	struct mtk_pci_dev *mtk_dev;

	mtk_dev = md->mtk_dev;

	if (stage == HIF_EX_CLEARQ_DONE)
		/* give DHL time to flush data.
		 * this is an empirical value that assure
		 * that DHL have enough time to flush all the date.
		 */
		msleep(PORT_RESET_DELAY_US);

	cldma_exception(ID_CLDMA1, stage);

	if (stage == HIF_EX_INIT)
		mhccif_h2d_swint_trigger(mtk_dev, H2D_CH_EXCEPTION_ACK);
	else if (stage == HIF_EX_CLEARQ_DONE)
		mhccif_h2d_swint_trigger(mtk_dev, H2D_CH_EXCEPTION_CLEARQ_ACK);
}

static int wait_hif_ex_hk_event(struct mtk_modem *md, int event_id)
{
	struct md_sys_info *md_info;
	int sleep_time = 10;
	int retries = 500; /* MD timeout is 5s */

	md_info = md->md_info;
	do {
		if (md_info->exp_id & event_id)
			return 0;

		msleep(sleep_time);
	} while (--retries);

	return -EFAULT;
}

static void md_sys_sw_init(struct mtk_pci_dev *mtk_dev)
{
	/* Register the MHCCIF isr for MD exception, port enum and
	 * async handshake notifications.
	 */
	mhccif_mask_set(mtk_dev, D2H_SW_INT_MASK);
	mtk_dev->mhccif_bitmask = D2H_SW_INT_MASK;
	mhccif_mask_clr(mtk_dev, D2H_INT_PORT_ENUM);

	/* register RGU irq handler for sAP exception notification */
	mtk_dev->rgu_pci_irq_en = true;
	mtk_pcie_register_rgu_isr(mtk_dev);
}

static void md_hk_wq(struct work_struct *work)
{
	struct ccci_fsm_ctl *ctl;
	struct mtk_modem *md;

	ctl = fsm_get_entry();

	cldma_switch_cfg(ID_CLDMA1);
	cldma_start(ID_CLDMA1);
	fsm_broadcast_state(ctl, MD_STATE_WAITING_FOR_HS2);
	md = container_of(work, struct mtk_modem, handshake_work);
	atomic_set(&md->core_md.ready, 1);
	wake_up(&ctl->async_hk_wq);
}

void mtk_md_event_notify(struct mtk_modem *md, enum md_event_id evt_id)
{
	struct md_sys_info *md_info;
	void __iomem *mhccif_base;
	struct ccci_fsm_ctl *ctl;
	unsigned int int_sta;
	unsigned long flags;

	ctl = fsm_get_entry();
	md_info = md->md_info;

	switch (evt_id) {
	case FSM_PRE_START:
		mhccif_mask_clr(md->mtk_dev, D2H_INT_PORT_ENUM);
		break;

	case FSM_START:
		mhccif_mask_set(md->mtk_dev, D2H_INT_PORT_ENUM);
		spin_lock_irqsave(&md_info->exp_spinlock, flags);
		int_sta = get_interrupt_status(md->mtk_dev);
		md_info->exp_id |= int_sta;
		if (md_info->exp_id & D2H_INT_EXCEPTION_INIT) {
			atomic_set(&ctl->exp_flg, 1);
			md_info->exp_id &= ~D2H_INT_EXCEPTION_INIT;
			md_info->exp_id &= ~D2H_INT_ASYNC_MD_HK;
		} else if (atomic_read(&ctl->exp_flg)) {
			md_info->exp_id &= ~D2H_INT_ASYNC_MD_HK;
		} else if (md_info->exp_id & D2H_INT_ASYNC_MD_HK) {
			queue_work(md->handshake_wq, &md->handshake_work);
			md_info->exp_id &= ~D2H_INT_ASYNC_MD_HK;
			mhccif_base = md->mtk_dev->base_addr.mhccif_rc_base;
			iowrite32(D2H_INT_ASYNC_MD_HK, mhccif_base + REG_EP2RC_SW_INT_ACK);
			mhccif_mask_set(md->mtk_dev, D2H_INT_ASYNC_MD_HK);
		} else {
			/* unmask async handshake interrupt */
			mhccif_mask_clr(md->mtk_dev, D2H_INT_ASYNC_MD_HK);
		}

		spin_unlock_irqrestore(&md_info->exp_spinlock, flags);
		/* unmask exception interrupt */
		mhccif_mask_clr(md->mtk_dev,
				D2H_INT_EXCEPTION_INIT |
				D2H_INT_EXCEPTION_INIT_DONE |
				D2H_INT_EXCEPTION_CLEARQ_DONE |
				D2H_INT_EXCEPTION_ALLQ_RESET);
		break;

	case FSM_READY:
		/* mask async handshake interrupt */
		mhccif_mask_set(md->mtk_dev, D2H_INT_ASYNC_MD_HK);
		break;

	default:
		break;
	}
}

static void md_structure_reset(struct mtk_modem *md)
{
	struct md_sys_info *md_info;

	md_info = md->md_info;
	md_info->exp_id = 0;
	spin_lock_init(&md_info->exp_spinlock);
}

void mtk_md_exception_handshake(struct mtk_modem *md)
{
	struct mtk_pci_dev *mtk_dev;
	int ret;

	mtk_dev = md->mtk_dev;
	md_exception(md, HIF_EX_INIT);
	ret = wait_hif_ex_hk_event(md, D2H_INT_EXCEPTION_INIT_DONE);

	if (ret)
		dev_err(&mtk_dev->pdev->dev, "EX CCIF HS timeout, RCH 0x%lx\n",
			D2H_INT_EXCEPTION_INIT_DONE);

	md_exception(md, HIF_EX_INIT_DONE);
	ret = wait_hif_ex_hk_event(md, D2H_INT_EXCEPTION_CLEARQ_DONE);
	if (ret)
		dev_err(&mtk_dev->pdev->dev, "EX CCIF HS timeout, RCH 0x%lx\n",
			D2H_INT_EXCEPTION_CLEARQ_DONE);

	md_exception(md, HIF_EX_CLEARQ_DONE);
	ret = wait_hif_ex_hk_event(md, D2H_INT_EXCEPTION_ALLQ_RESET);
	if (ret)
		dev_err(&mtk_dev->pdev->dev, "EX CCIF HS timeout, RCH 0x%lx\n",
			D2H_INT_EXCEPTION_ALLQ_RESET);

	md_exception(md, HIF_EX_ALLQ_RESET);
}

static struct mtk_modem *ccci_md_alloc(struct mtk_pci_dev *mtk_dev)
{
	struct mtk_modem *md;

	md = devm_kzalloc(&mtk_dev->pdev->dev, sizeof(*md), GFP_KERNEL);
	if (!md)
		return NULL;

	md->md_info = devm_kzalloc(&mtk_dev->pdev->dev, sizeof(*md->md_info), GFP_KERNEL);
	if (!md->md_info)
		return NULL;

	md->mtk_dev = mtk_dev;
	mtk_dev->md = md;
	atomic_set(&md->core_md.ready, 0);
	md->handshake_wq = alloc_workqueue("%s",
					   WQ_UNBOUND | WQ_MEM_RECLAIM | WQ_HIGHPRI,
					   0, "md_hk_wq");
	if (!md->handshake_wq)
		return NULL;

	INIT_WORK(&md->handshake_work, md_hk_wq);
	md->core_md.feature_set[RT_ID_MD_PORT_ENUM] &= ~FEATURE_MSK;
	md->core_md.feature_set[RT_ID_MD_PORT_ENUM] |=
		FIELD_PREP(FEATURE_MSK, MTK_FEATURE_MUST_BE_SUPPORTED);
	return md;
}

void mtk_md_reset(struct mtk_pci_dev *mtk_dev)
{
	struct mtk_modem *md;

	md = mtk_dev->md;
	md->md_init_finish = false;
	md_structure_reset(md);
	ccci_fsm_reset();
	cldma_reset(ID_CLDMA1);
	md->md_init_finish = true;
}

/**
 * mtk_md_init() - Initialize modem
 * @mtk_dev: MTK device
 *
 * Allocate and initialize MD ctrl block, and initialize data path
 * Register MHCCIF ISR and RGU ISR, and start the state machine
 *
 * Return: 0 on success or -ENOMEM on allocation failure
 */
int mtk_md_init(struct mtk_pci_dev *mtk_dev)
{
	struct ccci_fsm_ctl *fsm_ctl;
	struct mtk_modem *md;
	int ret;

	/* allocate and initialize md ctrl memory */
	md = ccci_md_alloc(mtk_dev);
	if (!md)
		return -ENOMEM;

	ret = cldma_alloc(ID_CLDMA1, mtk_dev);
	if (ret)
		goto err_alloc;

	/* initialize md ctrl block */
	md_structure_reset(md);

	ret = ccci_fsm_init(md);
	if (ret)
		goto err_alloc;

	ret = cldma_init(ID_CLDMA1);
	if (ret)
		goto err_fsm_init;

	fsm_ctl = fsm_get_entry();
	fsm_append_command(fsm_ctl, CCCI_COMMAND_START, 0);

	md_sys_sw_init(mtk_dev);

	md->md_init_finish = true;
	return 0;

err_fsm_init:
	ccci_fsm_uninit();
err_alloc:
	destroy_workqueue(md->handshake_wq);

	dev_err(&mtk_dev->pdev->dev, "modem init failed\n");
	return ret;
}

void mtk_md_exit(struct mtk_pci_dev *mtk_dev)
{
	struct mtk_modem *md = mtk_dev->md;
	struct ccci_fsm_ctl *fsm_ctl;

	mtk_pcie_mac_clear_int(mtk_dev, SAP_RGU_INT);

	if (!md->md_init_finish)
		return;

	fsm_ctl = fsm_get_entry();
	/* change FSM state, will auto jump to stopped */
	fsm_append_command(fsm_ctl, CCCI_COMMAND_PRE_STOP, 1);
	cldma_exit(ID_CLDMA1);
	ccci_fsm_uninit();
	destroy_workqueue(md->handshake_wq);
}
