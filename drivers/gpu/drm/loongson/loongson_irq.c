// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/pci.h>

#include <drm/drm_vblank.h>

#include "loongson_drv.h"

static irqreturn_t loongson_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = (struct drm_device *) arg;
	struct loongson_device *ldev = to_loongson_device(dev);
	struct loongson_crtc *lcrtc;
	u32 val;

	val = ls7a_mm_rreg(ldev, FB_INT_REG);
	ls7a_mm_wreg(ldev, FB_INT_REG, val & (0xffff << 16));

	if (val & FB_VSYNC0_INT)
		lcrtc = ldev->mode_info[0].crtc;
	else if (val & FB_VSYNC1_INT)
		lcrtc = ldev->mode_info[1].crtc;

	drm_crtc_handle_vblank(&lcrtc->base);

	return IRQ_HANDLED;
}

int loongson_irq_init(struct loongson_device *ldev)
{
	int ret;
	struct drm_device *dev = &ldev->dev;
	int irq = to_pci_dev(dev->dev)->irq;

	ret = drm_vblank_init(dev, ldev->num_crtc);
	if (ret) {
		dev_err(dev->dev, "Fatal error during vblank init: %d\n", ret);
		return ret;
	}
	DRM_INFO("drm vblank init finished\n");

	ret = devm_request_irq(dev->dev, irq, loongson_irq_handler, 0,
			       "loongson-drm", dev);
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
	u32 reg_val = ls7a_mm_rreg(ldev, FB_INT_REG);

	if (lcrtc->crtc_id)
		reg_val |= FB_VSYNC1_ENABLE;
	else
		reg_val |= FB_VSYNC0_ENABLE;

	ls7a_mm_wreg(ldev, FB_INT_REG, reg_val);

	return 0;
}

void loongson_crtc_disable_vblank(struct drm_crtc *crtc)
{
	struct loongson_crtc *lcrtc = to_loongson_crtc(crtc);
	struct loongson_device *ldev = lcrtc->ldev;
	u32 reg_val = ls7a_mm_rreg(ldev, FB_INT_REG);

	if (lcrtc->crtc_id)
		reg_val &= ~FB_VSYNC1_ENABLE;
	else
		reg_val &= ~FB_VSYNC0_ENABLE;

	ls7a_mm_wreg(ldev, FB_INT_REG, reg_val);
}

