// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

/*
 * Read before adding/removing content!
 *
 * Ensure that all functions defined here are also defined
 * in the i915_platform_arm64.c file.
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

#include <asm/hypervisor.h>

/* Start of i915_drv functionality */
bool run_as_guest(void)
{
	return !hypervisor_is_type(X86_HYPER_NATIVE);
}
/* End of i915_drv functionality */
