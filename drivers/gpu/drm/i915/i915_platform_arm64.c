// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

/*
 * Read before adding/removing content!
 *
 * Ensure that all functions defined here are also defined
 * in the i915_platform_x86.c file.
 *
 * If the function is a dummy function, be sure to add
 * a DRM_WARN() call to note that the function is a
 * dummy function to users so that we can better track
 * any issues that arise due to changes in either file.
 *
 * Also be sure to label Start/End of sections where
 * functions originate from. These files will host
 * architecture-specific content from a myriad of files,
 * labeling the sections will help devs keep track of
 * where the calls interact.
 */

#include "i915_platform.h"

/* Start of i915_drv functionality */
/* Intel VT-d is not used on ARM64 systems */
bool run_as_guest(void)
{
	WARN(1, "%s not supported on arm64 platforms.", __func__);
	return false;
}
/* End of i915_drv functionality */
