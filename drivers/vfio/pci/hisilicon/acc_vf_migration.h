/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021 HiSilicon Limited. */

#ifndef ACC_MIG_H
#define ACC_MIG_H

#include <linux/mdev.h>
#include <linux/pci.h>
#include <linux/vfio.h>

#include "../vfio_pci_private.h"
#include "../../../drivers/crypto/hisilicon/qm.h"

#define MIGRATION_REGION_SZ (sizeof(struct acc_vf_data) + \
			      sizeof(struct vfio_device_migration_info))
#define VFIO_DEV_DBG_LEN		256
#define VFIO_DBG_LOG_LEN		16
#define VFIO_DEVFN_MASK		0xFF
#define SEC_VF_DEVICE_ID		0xa256
#define HPRE_VF_DEVICE_ID		0xa259
#define ZIP_VF_DEVICE_ID		0xa251

#define PCI_BASE_BAR			2
#define MB_POLL_PERIOD_US		10
#define MB_POLL_TIMEOUT_US		1000
#define QM_CACHE_WB_START		0x204
#define QM_CACHE_WB_DONE		0x208
#define QM_MB_CMD_PAUSE_QM		0xe
#define QM_ABNORMAL_INT_STATUS	0x100008
#define QM_IFC_INT_STATUS		0x0028
#define SEC_CORE_INT_STATUS		0x301008
#define HPRE_HAC_INT_STATUS		0x301800
#define HZIP_CORE_INT_STATUS		0x3010AC
#define ERROR_CHECK_TIMEOUT		100
#define CHECK_DELAY_TIME		100

#define QM_SQC_VFT_BASE_SHIFT_V2	28
#define QM_SQC_VFT_BASE_MASK_V2	GENMASK(15, 0)
#define QM_SQC_VFT_NUM_SHIFT_V2	45
#define QM_SQC_VFT_NUM_MASK_V2	GENMASK(9, 0)

/* mailbox */
#define QM_MB_CMD_SQC_BT		0x4
#define QM_MB_CMD_CQC_BT		0x5
#define QM_MB_CMD_SQC_VFT_V2		0x6

#define QM_MB_CMD_SEND_BASE		0x300
#define QM_MB_BUSY_SHIFT		13
#define QM_MB_OP_SHIFT			14
#define QM_MB_CMD_DATA_ADDR_L		0x304
#define QM_MB_CMD_DATA_ADDR_H		0x308
#define QM_MB_MAX_WAIT_CNT		6000

/* doorbell */
#define QM_DOORBELL_CMD_SQ		0
#define QM_DOORBELL_CMD_CQ		1
#define QM_DOORBELL_SQ_CQ_BASE_V2	0x1000
#define QM_DOORBELL_EQ_AEQ_BASE_V2	0x2000
#define QM_DB_CMD_SHIFT_V2		12
#define QM_DB_RAND_SHIFT_V2		16
#define QM_DB_INDEX_SHIFT_V2		32
#define QM_DB_PRIORITY_SHIFT_V2	48

/* RW regs */
#define QM_REGS_MAX_LEN		7
#define QM_REG_ADDR_OFFSET		0x0004

#define QM_XQC_ADDR_OFFSET		32U
#define QM_VF_AEQ_INT_MASK		0x0004
#define QM_VF_EQ_INT_MASK		0x000c
#define QM_IFC_INT_SOURCE_V		0x0020
#define QM_IFC_INT_MASK		0x0024
#define QM_IFC_INT_SET_V		0x002c
#define QM_QUE_ISO_CFG_V		0x0030

#define QM_EQC_DW0		0X3000
#define QM_AEQC_DW0		0X3020

struct qm_mailbox {
	__le16 w0;
	__le16 queue_num;
	__le32 base_l;
	__le32 base_h;
	__le32 rsvd;
};

enum acc_type {
	HISI_SEC = 0x1,
	HISI_HPRE = 0x2,
	HISI_ZIP = 0x3,
};

struct vf_acc_type {
	const char *name;
	u32 type;
};

enum mig_debug_cmd {
	STATE_SAVE,
	STATE_RESUME,
	MB_TEST,
	MIG_DATA_DUMP,
	MIG_DEV_SHOW,
};

static const char * const vf_dev_state[] = {
	"Stop",
	"Running",
	"Saving",
	"Running & Saving",
	"Resuming",
};

#define QM_MATCH_SIZE		32L
struct acc_vf_data {
	/* QM match information */
	u32 qp_num;
	u32 acc_type;
	u32 que_iso_cfg;
	u32 qp_base;
	/* QM reserved 4 match information */
	u32 qm_rsv_state[4];

	/* QM RW regs */
	u32 aeq_int_mask;
	u32 eq_int_mask;
	u32 ifc_int_source;
	u32 ifc_int_mask;
	u32 ifc_int_set;

	/*
	 * QM_VF_MB has 4 regs don't need to migration
	 * mailbox regs writeback value will cause
	 * hardware to perform command operations
	 */

	/* QM_EQC_DW has 7 regs */
	u32 qm_eqc_dw[7];

	/* QM_AEQC_DW has 7 regs */
	u32 qm_aeqc_dw[7];

	/* QM reserved 5 regs */
	u32 qm_rsv_regs[5];

	/* qm memory init information */
	dma_addr_t eqe_dma;
	dma_addr_t aeqe_dma;
	dma_addr_t sqc_dma;
	dma_addr_t cqc_dma;
};

struct acc_vf_migration {
	struct pci_dev			*pf_dev;
	struct pci_dev			*vf_dev;
	struct hisi_qm			*pf_qm;
	struct hisi_qm			*vf_qm;
	int				vf_id;
	u8				acc_type;
	struct mutex			reflock;

	struct vfio_device_migration_info *mig_ctl;
	struct acc_vf_data		*vf_data;
	struct vfio_pci_region		*regions;
	int				num_regions;
	struct dentry			*debug_root;
};

#endif /* ACC_MIG_H */

