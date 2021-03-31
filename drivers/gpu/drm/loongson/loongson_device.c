// SPDX-License-Identifier: GPL-2.0-or-later

#include "loongson_drv.h"

u32 loongson_gpu_offset(struct drm_plane_state *state)
{
	struct drm_gem_vram_object *gbo;
	struct loongson_crtc *lcrtc;
	struct loongson_device *ldev;
	u32 gpu_addr;

	lcrtc = to_loongson_crtc(state->crtc);
	ldev = lcrtc->ldev;

	gbo = drm_gem_vram_of_gem(state->fb->obj[0]);
	gpu_addr = ldev->vram_start + drm_gem_vram_offset(gbo);

	return gpu_addr;
}

u32 ls7a_io_rreg(struct loongson_device *ldev, u32 offset)
{
	u32 val;

	val = readl(ldev->io + offset);

	return val;
}

void ls7a_io_wreg(struct loongson_device *ldev, u32 offset, u32 val)
{
	writel(val, ldev->io + offset);
}

u32 ls7a_mm_rreg(struct loongson_device *ldev, u32 offset)
{
	u32 val;

	val = readl(ldev->mmio + offset);

	return val;
}

void ls7a_mm_wreg(struct loongson_device *ldev, u32 offset, u32 val)
{
	writel(val, ldev->mmio + offset);
}
