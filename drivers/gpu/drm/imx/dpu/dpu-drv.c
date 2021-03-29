// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright 2019,2020 NXP
 */

#include <linux/component.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "dpu-drv.h"
#include "dpu-kms.h"

#define DRIVER_NAME	"imx-dpu-drm"

static int legacyfb_depth = 32;
module_param(legacyfb_depth, uint, 0444);

struct dpu_drm_drv_data {
	struct list_head crtc_np_list;
};

DEFINE_DRM_GEM_CMA_FOPS(dpu_drm_driver_fops);

static struct drm_driver dpu_drm_driver = {
	.driver_features		= DRIVER_MODESET | DRIVER_GEM |
					  DRIVER_ATOMIC,
	DRM_GEM_CMA_DRIVER_OPS,
	.fops				= &dpu_drm_driver_fops,
	.name				= "imx-dpu",
	.desc				= "i.MX DPU DRM graphics",
	.date				= "20200805",
	.major				= 1,
	.minor				= 0,
	.patchlevel			= 0,
};

static int dpu_drm_bind(struct device *dev)
{
	struct dpu_drm_device *dpu_drm;
	struct drm_device *drm;
	struct dpu_drm_drv_data *drv_data = dev_get_drvdata(dev);
	int ret;

	dpu_drm = devm_drm_dev_alloc(dev, &dpu_drm_driver,
				     struct dpu_drm_device, base);
	if (IS_ERR(dpu_drm)) {
		ret = PTR_ERR(dpu_drm);
		DRM_DEV_ERROR(dev, "failed to alloc drm device: %d\n", ret);
		return ret;
	}

	drm = &dpu_drm->base;

	drm->irq_enabled = true;

	ret = dpu_kms_prepare(dpu_drm, &drv_data->crtc_np_list);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			DRM_DEV_ERROR(dev, "failed to prepare kms: %d\n", ret);
		return ret;
	}

	ret = component_bind_all(dev, dpu_drm);
	if (ret) {
		if (ret != -EPROBE_DEFER)
			DRM_DEV_ERROR(dev,
				      "failed to bind all components: %d\n",
									ret);
		return ret;
	}

	drm_mode_config_reset(drm);

	drm_kms_helper_poll_init(drm);

	ret = drm_dev_register(drm, 0);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to register drm device: %d\n", ret);
		goto out_register;
	}

	if (legacyfb_depth != 16 && legacyfb_depth != 32) {
		DRM_DEV_INFO(dev,
			     "Invalid legacyfb_depth.  Defaulting to 32bpp\n");
		legacyfb_depth = 32;
	}

	drm_fbdev_generic_setup(drm, legacyfb_depth);

	return ret;

out_register:
	drm_kms_helper_poll_fini(drm);
	component_unbind_all(dev, NULL);

	return ret;
}

static void dpu_drm_unbind(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	drm_dev_unregister(drm);

	drm_kms_helper_poll_fini(drm);

	drm_atomic_helper_shutdown(drm);

	component_unbind_all(drm->dev, NULL);
}

static const struct component_master_ops dpu_drm_ops = {
	.bind = dpu_drm_bind,
	.unbind = dpu_drm_unbind,
};

static int compare_of(struct device *dev, void *data)
{
	struct device_node *np = data;

	return dev->of_node == np;
}

static int dpu_drm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct component_match *match = NULL;
	struct device_node *np, *ports, *port;
	struct dpu_drm_drv_data *drv_data;
	struct dpu_crtc_of_node *crtc_of_node;

	drv_data = devm_kzalloc(dev, sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data) {
		DRM_DEV_ERROR(dev, "failed to alloc driver data\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&drv_data->crtc_np_list);

	for_each_matching_node(np, dpu_dt_ids) {
		if (!of_device_is_available(np))
			continue;

		ports = of_get_child_by_name(np, "ports");
		if (!ports)
			ports = np;

		for_each_child_of_node(ports, port) {
			drm_of_component_match_add(dev, &match, compare_of,
								port);

			crtc_of_node = devm_kzalloc(dev, sizeof(*crtc_of_node),
								GFP_KERNEL);
			if (!crtc_of_node) {
				DRM_DEV_ERROR(dev,
					      "failed to alloc crtc_of_node\n");
				of_node_put(ports);
				return -ENOMEM;
			}

			crtc_of_node->np = port;

			list_add(&crtc_of_node->list, &drv_data->crtc_np_list);
		}

		of_node_put(ports);
	}

	if (!match) {
		DRM_DEV_ERROR(dev, "no available DPU display output port\n");
		return -ENODEV;
	}

	dev_set_drvdata(dev, drv_data);

	return component_master_add_with_match(dev, &dpu_drm_ops, match);
}

static int dpu_drm_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &dpu_drm_ops);

	return 0;
}

static int __maybe_unused dpu_drm_suspend(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(drm_dev);
}

static int __maybe_unused dpu_drm_resume(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	return drm_mode_config_helper_resume(drm_dev);
}

static SIMPLE_DEV_PM_OPS(dpu_drm_pm_ops, dpu_drm_suspend, dpu_drm_resume);

static struct platform_driver dpu_drm_platform_driver = {
	.probe = dpu_drm_probe,
	.remove = dpu_drm_remove,
	.driver = {
		.name = DRIVER_NAME,
		.pm = &dpu_drm_pm_ops,
	},
};

static struct platform_device *dpu_drm_platform_dev;

static struct platform_driver * const drivers[] = {
	&dpu_prg_driver,
	&dpu_dprc_driver,
	&dpu_core_driver,
	&dpu_crtc_driver,
	&dpu_drm_platform_driver,
};

static int __init dpu_init(void)
{
	struct platform_device *pdev;
	struct device_node *np;
	int ret;

	ret = platform_register_drivers(drivers, ARRAY_SIZE(drivers));
	if (ret)
		return ret;

	/*
	 * If the DT contains at least one available DPU device, instantiate
	 * the DRM platform device.
	 */
	for_each_matching_node(np, dpu_dt_ids) {
		if (!of_device_is_available(np))
			continue;

		pdev = platform_device_alloc(DRIVER_NAME, -1);
		if (!pdev) {
			ret = -ENOMEM;
			goto unregister_drivers;
		}

		ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
		if (ret)
			goto unregister_drivers;

		ret = platform_device_add(pdev);
		if (ret) {
			platform_device_put(pdev);
			goto unregister_drivers;
		}

		dpu_drm_platform_dev = pdev;
		of_node_put(np);
		break;
	}

	return ret;

unregister_drivers:
	of_node_put(np);
	platform_unregister_drivers(drivers, ARRAY_SIZE(drivers));
	return ret;
}
module_init(dpu_init);

static void __exit dpu_exit(void)
{
	platform_device_unregister(dpu_drm_platform_dev);
	platform_unregister_drivers(drivers, ARRAY_SIZE(drivers));
}
module_exit(dpu_exit);

MODULE_DESCRIPTION("i.MX DPU DRM Driver");
MODULE_AUTHOR("Liu Ying <victor.liu@nxp.com>");
MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_LICENSE("GPL v2");
