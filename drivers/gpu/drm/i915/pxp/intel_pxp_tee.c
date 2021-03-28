// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2020 Intel Corporation.
 */

#include <linux/component.h>
#include "drm/i915_pxp_tee_interface.h"
#include "drm/i915_component.h"
#include "i915_drv.h"
#include "intel_pxp.h"
#include "intel_pxp_session.h"
#include "intel_pxp_tee.h"

#define PXP_TEE_APIVER 0x40002
#define PXP_TEE_ARB_CMDID 0x1e
#define PXP_TEE_ARB_PROTECTION_MODE 0x2

/* PXP TEE message header */
struct pxp_tee_cmd_header {
	u32 api_version;
	u32 command_id;
	u32 status;
	/* Length of the message (excluding the header) */
	u32 buffer_len;
} __packed;

/* PXP TEE message input to create a arbitrary session */
struct pxp_tee_create_arb_in {
	struct pxp_tee_cmd_header header;
	u32 protection_mode;
	u32 session_id;
} __packed;

/* PXP TEE message output to create a arbitrary session */
struct pxp_tee_create_arb_out {
	struct pxp_tee_cmd_header header;
} __packed;

static inline struct intel_pxp *i915_dev_to_pxp(struct device *i915_kdev)
{
	return &kdev_to_i915(i915_kdev)->gt.pxp;
}

static int intel_pxp_tee_io_message(struct intel_pxp *pxp,
				    void *msg_in, u32 msg_in_size,
				    void *msg_out, u32 msg_out_max_size,
				    u32 *msg_out_rcv_size)
{
	struct drm_i915_private *i915 = pxp_to_gt(pxp)->i915;
	struct i915_pxp_component *pxp_component = pxp->pxp_component;
	int ret;

	ret = pxp_component->ops->send(pxp_component->tee_dev, msg_in, msg_in_size);
	if (ret) {
		drm_err(&i915->drm, "Failed to send PXP TEE message\n");
		return ret;
	}

	ret = pxp_component->ops->recv(pxp_component->tee_dev, msg_out, msg_out_max_size);
	if (ret < 0) {
		drm_err(&i915->drm, "Failed to receive PXP TEE message\n");
		return ret;
	}

	if (ret > msg_out_max_size) {
		drm_err(&i915->drm,
			"Failed to receive PXP TEE message due to unexpected output size\n");
		return -ENOSPC;
	}

	if (msg_out_rcv_size)
		*msg_out_rcv_size = ret;

	return 0;
}

/**
 * i915_pxp_tee_component_bind - bind function to pass the function pointers to pxp_tee
 * @i915_kdev: pointer to i915 kernel device
 * @tee_kdev: pointer to tee kernel device
 * @data: pointer to pxp_tee_master containing the function pointers
 *
 * This bind function is called during the system boot or resume from system sleep.
 *
 * Return: return 0 if successful.
 */
static int i915_pxp_tee_component_bind(struct device *i915_kdev,
				       struct device *tee_kdev, void *data)
{
	struct drm_i915_private *i915 = kdev_to_i915(i915_kdev);
	struct intel_pxp *pxp = i915_dev_to_pxp(i915_kdev);
	int ret;

	pxp->pxp_component = data;
	pxp->pxp_component->tee_dev = tee_kdev;

	/* the component is required to fully start the PXP HW */
	intel_pxp_init_hw(pxp);
	ret = intel_pxp_wait_for_arb_start(pxp);
	if (ret) {
		drm_err(&i915->drm, "Failed to create arb session during bind\n");
		intel_pxp_fini_hw(pxp);
		pxp->pxp_component = NULL;
	}

	return ret;
}

static void i915_pxp_tee_component_unbind(struct device *i915_kdev,
					  struct device *tee_kdev, void *data)
{
	struct intel_pxp *pxp = i915_dev_to_pxp(i915_kdev);

	intel_pxp_fini_hw(pxp);

	pxp->pxp_component = NULL;
}

static const struct component_ops i915_pxp_tee_component_ops = {
	.bind   = i915_pxp_tee_component_bind,
	.unbind = i915_pxp_tee_component_unbind,
};

int intel_pxp_tee_component_init(struct intel_pxp *pxp)
{
	int ret;
	struct intel_gt *gt = pxp_to_gt(pxp);
	struct drm_i915_private *i915 = gt->i915;

	ret = component_add_typed(i915->drm.dev, &i915_pxp_tee_component_ops,
				  I915_COMPONENT_PXP);
	if (ret < 0) {
		drm_err(&i915->drm, "Failed to add PXP component (%d)\n", ret);
		return ret;
	}

	return 0;
}

void intel_pxp_tee_component_fini(struct intel_pxp *pxp)
{
	struct intel_gt *gt = pxp_to_gt(pxp);
	struct drm_i915_private *i915 = gt->i915;

	if (!pxp->pxp_component)
		return;

	component_del(i915->drm.dev, &i915_pxp_tee_component_ops);
}

int intel_pxp_tee_cmd_create_arb_session(struct intel_pxp *pxp,
					 int arb_session_id)
{
	struct drm_i915_private *i915 = pxp_to_gt(pxp)->i915;
	struct pxp_tee_create_arb_in msg_in = {0};
	struct pxp_tee_create_arb_out msg_out = {0};
	int ret;

	msg_in.header.api_version = PXP_TEE_APIVER;
	msg_in.header.command_id = PXP_TEE_ARB_CMDID;
	msg_in.header.buffer_len = sizeof(msg_in) - sizeof(msg_in.header);
	msg_in.protection_mode = PXP_TEE_ARB_PROTECTION_MODE;
	msg_in.session_id = arb_session_id;

	ret = intel_pxp_tee_io_message(pxp,
				       &msg_in, sizeof(msg_in),
				       &msg_out, sizeof(msg_out),
				       NULL);

	if (ret)
		drm_err(&i915->drm, "Failed to send tee msg ret=[%d]\n", ret);

	return ret;
}
