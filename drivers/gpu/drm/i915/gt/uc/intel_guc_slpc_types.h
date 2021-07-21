/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef _INTEL_GUC_SLPC_TYPES_H_
#define _INTEL_GUC_SLPC_TYPES_H_

#include <linux/types.h>
#include "abi/guc_actions_slpc_abi.h"

#define SLPC_RESET_TIMEOUT_MS 5

struct intel_guc_slpc {

	struct i915_vma *vma;
	struct slpc_shared_data *vaddr;

	/* platform frequency limits */
	u32 min_freq;
	u32 rp0_freq;
	u32 rp1_freq;

	/* frequency softlimits */
	u32 min_freq_softlimit;
	u32 max_freq_softlimit;
};

#endif
