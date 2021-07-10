/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2020 Intel Corporation
 */

#include <asm/msr-index.h>

#include "gt/intel_gt.h"
#include "gt/intel_rps.h"

#include "i915_drv.h"
#include "intel_guc_slpc.h"
#include "intel_pm.h"

static inline struct intel_guc *slpc_to_guc(struct intel_guc_slpc *slpc)
{
	return container_of(slpc, struct intel_guc, slpc);
}

static int slpc_shared_data_init(struct intel_guc_slpc *slpc)
{
	struct intel_guc *guc = slpc_to_guc(slpc);
	int err;
	u32 size = PAGE_ALIGN(sizeof(struct slpc_shared_data));

	err = intel_guc_allocate_and_map_vma(guc, size, &slpc->vma, &slpc->vaddr);
	if (unlikely(err)) {
		DRM_ERROR("Failed to allocate slpc struct (err=%d)\n", err);
		i915_vma_unpin_and_release(&slpc->vma, I915_VMA_RELEASE_MAP);
		return err;
	}

	return err;
}

int intel_guc_slpc_init(struct intel_guc_slpc *slpc)
{
	GEM_BUG_ON(slpc->vma);

	return slpc_shared_data_init(slpc);
}

/*
 * intel_guc_slpc_enable() - Start SLPC
 * @slpc: pointer to intel_guc_slpc.
 *
 * SLPC is enabled by setting up the shared data structure and
 * sending reset event to GuC SLPC. Initial data is setup in
 * intel_guc_slpc_init. Here we send the reset event. We do
 * not currently need a slpc_disable since this is taken care
 * of automatically when a reset/suspend occurs and the guc
 * channels are destroyed.
 *
 * Return: 0 on success, non-zero error code on failure.
 */
int intel_guc_slpc_enable(struct intel_guc_slpc *slpc)
{
	return 0;
}

void intel_guc_slpc_fini(struct intel_guc_slpc *slpc)
{
	if (!slpc->vma)
		return;

	i915_vma_unpin_and_release(&slpc->vma, I915_VMA_RELEASE_MAP);
}
