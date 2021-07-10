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

static inline struct intel_gt *slpc_to_gt(struct intel_guc_slpc *slpc)
{
	return guc_to_gt(slpc_to_guc(slpc));
}

static inline struct drm_i915_private *slpc_to_i915(struct intel_guc_slpc *slpc)
{
	return (slpc_to_gt(slpc))->i915;
}

static void slpc_mem_set_param(struct slpc_shared_data *data,
				u32 id, u32 value)
{
	GEM_BUG_ON(id >= SLPC_MAX_OVERRIDE_PARAMETERS);
	/* When the flag bit is set, corresponding value will be read
	 * and applied by slpc.
	 */
	data->override_params_set_bits[id >> 5] |= (1 << (id % 32));
	data->override_params_values[id] = value;
}

static void slpc_mem_unset_param(struct slpc_shared_data *data,
				 u32 id)
{
	GEM_BUG_ON(id >= SLPC_MAX_OVERRIDE_PARAMETERS);
	/* When the flag bit is unset, corresponding value will not be
	 * read by slpc.
	 */
	data->override_params_set_bits[id >> 5] &= (~(1 << (id % 32)));
	data->override_params_values[id] = 0;
}

static void slpc_mem_task_control(struct slpc_shared_data *data,
				 u64 val, u32 enable_id, u32 disable_id)
{
	/* Enabling a param involves setting the enable_id
	 * to 1 and disable_id to 0. Setting it to default
	 * will unset both enable and disable ids and let
	 * slpc choose it's default values.
	 */
	if (val == SLPC_PARAM_TASK_DEFAULT) {
		/* set default */
		slpc_mem_unset_param(data, enable_id);
		slpc_mem_unset_param(data, disable_id);
	} else if (val == SLPC_PARAM_TASK_ENABLED) {
		/* set enable */
		slpc_mem_set_param(data, enable_id, 1);
		slpc_mem_set_param(data, disable_id, 0);
	} else if (val == SLPC_PARAM_TASK_DISABLED) {
		/* set disable */
		slpc_mem_set_param(data, disable_id, 1);
		slpc_mem_set_param(data, enable_id, 0);
	}
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

/*
 * Send SLPC event to guc
 *
 */
static int slpc_send(struct intel_guc_slpc *slpc,
			struct slpc_event_input *input,
			u32 in_len)
{
	struct intel_guc *guc = slpc_to_guc(slpc);
	u32 *action;

	action = (u32 *)input;
	action[0] = INTEL_GUC_ACTION_SLPC_REQUEST;

	return intel_guc_send(guc, action, in_len);
}

static bool slpc_running(struct intel_guc_slpc *slpc)
{
	struct slpc_shared_data *data;
	u32 slpc_global_state;

	GEM_BUG_ON(!slpc->vma);

	drm_clflush_virt_range(slpc->vaddr, sizeof(struct slpc_shared_data));
	data = slpc->vaddr;

	slpc_global_state = data->global_state;

	return (data->global_state == SLPC_GLOBAL_STATE_RUNNING);
}

static int host2guc_slpc_query_task_state(struct intel_guc_slpc *slpc)
{
	struct intel_guc *guc = slpc_to_guc(slpc);
	u32 shared_data_gtt_offset = intel_guc_ggtt_offset(guc, slpc->vma);
	struct slpc_event_input data = {0};

	data.header.value = SLPC_EVENT(SLPC_EVENT_QUERY_TASK_STATE, 2);
	data.args[0] = shared_data_gtt_offset;
	data.args[1] = 0;

	return slpc_send(slpc, &data, 4);
}

static int slpc_read_task_state(struct intel_guc_slpc *slpc)
{
	return host2guc_slpc_query_task_state(slpc);
}

static const char *slpc_state_stringify(enum slpc_global_state state)
{
	const char *str = NULL;

	switch (state) {
	case SLPC_GLOBAL_STATE_NOT_RUNNING:
		str = "not running";
		break;
	case SLPC_GLOBAL_STATE_INITIALIZING:
		str = "initializing";
		break;
	case SLPC_GLOBAL_STATE_RESETTING:
		str = "resetting";
		break;
	case SLPC_GLOBAL_STATE_RUNNING:
		str = "running";
		break;
	case SLPC_GLOBAL_STATE_SHUTTING_DOWN:
		str = "shutting down";
		break;
	case SLPC_GLOBAL_STATE_ERROR:
		str = "error";
		break;
	default:
		str = "unknown";
		break;
	}

	return str;
}

static const char *get_slpc_state(struct intel_guc_slpc *slpc)
{
	struct slpc_shared_data *data;
	u32 slpc_global_state;

	GEM_BUG_ON(!slpc->vma);

	drm_clflush_virt_range(slpc->vaddr, sizeof(struct slpc_shared_data));
	data = slpc->vaddr;

	slpc_global_state = data->global_state;

	return slpc_state_stringify(slpc_global_state);
}

static int host2guc_slpc_reset(struct intel_guc_slpc *slpc)
{
	struct intel_guc *guc = slpc_to_guc(slpc);
	u32 shared_data_gtt_offset = intel_guc_ggtt_offset(guc, slpc->vma);
	struct slpc_event_input data = {0};
	int ret;

	data.header.value = SLPC_EVENT(SLPC_EVENT_RESET, 2);
	data.args[0] = shared_data_gtt_offset;
	data.args[1] = 0;

	/* TODO: Hardcoded 4 needs define */
	ret = slpc_send(slpc, &data, 4);

	if (!ret) {
		/* TODO: How long to Wait until SLPC is running */
		if (wait_for(slpc_running(slpc), 5)) {
			DRM_ERROR("SLPC not enabled! State = %s\n",
				  get_slpc_state(slpc));
			return -EIO;
		}
	}

	return ret;
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
	struct drm_i915_private *i915 = slpc_to_i915(slpc);
	struct slpc_shared_data *data;
	int ret;

	GEM_BUG_ON(!slpc->vma);

	memset(slpc->vaddr, 0, sizeof(struct slpc_shared_data));

	data = slpc->vaddr;
	data->shared_data_size = sizeof(struct slpc_shared_data);

	/* Enable only GTPERF task, Disable others */
	slpc_mem_task_control(data, SLPC_PARAM_TASK_ENABLED,
				SLPC_PARAM_TASK_ENABLE_GTPERF,
				SLPC_PARAM_TASK_DISABLE_GTPERF);

	slpc_mem_task_control(data, SLPC_PARAM_TASK_DISABLED,
				SLPC_PARAM_TASK_ENABLE_BALANCER,
				SLPC_PARAM_TASK_DISABLE_BALANCER);

	slpc_mem_task_control(data, SLPC_PARAM_TASK_DISABLED,
				SLPC_PARAM_TASK_ENABLE_DCC,
				SLPC_PARAM_TASK_DISABLE_DCC);

	ret = host2guc_slpc_reset(slpc);
	if (ret) {
		drm_err(&i915->drm, "SLPC Reset event returned %d", ret);
		return -EIO;
	}

	DRM_INFO("SLPC state: %s\n", get_slpc_state(slpc));

	if (slpc_read_task_state(slpc))
		drm_err(&i915->drm, "Unable to read task state data");

	drm_clflush_virt_range(slpc->vaddr, sizeof(struct slpc_shared_data));

	/* min and max frequency limits being used by SLPC */
	drm_info(&i915->drm, "SLPC min freq: %u Mhz, max is %u Mhz",
			DIV_ROUND_CLOSEST(data->task_state_data.min_unslice_freq *
				GT_FREQUENCY_MULTIPLIER, GEN9_FREQ_SCALER),
			DIV_ROUND_CLOSEST(data->task_state_data.max_unslice_freq *
				GT_FREQUENCY_MULTIPLIER, GEN9_FREQ_SCALER));

	return 0;
}

void intel_guc_slpc_fini(struct intel_guc_slpc *slpc)
{
	if (!slpc->vma)
		return;

	i915_vma_unpin_and_release(&slpc->vma, I915_VMA_RELEASE_MAP);
}
