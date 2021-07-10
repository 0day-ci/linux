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

	slpc->max_freq_softlimit = 0;
	slpc->min_freq_softlimit = 0;

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

static int host2guc_slpc_set_param(struct intel_guc_slpc *slpc,
				   u32 id, u32 value)
{
	struct slpc_event_input data = {0};

	data.header.value = SLPC_EVENT(SLPC_EVENT_PARAMETER_SET, 2);
	data.args[0] = id;
	data.args[1] = value;

	return slpc_send(slpc, &data, 4);
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

static int slpc_set_param(struct intel_guc_slpc *slpc, u32 id, u32 value)
{
	struct drm_i915_private *i915 = slpc_to_i915(slpc);
	GEM_BUG_ON(id >= SLPC_MAX_PARAM);

	if (host2guc_slpc_set_param(slpc, id, value)) {
		drm_err(&i915->drm, "Unable to set param %x", id);
		return -EIO;
	}

	return 0;
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

/**
 * intel_guc_slpc_max_freq_set() - Set max frequency limit for SLPC.
 * @slpc: pointer to intel_guc_slpc.
 * @val: encoded frequency
 *
 * This function will invoke GuC SLPC action to update the max frequency
 * limit for slice and unslice.
 *
 * Return: 0 on success, non-zero error code on failure.
 */
int intel_guc_slpc_set_max_freq(struct intel_guc_slpc *slpc, u32 val)
{
	int ret;
	struct drm_i915_private *i915 = slpc_to_i915(slpc);
	intel_wakeref_t wakeref;

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	ret = slpc_set_param(slpc,
		       SLPC_PARAM_GLOBAL_MAX_GT_UNSLICE_FREQ_MHZ,
		       val);

	if (ret) {
		drm_err(&i915->drm,
			"Set max frequency unslice returned %d", ret);
		ret = -EIO;
		goto done;
	}

done:
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
	return ret;
}

int intel_guc_slpc_get_max_freq(struct intel_guc_slpc *slpc, u32 *val)
{
	struct slpc_shared_data *data;
	intel_wakeref_t wakeref;
	struct drm_i915_private *i915 = guc_to_gt(slpc_to_guc(slpc))->i915;
	int ret = 0;

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	/* Force GuC to update task data */
	if (slpc_read_task_state(slpc)) {
		DRM_ERROR("Unable to update task data");
		ret = -EIO;
		goto done;
	}

	GEM_BUG_ON(!slpc->vma);

	drm_clflush_virt_range(slpc->vaddr, sizeof(struct slpc_shared_data));
	data = slpc->vaddr;

	*val = DIV_ROUND_CLOSEST(data->task_state_data.max_unslice_freq *
				GT_FREQUENCY_MULTIPLIER, GEN9_FREQ_SCALER);

done:
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
	return ret;
}

/**
 * intel_guc_slpc_min_freq_set() - Set min frequency limit for SLPC.
 * @slpc: pointer to intel_guc_slpc.
 * @val: encoded frequency
 *
 * This function will invoke GuC SLPC action to update the min frequency
 * limit.
 *
 * Return: 0 on success, non-zero error code on failure.
 */
int intel_guc_slpc_set_min_freq(struct intel_guc_slpc *slpc, u32 val)
{
	int ret;
	struct intel_guc *guc = slpc_to_guc(slpc);
	struct drm_i915_private *i915 = guc_to_gt(guc)->i915;
	intel_wakeref_t wakeref;

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	ret = slpc_set_param(slpc,
		       SLPC_PARAM_GLOBAL_MIN_GT_UNSLICE_FREQ_MHZ,
		       val);
	if (ret) {
		drm_err(&i915->drm,
			"Set min frequency for unslice returned %d", ret);
		ret = -EIO;
		goto done;
	}

done:
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
	return ret;
}

int intel_guc_slpc_get_min_freq(struct intel_guc_slpc *slpc, u32 *val)
{
	struct slpc_shared_data *data;
	intel_wakeref_t wakeref;
	struct drm_i915_private *i915 = guc_to_gt(slpc_to_guc(slpc))->i915;
	int ret = 0;

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	/* Force GuC to update task data */
	if (slpc_read_task_state(slpc)) {
		DRM_ERROR("Unable to update task data");
		ret = -EIO;
		goto done;
	}

	GEM_BUG_ON(!slpc->vma);

	drm_clflush_virt_range(slpc->vaddr, sizeof(struct slpc_shared_data));
	data = slpc->vaddr;

	*val = DIV_ROUND_CLOSEST(data->task_state_data.min_unslice_freq *
				GT_FREQUENCY_MULTIPLIER, GEN9_FREQ_SCALER);

done:
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
	return ret;
}

void intel_guc_pm_intrmsk_enable(struct intel_gt *gt)
{
	u32 pm_intrmsk_mbz = 0;

	/* Allow GuC to receive ARAT timer expiry event.
	 * This interrupt register is setup by RPS code
	 * when host based Turbo is enabled.
	 */
	pm_intrmsk_mbz |= ARAT_EXPIRED_INTRMSK;

	intel_uncore_rmw(gt->uncore,
			   GEN6_PMINTRMSK, pm_intrmsk_mbz, 0);
}

static int intel_guc_slpc_set_softlimits(struct intel_guc_slpc *slpc)
{
	int ret = 0;

	/* Softlimits are initially equivalent to platform limits
	 * unless they have deviated from defaults, in which case,
	 * we retain the values and set min/max accordingly.
	 */
	if (!slpc->max_freq_softlimit)
		slpc->max_freq_softlimit = slpc->rp0_freq;
	else if (slpc->max_freq_softlimit != slpc->rp0_freq)
		ret = intel_guc_slpc_set_max_freq(slpc,
					slpc->max_freq_softlimit);

	if (!slpc->min_freq_softlimit)
		slpc->min_freq_softlimit = slpc->min_freq;
	else if (slpc->min_freq_softlimit != slpc->min_freq)
		ret = intel_guc_slpc_set_min_freq(slpc,
					slpc->min_freq_softlimit);

	return ret;
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
	u32 rp_state_cap;

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

	intel_guc_pm_intrmsk_enable(&i915->gt);

	if (slpc_read_task_state(slpc))
		drm_err(&i915->drm, "Unable to read task state data");

	drm_clflush_virt_range(slpc->vaddr, sizeof(struct slpc_shared_data));

	/* min and max frequency limits being used by SLPC */
	drm_info(&i915->drm, "SLPC min freq: %u Mhz, max is %u Mhz",
			DIV_ROUND_CLOSEST(data->task_state_data.min_unslice_freq *
				GT_FREQUENCY_MULTIPLIER, GEN9_FREQ_SCALER),
			DIV_ROUND_CLOSEST(data->task_state_data.max_unslice_freq *
				GT_FREQUENCY_MULTIPLIER, GEN9_FREQ_SCALER));

	rp_state_cap = intel_uncore_read(i915->gt.uncore, GEN6_RP_STATE_CAP);

	slpc->rp0_freq = ((rp_state_cap >> 0) & 0xff) * GT_FREQUENCY_MULTIPLIER;
	slpc->min_freq = ((rp_state_cap >> 16) & 0xff) * GT_FREQUENCY_MULTIPLIER;
	slpc->rp1_freq = ((rp_state_cap >> 8) & 0xff) * GT_FREQUENCY_MULTIPLIER;

	if (intel_guc_slpc_set_softlimits(slpc))
		drm_err(&i915->drm, "Unable to set softlimits");

	drm_info(&i915->drm,
		 "Platform fused frequency values -  min: %u Mhz, max: %u Mhz",
		 slpc->min_freq,
		 slpc->rp0_freq);

	return 0;
}

int intel_guc_slpc_info(struct intel_guc_slpc *slpc, struct drm_printer *p)
{
	struct drm_i915_private *i915 = guc_to_gt(slpc_to_guc(slpc))->i915;
	struct slpc_shared_data *data;
	struct slpc_platform_info *platform_info;
	struct slpc_task_state_data *task_state_data;
	intel_wakeref_t wakeref;
	int ret = 0;

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	if (slpc_read_task_state(slpc)) {
		ret = -EIO;
		goto done;
	}

	GEM_BUG_ON(!slpc->vma);

	drm_clflush_virt_range(slpc->vaddr, sizeof(struct slpc_shared_data));
	data = slpc->vaddr;

	platform_info = &data->platform_info;
	task_state_data = &data->task_state_data;

	drm_printf(p, "SLPC state: %s\n", slpc_state_stringify(data->global_state));
	drm_printf(p, "\tgtperf task active: %d\n",
			task_state_data->gtperf_task_active);
	drm_printf(p, "\tdcc task active: %d\n",
				task_state_data->dcc_task_active);
	drm_printf(p, "\tin dcc: %d\n",
				task_state_data->in_dcc);
	drm_printf(p, "\tfreq switch active: %d\n",
				task_state_data->freq_switch_active);
	drm_printf(p, "\tibc enabled: %d\n",
				task_state_data->ibc_enabled);
	drm_printf(p, "\tibc active: %d\n",
				task_state_data->ibc_active);
	drm_printf(p, "\tpg1 enabled: %s\n",
				yesno(task_state_data->pg1_enabled));
	drm_printf(p, "\tpg1 active: %s\n",
				yesno(task_state_data->pg1_active));
	drm_printf(p, "\tmax freq: %dMHz\n",
				DIV_ROUND_CLOSEST(data->task_state_data.max_unslice_freq *
				GT_FREQUENCY_MULTIPLIER, GEN9_FREQ_SCALER));
	drm_printf(p, "\tmin freq: %dMHz\n",
				DIV_ROUND_CLOSEST(data->task_state_data.min_unslice_freq *
				GT_FREQUENCY_MULTIPLIER, GEN9_FREQ_SCALER));

done:
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
	return ret;
}

void intel_guc_slpc_fini(struct intel_guc_slpc *slpc)
{
	if (!slpc->vma)
		return;

	i915_vma_unpin_and_release(&slpc->vma, I915_VMA_RELEASE_MAP);
}
