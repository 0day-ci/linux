// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <linux/list.h>
#include <linux/scatterlist.h>

#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_tt.h>

#include "i915_drv.h"
#include "intel_memory_region.h"
#include "intel_region_ttm.h"

#include "gem/i915_gem_region.h"
#include "gem/i915_gem_ttm.h"
#include "gem/i915_gem_ttm_pm.h"

/**
 * i915_ttm_backup_free - Free any backup attached to this object
 * @obj: The object whose backup is to be freed.
 */
void i915_ttm_backup_free(struct drm_i915_gem_object *obj)
{
	if (obj->ttm.backup) {
		i915_gem_object_put(obj->ttm.backup);
		obj->ttm.backup = NULL;
	}
}

static int i915_ttm_backup(struct i915_gem_apply_to_region *apply,
			   struct drm_i915_gem_object *obj)
{
	struct ttm_buffer_object *bo = i915_gem_to_ttm(obj);
	struct ttm_buffer_object *backup_bo;
	struct drm_i915_private *i915 = container_of(bo->bdev, typeof(*i915), bdev);
	struct intel_memory_region *sys_region;
	struct drm_i915_gem_object *backup;
	struct ttm_operation_ctx ctx = {};
	int err = 0;

	if (i915_gem_object_evictable(obj))
		return ttm_bo_validate(bo, i915_ttm_sys_placement(), &ctx);

	sys_region = i915->mm.regions[INTEL_REGION_SMEM];
	backup = i915_gem_object_create_region(sys_region,
					       obj->base.size,
					       0, 0);
	if (IS_ERR(backup))
		return PTR_ERR(backup);

	err = i915_gem_object_lock(backup, apply->ww);
	if (err)
		goto out_no_lock;

	backup_bo = i915_gem_to_ttm(backup);
	err = ttm_tt_populate(backup_bo->bdev, backup_bo->ttm, &ctx);
	if (err)
		goto out_no_populate;

	err = i915_gem_obj_copy_ttm(backup, obj, true, false);
	GEM_WARN_ON(err);

	obj->ttm.backup = backup;
	return 0;

out_no_populate:
	i915_gem_ww_unlock_single(backup);
out_no_lock:
	i915_gem_object_put(backup);

	return err;
}

static int i915_ttm_recover(struct i915_gem_apply_to_region *apply,
			    struct drm_i915_gem_object *obj)
{
	i915_ttm_backup_free(obj);
	return 0;
}

/**
 * i915_ttm_recover_region - Free the backup of all objects of a region
 * @mr: The memory region
 *
 * Checks all objects of a region if there is backup attached and if so
 * frees that backup. Typically this is called to recover after a partially
 * performed backup.
 */
void i915_ttm_recover_region(struct intel_memory_region *mr)
{
	static const struct i915_gem_apply_to_region_ops recover_ops = {
		.process_obj = i915_ttm_recover,
	};
	struct i915_gem_apply_to_region apply = {.ops = &recover_ops};
	int ret;

	ret = i915_gem_process_region(mr, &apply);
	GEM_WARN_ON(ret);
}

/**
 * i915_ttm_backup_region - Back up all objects of a region to smem.
 * @mr: The memory region
 * Loops over all objects of a region and either evicts them if they are
 * evictable or backs them up using a backup object if they are pinned.
 *
 * Return: Zero on success. Negative error code on error.
 */
int i915_ttm_backup_region(struct intel_memory_region *mr)
{
	static const struct i915_gem_apply_to_region_ops backup_ops = {
		.process_obj = i915_ttm_backup,
	};
	struct i915_gem_apply_to_region apply = {.ops = &backup_ops};
	int ret;

	ret = i915_gem_process_region(mr, &apply);
	if (ret)
		i915_ttm_recover_region(mr);

	return ret;
}

/**
 * struct i915_gem_ttm_pm_apply - Apply-to-region subclass for restore
 * @base: The i915_gem_apply_to_region we derive from.
 * @early_restore: Whether this is an early restore using memcpy only.
 */
struct i915_gem_ttm_pm_apply {
	struct i915_gem_apply_to_region base;
	bool early_restore;
};

static int i915_ttm_restore(struct i915_gem_apply_to_region *apply,
			    struct drm_i915_gem_object *obj)
{
	struct i915_gem_ttm_pm_apply *pm_apply =
		container_of(apply, typeof(*pm_apply), base);
	struct drm_i915_gem_object *backup = obj->ttm.backup;
	struct ttm_buffer_object *backup_bo = i915_gem_to_ttm(backup);
	struct ttm_operation_ctx ctx = {};
	int err;

	if (!obj->ttm.backup)
		return 0;

	if (pm_apply->early_restore && (obj->flags & I915_BO_ALLOC_USER))
		return 0;

	err = i915_gem_object_lock(backup, apply->ww);
	if (err)
		return err;

	/* Content may have been swapped. */
	err = ttm_tt_populate(backup_bo->bdev, backup_bo->ttm, &ctx);
	if (!err) {
		err = i915_gem_obj_copy_ttm(obj, backup, !pm_apply->early_restore,
					    false);
		GEM_WARN_ON(err);

		obj->ttm.backup = NULL;
		err = 0;
	}

	i915_gem_ww_unlock_single(backup);
	i915_gem_object_put(backup);

	return err;
}

/**
 * i915_ttm_restore_region - Back up all objects of a region to smem.
 * @mr: The memory region
 * @early: Whether to use memcpy only for early restore.
 *
 * Loops over all objects of a region and either evicts them if they are
 * evictable or backs them up using a backup object if they are pinned.
 *
 * Return: Zero on success. Negative error code on error.
 */
int i915_ttm_restore_region(struct intel_memory_region *mr,
			    bool early)
{
	static const struct i915_gem_apply_to_region_ops restore_ops = {
		.process_obj = i915_ttm_restore,
	};
	struct i915_gem_ttm_pm_apply pm_apply = {
		.base = {.ops = &restore_ops},
		.early_restore = early,
	};

	return i915_gem_process_region(mr, &pm_apply.base);
}
