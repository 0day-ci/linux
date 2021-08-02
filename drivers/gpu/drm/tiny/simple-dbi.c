// SPDX-License-Identifier: GPL-2.0+
/*
 * DRM driver for display panels with configuration preset and needs only
 * standard MIPI DCS commands to bring up.
 *
 * Copyright (C) 2021 Sipeed
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/dma-buf.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/spi/spi.h>
#include <video/mipi_display.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_mipi_dbi.h>

#define MIPI_DCS_ADDRESS_MODE_BGR		BIT(3)
#define MIPI_DCS_ADDRESS_MODE_REVERSE		BIT(5)
#define MIPI_DCS_ADDRESS_MODE_RTL		BIT(6)
#define MIPI_DCS_ADDRESS_MODE_BTT		BIT(7)

struct simple_dbi_cfg {
	const struct drm_display_mode mode;
	unsigned int left_offset;
	unsigned int top_offset;
	bool inverted;
	bool write_only;
	bool bgr;
	bool right_to_left;
	bool bottom_to_top;
};

struct simple_dbi_priv {
	struct mipi_dbi_dev dbidev;	/* Must be first for .release() */
	const struct simple_dbi_cfg *cfg;
};

static void simple_dbi_pipe_enable(struct drm_simple_display_pipe *pipe,
				struct drm_crtc_state *crtc_state,
				struct drm_plane_state *plane_state)
{
	struct mipi_dbi_dev *dbidev = drm_to_mipi_dbi_dev(pipe->crtc.dev);
	struct simple_dbi_priv *priv = container_of(dbidev,
						    struct simple_dbi_priv,
						    dbidev);
	struct mipi_dbi *dbi = &dbidev->dbi;
	int ret, idx;
	u8 addr_mode = 0x00;

	if (!drm_dev_enter(pipe->crtc.dev, &idx))
		return;

	DRM_DEBUG_KMS("\n");

	ret = mipi_dbi_poweron_reset(dbidev);
	if (ret)
		goto out_exit;

	mipi_dbi_command(dbi, MIPI_DCS_EXIT_SLEEP_MODE);
	msleep(5);

	/* Currently tinydrm supports 16bit only now */
	mipi_dbi_command(dbi, MIPI_DCS_SET_PIXEL_FORMAT,
			 MIPI_DCS_PIXEL_FMT_16BIT);

	if (priv->cfg->inverted)
		mipi_dbi_command(dbi, MIPI_DCS_ENTER_INVERT_MODE);
	else
		mipi_dbi_command(dbi, MIPI_DCS_EXIT_INVERT_MODE);

	if (priv->cfg->bgr)
		addr_mode |= MIPI_DCS_ADDRESS_MODE_BGR;

	if (priv->cfg->right_to_left)
		addr_mode |= MIPI_DCS_ADDRESS_MODE_RTL;

	if (priv->cfg->bottom_to_top)
		addr_mode |= MIPI_DCS_ADDRESS_MODE_BTT;

	switch (dbidev->rotation) {
	default:
		break;
	case 90:
		addr_mode ^= MIPI_DCS_ADDRESS_MODE_REVERSE;
		addr_mode ^= MIPI_DCS_ADDRESS_MODE_RTL;
		break;
	case 180:
		addr_mode ^= MIPI_DCS_ADDRESS_MODE_RTL;
		addr_mode ^= MIPI_DCS_ADDRESS_MODE_BTT;
		break;
	case 270:
		addr_mode ^= MIPI_DCS_ADDRESS_MODE_REVERSE;
		addr_mode ^= MIPI_DCS_ADDRESS_MODE_BTT;
		break;
	}

	mipi_dbi_command(dbi, MIPI_DCS_SET_ADDRESS_MODE, addr_mode);

	mipi_dbi_command(dbi, MIPI_DCS_ENTER_NORMAL_MODE);

	mipi_dbi_command(dbi, MIPI_DCS_SET_DISPLAY_ON);

	mipi_dbi_enable_flush(dbidev, crtc_state, plane_state);
out_exit:
	drm_dev_exit(idx);
}

static const struct drm_simple_display_pipe_funcs simple_dbi_pipe_funcs = {
	.enable		= simple_dbi_pipe_enable,
	.disable	= mipi_dbi_pipe_disable,
	.update		= mipi_dbi_pipe_update,
	.prepare_fb	= drm_gem_simple_display_pipe_prepare_fb,
};

static const struct simple_dbi_cfg zsx154_b1206_cfg = {
	.mode		= { DRM_SIMPLE_MODE(240, 240, 28, 28) },
	.inverted	= true,
	.write_only	= true,
};

DEFINE_DRM_GEM_CMA_FOPS(simple_dbi_fops);

static const struct drm_driver simple_dbi_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops			= &simple_dbi_fops,
	DRM_GEM_CMA_DRIVER_OPS_VMAP,
	.debugfs_init		= mipi_dbi_debugfs_init,
	.name			= "simple-dbi",
	.desc			= "Generic MIPI-DCS compatible DBI panel",
	.date			= "20210723",
	.major			= 1,
	.minor			= 0,
};

static const struct of_device_id simple_dbi_of_match[] = {
	{ .compatible = "zsx,zsx154-b1206", .data = &zsx154_b1206_cfg },
	{ },
}
MODULE_DEVICE_TABLE(of, simple_dbi_of_match);

static int simple_dbi_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	const struct simple_dbi_cfg *cfg;
	struct mipi_dbi_dev *dbidev;
	struct simple_dbi_priv *priv;
	struct drm_device *drm;
	struct mipi_dbi *dbi;
	struct gpio_desc *dc;
	u32 rotation = 0;
	int ret;

	cfg = device_get_match_data(&spi->dev);
	if (!cfg)
		cfg = (void *)spi_get_device_id(spi)->driver_data;

	priv = devm_drm_dev_alloc(dev, &simple_dbi_driver,
				  struct simple_dbi_priv, dbidev.drm);
	if (IS_ERR(priv))
		return PTR_ERR(priv);

	dbidev = &priv->dbidev;
	priv->cfg = cfg;

	dbi = &dbidev->dbi;
	drm = &dbidev->drm;

	dbi->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(dbi->reset))
		return dev_err_probe(dev, PTR_ERR(dbi->reset), "Failed to get reset GPIO\n");

	dc = devm_gpiod_get_optional(dev, "dc", GPIOD_OUT_LOW);
	if (IS_ERR(dc))
		return dev_err_probe(dev, PTR_ERR(dc), "Failed to get D/C GPIO\n");

	dbidev->backlight = devm_of_find_backlight(dev);
	if (IS_ERR(dbidev->backlight))
		return PTR_ERR(dbidev->backlight);

	device_property_read_u32(dev, "rotation", &rotation);

	ret = mipi_dbi_spi_init(spi, dbi, dc);
	if (ret)
		return ret;

	if (cfg->write_only)
		dbi->read_commands = NULL;

	ret = mipi_dbi_dev_init(dbidev, &simple_dbi_pipe_funcs, &cfg->mode,
				rotation);
	if (ret)
		return ret;

	drm_mode_config_reset(drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		return ret;

	spi_set_drvdata(spi, drm);

	drm_fbdev_generic_setup(drm, 0);

	return 0;
}

static int simple_dbi_remove(struct spi_device *spi)
{
	struct drm_device *drm = spi_get_drvdata(spi);

	drm_dev_unplug(drm);
	drm_atomic_helper_shutdown(drm);

	return 0;
}

static void simple_dbi_shutdown(struct spi_device *spi)
{
	drm_atomic_helper_shutdown(spi_get_drvdata(spi));
}

static struct spi_driver simple_dbi_spi_driver = {
	.driver = {
		.name = "simple-dbi",
		.of_match_table = simple_dbi_of_match,
	},
	.probe = simple_dbi_probe,
	.remove = simple_dbi_remove,
	.shutdown = simple_dbi_shutdown,
};
module_spi_driver(simple_dbi_spi_driver);

MODULE_DESCRIPTION("Simple DBI panel DRM driver");
MODULE_AUTHOR("Icenowy Zheng <icenowy@aosc.io>");
MODULE_LICENSE("GPL");
