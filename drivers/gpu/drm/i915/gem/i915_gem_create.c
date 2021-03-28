// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include "gem/i915_gem_ioctls.h"
#include "gem/i915_gem_region.h"

#include "i915_drv.h"
#include "i915_user_extensions.h"

static int
i915_gem_create(struct drm_file *file,
		struct intel_memory_region *mr,
		u64 *size_p,
		u32 *handle_p)
{
	struct drm_i915_gem_object *obj;
	u32 handle;
	u64 size;
	int ret;

	GEM_BUG_ON(!is_power_of_2(mr->min_page_size));
	size = round_up(*size_p, mr->min_page_size);
	if (size == 0)
		return -EINVAL;

	/* For most of the ABI (e.g. mmap) we think in system pages */
	GEM_BUG_ON(!IS_ALIGNED(size, PAGE_SIZE));

	/* Allocate the new object */
	obj = i915_gem_object_create_region(mr, size, 0);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	GEM_BUG_ON(size != obj->base.size);

	ret = drm_gem_handle_create(file, &obj->base, &handle);
	/* drop reference from allocate - handle holds it now */
	i915_gem_object_put(obj);
	if (ret)
		return ret;

	*handle_p = handle;
	*size_p = size;
	return 0;
}

int
i915_gem_dumb_create(struct drm_file *file,
		     struct drm_device *dev,
		     struct drm_mode_create_dumb *args)
{
	enum intel_memory_type mem_type;
	int cpp = DIV_ROUND_UP(args->bpp, 8);
	u32 format;

	switch (cpp) {
	case 1:
		format = DRM_FORMAT_C8;
		break;
	case 2:
		format = DRM_FORMAT_RGB565;
		break;
	case 4:
		format = DRM_FORMAT_XRGB8888;
		break;
	default:
		return -EINVAL;
	}

	/* have to work out size/pitch and return them */
	args->pitch = ALIGN(args->width * cpp, 64);

	/* align stride to page size so that we can remap */
	if (args->pitch > intel_plane_fb_max_stride(to_i915(dev), format,
						    DRM_FORMAT_MOD_LINEAR))
		args->pitch = ALIGN(args->pitch, 4096);

	if (args->pitch < args->width)
		return -EINVAL;

	args->size = mul_u32_u32(args->pitch, args->height);

	mem_type = INTEL_MEMORY_SYSTEM;
	if (HAS_LMEM(to_i915(dev)))
		mem_type = INTEL_MEMORY_LOCAL;

	return i915_gem_create(file,
			       intel_memory_region_by_type(to_i915(dev),
							   mem_type),
			       &args->size, &args->handle);
}

struct create_ext {
	struct drm_i915_private *i915;
};

static int __create_setparam(struct drm_i915_gem_object_param *args,
			     struct create_ext *ext_data)
{
	if (!(args->param & I915_OBJECT_PARAM)) {
		DRM_DEBUG("Missing I915_OBJECT_PARAM namespace\n");
		return -EINVAL;
	}

	return -EINVAL;
}

static int create_setparam(struct i915_user_extension __user *base, void *data)
{
	struct drm_i915_gem_create_ext_setparam ext;

	if (copy_from_user(&ext, base, sizeof(ext)))
		return -EFAULT;

	return __create_setparam(&ext.param, data);
}

static const i915_user_extension_fn create_extensions[] = {
	[I915_GEM_CREATE_EXT_SETPARAM] = create_setparam,
};

/**
 * Creates a new mm object and returns a handle to it.
 * @dev: drm device pointer
 * @data: ioctl data blob
 * @file: drm file pointer
 */
int
i915_gem_create_ioctl(struct drm_device *dev, void *data,
		      struct drm_file *file)
{
	struct drm_i915_private *i915 = to_i915(dev);
	struct create_ext ext_data = { .i915 = i915 };
	struct drm_i915_gem_create_ext *args = data;
	int ret;

	i915_gem_flush_free_objects(i915);

	ret = i915_user_extensions(u64_to_user_ptr(args->extensions),
				   create_extensions,
				   ARRAY_SIZE(create_extensions),
				   &ext_data);
	if (ret)
		return ret;

	return i915_gem_create(file,
			       intel_memory_region_by_type(i915,
							   INTEL_MEMORY_SYSTEM),
			       &args->size, &args->handle);
}
