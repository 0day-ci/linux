// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Keem Bay camera driver.
 *
 * Copyright (C) 2021 Intel Corporation
 */
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>

#include <media/v4l2-fwnode.h>

#include "keembay-camera.h"

#define KMB_CAM_NUM_PORTS 6

/* RX-CTRL to data lanes mapping
 * 2-lanes
 * RX-CTRL#0 - {1, 2}
 * RX-CTRL#1 - {4, 5}
 * RX-CTRL#2 - {7, 8}
 * RX-CTRL#3 - {10, 11}
 * RX-CTRL#4 - {13, 14}
 * RX-CTRL#5 - {16, 17}
 * 4-lanes
 * RX-CTRL#0 - {1, 2, 4, 5}
 * RX-CTRL#2 - {7, 8, 10, 11}
 * RX-CTRL#4 - {13, 14, 16, 17}
 */
static const u8 rx_ctrl[KMB_CAM_NUM_PORTS][2][4] = {
	{ { 1, 2 }, { 1, 2, 4, 5 } },
	{ { 4, 5 }, {} },
	{ { 7, 8 }, { 7, 8, 10, 11 } },
	{ { 10, 11 }, {} },
	{ { 13, 14 }, { 13, 14, 16, 17 } },
	{ { 16, 17 }, {} }
};

static int get_rx_id(const u8 data_lanes[],
		     const unsigned short num_data_lanes)
{
	unsigned int i, j;

	for (i = 0; i < KMB_CAM_NUM_PORTS; i++) {
		for (j = 0; j < ARRAY_SIZE(rx_ctrl[i]); j++) {
			if (!memcmp(data_lanes, rx_ctrl[i][j],
				    num_data_lanes * sizeof(u8)))
				return i;
		}
	}

	return -EINVAL;
}

static int kmb_cam_bound(struct v4l2_async_notifier *n,
			 struct v4l2_subdev *sd,
			 struct v4l2_async_subdev *asd)
{
	struct v4l2_device *v4l2_dev = n->v4l2_dev;
	struct kmb_camera *kmb_cam =
		container_of(v4l2_dev, struct kmb_camera, v4l2_dev);
	struct kmb_camera_receiver *receiver =
		container_of(asd, struct kmb_camera_receiver, asd);
	int ret;

	ret = kmb_isp_init(&receiver->isp, kmb_cam->dev,
			   &receiver->csi2_config, &kmb_cam->xlink_cam);
	if (ret < 0)
		return ret;

	ret = kmb_isp_register_entities(&receiver->isp, &kmb_cam->v4l2_dev);
	if (ret < 0)
		goto error_isp_cleanup;

	ret = media_create_pad_link(&sd->entity, 0,
				    &receiver->isp.subdev.entity,
				    KMB_ISP_SINK_PAD_SENSOR,
				    MEDIA_LNK_FL_IMMUTABLE |
				    MEDIA_LNK_FL_ENABLED);
	if (ret < 0) {
		dev_err(kmb_cam->dev, "Fail to link %s->%s entities",
			sd->entity.name, receiver->isp.subdev.entity.name);
		goto error_unregister_entities;
	}

	return 0;

error_unregister_entities:
	kmb_isp_unregister_entities(&receiver->isp);
error_isp_cleanup:
	kmb_isp_cleanup(&receiver->isp);

	return ret;
}

static int kmb_cam_complete(struct v4l2_async_notifier *n)
{
	return v4l2_device_register_subdev_nodes(n->v4l2_dev);
}

static void kmb_cam_unbind(struct v4l2_async_notifier *n,
			   struct v4l2_subdev *sd,
			   struct v4l2_async_subdev *asd)
{
	struct kmb_camera_receiver *receiver =
		container_of(asd, struct kmb_camera_receiver, asd);

	kmb_isp_unregister_entities(&receiver->isp);
	kmb_isp_cleanup(&receiver->isp);
}

static const struct v4l2_async_notifier_operations notifier_ops = {
	.bound = kmb_cam_bound,
	.complete = kmb_cam_complete,
	.unbind = kmb_cam_unbind
};

static int kmb_cam_parse_nodes(struct kmb_camera *kmb_cam,
			       struct v4l2_async_notifier *n)
{
	struct fwnode_handle *fwnode = NULL;
	unsigned int i;
	int ret;

	for (i = 0; i < KMB_CAM_NUM_PORTS; i++) {
		struct v4l2_fwnode_endpoint ep_data = {
			.bus_type = V4L2_MBUS_CSI2_DPHY,
		};
		struct kmb_camera_receiver *receiver;
		int rx_id;

		fwnode = fwnode_graph_get_endpoint_by_id(dev_fwnode(kmb_cam->dev),
							 i, 0,
							 FWNODE_GRAPH_ENDPOINT_NEXT);
		if (!fwnode)
			continue;

		ret = v4l2_fwnode_endpoint_parse(fwnode, &ep_data);
		if (ret < 0) {
			dev_err(kmb_cam->dev,
				"No endpoint to parse in this fwnode");
			goto error_fwnode_handle_put;
		}

		rx_id = get_rx_id(ep_data.bus.mipi_csi2.data_lanes,
				  ep_data.bus.mipi_csi2.num_data_lanes);
		if (rx_id < 0) {
			dev_err(kmb_cam->dev, "Invalid RX ID");
			ret = rx_id;
			goto error_fwnode_handle_put;
		}

		receiver =
			v4l2_async_notifier_add_fwnode_remote_subdev(&kmb_cam->v4l2_notifier,
								     fwnode,
								     struct kmb_camera_receiver);
		if (IS_ERR(receiver)) {
			ret = PTR_ERR(receiver);
			goto error_fwnode_handle_put;
		}

		receiver->csi2_config.rx_id = rx_id;
		receiver->csi2_config.num_lanes =
			ep_data.bus.mipi_csi2.num_data_lanes;

		fwnode_handle_put(fwnode);
	}

	return 0;

error_fwnode_handle_put:
	fwnode_handle_put(fwnode);

	return ret;
}

static int kmb_cam_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct v4l2_device *v4l2_dev;
	struct kmb_camera *kmb_cam;
	int ret;

	kmb_cam = devm_kzalloc(dev, sizeof(*kmb_cam), GFP_KERNEL);
	if (!kmb_cam)
		return -ENOMEM;

	kmb_cam->dev = dev;

	platform_set_drvdata(pdev, kmb_cam);

	ret = kmb_cam_xlink_init(&kmb_cam->xlink_cam, dev);
	if (ret < 0)
		return ret;

	strscpy(kmb_cam->media_dev.model, "Keem Bay camera",
		sizeof(kmb_cam->media_dev.model));
	kmb_cam->media_dev.dev = dev;
	kmb_cam->media_dev.hw_revision = 0;
	media_device_init(&kmb_cam->media_dev);

	v4l2_dev = &kmb_cam->v4l2_dev;
	v4l2_dev->mdev = &kmb_cam->media_dev;
	strscpy(v4l2_dev->name, "keembay-camera", sizeof(v4l2_dev->name));

	ret = v4l2_device_register(dev, &kmb_cam->v4l2_dev);
	if (ret < 0) {
		dev_err(kmb_cam->dev, "Fail to register v4l2_device: %d", ret);
		goto error_xlink_cleanup;
	}

	ret = of_reserved_mem_device_init(dev);
	if (ret)
		dev_info(dev, "Default CMA memory region will be used!");

	v4l2_async_notifier_init(&kmb_cam->v4l2_notifier);
	ret = kmb_cam_parse_nodes(kmb_cam, &kmb_cam->v4l2_notifier);
	if (ret < 0) {
		dev_err(kmb_cam->dev, "Fail to parse nodes: %d", ret);
		goto error_async_notifier_cleanup;
	}

	kmb_cam->v4l2_notifier.ops = &notifier_ops;
	ret = v4l2_async_notifier_register(&kmb_cam->v4l2_dev,
					   &kmb_cam->v4l2_notifier);
	if (ret < 0) {
		dev_err(kmb_cam->dev, "Could not register notifier! %d", ret);
		goto error_async_notifier_cleanup;
	}

	ret = media_device_register(&kmb_cam->media_dev);
	if (ret < 0) {
		dev_err(kmb_cam->dev, "Fail to register media device %d", ret);
		goto error_async_notifier_unregister;
	}

	return 0;

error_async_notifier_unregister:
	v4l2_async_notifier_unregister(&kmb_cam->v4l2_notifier);
error_async_notifier_cleanup:
	v4l2_async_notifier_cleanup(&kmb_cam->v4l2_notifier);
	v4l2_device_unregister(&kmb_cam->v4l2_dev);
error_xlink_cleanup:
	kmb_cam_xlink_cleanup(&kmb_cam->xlink_cam);

	return ret;
}

static int kmb_cam_remove(struct platform_device *pdev)
{
	struct kmb_camera *kmb_cam = platform_get_drvdata(pdev);

	v4l2_async_notifier_unregister(&kmb_cam->v4l2_notifier);
	v4l2_async_notifier_cleanup(&kmb_cam->v4l2_notifier);

	media_device_unregister(&kmb_cam->media_dev);
	media_device_cleanup(&kmb_cam->media_dev);
	v4l2_device_unregister(&kmb_cam->v4l2_dev);

	kmb_cam_xlink_cleanup(&kmb_cam->xlink_cam);

	return 0;
}

static const struct of_device_id kmb_cam_dt_match[] = {
	{.compatible = "intel,keembay-camera"},
	{}
};
MODULE_DEVICE_TABLE(of, kmb_cam_dt_match);

static struct platform_driver keembay_camera_driver = {
	.probe	= kmb_cam_probe,
	.remove = kmb_cam_remove,
	.driver = {
		.name = "keembay-camera",
		.owner = THIS_MODULE,
		.of_match_table = kmb_cam_dt_match,
	}
};

module_platform_driver(keembay_camera_driver);

MODULE_DESCRIPTION("Intel Keem Bay camera");
MODULE_LICENSE("GPL");
