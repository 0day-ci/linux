/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2017 Intel Corporation
 */

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pagemap.h>

#include "i915_drv.h"
#include "i915_gemfs.h"

#if defined(CONFIG_DRM_I915_THP_NATIVE) && defined(CONFIG_DRM_I915_THP_IOMMU)
static char *gemfd_mount_opts(struct drm_i915_private *i915)
{
	static char thp_native[] = "huge=" CONFIG_DRM_I915_THP_NATIVE;
	static char thp_iommu[] = "huge=" CONFIG_DRM_I915_THP_IOMMU;
	char *opts;

	opts = intel_vtd_active() ? thp_iommu : thp_native;
	drm_info(&i915->drm, "Transparent Hugepage mode '%s'", opts);

	return opts;
}
#else
static char *gemfd_mount_opts(struct drm_i915_private *i915)
{
	return NULL;
}
#endif


int i915_gemfs_init(struct drm_i915_private *i915)
{
	struct file_system_type *type;
	struct vfsmount *gemfs;

	type = get_fs_type("tmpfs");
	if (!type)
		return -ENODEV;

	/*
	 * By creating our own shmemfs mountpoint, we can pass in
	 * mount flags that better match our usecase.
	 *
	 * One example, although it is probably better with a per-file
	 * control, is selecting huge page allocations ("huge=within_size").
	 * However, we only do so to offset the overhead of iommu lookups
	 * due to bandwidth issues (slow reads) on Broadwell+.
	 */
	gemfs = vfs_kern_mount(type, SB_KERNMOUNT, type->name,
			       gemfd_mount_opts(i915));
	if (IS_ERR(gemfs))
		return PTR_ERR(gemfs);

	i915->mm.gemfs = gemfs;

	return 0;
}

void i915_gemfs_fini(struct drm_i915_private *i915)
{
	kern_unmount(i915->mm.gemfs);
}
