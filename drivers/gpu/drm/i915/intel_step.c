// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020,2021 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_step.h"

/*
 * Some platforms have unusual ways of mapping PCI revision ID to GT/display
 * steppings.  E.g., in some cases a higher PCI revision may translate to a
 * lower stepping of the GT and/or display IP.  This file provides lookup
 * tables to map the PCI revision into a standard set of stepping values that
 * can be compared numerically.
 *
 * Also note that some revisions/steppings may have been set aside as
 * placeholders but never materialized in real hardware; in those cases there
 * may be jumps in the revision IDs or stepping values in the tables below.
 */

static const struct intel_step_info skl_revid_step_tbl[] = {
	[0x0] = { .gt_step = STEP_A0, .display_step = STEP_A0 },
	[0x1] = { .gt_step = STEP_B0, .display_step = STEP_B0 },
	[0x2] = { .gt_step = STEP_C0, .display_step = STEP_C0 },
	[0x3] = { .gt_step = STEP_D0, .display_step = STEP_D0 },
	[0x4] = { .gt_step = STEP_E0, .display_step = STEP_E0 },
	[0x5] = { .gt_step = STEP_F0, .display_step = STEP_F0 },
	[0x6] = { .gt_step = STEP_G0, .display_step = STEP_G0 },
	[0x7] = { .gt_step = STEP_H0, .display_step = STEP_H0 },
	[0x9] = { .gt_step = STEP_J0, .display_step = STEP_J0 },
	[0xA] = { .gt_step = STEP_I1, .display_step = STEP_I1 },
};

static const struct intel_step_info kbl_revid_step_tbl[] = {
	[0] = { .gt_step = STEP_A0, .display_step = STEP_A0 },
	[1] = { .gt_step = STEP_B0, .display_step = STEP_B0 },
	[2] = { .gt_step = STEP_C0, .display_step = STEP_B0 },
	[3] = { .gt_step = STEP_D0, .display_step = STEP_B0 },
	[4] = { .gt_step = STEP_F0, .display_step = STEP_C0 },
	[5] = { .gt_step = STEP_C0, .display_step = STEP_B1 },
	[6] = { .gt_step = STEP_D1, .display_step = STEP_B1 },
	[7] = { .gt_step = STEP_G0, .display_step = STEP_C0 },
};

static const struct intel_step_info icl_revid_step_tbl[] = {
	[0] = { .gt_step = STEP_A0, .display_step = STEP_A0 },
	[3] = { .gt_step = STEP_B0, .display_step = STEP_B0 },
	[4] = { .gt_step = STEP_B2, .display_step = STEP_B2 },
	[5] = { .gt_step = STEP_C0, .display_step = STEP_C0 },
	[6] = { .gt_step = STEP_C1, .display_step = STEP_C1 },
	[7] = { .gt_step = STEP_D0, .display_step = STEP_D0 },
};

static const struct intel_step_info jsl_ehl_revid_step_tbl[] = {
	[0] = { .gt_step = STEP_A0, .display_step = STEP_A0 },
	[1] = { .gt_step = STEP_B0, .display_step = STEP_B0 },
};

static const struct intel_step_info tgl_uy_revid_step_tbl[] = {
	[0] = { .gt_step = STEP_A0, .display_step = STEP_A0 },
	[1] = { .gt_step = STEP_B0, .display_step = STEP_C0 },
	[2] = { .gt_step = STEP_B1, .display_step = STEP_C0 },
	[3] = { .gt_step = STEP_C0, .display_step = STEP_D0 },
};

/* Same GT stepping between tgl_uy_revids and tgl_revids don't mean the same HW */
static const struct intel_step_info tgl_revid_step_tbl[] = {
	[0] = { .gt_step = STEP_A0, .display_step = STEP_B0 },
	[1] = { .gt_step = STEP_B0, .display_step = STEP_D0 },
};

static const struct intel_step_info adls_revid_step_tbl[] = {
	[0x0] = { .gt_step = STEP_A0, .display_step = STEP_A0 },
	[0x1] = { .gt_step = STEP_A0, .display_step = STEP_A2 },
	[0x4] = { .gt_step = STEP_B0, .display_step = STEP_B0 },
	[0x8] = { .gt_step = STEP_C0, .display_step = STEP_B0 },
	[0xC] = { .gt_step = STEP_D0, .display_step = STEP_C0 },
};

static const struct intel_step_info adlp_revid_step_tbl[] = {
	[0x0] = { .gt_step = STEP_A0, .display_step = STEP_A0 },
	[0x4] = { .gt_step = STEP_B0, .display_step = STEP_B0 },
	[0x8] = { .gt_step = STEP_C0, .display_step = STEP_C0 },
	[0xC] = { .gt_step = STEP_C0, .display_step = STEP_D0 },
};

void intel_step_init(struct drm_i915_private *i915)
{
	const struct intel_step_info *revids = NULL;
	int size = 0;
	int revid = INTEL_REVID(i915);
	struct intel_step_info step = {};

	if (IS_ALDERLAKE_P(i915)) {
		revids = adlp_revid_step_tbl;
		size = ARRAY_SIZE(adlp_revid_step_tbl);
	} else if (IS_ALDERLAKE_S(i915)) {
		revids = adls_revid_step_tbl;
		size = ARRAY_SIZE(adls_revid_step_tbl);
	} else if (IS_TGL_U(i915) || IS_TGL_Y(i915)) {
		revids = tgl_uy_revid_step_tbl;
		size = ARRAY_SIZE(tgl_uy_revid_step_tbl);
	} else if (IS_TIGERLAKE(i915)) {
		revids = tgl_revid_step_tbl;
		size = ARRAY_SIZE(tgl_revid_step_tbl);
	} else if (IS_JSL_EHL(i915)) {
		revids = jsl_ehl_revid_step_tbl;
		size = ARRAY_SIZE(jsl_ehl_revid_step_tbl);
	} else if (IS_ICELAKE(i915)) {
		revids = icl_revid_step_tbl;
		size = ARRAY_SIZE(icl_revid_step_tbl);
	} else if (IS_KABYLAKE(i915)) {
		revids = kbl_revid_step_tbl;
		size = ARRAY_SIZE(kbl_revid_step_tbl);
	} else if (IS_SKYLAKE(i915)) {
		revids = skl_revid_step_tbl;
		size = ARRAY_SIZE(skl_revid_step_tbl);
	}

	/* Not using the stepping scheme for the platform yet. */
	if (!revids)
		return;

	if (revid < size && revids[revid].gt_step != STEP_NONE) {
		step = revids[revid];
	} else {
		drm_warn(&i915->drm, "Unknown revid 0x%02x\n", revid);

		/*
		 * If we hit a gap in the revid array, use the information for
		 * the next revid.
		 *
		 * This may be wrong in all sorts of ways, especially if the
		 * steppings in the array are not monotonically increasing, but
		 * it's better than defaulting to 0.
		 */
		while (revid < size && revids[revid].gt_step == STEP_NONE)
			revid++;

		if (revid < size) {
			drm_dbg(&i915->drm, "Using steppings for revid 0x%02x\n",
				revid);
			step = revids[revid];
		} else {
			drm_dbg(&i915->drm, "Using future steppings\n");
			step.gt_step = STEP_FUTURE;
			step.display_step = STEP_FUTURE;
		}
	}

	if (drm_WARN_ON(&i915->drm, step.gt_step == STEP_NONE))
		return;

	RUNTIME_INFO(i915)->step = step;
}
