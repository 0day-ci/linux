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
};

#endif
