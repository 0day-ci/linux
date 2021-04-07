// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021, Intel Corporation. */

/* Inter-Driver Communication */
#include "ice.h"
#include "ice_lib.h"
#include "ice_dcb_lib.h"

static DEFINE_IDA(ice_cdev_info_ida);

static struct cdev_info_id ice_cdev_ids[] = ASSIGN_IIDC_INFO;

/**
 * ice_get_auxiliary_ops - retrieve iidc_auxiliary_ops struct
 * @cdev_info: pointer to iidc_core_dev_info struct
 *
 * This function has to be called with a device_lock on the
 * cdev_info->adev.dev to avoid race conditions.
 */
struct iidc_auxiliary_ops *
ice_get_auxiliary_ops(struct iidc_core_dev_info *cdev_info)
{
	struct iidc_auxiliary_drv *iadrv;
	struct auxiliary_device *adev;

	if (!cdev_info)
		return NULL;

	adev = cdev_info->adev;
	if (!adev || !adev->dev.driver)
		return NULL;

	iadrv = container_of(adev->dev.driver, struct iidc_auxiliary_drv,
			     adrv.driver);
	if (!iadrv)
		return NULL;

	return iadrv->ops;
}

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
 * ice_send_event_to_aux - send event to a specific AUX driver
 * @cdev_info: pointer to iidc_core_dev_info struct for this AUX
 * @data: opaque pointer used to pass event struct
 *
 * This function is only meant to be called through a ice_for_each_aux call
 */
static int
ice_send_event_to_aux(struct iidc_core_dev_info *cdev_info, void *data)
{
	struct iidc_event *event = data;
	struct iidc_auxiliary_ops *ops;

	device_lock(&cdev_info->adev->dev);
	ops = ice_get_auxiliary_ops(cdev_info);
	if (ops && ops->event_handler)
		ops->event_handler(cdev_info, event);
	device_unlock(&cdev_info->adev->dev);

	return 0;
}

/**
 * ice_send_event_to_auxs - send event to all auxiliary drivers
 * @pf: pointer to PF struct
 * @event: pointer to iidc_event to propagate
 *
 * event struct to be populated by caller
 */
int ice_send_event_to_auxs(struct ice_pf *pf, struct iidc_event *event)
{
	return ice_for_each_aux(pf, event, ice_send_event_to_aux);
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
 * ice_find_vsi - Find the VSI from VSI ID
 * @pf: The PF pointer to search in
 * @vsi_num: The VSI ID to search for
 */
static struct ice_vsi *ice_find_vsi(struct ice_pf *pf, u16 vsi_num)
{
	int i;

	ice_for_each_vsi(pf, i)
		if (pf->vsi[i] && pf->vsi[i]->vsi_num == vsi_num)
			return  pf->vsi[i];
	return NULL;
}

/**
 * ice_alloc_rdma_qsets - Allocate Leaf Nodes for RDMA Qset
 * @cdev_info: AUX driver that is requesting the Leaf Nodes
 * @res: Resources to be allocated
 * @partial_acceptable: If partial allocation is acceptable
 *
 * This function allocates Leaf Nodes for given RDMA Qset resources
 * for the AUX object.
 */
static int
ice_alloc_rdma_qsets(struct iidc_core_dev_info *cdev_info,
		     struct iidc_res *res,
		     int __always_unused partial_acceptable)
{
	u16 max_rdmaqs[ICE_MAX_TRAFFIC_CLASS];
	enum ice_status status;
	struct ice_vsi *vsi;
	struct device *dev;
	struct ice_pf *pf;
	int i, ret = 0;
	u32 *qset_teid;
	u16 *qs_handle;

	if (!cdev_info || !res)
		return -EINVAL;

	pf = pci_get_drvdata(cdev_info->pdev);
	dev = ice_pf_to_dev(pf);

	if (!test_bit(ICE_FLAG_IWARP_ENA, pf->flags))
		return -EINVAL;

	if (res->cnt_req > ICE_MAX_TXQ_PER_TXQG)
		return -EINVAL;

	qset_teid = kcalloc(res->cnt_req, sizeof(*qset_teid), GFP_KERNEL);
	if (!qset_teid)
		return -ENOMEM;

	qs_handle = kcalloc(res->cnt_req, sizeof(*qs_handle), GFP_KERNEL);
	if (!qs_handle) {
		kfree(qset_teid);
		return -ENOMEM;
	}

	ice_for_each_traffic_class(i)
		max_rdmaqs[i] = 0;

	for (i = 0; i < res->cnt_req; i++) {
		struct iidc_rdma_qset_params *qset;

		qset = &res->res[i].res.qsets;
		if (qset->vport_id != cdev_info->vport_id) {
			dev_err(dev, "RDMA QSet invalid VSI requested\n");
			ret = -EINVAL;
			goto out;
		}
		max_rdmaqs[qset->tc]++;
		qs_handle[i] = qset->qs_handle;
	}

	vsi = ice_find_vsi(pf, cdev_info->vport_id);
	if (!vsi) {
		dev_err(dev, "RDMA QSet invalid VSI\n");
		ret = -EINVAL;
		goto out;
	}

	status = ice_cfg_vsi_rdma(vsi->port_info, vsi->idx, vsi->tc_cfg.ena_tc,
				  max_rdmaqs);
	if (status) {
		dev_err(dev, "Failed VSI RDMA qset config\n");
		ret = -EINVAL;
		goto out;
	}

	for (i = 0; i < res->cnt_req; i++) {
		struct iidc_rdma_qset_params *qset;

		qset = &res->res[i].res.qsets;
		status = ice_ena_vsi_rdma_qset(vsi->port_info, vsi->idx,
					       qset->tc, &qs_handle[i], 1,
					       &qset_teid[i]);
		if (status) {
			dev_err(dev, "Failed VSI RDMA qset enable\n");
			ret = -EINVAL;
			goto out;
		}
		vsi->qset_handle[qset->tc] = qset->qs_handle;
		qset->teid = qset_teid[i];
	}

out:
	kfree(qset_teid);
	kfree(qs_handle);
	return ret;
}

/**
 * ice_free_rdma_qsets - Free leaf nodes for RDMA Qset
 * @cdev_info: AUX driver that requested Qsets to be freed
 * @res: Resource to be freed
 */
static int
ice_free_rdma_qsets(struct iidc_core_dev_info *cdev_info, struct iidc_res *res)
{
	enum ice_status status;
	int count, i, ret = 0;
	struct ice_vsi *vsi;
	struct device *dev;
	struct ice_pf *pf;
	u16 vsi_id;
	u32 *teid;
	u16 *q_id;

	if (!cdev_info || !res)
		return -EINVAL;

	pf = pci_get_drvdata(cdev_info->pdev);
	dev = ice_pf_to_dev(pf);

	count = res->res_allocated;
	if (count > ICE_MAX_TXQ_PER_TXQG)
		return -EINVAL;

	teid = kcalloc(count, sizeof(*teid), GFP_KERNEL);
	if (!teid)
		return -ENOMEM;

	q_id = kcalloc(count, sizeof(*q_id), GFP_KERNEL);
	if (!q_id) {
		kfree(teid);
		return -ENOMEM;
	}

	vsi_id = res->res[0].res.qsets.vport_id;
	vsi = ice_find_vsi(pf, vsi_id);
	if (!vsi) {
		dev_err(dev, "RDMA Invalid VSI\n");
		ret = -EINVAL;
		goto rdma_free_out;
	}

	for (i = 0; i < count; i++) {
		struct iidc_rdma_qset_params *qset;

		qset = &res->res[i].res.qsets;
		if (qset->vport_id != vsi_id) {
			dev_err(dev, "RDMA Invalid VSI ID\n");
			ret = -EINVAL;
			goto rdma_free_out;
		}
		q_id[i] = qset->qs_handle;
		teid[i] = qset->teid;

		vsi->qset_handle[qset->tc] = 0;
	}

	status = ice_dis_vsi_rdma_qset(vsi->port_info, count, teid, q_id);
	if (status)
		ret = -EINVAL;

rdma_free_out:
	kfree(teid);
	kfree(q_id);

	return ret;
}

/**
 * ice_cdev_info_alloc_res - Allocate requested resources for AUX objects
 * @cdev_info: struct for AUX driver that is requesting resources
 * @res: Resources to be allocated
 * @partial_acceptable: If partial allocation is acceptable
 *
 * This function allocates requested resources for the AUX object.
 */
static int
ice_cdev_info_alloc_res(struct iidc_core_dev_info *cdev_info,
			struct iidc_res *res, int partial_acceptable)
{
	struct ice_pf *pf;
	int ret;

	if (!cdev_info || !res)
		return -EINVAL;

	pf = pci_get_drvdata(cdev_info->pdev);
	if (!ice_pf_state_is_nominal(pf))
		return -EBUSY;

	switch (res->res_type) {
	case IIDC_RDMA_QSETS_TXSCHED:
		ret = ice_alloc_rdma_qsets(cdev_info, res, partial_acceptable);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/**
 * ice_cdev_info_free_res - Free given resources
 * @cdev_info: struct for AUX driver that is requesting freeing of resources
 * @res: Resources to be freed
 *
 * Free/Release resources allocated to given AUX objects.
 */
static int
ice_cdev_info_free_res(struct iidc_core_dev_info *cdev_info,
		       struct iidc_res *res)
{
	int ret;

	if (!cdev_info || !res)
		return -EINVAL;

	switch (res->res_type) {
	case IIDC_RDMA_QSETS_TXSCHED:
		ret = ice_free_rdma_qsets(cdev_info, res);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/**
 * ice_cdev_info_request_reset - request from AUX driver to perform a reset
 * @cdev_info: struct for AUX driver that is requesting a reset
 * @reset_type: type of reset the AUX driver is requesting
 */
static int
ice_cdev_info_request_reset(struct iidc_core_dev_info *cdev_info,
			    enum iidc_reset_type reset_type)
{
	enum ice_reset_req reset;
	struct ice_pf *pf;

	if (!cdev_info)
		return -EINVAL;

	pf = pci_get_drvdata(cdev_info->pdev);

	switch (reset_type) {
	case IIDC_PFR:
		reset = ICE_RESET_PFR;
		break;
	case IIDC_CORER:
		reset = ICE_RESET_CORER;
		break;
	case IIDC_GLOBR:
		reset = ICE_RESET_GLOBR;
		break;
	default:
		dev_err(ice_pf_to_dev(pf), "incorrect reset request from aux driver\n");
		return -EINVAL;
	}

	return ice_schedule_reset(pf, reset);
}

/**
 * ice_cdev_info_update_vsi_filter - update main VSI filters for RDMA
 * @cdev_info: pointer to struct for AUX device updating filters
 * @vsi_id: VSI HW index to update filter on
 * @enable: bool whether to enable or disable filters
 */
static int
ice_cdev_info_update_vsi_filter(struct iidc_core_dev_info *cdev_info,
				u16 vsi_id, bool enable)
{
	enum ice_status status;
	struct ice_vsi *vsi;
	struct ice_pf *pf;

	if (!cdev_info)
		return -EINVAL;

	pf = pci_get_drvdata(cdev_info->pdev);

	vsi = ice_find_vsi(pf, vsi_id);
	if (!vsi)
		return -EINVAL;

	status = ice_cfg_iwarp_fltr(&pf->hw, vsi->idx, enable);
	if (status) {
		dev_err(ice_pf_to_dev(pf), "Failed to  %sable iWARP filtering\n",
			enable ? "en" : "dis");
	} else {
		if (enable)
			vsi->info.q_opt_flags |= ICE_AQ_VSI_Q_OPT_PE_FLTR_EN;
		else
			vsi->info.q_opt_flags &= ~ICE_AQ_VSI_Q_OPT_PE_FLTR_EN;
	}

	return ice_status_to_errno(status);
}

/**
 * ice_cdev_info_vc_send - send a virt channel message from an AUX driver
 * @cdev_info: pointer to cdev_info struct for AUX driver
 * @vf_id: the absolute VF ID of recipient of message
 * @msg: pointer to message contents
 * @len: len of message
 */
static int
ice_cdev_info_vc_send(struct iidc_core_dev_info *cdev_info, u32 vf_id, u8 *msg,
		      u16 len)
{
	enum ice_status status;
	struct ice_pf *pf;

	if (!cdev_info)
		return -EINVAL;
	if (!msg || !len)
		return -ENOMEM;

	pf = pci_get_drvdata(cdev_info->pdev);
	if (len > ICE_AQ_MAX_BUF_LEN)
		return -EINVAL;

	if (ice_is_reset_in_progress(pf->state))
		return -EBUSY;

	switch (cdev_info->cdev_info_id) {
	case IIDC_RDMA_ID:
		if (vf_id >= pf->num_alloc_vfs)
			return -ENODEV;

		/* VIRTCHNL_OP_IWARP is being used for RoCEv2 msg also */
		status = ice_aq_send_msg_to_vf(&pf->hw, vf_id, VIRTCHNL_OP_IWARP,
					       0, msg, len, NULL);
		break;
	default:
		dev_err(ice_pf_to_dev(pf), "aux driver (%i) not supported!",
			cdev_info->cdev_info_id);
		return -ENODEV;
	}

	if (status)
		dev_err(ice_pf_to_dev(pf), "Unable to send msg to VF, error %s\n",
			ice_stat_str(status));
	return ice_status_to_errno(status);
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

/* Initialize the ice_ops struct, which is used in 'ice_init_aux_devices' */
static const struct iidc_core_ops ops = {
	.alloc_res			= ice_cdev_info_alloc_res,
	.free_res			= ice_cdev_info_free_res,
	.request_reset			= ice_cdev_info_request_reset,
	.update_vport_filter		= ice_cdev_info_update_vsi_filter,
	.vc_send			= ice_cdev_info_vc_send,

};

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
		/* Initialize ice_ops */
		cdev_info->ops = &ops;

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
