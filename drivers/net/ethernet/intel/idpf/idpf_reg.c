// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019 Intel Corporation */

#include "idpf_dev.h"
#include "iecm_lan_pf_regs.h"

/**
 * idpf_ctlq_reg_init - initialize default mailbox registers
 * @cq: pointer to the array of create control queues
 */
void idpf_ctlq_reg_init(struct iecm_ctlq_create_info *cq)
{
	int i;

#define NUM_Q 2
	for (i = 0; i < NUM_Q; i++) {
		struct iecm_ctlq_create_info *ccq = cq + i;

		switch (ccq->type) {
		case IECM_CTLQ_TYPE_MAILBOX_TX:
			/* set head and tail registers in our local struct */
			ccq->reg.head = PF_FW_ATQH;
			ccq->reg.tail = PF_FW_ATQT;
			ccq->reg.len = PF_FW_ATQLEN;
			ccq->reg.bah = PF_FW_ATQBAH;
			ccq->reg.bal = PF_FW_ATQBAL;
			ccq->reg.len_mask = PF_FW_ATQLEN_ATQLEN_M;
			ccq->reg.len_ena_mask = PF_FW_ATQLEN_ATQENABLE_M;
			ccq->reg.head_mask = PF_FW_ATQH_ATQH_M;
			break;
		case IECM_CTLQ_TYPE_MAILBOX_RX:
			/* set head and tail registers in our local struct */
			ccq->reg.head = PF_FW_ARQH;
			ccq->reg.tail = PF_FW_ARQT;
			ccq->reg.len = PF_FW_ARQLEN;
			ccq->reg.bah = PF_FW_ARQBAH;
			ccq->reg.bal = PF_FW_ARQBAL;
			ccq->reg.len_mask = PF_FW_ARQLEN_ARQLEN_M;
			ccq->reg.len_ena_mask = PF_FW_ARQLEN_ARQENABLE_M;
			ccq->reg.head_mask = PF_FW_ARQH_ARQH_M;
			break;
		default:
			break;
		}
	}
}

/**
 * idpf_mb_intr_reg_init - Initialize mailbox interrupt register
 * @adapter: adapter structure
 */
void idpf_mb_intr_reg_init(struct iecm_adapter *adapter)
{
	struct iecm_intr_reg *intr = &adapter->mb_vector.intr_reg;
	struct virtchnl2_get_capabilities *caps;

	caps = (struct virtchnl2_get_capabilities *)adapter->caps;
	intr->dyn_ctl = le32_to_cpu(caps->mailbox_dyn_ctl);
	intr->dyn_ctl_intena_m = PF_GLINT_DYN_CTL_INTENA_M;
	intr->dyn_ctl_itridx_m = PF_GLINT_DYN_CTL_ITR_INDX_M;
	intr->icr_ena = PF_INT_DIR_OICR_ENA;
	intr->icr_ena_ctlq_m = PF_INT_DIR_OICR_ENA_M;
}

/**
 * idpf_intr_reg_init - Initialize interrupt registers
 * @vport: virtual port structure
 */
int idpf_intr_reg_init(struct iecm_vport *vport)
{
	int num_vecs = vport->num_q_vectors;
	struct iecm_vec_regs *reg_vals;
	int num_regs, i, err = 0;

	reg_vals = kmalloc(sizeof(void *) * IECM_LARGE_MAX_Q,
			   GFP_KERNEL);
	if (!reg_vals)
		return -ENOMEM;

	num_regs = iecm_get_reg_intr_vecs(vport, reg_vals, num_vecs);
	if (num_regs != num_vecs) {
		err = -EINVAL;
		goto free_reg_vals;
	}

	for (i = 0; i < num_regs; i++) {
		struct iecm_q_vector *q_vector = &vport->q_vectors[i];
		struct iecm_intr_reg *intr = &q_vector->intr_reg;

		intr->dyn_ctl = reg_vals[i].dyn_ctl_reg;
		intr->dyn_ctl_clrpba_m = PF_GLINT_DYN_CTL_CLEARPBA_M;
		intr->dyn_ctl_intena_m = PF_GLINT_DYN_CTL_INTENA_M;
		intr->dyn_ctl_itridx_s = PF_GLINT_DYN_CTL_ITR_INDX_S;
		intr->dyn_ctl_intrvl_s = PF_GLINT_DYN_CTL_INTERVAL_S;

		intr->rx_itr = PF_GLINT_ITR_V2(VIRTCHNL2_ITR_IDX_0,
					       reg_vals[i].itrn_reg);
		intr->tx_itr = PF_GLINT_ITR_V2(VIRTCHNL2_ITR_IDX_1,
					       reg_vals[i].itrn_reg);
	}

free_reg_vals:
	kfree(reg_vals);
	return err;
}

/**
 * idpf_reset_reg_init - Initialize reset registers
 * @reset_reg: struct to be filled in with reset registers
 */
void idpf_reset_reg_init(struct iecm_reset_reg *reset_reg)
{
	reset_reg->rstat = PFGEN_RSTAT;
	reset_reg->rstat_m = PFGEN_RSTAT_PFR_STATE_M;
}

/**
 * idpf_trigger_reset - trigger reset
 * @adapter: Driver specific private structure
 * @trig_cause: Reason to trigger a reset
 */
void idpf_trigger_reset(struct iecm_adapter *adapter,
			enum iecm_flags __always_unused trig_cause)
{
	u32 reset_reg;

	reset_reg = rd32(&adapter->hw, PFGEN_CTRL);
	wr32(&adapter->hw, PFGEN_CTRL, (reset_reg | PFGEN_CTRL_PFSWR));
}

