// SPDX-License-Identifier: GPL-2.0-or-later

#include "loongson_drv.h"
#include <linux/pci.h>

int loongson_irq_init(struct loongson_device *ldev)
{
	struct drm_device *dev;
	int ret, irq;

	dev = ldev->dev;
	irq = dev->pdev->irq;

	ret = drm_vblank_init(dev, ldev->num_crtc);
	if (ret) {
		dev_err(dev->dev, "Fatal error during vblank init: %d\n", ret);
		return ret;
	}
	DRM_INFO("drm vblank init finished\n");

	ret = drm_irq_install(dev, irq);
	if (ret) {
		dev_err(dev->dev, "Fatal error during irq install: %d\n", ret);
		return ret;
	}
	DRM_INFO("loongson irq initialized\n");

	return 0;
}

int loongson_crtc_enable_vblank(struct drm_crtc *crtc)
{
	struct loongson_crtc *lcrtc = to_loongson_crtc(crtc);
	struct loongson_device *ldev = lcrtc->ldev;
	u32 reg_val;

	if (lcrtc->crtc_id) {
		reg_val = ls7a_mm_rreg(ldev, FB_INT_REG);
		reg_val |= FB_VSYNC1_ENABLE;
		ls7a_mm_wreg(ldev, FB_INT_REG, reg_val);
	} else {
		reg_val = ls7a_mm_rreg(ldev, FB_INT_REG);
		reg_val |= FB_VSYNC0_ENABLE;
		ls7a_mm_wreg(ldev, FB_INT_REG, reg_val);
	}

	return 0;
}

void loongson_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct loongson_crtc *lcrtc = to_loongson_crtc(crtc);
	struct loongson_device *ldev = lcrtc->ldev;
	u32 reg_val;

	if (lcrtc->crtc_id) {
		reg_val = ls7a_mm_rreg(ldev, FB_INT_REG);
		reg_val &= ~FB_VSYNC1_ENABLE;
		ls7a_mm_wreg(ldev, FB_INT_REG, reg_val);
	} else {
		reg_val = ls7a_mm_rreg(ldev, FB_INT_REG);
		reg_val &= ~FB_VSYNC0_ENABLE;
		ls7a_mm_wreg(ldev, FB_INT_REG, reg_val);
	}
}

irqreturn_t loongson_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = (struct drm_device *) arg;
	struct loongson_device *ldev = dev->dev_private;
	struct loongson_crtc *lcrtc;
	u32 val;

	val = ls7a_mm_rreg(ldev, FB_INT_REG);
	ls7a_mm_wreg(ldev, FB_INT_REG, val & (0xffff << 16));

	if (val & FB_VSYNC0_INT) {
		lcrtc = ldev->mode_info[0].crtc;
		drm_crtc_handle_vblank(&lcrtc->base);
	}

	if (val & FB_VSYNC1_INT) {
		lcrtc = ldev->mode_info[1].crtc;
		drm_crtc_handle_vblank(&lcrtc->base);
	}

	return IRQ_HANDLED;
}

void loongson_irq_preinstall(struct drm_device *dev)
{
	struct loongson_device *ldev = dev->dev_private;

	ls7a_mm_wreg(ldev, FB_INT_REG, 0x0000 << 16);
}

void loongson_irq_uninstall(struct drm_device *dev)
{
	struct loongson_device *ldev = dev->dev_private;

	if (ldev == NULL)
		return;

	ls7a_mm_wreg(ldev, FB_INT_REG, 0x0000 << 16);
}
