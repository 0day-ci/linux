// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/of_irq.h>
#include "edp_v510.h"

static irqreturn_t edp_irq(int irq, void *dev_id)
{
	struct msm_edp_v510 *edp = dev_id;

	/* Process eDP irq */
	return msm_edp_v510_ctrl_irq(edp->ctrl);
}

static void edp_destroy(struct platform_device *pdev)
{
	struct msm_edp_v510 *edp = platform_get_drvdata(pdev);

	if (!edp)
		return;

	if (edp->ctrl) {
		msm_edp_v510_ctrl_destroy(edp->ctrl);
		edp->ctrl = NULL;
	}

	platform_set_drvdata(pdev, NULL);
}

/* construct eDP at bind/probe time, grab all the resources. */
static struct msm_edp *edp_init(struct platform_device *pdev)
{
	struct msm_edp_v510 *edp = NULL;
	int ret;

	if (!pdev) {
		DRM_ERROR("no eDP device\n");
		ret = -ENXIO;
		goto fail;
	}

	edp = devm_kzalloc(&pdev->dev, sizeof(*edp), GFP_KERNEL);
	if (!edp) {
		ret = -ENOMEM;
		goto fail;
	}
	DRM_INFO("eDP v510 probed\n");
	edp->base.version = MSM_EDP_VERSION_510;

	edp->base.pdev = pdev;
	platform_set_drvdata(pdev, edp);

	ret = msm_edp_v510_ctrl_init(edp);
	if (ret)
		goto fail;

	return &edp->base;

fail:
	if (edp)
		edp_destroy(pdev);

	return ERR_PTR(ret);
}

static int edp_bind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm = dev_get_drvdata(master);
	struct msm_drm_private *priv = drm->dev_private;
	struct msm_edp *edp;

	edp = edp_init(to_platform_device(dev));
	if (IS_ERR(edp))
		return PTR_ERR(edp);
	priv->edp = edp;

	return 0;
}

static void edp_unbind(struct device *dev, struct device *master, void *data)
{
	struct drm_device *drm = dev_get_drvdata(master);
	struct msm_drm_private *priv = drm->dev_private;

	if (priv->edp) {
		edp_destroy(to_platform_device(dev));
		priv->edp = NULL;
	}
}

static const struct component_ops edp_ops = {
		.bind   = edp_bind,
		.unbind = edp_unbind,
};

static int edp_dev_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &edp_ops);
}

static int edp_dev_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &edp_ops);
	return 0;
}

static const struct of_device_id dt_match[] = {
	{ .compatible = "qcom,mdss-edp-v510" },
	{}
};

static struct platform_driver edp_driver = {
	.probe = edp_dev_probe,
	.remove = edp_dev_remove,
	.driver = {
		.name = "msm_edp_v510",
		.of_match_table = dt_match,
	},
};

void __init msm_edp_v510_register(void)
{
	platform_driver_register(&edp_driver);
}

void __exit msm_edp_v510_unregister(void)
{
	platform_driver_unregister(&edp_driver);
}

static void edp_display_set_encoder_mode(struct msm_edp_v510 *edp)
{
	struct msm_drm_private *priv = edp->base.dev->dev_private;
	struct msm_kms *kms = priv->kms;

	if (!edp->encoder_mode_set && edp->base.encoder &&
				kms->funcs->set_encoder_mode) {
		kms->funcs->set_encoder_mode(kms,
				edp->base.encoder, true);

		edp->encoder_mode_set = true;
	}
}

/* Second part of initialization, the drm/kms level modeset_init */
int msm_edp_v510_modeset_init(struct msm_edp *edp, struct drm_device *dev,
				struct drm_encoder *encoder)
{
	struct platform_device *pdev = edp->pdev;
	struct msm_drm_private *priv = dev->dev_private;
	struct msm_edp_v510 *edp_ptr = NULL;
	int ret;

	if (WARN_ON(!encoder) || WARN_ON(!edp) || WARN_ON(!dev))
		return -EINVAL;

	edp->encoder = encoder;
	edp->dev = dev;

	edp_ptr = container_of(edp, struct msm_edp_v510, base);
	if (IS_ERR(edp_ptr)) {
		ret = PTR_ERR(edp_ptr);
		DRM_DEV_ERROR(dev->dev, "failed to retrieve edp_v510 ptr: %d\n", ret);
		goto fail;
	}

	edp_ptr->base.bridge = msm_edp_v510_bridge_init(edp_ptr);
	if (IS_ERR(edp_ptr->base.bridge)) {
		ret = PTR_ERR(edp_ptr->base.bridge);
		DRM_DEV_ERROR(dev->dev, "failed to create eDP bridge: %d\n", ret);
		edp_ptr->base.bridge = NULL;
		goto fail;
	}

	edp_ptr->base.connector = msm_edp_v510_connector_init(edp_ptr);
	if (IS_ERR(edp_ptr->base.connector)) {
		ret = PTR_ERR(edp_ptr->base.connector);
		DRM_DEV_ERROR(dev->dev, "failed to create eDP connector: %d\n", ret);
		edp_ptr->base.connector = NULL;
		goto fail;
	}

	edp_ptr->irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (edp_ptr->irq < 0) {
		ret = edp_ptr->irq;
		DRM_DEV_ERROR(dev->dev, "failed to get IRQ: %d\n", ret);
		goto fail;
	}

	ret = devm_request_irq(&pdev->dev, edp_ptr->irq,
			edp_irq, IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
			"edp_isr", edp_ptr);
	if (ret < 0) {
		DRM_DEV_ERROR(dev->dev, "failed to request IRQ%u: %d\n",
				edp_ptr->irq, ret);
		goto fail;
	}

	edp_display_set_encoder_mode(edp_ptr);

	priv->bridges[priv->num_bridges++]       = edp->bridge;
	priv->connectors[priv->num_connectors++] = edp->connector;

	return 0;

fail:
	/* bridge/connector are normally destroyed by drm */
	if (edp->bridge) {
		edp_v510_bridge_destroy(edp->bridge);
		edp->bridge = NULL;
	}
	if (edp->connector) {
		edp->connector->funcs->destroy(edp->connector);
		edp->connector = NULL;
	}

	return ret;
}
