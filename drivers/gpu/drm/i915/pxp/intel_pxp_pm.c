// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2020 Intel Corporation.
 */

#include "intel_pxp.h"
#include "intel_pxp_irq.h"
#include "intel_pxp_pm.h"
#include "intel_pxp_session.h"

void intel_pxp_suspend(struct intel_pxp *pxp)
{
	if (!intel_pxp_is_enabled(pxp))
		return;

	pxp->arb_is_valid = false;

	intel_pxp_fini_hw(pxp);

	pxp->global_state_attacked = false;
}

void intel_pxp_resume(struct intel_pxp *pxp)
{
	if (!intel_pxp_is_enabled(pxp))
		return;

	/*
	 * The PXP component gets automatically unbound when we go into S3 and
	 * re-bound after we come out, so in that scenario we can defer the
	 * termination and re-creation of the arb session to the bind call.
	 */
	if (!pxp->pxp_component)
		return;

	intel_pxp_init_hw(pxp);
}
