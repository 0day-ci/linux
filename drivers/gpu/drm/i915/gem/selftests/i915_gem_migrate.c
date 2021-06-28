// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020-2021 Intel Corporation
 */

#include "gt/intel_migrate.h"

static int igt_smem_create_migrate(void *arg)
{
	struct intel_gt *gt = arg;
	struct drm_i915_private *i915 = gt->i915;
	struct drm_i915_gem_object *obj;
	struct i915_gem_ww_ctx ww;
	int err = 0;

	/* Switch object backing-store on create */
	obj = i915_gem_object_create_lmem(i915, PAGE_SIZE, 0);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	for_i915_gem_ww(&ww, err, true) {
		err = i915_gem_object_lock(obj, &ww);
		if (err)
			continue;

		if (!i915_gem_object_can_migrate(obj, INTEL_REGION_SMEM)) {
			err = -EINVAL;
			continue;
		}

		err = i915_gem_object_migrate(obj, &ww, INTEL_REGION_SMEM);
		if (err)
			continue;

		err = i915_gem_object_pin_pages(obj);
		if (err)
			continue;

		if (i915_gem_object_can_migrate(obj, INTEL_REGION_LMEM))
			err = -EINVAL;

		i915_gem_object_unpin_pages(obj);
	}
	i915_gem_object_put(obj);

	return err;
}

static int igt_lmem_create_migrate(void *arg)
{
	struct intel_gt *gt = arg;
	struct drm_i915_private *i915 = gt->i915;
	struct drm_i915_gem_object *obj;
	struct i915_gem_ww_ctx ww;
	int err = 0;

	/* Switch object backing-store on create */
	obj = i915_gem_object_create_shmem(i915, PAGE_SIZE);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	for_i915_gem_ww(&ww, err, true) {
		err = i915_gem_object_lock(obj, &ww);
		if (err)
			continue;

		if (!i915_gem_object_can_migrate(obj, INTEL_REGION_LMEM)) {
			err = -EINVAL;
			continue;
		}

		err = i915_gem_object_migrate(obj, &ww, INTEL_REGION_LMEM);
		if (err)
			continue;

		err = i915_gem_object_pin_pages(obj);
		if (err)
			continue;

		if (i915_gem_object_can_migrate(obj, INTEL_REGION_SMEM))
			err = -EINVAL;

		i915_gem_object_unpin_pages(obj);
	}
	i915_gem_object_put(obj);

	return err;
}

static int lmem_pages_migrate_one(struct i915_gem_ww_ctx *ww,
				  struct drm_i915_gem_object *obj)
{
	int err;

	err = i915_gem_object_lock(obj, ww);
	if (err)
		return err;

	err = i915_gem_object_wait(obj,
				   I915_WAIT_INTERRUPTIBLE |
				   I915_WAIT_PRIORITY |
				   I915_WAIT_ALL,
				   MAX_SCHEDULE_TIMEOUT);
	if (err)
		return err;

	if (i915_gem_object_is_lmem(obj)) {
		if (!i915_gem_object_can_migrate(obj, INTEL_REGION_SMEM)) {
			pr_err("object can't migrate to smem.\n");
			return -EINVAL;
		}

		err = i915_gem_object_migrate(obj, ww, INTEL_REGION_SMEM);
		if (err) {
			pr_err("Object failed migration to smem\n");
			if (err)
				return err;
		}

		if (i915_gem_object_is_lmem(obj)) {
			pr_err("object still backed by lmem\n");
			err = -EINVAL;
		}

		if (!i915_gem_object_has_struct_page(obj)) {
			pr_err("object not backed by struct page\n");
			err = -EINVAL;
		}

	} else {
		if (!i915_gem_object_can_migrate(obj, INTEL_REGION_LMEM)) {
			pr_err("object can't migrate to lmem.\n");
			return -EINVAL;
		}

		err = i915_gem_object_migrate(obj, ww, INTEL_REGION_LMEM);
		if (err) {
			pr_err("Object failed migration to lmem\n");
			if (err)
				return err;
		}

		if (i915_gem_object_has_struct_page(obj)) {
			pr_err("object still backed by struct page\n");
			err = -EINVAL;
		}

		if (!i915_gem_object_is_lmem(obj)) {
			pr_err("object not backed by lmem\n");
			err = -EINVAL;
		}
	}

	return err;
}

static int igt_lmem_pages_migrate(void *arg)
{
	struct intel_gt *gt = arg;
	struct drm_i915_private *i915 = gt->i915;
	struct drm_i915_gem_object *obj;
	struct i915_gem_ww_ctx ww;
	struct i915_request *rq;
	int err;
	int i;

	/* From LMEM to shmem and back again */

	obj = i915_gem_object_create_lmem(i915, SZ_2M, 0);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	err = i915_gem_object_lock(obj, NULL);
	if (err)
		goto out_put;

	err = ____i915_gem_object_get_pages(obj);
	if (err) {
		i915_gem_object_unlock(obj);
		goto out_put;
	}

	err = intel_context_migrate_clear(gt->migrate.context, NULL,
					  obj->mm.pages->sgl, obj->cache_level,
					  i915_gem_object_is_lmem(obj),
					  0, &rq);
	if (rq) {
		dma_resv_add_excl_fence(obj->base.resv, &rq->fence);
		i915_request_put(rq);
	}
	i915_gem_object_unlock(obj);
	if (err)
		goto out_put;

	for (i = 1; i <= 4; ++i) {
		for_i915_gem_ww(&ww, err, true) {
			err = lmem_pages_migrate_one(&ww, obj);
			if (err)
				continue;

			err = i915_gem_object_wait_migration(obj, true);
			if (err)
				continue;

			err = intel_migrate_clear(&gt->migrate, &ww, NULL,
						  obj->mm.pages->sgl,
						  obj->cache_level,
						  i915_gem_object_is_lmem(obj),
						  0xdeadbeaf, &rq);
			if (rq) {
				dma_resv_add_excl_fence(obj->base.resv,
							&rq->fence);
				i915_request_put(rq);
			}
		}
		if (err)
			break;
	}
out_put:
	i915_gem_object_put(obj);

	return err;
}

int i915_gem_migrate_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(igt_smem_create_migrate),
		SUBTEST(igt_lmem_create_migrate),
		SUBTEST(igt_lmem_pages_migrate),
	};

	if (!HAS_LMEM(i915))
		return 0;

	return intel_gt_live_subtests(tests, &i915->gt);
}
