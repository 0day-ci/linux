// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2019 Intel Corporation */

#include "iecm.h"

/**
 * iecm_vport_intr_clean_queues - MSIX mode Interrupt Handler
 * @irq: interrupt number
 * @data: pointer to a q_vector
 *
 */
irqreturn_t
iecm_vport_intr_clean_queues(int __always_unused irq, void *data)
{
	struct iecm_q_vector *q_vector = (struct iecm_q_vector *)data;

	q_vector->total_events++;
	napi_schedule(&q_vector->napi);

	return IRQ_HANDLED;
}

