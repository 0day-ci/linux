/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2020 Intel Corporation
 */

#ifndef _INTEL_GUC_SLPC_H_
#define _INTEL_GUC_SLPC_H_

#include <linux/mutex.h>
#include "intel_guc_slpc_fwif.h"

struct intel_guc_slpc {
	/*Protects access to vma and SLPC actions */
	struct i915_vma *vma;
	void *vaddr;

	/* platform frequency limits */
	u32 min_freq;
	u32 rp0_freq;
	u32 rp1_freq;

	/* frequency softlimits */
	u32 min_freq_softlimit;
	u32 max_freq_softlimit;

	struct {
		u32 param_id;
		u32 param_value;
		u32 param_override;
	} debug;
};

int intel_guc_slpc_init(struct intel_guc_slpc *slpc);
int intel_guc_slpc_enable(struct intel_guc_slpc *slpc);
void intel_guc_slpc_fini(struct intel_guc_slpc *slpc);

#endif
