// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, HiSilicon Ltd.
 */

#include <linux/device.h>
#include <linux/eventfd.h>
#include <linux/file.h>
#include <linux/hisi_acc_qm.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/vfio.h>
#include <linux/vfio_pci_core.h>

#include "hisi_acc_vfio_pci.h"

/* return 0 VM acc device ready, -ETIMEDOUT hardware timeout */
static int qm_wait_dev_ready(struct hisi_qm *qm)
{
	u32 val;

	return readl_relaxed_poll_timeout(qm->io_base + QM_VF_STATE,
				val, !(val & 0x1), MB_POLL_PERIOD_US,
				MB_POLL_TIMEOUT_US);
}

/*
 * Each state Reg is checked 100 times,
 * with a delay of 100 microseconds after each check
 */
static u32 acc_check_reg_state(struct hisi_qm *qm, u32 regs)
{
	int check_times = 0;
	u32 state;

	state = readl(qm->io_base + regs);
	while (state && check_times < ERROR_CHECK_TIMEOUT) {
		udelay(CHECK_DELAY_TIME);
		state = readl(qm->io_base + regs);
		check_times++;
	}

	return state;
}

/* Check the PF's RAS state and Function INT state */
static int qm_check_int_state(struct acc_vf_migration *acc_vf_dev)
{
	struct hisi_qm *vfqm = acc_vf_dev->vf_qm;
	struct hisi_qm *qm = acc_vf_dev->pf_qm;
	struct pci_dev *vf_pdev = acc_vf_dev->vf_dev;
	struct device *dev = &qm->pdev->dev;
	u32 state;

	/* Check RAS state */
	state = acc_check_reg_state(qm, QM_ABNORMAL_INT_STATUS);
	if (state) {
		dev_err(dev, "failed to check QM RAS state!\n");
		return -EBUSY;
	}

	/* Check Function Communication state between PF and VF */
	state = acc_check_reg_state(vfqm, QM_IFC_INT_STATUS);
	if (state) {
		dev_err(dev, "failed to check QM IFC INT state!\n");
		return -EBUSY;
	}
	state = acc_check_reg_state(vfqm, QM_IFC_INT_SET_V);
	if (state) {
		dev_err(dev, "failed to check QM IFC INT SET state!\n");
		return -EBUSY;
	}

	/* Check submodule task state */
	switch (vf_pdev->device) {
	case SEC_VF_PCI_DEVICE_ID:
		state = acc_check_reg_state(qm, SEC_CORE_INT_STATUS);
		if (state) {
			dev_err(dev, "failed to check QM SEC Core INT state!\n");
			return -EBUSY;
		}
		return 0;
	case HPRE_VF_PCI_DEVICE_ID:
		state = acc_check_reg_state(qm, HPRE_HAC_INT_STATUS);
		if (state) {
			dev_err(dev, "failed to check QM HPRE HAC INT state!\n");
			return -EBUSY;
		}
		return 0;
	case ZIP_VF_PCI_DEVICE_ID:
		state = acc_check_reg_state(qm, HZIP_CORE_INT_STATUS);
		if (state) {
			dev_err(dev, "failed to check QM ZIP Core INT state!\n");
			return -EBUSY;
		}
		return 0;
	default:
		dev_err(dev, "failed to detect acc module type!\n");
		return -EINVAL;
	}
}

static int qm_read_reg(struct hisi_qm *qm, u32 reg_addr,
		       u32 *data, u8 nums)
{
	int i;

	if (nums < 1 || nums > QM_REGS_MAX_LEN)
		return -EINVAL;

	for (i = 0; i < nums; i++) {
		data[i] = readl(qm->io_base + reg_addr);
		reg_addr += QM_REG_ADDR_OFFSET;
	}

	return 0;
}

static int qm_write_reg(struct hisi_qm *qm, u32 reg,
			u32 *data, u8 nums)
{
	int i;

	if (nums < 1 || nums > QM_REGS_MAX_LEN)
		return -EINVAL;

	for (i = 0; i < nums; i++)
		writel(data[i], qm->io_base + reg + i * QM_REG_ADDR_OFFSET);

	return 0;
}

static int qm_get_vft(struct hisi_qm *qm, u32 *base)
{
	u64 sqc_vft;
	u32 qp_num;
	int ret;

	ret = qm_mb(qm, QM_MB_CMD_SQC_VFT_V2, 0, 0, 1);
	if (ret)
		return ret;

	sqc_vft = readl(qm->io_base + QM_MB_CMD_DATA_ADDR_L) |
		  ((u64)readl(qm->io_base + QM_MB_CMD_DATA_ADDR_H) <<
		  QM_XQC_ADDR_OFFSET);
	*base = QM_SQC_VFT_BASE_MASK_V2 & (sqc_vft >> QM_SQC_VFT_BASE_SHIFT_V2);
	qp_num = (QM_SQC_VFT_NUM_MASK_V2 &
		  (sqc_vft >> QM_SQC_VFT_NUM_SHIFT_V2)) + 1;

	return qp_num;
}

static int qm_get_sqc(struct hisi_qm *qm, u64 *addr)
{
	int ret;

	ret = qm_mb(qm, QM_MB_CMD_SQC_BT, 0, 0, 1);
	if (ret)
		return ret;

	*addr = readl(qm->io_base + QM_MB_CMD_DATA_ADDR_L) |
		  ((u64)readl(qm->io_base + QM_MB_CMD_DATA_ADDR_H) <<
		  QM_XQC_ADDR_OFFSET);

	return 0;
}

static int qm_get_cqc(struct hisi_qm *qm, u64 *addr)
{
	int ret;

	ret = qm_mb(qm, QM_MB_CMD_CQC_BT, 0, 0, 1);
	if (ret)
		return ret;

	*addr = readl(qm->io_base + QM_MB_CMD_DATA_ADDR_L) |
		  ((u64)readl(qm->io_base + QM_MB_CMD_DATA_ADDR_H) <<
		  QM_XQC_ADDR_OFFSET);

	return 0;
}

static int qm_rw_regs_read(struct hisi_qm *qm, struct acc_vf_data *vf_data)
{
	struct device *dev = &qm->pdev->dev;
	int ret;

	ret = qm_read_reg(qm, QM_VF_AEQ_INT_MASK, &vf_data->aeq_int_mask, 1);
	if (ret) {
		dev_err(dev, "failed to read QM_VF_AEQ_INT_MASK\n");
		return ret;
	}

	ret = qm_read_reg(qm, QM_VF_EQ_INT_MASK, &vf_data->eq_int_mask, 1);
	if (ret) {
		dev_err(dev, "failed to read QM_VF_EQ_INT_MASK\n");
		return ret;
	}

	ret = qm_read_reg(qm, QM_IFC_INT_SOURCE_V,
			  &vf_data->ifc_int_source, 1);
	if (ret) {
		dev_err(dev, "failed to read QM_IFC_INT_SOURCE_V\n");
		return ret;
	}

	ret = qm_read_reg(qm, QM_IFC_INT_MASK, &vf_data->ifc_int_mask, 1);
	if (ret) {
		dev_err(dev, "failed to read QM_IFC_INT_MASK\n");
		return ret;
	}

	ret = qm_read_reg(qm, QM_IFC_INT_SET_V, &vf_data->ifc_int_set, 1);
	if (ret) {
		dev_err(dev, "failed to read QM_IFC_INT_SET_V\n");
		return ret;
	}

	ret = qm_read_reg(qm, QM_QUE_ISO_CFG_V, &vf_data->que_iso_cfg, 1);
	if (ret) {
		dev_err(dev, "failed to read QM_QUE_ISO_CFG_V\n");
		return ret;
	}

	ret = qm_read_reg(qm, QM_PAGE_SIZE, &vf_data->page_size, 1);
	if (ret) {
		dev_err(dev, "failed to read QM_PAGE_SIZE\n");
		return ret;
	}

	ret = qm_read_reg(qm, QM_VF_STATE, &vf_data->vf_state, 1);
	if (ret) {
		dev_err(dev, "failed to read QM_VF_STATE\n");
		return ret;
	}

	/* QM_EQC_DW has 7 regs */
	ret = qm_read_reg(qm, QM_EQC_DW0, vf_data->qm_eqc_dw, 7);
	if (ret) {
		dev_err(dev, "failed to read QM_EQC_DW\n");
		return ret;
	}

	/* QM_AEQC_DW has 7 regs */
	ret = qm_read_reg(qm, QM_AEQC_DW0, vf_data->qm_aeqc_dw, 7);
	if (ret) {
		dev_err(dev, "failed to read QM_AEQC_DW\n");
		return ret;
	}

	return 0;
}

static int qm_rw_regs_write(struct hisi_qm *qm, struct acc_vf_data *vf_data)
{
	struct device *dev = &qm->pdev->dev;
	int ret;

	/* check VF state */
	if (unlikely(qm_wait_mb_ready(qm))) {
		dev_err(&qm->pdev->dev, "QM device is not ready to write\n");
		return -EBUSY;
	}

	ret = qm_write_reg(qm, QM_VF_AEQ_INT_MASK, &vf_data->aeq_int_mask, 1);
	if (ret) {
		dev_err(dev, "failed to write QM_VF_AEQ_INT_MASK\n");
		return ret;
	}

	ret = qm_write_reg(qm, QM_VF_EQ_INT_MASK, &vf_data->eq_int_mask, 1);
	if (ret) {
		dev_err(dev, "failed to write QM_VF_EQ_INT_MASK\n");
		return ret;
	}

	ret = qm_write_reg(qm, QM_IFC_INT_SOURCE_V,
			   &vf_data->ifc_int_source, 1);
	if (ret) {
		dev_err(dev, "failed to write QM_IFC_INT_SOURCE_V\n");
		return ret;
	}

	ret = qm_write_reg(qm, QM_IFC_INT_MASK, &vf_data->ifc_int_mask, 1);
	if (ret) {
		dev_err(dev, "failed to write QM_IFC_INT_MASK\n");
		return ret;
	}

	ret = qm_write_reg(qm, QM_IFC_INT_SET_V, &vf_data->ifc_int_set, 1);
	if (ret) {
		dev_err(dev, "failed to write QM_IFC_INT_SET_V\n");
		return ret;
	}

	ret = qm_write_reg(qm, QM_QUE_ISO_CFG_V, &vf_data->que_iso_cfg, 1);
	if (ret) {
		dev_err(dev, "failed to write QM_QUE_ISO_CFG_V\n");
		return ret;
	}

	ret = qm_write_reg(qm, QM_PAGE_SIZE, &vf_data->page_size, 1);
	if (ret) {
		dev_err(dev, "failed to write QM_PAGE_SIZE\n");
		return ret;
	}

	ret = qm_write_reg(qm, QM_VF_STATE, &vf_data->vf_state, 1);
	if (ret) {
		dev_err(dev, "failed to write QM_VF_STATE\n");
		return ret;
	}

	/* QM_EQC_DW has 7 regs */
	ret = qm_write_reg(qm, QM_EQC_DW0, vf_data->qm_eqc_dw, 7);
	if (ret) {
		dev_err(dev, "failed to write QM_EQC_DW\n");
		return ret;
	}

	/* QM_AEQC_DW has 7 regs */
	ret = qm_write_reg(qm, QM_AEQC_DW0, vf_data->qm_aeqc_dw, 7);
	if (ret) {
		dev_err(dev, "failed to write QM_AEQC_DW\n");
		return ret;
	}

	return 0;
}

static void qm_db(struct hisi_qm *qm, u16 qn, u8 cmd,
		  u16 index, u8 priority)
{
	u64 doorbell;
	u64 dbase;
	u16 randata = 0;

	if (cmd == QM_DOORBELL_CMD_SQ || cmd == QM_DOORBELL_CMD_CQ)
		dbase = QM_DOORBELL_SQ_CQ_BASE_V2;
	else
		dbase = QM_DOORBELL_EQ_AEQ_BASE_V2;

	doorbell = qn | ((u64)cmd << QM_DB_CMD_SHIFT_V2) |
		   ((u64)randata << QM_DB_RAND_SHIFT_V2) |
		   ((u64)index << QM_DB_INDEX_SHIFT_V2)	 |
		   ((u64)priority << QM_DB_PRIORITY_SHIFT_V2);

	writeq(doorbell, qm->io_base + dbase);
}

static int vf_migration_data_store(struct hisi_qm *qm,
				   struct acc_vf_migration *acc_vf_dev)
{
	struct acc_vf_data *vf_data = acc_vf_dev->vf_data;
	struct device *dev = &qm->pdev->dev;
	int ret;

	ret = qm_rw_regs_read(qm, vf_data);
	if (ret)
		return -EINVAL;

	/* Every reg is 32 bit, the dma address is 64 bit. */
	vf_data->eqe_dma = vf_data->qm_eqc_dw[2];
	vf_data->eqe_dma <<= QM_XQC_ADDR_OFFSET;
	vf_data->eqe_dma |= vf_data->qm_eqc_dw[1];
	vf_data->aeqe_dma = vf_data->qm_aeqc_dw[2];
	vf_data->aeqe_dma <<= QM_XQC_ADDR_OFFSET;
	vf_data->aeqe_dma |= vf_data->qm_aeqc_dw[1];

	/* Through SQC_BT/CQC_BT to get sqc and cqc address */
	ret = qm_get_sqc(qm, &vf_data->sqc_dma);
	if (ret) {
		dev_err(dev, "failed to read SQC addr!\n");
		return -EINVAL;
	}

	ret = qm_get_cqc(qm, &vf_data->cqc_dma);
	if (ret) {
		dev_err(dev, "failed to read CQC addr!\n");
		return -EINVAL;
	}

	return 0;
}

static void qm_dev_cmd_init(struct hisi_qm *qm)
{
	/* Clear VF communication status registers. */
	writel(0x1, qm->io_base + QM_IFC_INT_SOURCE_V);

	/* Enable pf and vf communication. */
	writel(0x0, qm->io_base + QM_IFC_INT_MASK);
}

static int vf_qm_cache_wb(struct hisi_qm *qm)
{
	unsigned int val;

	writel(0x1, qm->io_base + QM_CACHE_WB_START);
	if (readl_relaxed_poll_timeout(qm->io_base + QM_CACHE_WB_DONE,
				       val, val & BIT(0), MB_POLL_PERIOD_US,
				       MB_POLL_TIMEOUT_US)) {
		dev_err(&qm->pdev->dev, "vf QM writeback sqc cache fail\n");
		return -EINVAL;
	}

	return 0;
}

static void vf_qm_fun_reset(struct hisi_qm *qm,
			    struct acc_vf_migration *acc_vf_dev)
{
	struct acc_vf_data *vf_data = acc_vf_dev->vf_data;
	int i;

	if (vf_data->vf_state != VF_READY)
		return;

	for (i = 0; i < qm->qp_num; i++)
		qm_db(qm, i, QM_DOORBELL_CMD_SQ, 0, 1);
}

static int vf_qm_func_stop(struct hisi_qm *qm)
{
	return qm_mb(qm, QM_MB_CMD_PAUSE_QM, 0, 0, 0);
}

static int pf_qm_get_qp_num(struct hisi_qm *qm, int vf_id, u32 *rbase)
{
	unsigned int val;
	u64 sqc_vft;
	u32 qp_num;
	int ret;

	ret = readl_relaxed_poll_timeout(qm->io_base + QM_VFT_CFG_RDY, val,
					 val & BIT(0), MB_POLL_PERIOD_US,
					 MB_POLL_TIMEOUT_US);
	if (ret)
		return ret;

	writel(0x1, qm->io_base + QM_VFT_CFG_OP_WR);
	/* 0 mean SQC VFT */
	writel(0x0, qm->io_base + QM_VFT_CFG_TYPE);
	writel(vf_id, qm->io_base + QM_VFT_CFG);

	writel(0x0, qm->io_base + QM_VFT_CFG_RDY);
	writel(0x1, qm->io_base + QM_VFT_CFG_OP_ENABLE);

	ret = readl_relaxed_poll_timeout(qm->io_base + QM_VFT_CFG_RDY, val,
					 val & BIT(0), MB_POLL_PERIOD_US,
					 MB_POLL_TIMEOUT_US);
	if (ret)
		return ret;

	sqc_vft = readl(qm->io_base + QM_VFT_CFG_DATA_L) |
		  ((u64)readl(qm->io_base + QM_VFT_CFG_DATA_H) <<
		  QM_XQC_ADDR_OFFSET);
	*rbase = QM_SQC_VFT_BASE_MASK_V2 &
		  (sqc_vft >> QM_SQC_VFT_BASE_SHIFT_V2);
	qp_num = (QM_SQC_VFT_NUM_MASK_V2 &
		  (sqc_vft >> QM_SQC_VFT_NUM_SHIFT_V2)) + 1;

	return qp_num;
}

/*
 * HiSilicon ACC VF dev MMIO space contains both the functional register
 * space and the migration control register space. We hide the migration
 * control space from the Guest. But to successfully complete the live
 * migration, we still need access to the functional MMIO space assigned
 * to the Guest. To avoid any potential security issues, we need to be
 * careful not to access this region while the Guest vCPUs are running.
 *
 * Hence check the device state before we map the region.
 */
static int hisi_acc_vf_ioremap(struct acc_vf_migration *acc_vf_dev, u32 state)
{
	struct hisi_qm *vfqm = acc_vf_dev->vf_qm;
	struct pci_dev *pdev = acc_vf_dev->vf_dev;

	if (state == (VFIO_DEVICE_STATE_SAVING | VFIO_DEVICE_STATE_RUNNING))
		return -EINVAL;

	if (vfqm->io_base)
		return 0;

	vfqm->io_base = ioremap(vfqm->phys_base,
				pci_resource_len(pdev, VFIO_PCI_BAR2_REGION_INDEX));
	if (!vfqm->io_base)
		return -EIO;

	return 0;
}

static void hisi_acc_vf_iounmap(struct acc_vf_migration *acc_vf_dev)
{
	struct hisi_qm *vfqm = acc_vf_dev->vf_qm;

	if (!vfqm->io_base)
		return;

	iounmap(vfqm->io_base);
	vfqm->io_base = NULL;
}

static int vf_migration_data_recover(struct hisi_qm *qm,
				     struct acc_vf_migration *acc_vf_dev)
{
	struct device *dev = &qm->pdev->dev;
	struct acc_vf_data *vf_data = acc_vf_dev->vf_data;
	int ret;

	qm->eqe_dma = vf_data->eqe_dma;
	qm->aeqe_dma = vf_data->aeqe_dma;
	qm->sqc_dma = vf_data->sqc_dma;
	qm->cqc_dma = vf_data->cqc_dma;

	qm->qp_base = vf_data->qp_base;
	qm->qp_num = vf_data->qp_num;

	ret = qm_rw_regs_write(qm, vf_data);
	if (ret) {
		dev_err(dev, "Set VF regs failed\n");
		return ret;
	}

	ret = qm_mb(qm, QM_MB_CMD_SQC_BT, qm->sqc_dma, 0, 0);
	if (ret) {
		dev_err(dev, "Set sqc failed\n");
		return ret;
	}

	ret = qm_mb(qm, QM_MB_CMD_CQC_BT, qm->cqc_dma, 0, 0);
	if (ret) {
		dev_err(dev, "Set cqc failed\n");
		return ret;
	}

	qm_dev_cmd_init(qm);

	return 0;
}

static int pf_qm_state_pre_save(struct acc_vf_migration *acc_vf_dev)
{
	struct acc_vf_data *vf_data = acc_vf_dev->vf_data;
	struct hisi_qm *pf_qm = acc_vf_dev->pf_qm;
	struct vfio_device_migration_info *mig_ctl = acc_vf_dev->mig_ctl;
	struct device *dev = &pf_qm->pdev->dev;
	int vf_id = acc_vf_dev->vf_id;
	int ret;

	/* save device id */
	vf_data->dev_id = acc_vf_dev->vf_dev->device;

	/* vf qp num save from PF */
	ret = pf_qm_get_qp_num(pf_qm, vf_id, &pf_qm->qp_base);
	if (ret <= 0) {
		dev_err(dev, "failed to get vft qp nums!\n");
		return -EINVAL;
	}
	pf_qm->qp_num = ret;
	vf_data->qp_base = pf_qm->qp_base;
	vf_data->qp_num = pf_qm->qp_num;

	/* vf isolation state save from PF */
	ret = qm_read_reg(pf_qm, QM_QUE_ISO_CFG, &vf_data->que_iso_cfg, 1);
	if (ret) {
		dev_err(dev, "failed to read QM_QUE_ISO_CFG!\n");
		return ret;
	}

	mig_ctl->data_size = QM_MATCH_SIZE;
	mig_ctl->pending_bytes = mig_ctl->data_size;

	return 0;
}

static int vf_qm_state_save(struct acc_vf_migration *acc_vf_dev, u32 state)
{
	struct device *dev = &acc_vf_dev->vf_dev->dev;
	struct hisi_qm *vf_qm = acc_vf_dev->vf_qm;
	struct vfio_device_migration_info *mig_ctl = acc_vf_dev->mig_ctl;
	int ret;

	mig_ctl->data_size = 0;
	mig_ctl->pending_bytes = 0;

	ret = hisi_acc_vf_ioremap(acc_vf_dev, state);
	if (ret)
		return ret;

	if (unlikely(qm_wait_dev_ready(vf_qm))) {
		dev_info(dev, "QM device not ready, no data to transfer\n");
		hisi_acc_vf_iounmap(acc_vf_dev);
		return 0;
	}

	/* First stop the ACC vf function */
	ret = vf_qm_func_stop(vf_qm);
	if (ret) {
		dev_err(dev, "failed to stop QM VF function!\n");
		hisi_acc_vf_iounmap(acc_vf_dev);
		return ret;
	}

	ret = qm_check_int_state(acc_vf_dev);
	if (ret) {
		dev_err(dev, "failed to check QM INT state!\n");
		goto state_error;
	}

	ret = vf_qm_cache_wb(vf_qm);
	if (ret) {
		dev_err(dev, "failed to writeback QM Cache!\n");
		goto state_error;
	}

	ret = vf_migration_data_store(vf_qm, acc_vf_dev);
	if (ret) {
		dev_err(dev, "failed to get and store migration data!\n");
		goto state_error;
	}

	mig_ctl->data_size = sizeof(struct acc_vf_data);
	mig_ctl->pending_bytes = mig_ctl->data_size;
	hisi_acc_vf_iounmap(acc_vf_dev);

	return 0;

state_error:
	vf_qm_fun_reset(vf_qm, acc_vf_dev);

	hisi_acc_vf_iounmap(acc_vf_dev);
	return ret;
}

static int vf_qm_state_resume(struct acc_vf_migration *acc_vf_dev,
			      u32 state)
{
	struct device *dev = &acc_vf_dev->vf_dev->dev;
	struct hisi_qm *vf_qm = acc_vf_dev->vf_qm;
	struct vfio_device_migration_info *mig_ctl = acc_vf_dev->mig_ctl;
	int ret;

	if (!mig_ctl->data_size)
		return 0;

	ret = hisi_acc_vf_ioremap(acc_vf_dev, state);
	if (ret)
		return ret;

	/* recover data to VF */
	ret = vf_migration_data_recover(vf_qm, acc_vf_dev);
	if (ret) {
		dev_err(dev, "failed to recover the VF!\n");
		hisi_acc_vf_iounmap(acc_vf_dev);
		return ret;
	}

	/* restart all destination VF's QP */
	vf_qm_fun_reset(vf_qm, acc_vf_dev);
	hisi_acc_vf_iounmap(acc_vf_dev);

	return 0;
}

static int hisi_acc_vf_set_device_state(struct acc_vf_migration *acc_vf_dev,
					u32 state)
{
	struct vfio_device_migration_info *mig_ctl = acc_vf_dev->mig_ctl;
	int ret = 0;

	if (state == mig_ctl->device_state)
		return 0;

	switch (state) {
	case VFIO_DEVICE_STATE_RUNNING:
		if (mig_ctl->device_state == VFIO_DEVICE_STATE_RESUMING)
			ret = vf_qm_state_resume(acc_vf_dev, state);
		break;
	case VFIO_DEVICE_STATE_SAVING | VFIO_DEVICE_STATE_RUNNING:
		ret = pf_qm_state_pre_save(acc_vf_dev);
		break;
	case VFIO_DEVICE_STATE_SAVING:
		ret = vf_qm_state_save(acc_vf_dev, state);
		break;
	case VFIO_DEVICE_STATE_STOP:
	case VFIO_DEVICE_STATE_RESUMING:
		break;
	default:
		return -EFAULT;
	}

	if (!ret)
		mig_ctl->device_state = state;

	return ret;
}

static int hisi_acc_vf_match_check(struct acc_vf_migration *acc_vf_dev)
{
	struct vfio_device_migration_info *mig_ctl = acc_vf_dev->mig_ctl;
	struct acc_vf_data *vf_data = acc_vf_dev->vf_data;
	struct hisi_qm *qm = acc_vf_dev->vf_qm;
	struct device *dev = &qm->pdev->dev;
	u32 que_iso_state;
	int ret;

	/*
	 * Check we are in the correct dev state and have enough data to
	 * perform the check.
	 */
	if (mig_ctl->device_state != VFIO_DEVICE_STATE_RESUMING ||
	    mig_ctl->data_size != QM_MATCH_SIZE)
		return 0;

	/* vf acc dev type check */
	if (vf_data->dev_id != acc_vf_dev->vf_dev->device) {
		dev_err(dev, "failed to match VF devices\n");
		return -EINVAL;
	}

	ret = hisi_acc_vf_ioremap(acc_vf_dev, mig_ctl->device_state);
	if (ret)
		return ret;

	/* vf qp num check */
	ret = qm_get_vft(qm, &qm->qp_base);
	if (ret <= 0) {
		dev_err(dev, "failed to get vft qp nums\n");
		ret = -EINVAL;
		goto out;
	}
	qm->qp_num = ret;

	if (vf_data->qp_num != qm->qp_num) {
		dev_err(dev, "failed to match VF qp num\n");
		ret = -EINVAL;
		goto out;
	}

	/* vf isolation state check */
	ret = qm_read_reg(qm, QM_QUE_ISO_CFG_V, &que_iso_state, 1);
	if (ret) {
		dev_err(dev, "failed to read QM_QUE_ISO_CFG_V\n");
		goto out;
	}
	if (vf_data->que_iso_cfg != que_iso_state) {
		dev_err(dev, "failed to match isolation state\n");
		ret = -EINVAL;
		goto out;
	}

	/* clear the VF match data size */
	mig_ctl->pending_bytes = 0;
	mig_ctl->data_size = 0;

out:
	hisi_acc_vf_iounmap(acc_vf_dev);
	return ret;
}

static int hisi_acc_vf_data_transfer(struct acc_vf_migration *acc_vf_dev,
				     char __user *buf, size_t count, u64 offset,
				     bool iswrite)
{
	struct vfio_device_migration_info *mig_ctl = acc_vf_dev->mig_ctl;
	void *data_addr = acc_vf_dev->vf_data;
	int ret = 0;

	data_addr += offset;
	if (iswrite)  {
		ret = copy_from_user(data_addr, buf, count);
		if (ret)
			return -EFAULT;

		mig_ctl->pending_bytes += count;
	} else {
		ret = copy_to_user(buf, data_addr, count);
		if (ret)
			return -EFAULT;

		mig_ctl->pending_bytes -= count;
	}

	return count;
}

static ssize_t hisi_acc_vf_migrn_rw(struct vfio_pci_core_device *vdev,
				    char __user *buf, size_t count, loff_t *ppos,
				    bool iswrite)
{
	unsigned int index = VFIO_PCI_OFFSET_TO_INDEX(*ppos) -
				VFIO_PCI_NUM_REGIONS;
	struct vfio_pci_region	*region = &vdev->region[index];
	struct acc_vf_migration *acc_vf_dev;
	struct vfio_device_migration_info *mig_ctl;
	u64 pos = *ppos & VFIO_PCI_OFFSET_MASK;
	int ret;

	if (region->type != VFIO_REGION_TYPE_MIGRATION ||
	    region->subtype != VFIO_REGION_SUBTYPE_MIGRATION)
		return -EINVAL;

	acc_vf_dev = region->data;
	if (!acc_vf_dev)
		return -EINVAL;

	mig_ctl = acc_vf_dev->mig_ctl;
	if (pos >= mig_ctl->data_offset) {
		u64 offset;

		offset = pos - mig_ctl->data_offset;
		if (offset + count > region->size)
			return -EINVAL;

		return hisi_acc_vf_data_transfer(acc_vf_dev, buf,
						count, offset, iswrite);
	}

	switch (pos) {
	case VDM_OFFSET(device_state):
		if (count != sizeof(mig_ctl->device_state))
			return -EFAULT;

		if (iswrite) {
			u32 device_state;

			ret = copy_from_user(&device_state, buf, count);
			if (ret)
				return -EFAULT;

			ret = hisi_acc_vf_set_device_state(acc_vf_dev, device_state);
			if (ret)
				return ret;
		} else {
			ret = copy_to_user(buf, &mig_ctl->device_state, count);
			if (ret)
				return -EFAULT;
		}

		return count;
	case VDM_OFFSET(reserved):
		return -EFAULT;
	case VDM_OFFSET(pending_bytes):
		if (count != sizeof(mig_ctl->pending_bytes))
			return -EINVAL;

		if (iswrite)
			return -EFAULT;

		ret = copy_to_user(buf, &mig_ctl->pending_bytes, count);
		if (ret)
			return -EFAULT;

		return count;
	case VDM_OFFSET(data_offset):
		if (count != sizeof(mig_ctl->data_offset))
			return -EINVAL;

		if (iswrite) {
			ret = copy_from_user(&mig_ctl->data_offset, buf, count);
			if (ret)
				return -EFAULT;
		} else {
			ret = copy_to_user(buf, &mig_ctl->data_offset, count);
			if (ret)
				return -EFAULT;
		}

		return count;
	case VDM_OFFSET(data_size):
		if (count != sizeof(mig_ctl->data_size))
			return -EINVAL;

		if (iswrite) {
			ret = copy_from_user(&mig_ctl->data_size, buf, count);
			if (ret)
				return -EFAULT;

			/* Check whether the src and dst VF's match */
			ret = hisi_acc_vf_match_check(acc_vf_dev);
			if (ret)
				return ret;
		} else {
			ret = copy_to_user(buf, &mig_ctl->data_size, count);
			if (ret)
				return -EFAULT;
		}

		return count;
	default:
		return -EFAULT;
	}
}

static void hisi_acc_vfio_pci_uninit(struct acc_vf_migration *acc_vf_dev)
{
	kfree(acc_vf_dev->mig_ctl);
	kfree(acc_vf_dev->vf_qm);
}

static void hisi_acc_vf_migrn_release(struct vfio_pci_core_device *vdev,
				      struct vfio_pci_region *region)
{
	struct acc_vf_migration *acc_vf_dev = region->data;

	hisi_acc_vfio_pci_uninit(acc_vf_dev);
	kfree(acc_vf_dev);
}

static const struct vfio_pci_regops hisi_acc_vfio_pci_regops = {
	.rw = hisi_acc_vf_migrn_rw,
	.release = hisi_acc_vf_migrn_release,
};

static int hisi_acc_vf_dev_init(struct pci_dev *pdev, struct hisi_qm *pf_qm,
				struct acc_vf_migration *acc_vf_dev)
{
	struct vfio_device_migration_info *mig_ctl;
	struct hisi_qm *vf_qm;

	vf_qm = kzalloc(sizeof(*vf_qm), GFP_KERNEL);
	if (!vf_qm)
		return -ENOMEM;

	vf_qm->dev_name = pf_qm->dev_name;
	vf_qm->fun_type = QM_HW_VF;
	vf_qm->phys_base = pci_resource_start(pdev, VFIO_PCI_BAR2_REGION_INDEX);
	vf_qm->pdev = pdev;
	mutex_init(&vf_qm->mailbox_lock);

	acc_vf_dev->vf_qm = vf_qm;
	acc_vf_dev->pf_qm = pf_qm;

	/* the data region must follow migration info */
	mig_ctl = kzalloc(MIGRATION_REGION_SZ, GFP_KERNEL);
	if (!mig_ctl)
		goto init_qm_error;

	acc_vf_dev->mig_ctl = mig_ctl;

	acc_vf_dev->vf_data = (void *)(mig_ctl + 1);

	mig_ctl->device_state = VFIO_DEVICE_STATE_RUNNING;
	mig_ctl->data_offset = sizeof(*mig_ctl);
	mig_ctl->data_size = sizeof(struct acc_vf_data);

	return 0;

init_qm_error:
	kfree(vf_qm);
	return -ENOMEM;
}

static int hisi_acc_vfio_pci_init(struct vfio_pci_core_device *vdev)
{
	struct acc_vf_migration *acc_vf_dev;
	struct pci_dev *pdev = vdev->pdev;
	struct pci_dev *pf_dev, *vf_dev;
	struct hisi_qm *pf_qm;
	int vf_id, ret;

	pf_dev = pdev->physfn;
	vf_dev = pdev;

	pf_qm = pci_get_drvdata(pf_dev);
	if (!pf_qm) {
		pr_err("HiSi ACC qm driver not loaded\n");
		return -EINVAL;
	}

	if (pf_qm->ver < QM_HW_V3) {
		dev_err(&pdev->dev,
			"Migration not supported, hw version: 0x%x\n",
			 pf_qm->ver);
		return -ENODEV;
	}

	vf_id = PCI_FUNC(vf_dev->devfn);
	acc_vf_dev = kzalloc(sizeof(*acc_vf_dev), GFP_KERNEL);
	if (!acc_vf_dev)
		return -ENOMEM;

	acc_vf_dev->vf_id = vf_id;
	acc_vf_dev->pf_dev = pf_dev;
	acc_vf_dev->vf_dev = vf_dev;

	ret = hisi_acc_vf_dev_init(pdev, pf_qm, acc_vf_dev);
	if (ret) {
		kfree(acc_vf_dev);
		return -ENOMEM;
	}

	ret = vfio_pci_register_dev_region(vdev, VFIO_REGION_TYPE_MIGRATION,
					   VFIO_REGION_SUBTYPE_MIGRATION,
					   &hisi_acc_vfio_pci_regops,
					   MIGRATION_REGION_SZ,
					   VFIO_REGION_INFO_FLAG_READ |
					   VFIO_REGION_INFO_FLAG_WRITE,
					   acc_vf_dev);
	if (ret)
		goto out;

	return 0;
out:
	hisi_acc_vfio_pci_uninit(acc_vf_dev);
	kfree(acc_vf_dev);
	return ret;
}

static int hisi_acc_pci_rw_access_check(struct vfio_device *core_vdev,
					size_t count, loff_t *ppos,
					size_t *new_count)
{
	unsigned int index = VFIO_PCI_OFFSET_TO_INDEX(*ppos);
	struct vfio_pci_core_device *vdev =
		container_of(core_vdev, struct vfio_pci_core_device, vdev);

	if (index == VFIO_PCI_BAR2_REGION_INDEX) {
		loff_t pos = *ppos & VFIO_PCI_OFFSET_MASK;
		resource_size_t end = pci_resource_len(vdev->pdev, index) / 2;

		/* Check if access is for migration control region */
		if (pos >= end)
			return -EINVAL;

		*new_count = min(count, (size_t)(end - pos));
	}

	return 0;
}

static int hisi_acc_vfio_pci_mmap(struct vfio_device *core_vdev,
				  struct vm_area_struct *vma)
{
	struct vfio_pci_core_device *vdev =
		container_of(core_vdev, struct vfio_pci_core_device, vdev);
	unsigned int index;

	index = vma->vm_pgoff >> (VFIO_PCI_OFFSET_SHIFT - PAGE_SHIFT);
	if (index == VFIO_PCI_BAR2_REGION_INDEX) {
		u64 req_len, pgoff, req_start;
		resource_size_t end = pci_resource_len(vdev->pdev, index) / 2;

		req_len = vma->vm_end - vma->vm_start;
		pgoff = vma->vm_pgoff &
			((1U << (VFIO_PCI_OFFSET_SHIFT - PAGE_SHIFT)) - 1);
		req_start = pgoff << PAGE_SHIFT;

		if (req_start + req_len > end)
			return -EINVAL;
	}

	return vfio_pci_core_mmap(core_vdev, vma);
}

static ssize_t hisi_acc_vfio_pci_write(struct vfio_device *core_vdev,
				       const char __user *buf, size_t count,
				       loff_t *ppos)
{
	size_t new_count = count;
	int ret;

	ret = hisi_acc_pci_rw_access_check(core_vdev, count, ppos, &new_count);
	if (ret)
		return ret;

	return vfio_pci_core_write(core_vdev, buf, new_count, ppos);
}

static ssize_t hisi_acc_vfio_pci_read(struct vfio_device *core_vdev,
				      char __user *buf, size_t count,
				      loff_t *ppos)
{
	size_t new_count = count;
	int ret;

	ret = hisi_acc_pci_rw_access_check(core_vdev, count, ppos, &new_count);
	if (ret)
		return ret;

	return vfio_pci_core_read(core_vdev, buf, new_count, ppos);
}

static long hisi_acc_vfio_pci_ioctl(struct vfio_device *core_vdev, unsigned int cmd,
				    unsigned long arg)
{
	struct vfio_pci_core_device *vdev =
		container_of(core_vdev, struct vfio_pci_core_device, vdev);

	if (cmd == VFIO_DEVICE_GET_REGION_INFO) {
		struct pci_dev *pdev = vdev->pdev;
		struct vfio_region_info info;
		unsigned long minsz;

		minsz = offsetofend(struct vfio_region_info, offset);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		if (info.index == VFIO_PCI_BAR2_REGION_INDEX) {
			info.offset = VFIO_PCI_INDEX_TO_OFFSET(info.index);

			/*
			 * ACC VF dev BAR2 region(64K) consists of both functional
			 * register space and migration control register space.
			 * Report only the first 32K(functional region) to Guest.
			 */
			info.size = pci_resource_len(pdev, info.index) / 2;

			info.flags = VFIO_REGION_INFO_FLAG_READ |
					VFIO_REGION_INFO_FLAG_WRITE |
					VFIO_REGION_INFO_FLAG_MMAP;

			return copy_to_user((void __user *)arg, &info, minsz) ?
					    -EFAULT : 0;
		}
	}
	return vfio_pci_core_ioctl(core_vdev, cmd, arg);
}

static int hisi_acc_vfio_pci_open_device(struct vfio_device *core_vdev)
{
	struct vfio_pci_core_device *vdev =
		container_of(core_vdev, struct vfio_pci_core_device, vdev);
	int ret;

	ret = vfio_pci_core_enable(vdev);
	if (ret)
		return ret;

	ret = hisi_acc_vfio_pci_init(vdev);
	if (ret) {
		vfio_pci_core_disable(vdev);
		return ret;
	}

	vfio_pci_core_finish_enable(vdev);

	return 0;
}

static const struct vfio_device_ops hisi_acc_vfio_pci_ops = {
	.name		= "hisi-acc-vfio-pci",
	.open_device	= hisi_acc_vfio_pci_open_device,
	.close_device	= vfio_pci_core_close_device,
	.ioctl		= hisi_acc_vfio_pci_ioctl,
	.read		= hisi_acc_vfio_pci_read,
	.write		= hisi_acc_vfio_pci_write,
	.mmap		= hisi_acc_vfio_pci_mmap,
	.request	= vfio_pci_core_request,
	.match		= vfio_pci_core_match,
};

static int hisi_acc_vfio_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct vfio_pci_core_device *vdev;
	int ret;

	vdev = kzalloc(sizeof(*vdev), GFP_KERNEL);
	if (!vdev)
		return -ENOMEM;

	vfio_pci_core_init_device(vdev, pdev, &hisi_acc_vfio_pci_ops);

	ret = vfio_pci_core_register_device(vdev);
	if (ret)
		goto out_free;

	dev_set_drvdata(&pdev->dev, vdev);

	return 0;

out_free:
	vfio_pci_core_uninit_device(vdev);
	kfree(vdev);
	return ret;
}

static void hisi_acc_vfio_pci_remove(struct pci_dev *pdev)
{
	struct vfio_pci_core_device *vdev = dev_get_drvdata(&pdev->dev);

	vfio_pci_core_unregister_device(vdev);
	vfio_pci_core_uninit_device(vdev);
	kfree(vdev);
}

static const struct pci_device_id hisi_acc_vfio_pci_table[] = {
	{ PCI_DRIVER_OVERRIDE_DEVICE_VFIO(PCI_VENDOR_ID_HUAWEI, SEC_VF_PCI_DEVICE_ID) },
	{ PCI_DRIVER_OVERRIDE_DEVICE_VFIO(PCI_VENDOR_ID_HUAWEI, HPRE_VF_PCI_DEVICE_ID) },
	{ PCI_DRIVER_OVERRIDE_DEVICE_VFIO(PCI_VENDOR_ID_HUAWEI, ZIP_VF_PCI_DEVICE_ID) },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, hisi_acc_vfio_pci_table);

static struct pci_driver hisi_acc_vfio_pci_driver = {
	.name			= "hisi-acc-vfio-pci",
	.id_table		= hisi_acc_vfio_pci_table,
	.probe			= hisi_acc_vfio_pci_probe,
	.remove			= hisi_acc_vfio_pci_remove,
	.err_handler		= &vfio_pci_core_err_handlers,
};

module_pci_driver(hisi_acc_vfio_pci_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Liu Longfang <liulongfang@huawei.com>");
MODULE_AUTHOR("Shameer Kolothum <shameerali.kolothum.thodi@huawei.com>");
MODULE_DESCRIPTION("HiSilicon VFIO PCI - VFIO PCI driver with live migration support for HiSilicon ACC device family");
