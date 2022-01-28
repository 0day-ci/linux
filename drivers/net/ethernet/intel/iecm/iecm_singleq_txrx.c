// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019 Intel Corporation */

#include "iecm.h"

/**
 * iecm_rx_singleq_buf_hw_alloc_all - Replace used receive buffers
 * @rx_q: queue for which the hw buffers are allocated
 * @cleaned_count: number of buffers to replace
 *
 * Returns false if all allocations were successful, true if any fail
 */
bool iecm_rx_singleq_buf_hw_alloc_all(struct iecm_queue *rx_q,
				      u16 cleaned_count)
{
	/* stub */
	return true;
}

/**
 * iecm_vport_singleq_napi_poll - NAPI handler
 * @napi: struct from which you get q_vector
 * @budget: budget provided by stack
 */
int iecm_vport_singleq_napi_poll(struct napi_struct *napi, int budget)
{
	/* stub */
	return 0;
}
