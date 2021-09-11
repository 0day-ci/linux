// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Loongson LS7A1000 bridge chipset drm driver
 */

#include <linux/console.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_probe_helper.h>

#include "loongson_drv.h"

/* Interface history:
 * 0.1 - original.
 */
#define DRIVER_MAJOR 0
#define DRIVER_MINOR 1

static const struct drm_mode_config_funcs loongson_mode_funcs = {
	.fb_create = drm_gem_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
	.output_poll_changed = drm_fb_helper_output_poll_changed,
	.mode_valid = drm_vram_helper_mode_valid
};

static int loongson_device_init(struct drm_device *dev)
{
	struct loongson_device *ldev = to_loongson_device(dev);
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	struct pci_dev *gpu_pdev;
	resource_size_t aper_base;
	resource_size_t aper_size;
	resource_size_t mmio_base;
	resource_size_t mmio_size;
	int ret;

	/* GPU MEM */
	/* We need get 7A-gpu pci device information for ldev->gpu_pdev */
	/* dev->pdev save 7A-dc pci device information */
	gpu_pdev = pci_get_device(PCI_VENDOR_ID_LOONGSON,
				  PCI_DEVICE_ID_LOONGSON_GPU, NULL);
	ret = pci_enable_device(gpu_pdev);
	if (ret)
		return ret;
	pci_set_drvdata(gpu_pdev, dev);

	aper_base = pci_resource_start(gpu_pdev, 2);
	aper_size = pci_resource_len(gpu_pdev, 2);
	ldev->vram_start = aper_base;
	ldev->vram_size = aper_size;

	if (!devm_request_mem_region(dev->dev, ldev->vram_start,
				     ldev->vram_size, "loongson_vram")) {
		drm_err(dev, "Can't reserve VRAM\n");
		return -ENXIO;
	}

	/* DC MEM */
	mmio_base = pci_resource_start(pdev, 0);
	mmio_size = pci_resource_len(pdev, 0);
	ldev->mmio = devm_ioremap(dev->dev, mmio_base, mmio_size);
	if (!ldev->mmio) {
		drm_err(dev, "Cannot map mmio region\n");
		return -ENOMEM;
	}

	if (!devm_request_mem_region(dev->dev, mmio_base,
				     mmio_size, "loongson_mmio")) {
		drm_err(dev, "Can't reserve mmio registers\n");
		return -ENOMEM;
	}

	/* DC IO */
	ldev->io = devm_ioremap(dev->dev, LS7A_CHIPCFG_REG_BASE, 0xf);
	if (!ldev->io)
		return -ENOMEM;

	ldev->num_crtc = 2;

	drm_info(dev, "DC mmio base 0x%llx size 0x%llx io 0x%llx\n",
		 mmio_base, mmio_size, *(u64 *)ldev->io);
	drm_info(dev, "GPU vram start = 0x%x size = 0x%x\n",
		 ldev->vram_start, ldev->vram_size);

	return 0;
}

int loongson_modeset_init(struct loongson_device *ldev)
{
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	int i;
	int ret;

	for (i = 0; i < ldev->num_crtc; i++) {
		ret = loongson_crtc_init(ldev, i);
		if (ret) {
			drm_warn(&ldev->dev, "loongson crtc%d init fail\n", i);
			continue;
		}

		ret = loongson_encoder_init(ldev, i);
		if (ret) {
			drm_err(&ldev->dev, "loongson_encoder_init failed\n");
			return ret;
		}

		ret = loongson_connector_init(ldev, i);
		if (ret) {
			drm_err(&ldev->dev, "loongson_connector_init failed\n");
			return ret;
		}

		encoder = &ldev->mode_info[i].encoder->base;
		connector = &ldev->mode_info[i].connector->base;
		drm_connector_attach_encoder(connector, encoder);
	}

	return 0;
}

static int loongson_driver_init(struct drm_device *dev)
{
	struct loongson_device *ldev = to_loongson_device(dev);
	int ret;

	ret = loongson_device_init(dev);
	if (ret)
		goto err;

	ret = drmm_vram_helper_init(dev, ldev->vram_start, ldev->vram_size);
	if (ret) {
		drm_err(dev, "Error initializing vram %d\n", ret);
		goto err;
	}

	ret = drmm_mode_config_init(dev);
	if (ret)
		return ret;

	dev->mode_config.min_width = 1;
	dev->mode_config.min_height = 1;
	dev->mode_config.max_width = 4096;
	dev->mode_config.max_height = 4096;
	dev->mode_config.preferred_depth = 32;
	dev->mode_config.prefer_shadow = 1;
	dev->mode_config.fb_base = ldev->vram_start;
	dev->mode_config.funcs = (void *)&loongson_mode_funcs;
	dev->mode_config.allow_fb_modifiers = true;

	ret = loongson_modeset_init(ldev);
	if (ret) {
		drm_err(dev, "Fatal error during modeset init: %d\n", ret);
		goto err;
	}

	drm_kms_helper_poll_init(dev);
	drm_mode_config_reset(dev);

	return 0;

err:
	drm_err(dev, "failed to initialize drm driver: %d\n", ret);
	return ret;
}

static void loongson_driver_fini(struct drm_device *dev)
{
	dev->dev_private = NULL;
	dev_set_drvdata(dev->dev, NULL);
}

DEFINE_DRM_GEM_FOPS(fops);

static struct drm_driver loongson_driver = {
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
	struct loongson_device *ldev;
	struct drm_device *dev;
	int ret;

	DRM_INFO("Start loongson drm probe.\n");
	ldev = devm_drm_dev_alloc(&pdev->dev, &loongson_driver,
				  struct loongson_device, dev);
	if (IS_ERR(ldev))
		return PTR_ERR(ldev);
	dev = &ldev->dev;

	pci_set_drvdata(pdev, dev);

	ret = pcim_enable_device(pdev);
	if (ret) {
		drm_err(dev, "failed to enable pci device: %d\n", ret);
		goto err_free;
	}

	ret = loongson_driver_init(dev);
	if (ret) {
		drm_err(dev, "failed to load loongson: %d\n", ret);
		goto err_pdev;
	}

	ret = drm_dev_register(dev, 0);
	if (ret) {
		drm_err(dev, "failed to register drv for userspace access: %d\n",
			ret);
		goto driver_fini;
	}

	drm_fbdev_generic_setup(dev, dev->mode_config.preferred_depth);
	DRM_INFO("loongson fbdev enabled.\n");

	return 0;

driver_fini:
	loongson_driver_fini(dev);
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
	loongson_driver_fini(dev);
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
