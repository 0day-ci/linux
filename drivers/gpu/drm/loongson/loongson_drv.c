// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Loongson LS7A1000 bridge chipset drm driver
 */

#include <linux/console.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include "loongson_drv.h"

/* Interface history:
 * 0.1 - original.
 * 0.2 - add i2c and connector detect.
 */
#define DRIVER_MAJOR 0
#define DRIVER_MINOR 2

static const struct drm_mode_config_funcs loongson_mode_funcs = {
	.fb_create = drm_gem_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
	.output_poll_changed = drm_fb_helper_output_poll_changed,
	.mode_valid = drm_vram_helper_mode_valid
};

static int loongson_device_init(struct drm_device *dev, uint32_t flags)
{
	struct loongson_device *ldev = dev->dev_private;
	struct pci_dev *gpu_pdev;
	resource_size_t aper_base;
	resource_size_t aper_size;
	resource_size_t mmio_base;
	resource_size_t mmio_size;
	u32 ret;

	/* GPU MEM */
	/* We need get 7A-gpu pci device information for ldev->gpu_pdev */
	/* dev->pdev save 7A-dc pci device information */
	gpu_pdev = pci_get_device(PCI_VENDOR_ID_LOONGSON,
				  PCI_DEVICE_ID_LOONGSON_GPU, NULL);
	if (IS_ERR(gpu_pdev))
		return PTR_ERR(gpu_pdev);

	ldev->gpu_pdev = gpu_pdev;
	aper_base = pci_resource_start(gpu_pdev, 2);
	aper_size = pci_resource_len(gpu_pdev, 2);
	ldev->vram_start = (u32)aper_base;
	ldev->vram_size = (u32)aper_size;

	if (!devm_request_mem_region(ldev->dev->dev, ldev->vram_start,
				     ldev->vram_size, "loongson_vram")) {
		DRM_ERROR("Can't reserve VRAM\n");
		return -ENXIO;
	}

	/* DC MEM */
	mmio_base = pci_resource_start(ldev->dev->pdev, 0);
	mmio_size = pci_resource_len(ldev->dev->pdev, 0);
	ldev->mmio = devm_ioremap(dev->dev, mmio_base, mmio_size);
	if (!ldev->mmio) {
		drm_err(dev, "Cannot map mmio region\n");
		return -ENOMEM;
	}

	if (!devm_request_mem_region(ldev->dev->dev, mmio_base,
				     mmio_size, "loongson_mmio")) {
		DRM_ERROR("Can't reserve mmio registers\n");
		return -ENOMEM;
	}

	/* DC IO */
	ldev->io = ioremap(LS7A_CHIPCFG_REG_BASE, 0xf);
	if (ldev->io == NULL)
		return -ENOMEM;

	ret = loongson_dc_gpio_init(ldev);
	if (ret) {
		DRM_ERROR("Failed to initialize dc gpios\n");
		return ret;
	}

	ret = loongson_i2c_init(ldev);
	if (ret) {
		DRM_ERROR("Failed to initialize dc i2c\n");
		return ret;
	}

	DRM_INFO("DC mmio base 0x%llx size 0x%llx io 0x%llx\n",
		 mmio_base, mmio_size, *(u64 *)ldev->io);
	DRM_INFO("GPU vram start = 0x%x size = 0x%x\n",
		 ldev->vram_start, ldev->vram_size);

	return 0;
}

int loongson_modeset_init(struct loongson_device *ldev)
{
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	int i;
	u32 ret;

	ldev->dev->mode_config.allow_fb_modifiers = true;

	for (i = 0; i < 2; i++) {
		ret = loongson_crtc_init(ldev, i);
		if (ret) {
			DRM_WARN("loongson crtc%d init failed\n", i);
			continue;
		}

		ret = loongson_encoder_init(ldev, i);
		if (ret) {
			DRM_ERROR("loongson_encoder_init failed\n");
			return -1;
		}

		ret = loongson_connector_init(ldev, i);
		if (ret) {
			DRM_ERROR("loongson_connector_init failed\n");
			return -1;
		}

		encoder = &ldev->mode_info[i].encoder->base;
		connector = &ldev->mode_info[i].connector->base;
		drm_connector_attach_encoder(connector, encoder);
		ldev->num_crtc++;
	}

	return 0;
}

static int loongson_drm_load(struct drm_device *dev, unsigned long flags)
{
	struct loongson_device *ldev;
	int ret;

	ldev = devm_kzalloc(dev->dev, sizeof(*ldev), GFP_KERNEL);
	if (!ldev)
		return -ENOMEM;

	dev->dev_private = ldev;
	ldev->dev = dev;

	ret = loongson_device_init(dev, flags);
	if (ret)
		goto err;

	ret = drmm_vram_helper_init(dev, ldev->vram_start, ldev->vram_size);
	if (ret)
		goto err;

	drm_mode_config_init(dev);
	dev->mode_config.funcs = (void *)&loongson_mode_funcs;
	dev->mode_config.min_width = 1;
	dev->mode_config.min_height = 1;
	dev->mode_config.max_width = 4096;
	dev->mode_config.max_height = 4096;
	dev->mode_config.preferred_depth = 32;
	dev->mode_config.prefer_shadow = 1;
	dev->mode_config.fb_base = ldev->vram_start;

	pci_set_drvdata(dev->pdev, dev);

	ret = loongson_modeset_init(ldev);
	if (ret)
		dev_err(dev->dev, "Fatal error during modeset init: %d\n", ret);

	drm_kms_helper_poll_init(dev);
	drm_mode_config_reset(dev);

	return 0;

err:
	drm_err(dev, "failed to initialize drm driver: %d\n", ret);
	return ret;
}

static void loongson_drm_unload(struct drm_device *dev)
{
	drm_vram_helper_release_mm(dev);
	drm_mode_config_cleanup(dev);
	dev->dev_private = NULL;
	dev_set_drvdata(dev->dev, NULL);
}

DEFINE_DRM_GEM_FOPS(fops);

static struct drm_driver loongson_drm_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.fops = &fops,
	DRM_GEM_VRAM_DRIVER,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
};

static int loongson_pci_probe(struct pci_dev *pdev,
			      const struct pci_device_id *ent)
{
	int ret;
	struct drm_device *dev;

	DRM_INFO("Start loongson drm probe\n");
	dev = drm_dev_alloc(&loongson_drm_driver, &pdev->dev);
	if (IS_ERR(dev)) {
		DRM_ERROR("failed to allocate drm_device\n");
		return PTR_ERR(dev);
	}

	dev->pdev = pdev;
	pci_set_drvdata(pdev, dev);

	ret = pci_enable_device(pdev);
	if (ret) {
		drm_err(dev, "failed to enable pci device: %d\n", ret);
		goto err_free;
	}

	ret = loongson_drm_load(dev, 0x0);
	if (ret) {
		drm_err(dev, "failed to load loongson: %d\n", ret);
		goto err_pdev;
	}

	ret = drm_dev_register(dev, 0);
	if (ret) {
		drm_err(dev, "failed to register drv for userspace access: %d\n",
			ret);
		goto err_pdev;
	}

	drm_fbdev_generic_setup(dev, dev->mode_config.preferred_depth);

	return 0;

err_pdev:
	pci_disable_device(pdev);
err_free:
	drm_dev_put(dev);
	return ret;
}

static void loongson_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_dev_unregister(dev);
	loongson_drm_unload(dev);
	drm_dev_put(dev);
}

static struct pci_device_id loongson_pci_devices[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_LOONGSON, PCI_DEVICE_ID_LOONGSON_DC) },
	{0,}
};

static struct pci_driver loongson_drm_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = loongson_pci_devices,
	.probe = loongson_pci_probe,
	.remove = loongson_pci_remove,
};

static int __init loongson_drm_init(void)
{
	return pci_register_driver(&loongson_drm_pci_driver);
}

static void __exit loongson_drm_exit(void)
{
	pci_unregister_driver(&loongson_drm_pci_driver);
}

module_init(loongson_drm_init);
module_exit(loongson_drm_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL v2");
