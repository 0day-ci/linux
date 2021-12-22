// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021, Intel Corporation. */

#include "ice.h"
#include "ice_lib.h"
#include "ice_irq.h"

static void ice_dis_msix(struct ice_pf *pf)
{
	pci_free_irq_vectors(pf->pdev);
}

static int ice_ena_msix(struct ice_pf *pf, int nvec)
{
	return pci_alloc_irq_vectors(pf->pdev, ICE_MIN_MSIX, nvec,
				     PCI_IRQ_MSIX);
}

#define ICE_ADJ_VEC_STEPS 5
static void ice_adj_vec_sum(int *dst, int *src)
{
	int i;

	for (i = 0; i < ICE_ADJ_VEC_STEPS; i++)
		dst[i] += src[i];
}

/**
 * ice_ena_msix_range - request a range of MSI-X vectors from the OS
 * @pf: board private structure
 *
 * The driver tries to enable best-case scenario MSI-X vectors. If that doesn't
 * succeed then adjust to irqs number returned by kernel.
 *
 * The fall-back logic is described below with each [#] represented needed irqs
 * number for the step. If any of the steps is lower than received number, then
 * return the number of MSI-X. If any of the steps is greater, then check next
 * one. If received value is lower than irqs value in last step return error.
 *
 * Step [4]: Enable the best-case scenario MSI-X vectors.
 *
 * Step [3]: Enable MSI-X vectors with eswitch support disabled
 *
 * Step [2]: Enable MSI-X vectors with the number of pf->num_lan_msix reduced
 * by a factor of 2 from the previous step (i.e. num_online_cpus() / 2).
 * Also, with the number of pf->num_rdma_msix reduced by a factor of ~2 from the
 * previous step (i.e. num_online_cpus() / 2 + ICE_RDMA_NUM_AEQ_MSIX).
 *
 * Step [1]: Same as step [2], except reduce both by a factor of 4.
 *
 * Step [0]: Enable the bare-minimum MSI-X vectors.
 *
 * Each feature has separeate table with needed irqs in each step. Sum of these
 * tables is tracked in adj_vec to show needed irqs in each step. Separate
 * tables are later use to set correct number of irqs for each feature based on
 * choosed step.
 */
static int ice_ena_msix_range(struct ice_pf *pf)
{
	enum {
		ICE_ADJ_VEC_WORST_CASE	= 0,
		ICE_ADJ_VEC_STEP_1	= 1,
		ICE_ADJ_VEC_STEP_2	= 2,
		ICE_ADJ_VEC_STEP_3	= 3,
		ICE_ADJ_VEC_BEST_CASE	= ICE_ADJ_VEC_STEPS - 1,
	};
	int num_cpus = num_possible_cpus();
	int rdma_adj_vec[ICE_ADJ_VEC_STEPS] = {
		[ICE_ADJ_VEC_WORST_CASE] = ICE_MIN_RDMA_MSIX,
		[ICE_ADJ_VEC_STEP_1] = num_cpus / 4 > ICE_MIN_RDMA_MSIX ?
			num_cpus / 4 + ICE_RDMA_NUM_AEQ_MSIX :
			ICE_MIN_RDMA_MSIX,
		[ICE_ADJ_VEC_STEP_2] = num_cpus / 2 > ICE_MIN_RDMA_MSIX ?
			num_cpus / 2 + ICE_RDMA_NUM_AEQ_MSIX :
			ICE_MIN_RDMA_MSIX,
		[ICE_ADJ_VEC_STEP_3] = num_cpus > ICE_MIN_RDMA_MSIX ?
			num_cpus + ICE_RDMA_NUM_AEQ_MSIX : ICE_MIN_RDMA_MSIX,
		[ICE_ADJ_VEC_BEST_CASE] = num_cpus > ICE_MIN_RDMA_MSIX ?
			num_cpus + ICE_RDMA_NUM_AEQ_MSIX : ICE_MIN_RDMA_MSIX,
	};
	int lan_adj_vec[ICE_ADJ_VEC_STEPS] = {
		[ICE_ADJ_VEC_WORST_CASE] = ICE_MIN_LAN_MSIX,
		[ICE_ADJ_VEC_STEP_1] =
			max_t(int, num_cpus / 4, ICE_MIN_LAN_MSIX),
		[ICE_ADJ_VEC_STEP_2] =
			max_t(int, num_cpus / 2, ICE_MIN_LAN_MSIX),
		[ICE_ADJ_VEC_STEP_3] =
			max_t(int, num_cpus, ICE_MIN_LAN_MSIX),
		[ICE_ADJ_VEC_BEST_CASE] =
			max_t(int, num_cpus, ICE_MIN_LAN_MSIX),
	};
	int fdir_adj_vec[ICE_ADJ_VEC_STEPS] = {
		ICE_FDIR_MSIX, ICE_FDIR_MSIX, ICE_FDIR_MSIX,
		ICE_FDIR_MSIX, ICE_FDIR_MSIX,
	};
	int adj_vec[ICE_ADJ_VEC_STEPS] = {
		ICE_OICR_MSIX, ICE_OICR_MSIX, ICE_OICR_MSIX,
		ICE_OICR_MSIX, ICE_OICR_MSIX,
	};
	int eswitch_adj_vec[ICE_ADJ_VEC_STEPS] = {
		0, 0, 0, 0,
		[ICE_ADJ_VEC_BEST_CASE] = ICE_ESWITCH_MSIX,
	};
	struct device *dev = ice_pf_to_dev(pf);
	int adj_step = ICE_ADJ_VEC_BEST_CASE;
	int needed = ICE_OICR_MSIX;
	int err = -ENOSPC;
	int v_actual, i;

	needed += lan_adj_vec[ICE_ADJ_VEC_BEST_CASE];
	ice_adj_vec_sum(adj_vec, lan_adj_vec);

	if (ice_is_eswitch_supported(pf)) {
		needed += eswitch_adj_vec[ICE_ADJ_VEC_BEST_CASE];
		ice_adj_vec_sum(adj_vec, eswitch_adj_vec);
	} else {
		memset(&eswitch_adj_vec, 0, sizeof(eswitch_adj_vec));
	}

	if (ice_is_rdma_ena(pf)) {
		needed += rdma_adj_vec[ICE_ADJ_VEC_BEST_CASE];
		ice_adj_vec_sum(adj_vec, rdma_adj_vec);
	} else {
		memset(&rdma_adj_vec, 0, sizeof(rdma_adj_vec));
	}

	if (test_bit(ICE_FLAG_FD_ENA, pf->flags)) {
		needed += fdir_adj_vec[ICE_ADJ_VEC_BEST_CASE];
		ice_adj_vec_sum(adj_vec, fdir_adj_vec);
	} else {
		memset(&fdir_adj_vec, 0, sizeof(fdir_adj_vec));
	}

	v_actual = ice_ena_msix(pf, needed);
	if (v_actual < 0) {
		err = v_actual;
		goto err;
	} else if (v_actual < adj_vec[ICE_ADJ_VEC_WORST_CASE]) {
		ice_dis_msix(pf);
		goto err;
	}

	for (i = ICE_ADJ_VEC_WORST_CASE + 1; i < ICE_ADJ_VEC_STEPS; i++) {
		if (v_actual < adj_vec[i]) {
			adj_step = i - 1;
			break;
		}
	}

	pf->num_lan_msix = lan_adj_vec[adj_step];
	pf->num_rdma_msix = rdma_adj_vec[adj_step];

	if (ice_is_eswitch_supported(pf) &&
	    !eswitch_adj_vec[adj_step]) {
		dev_warn(dev, "Not enough MSI-X for eswitch support, disabling feature\n");
	}

	return v_actual;

err:
	dev_err(dev, "Failed to enable MSI-X vectors\n");
	return  err;
}

/**
 * ice_init_interrupt_scheme - Determine proper interrupt scheme
 * @pf: board private structure to initialize
 */
int ice_init_interrupt_scheme(struct ice_pf *pf)
{
	int vectors = ice_ena_msix_range(pf);

	if (vectors < 0)
		return vectors;

	/* set up vector assignment tracking */
	pf->irq_tracker =
		kzalloc(struct_size(pf->irq_tracker, list, vectors),
			GFP_KERNEL);
	if (!pf->irq_tracker) {
		ice_dis_msix(pf);
		return -ENOMEM;
	}

	/* populate SW interrupts pool with number of OS granted IRQs. */
	pf->num_avail_sw_msix = (u16)vectors;
	pf->irq_tracker->num_entries = (u16)vectors;
	pf->irq_tracker->end = pf->irq_tracker->num_entries;

	return 0;
}

/**
 * ice_clear_interrupt_scheme - Undo things done by ice_init_interrupt_scheme
 * @pf: board private structure
 */
void ice_clear_interrupt_scheme(struct ice_pf *pf)
{
	ice_dis_msix(pf);

	kfree(pf->irq_tracker);
	pf->irq_tracker = NULL;
}

/**
 * ice_get_irq_num - get system irq number based on index from driver
 * @pf: board private structure
 * @idx: driver irq index
 */
int ice_get_irq_num(struct ice_pf *pf, int idx)
{
	return pci_irq_vector(pf->pdev, idx);
}
