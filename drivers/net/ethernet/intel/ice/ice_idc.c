// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021, Intel Corporation. */

/* Inter-Driver Communication */
#include "ice.h"
#include "ice_lib.h"
#include "ice_dcb_lib.h"

static DEFINE_IDA(ice_cdev_info_ida);

static struct cdev_info_id ice_cdev_ids[] = ASSIGN_IIDC_INFO;

/**
 * ice_for_each_aux - iterate across and call function for each AUX driver
 * @pf: pointer to private board struct
 * @data: data to pass to function on each call
 * @fn: pointer to function to call for each AUX driver
 */
int
ice_for_each_aux(struct ice_pf *pf, void *data,
		 int (*fn)(struct iidc_core_dev_info *, void *))
{
	unsigned int i;

	if (!pf->cdev_infos)
		return 0;

	for (i = 0; i < ARRAY_SIZE(ice_cdev_ids); i++) {
		struct iidc_core_dev_info *cdev_info;

		cdev_info = pf->cdev_infos[i];
		if (cdev_info) {
			int ret = fn(cdev_info, data);

			if (ret)
				return ret;
		}
	}

	return 0;
}

/**
 * ice_unroll_cdev_info - destroy cdev_info resources
 * @cdev_info: ptr to cdev_info struct
 * @data: ptr to opaque data
 *
 * This function releases resources for cdev_info objects.
 * Meant to be called from a ice_for_each_aux invocation
 */
int ice_unroll_cdev_info(struct iidc_core_dev_info *cdev_info,
			 void __always_unused *data)
{
	if (!cdev_info)
		return 0;

	kfree(cdev_info);

	return 0;
}

/**
 * ice_cdev_info_refresh_msix - load new values into iidc_core_dev_info structs
 * @pf: pointer to private board struct
 */
void ice_cdev_info_refresh_msix(struct ice_pf *pf)
{
	struct iidc_core_dev_info *cdev_info;
	unsigned int i;

	if (!pf->cdev_infos)
		return;

	for (i = 0; i < ARRAY_SIZE(ice_cdev_ids); i++) {
		if (!pf->cdev_infos[i])
			continue;

		cdev_info = pf->cdev_infos[i];

		switch (cdev_info->cdev_info_id) {
		case IIDC_RDMA_ID:
			cdev_info->msix_count = pf->num_rdma_msix;
			cdev_info->msix_entries =
				&pf->msix_entries[pf->rdma_base_vector];
			break;
		default:
			break;
		}
	}
}

/**
 * ice_reserve_cdev_info_qvector - Reserve vector resources for AUX drivers
 * @pf: board private structure to initialize
 */
static int ice_reserve_cdev_info_qvector(struct ice_pf *pf)
{
	if (test_bit(ICE_FLAG_IWARP_ENA, pf->flags)) {
		int index;

		index = ice_get_res(pf, pf->irq_tracker, pf->num_rdma_msix, ICE_RES_RDMA_VEC_ID);
		if (index < 0)
			return index;
		pf->num_avail_sw_msix -= pf->num_rdma_msix;
		pf->rdma_base_vector = (u16)index;
	}
	return 0;
}

/**
 * ice_find_cdev_info_by_id - find cdev_info instance by its ID
 * @pf: pointer to private board struct
 * @cdev_info_id: AUX driver ID
 */
struct iidc_core_dev_info *
ice_find_cdev_info_by_id(struct ice_pf *pf, int cdev_info_id)
{
	struct iidc_core_dev_info *cdev_info = NULL;
	unsigned int i;

	if (!pf->cdev_infos)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(ice_cdev_ids); i++) {
		cdev_info = pf->cdev_infos[i];
		if (cdev_info && cdev_info->cdev_info_id == cdev_info_id)
			break;
		cdev_info = NULL;
	}
	return cdev_info;
}

/**
 * ice_cdev_info_update_vsi - update the pf_vsi info in cdev_info struct
 * @cdev_info: pointer to cdev_info struct
 * @data: opaque pointer - VSI to be updated
 */
int ice_cdev_info_update_vsi(struct iidc_core_dev_info *cdev_info, void *data)
{
	struct ice_vsi *vsi = data;

	if (!cdev_info)
		return 0;

	cdev_info->vport_id = vsi->vsi_num;
	return 0;
}

/**
 * ice_init_aux_devices - initializes cdev_info objects and AUX devices
 * @pf: ptr to ice_pf
 */
int ice_init_aux_devices(struct ice_pf *pf)
{
	struct ice_vsi *vsi = ice_get_main_vsi(pf);
	struct pci_dev *pdev = pf->pdev;
	struct device *dev = &pdev->dev;
	unsigned int i;
	int ret;

	/* Reserve vector resources */
	ret = ice_reserve_cdev_info_qvector(pf);
	if (ret) {
		dev_err(dev, "failed to reserve vectors for aux drivers\n");
		return ret;
	}

	/* This PFs auxiliary ID value */
	pf->aux_idx = ida_alloc(&ice_cdev_info_ida, GFP_KERNEL);
	if (pf->aux_idx < 0) {
		dev_err(dev, "failed to allocate device ID for aux drvs\n");
		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(ice_cdev_ids); i++) {
		struct iidc_core_dev_info *cdev_info;
		struct iidc_qos_params *qos_info;
		struct msix_entry *entry = NULL;
		int j;

		cdev_info = kzalloc(sizeof(*cdev_info), GFP_KERNEL);
		if (!cdev_info) {
			ida_simple_remove(&ice_cdev_info_ida, pf->aux_idx);
			pf->aux_idx = -1;
			return -ENOMEM;
		}

		pf->cdev_infos[i] = cdev_info;

		cdev_info->hw_addr = (u8 __iomem *)pf->hw.hw_addr;
		cdev_info->cdev_info_id = ice_cdev_ids[i].id;
		cdev_info->vport_id = vsi->vsi_num;
		cdev_info->netdev = vsi->netdev;

		cdev_info->pdev = pdev;
		qos_info = &cdev_info->qos_info;

		/* setup qos_info fields with defaults */
		qos_info->num_apps = 0;
		qos_info->num_tc = 1;

		for (j = 0; j < IIDC_MAX_USER_PRIORITY; j++)
			qos_info->up2tc[j] = 0;

		qos_info->tc_info[0].rel_bw = 100;
		for (j = 1; j < IEEE_8021QAZ_MAX_TCS; j++)
			qos_info->tc_info[j].rel_bw = 0;

		/* for DCB, override the qos_info defaults. */
		ice_setup_dcb_qos_info(pf, qos_info);

		/* make sure AUX specific resources such as msix_count and
		 * msix_entries are initialized
		 */
		switch (ice_cdev_ids[i].id) {
		case IIDC_RDMA_ID:
			if (test_bit(ICE_FLAG_IWARP_ENA, pf->flags)) {
				cdev_info->msix_count = pf->num_rdma_msix;
				entry = &pf->msix_entries[pf->rdma_base_vector];
			}
			cdev_info->rdma_protocol = IIDC_RDMA_PROTOCOL_IWARP;
			break;
		default:
			break;
		}

		cdev_info->msix_entries = entry;
	}

	return ret;
}
