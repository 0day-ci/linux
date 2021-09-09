// SPDX-License-Identifier: GPL-2.0-only
/*
 * LG.Philips SW43101 OLED Panel driver
 * Generated with linux-mdss-dsi-panel-driver-generator
 *
 * Copyright (c) 2020 Yassine Oudjana <y.oudjana@protonmail.com>
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

static const char * const regulator_names[] = {
	"vdd",
	"avdd",
	"elvdd",
	"elvss",
};

struct sw43101_device {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[ARRAY_SIZE(regulator_names)];
	bool prepared;
};

static inline
struct sw43101_device *to_sw43101_device(struct drm_panel *panel)
{
	return container_of(panel, struct sw43101_device, panel);
}

#define dsi_dcs_write_seq(dsi, seq...) do {				\
		static const u8 d[] = { seq };				\
		int ret;						\
		ret = mipi_dsi_dcs_write_buffer(dsi, d, ARRAY_SIZE(d));	\
		if (ret < 0)						\
			return ret;					\
	} while (0)

static void sw43101_reset(struct sw43101_device *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);
}

static int sw43101_on(struct sw43101_device *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	dsi_dcs_write_seq(dsi, 0xb0, 0x5a);
	usleep_range(1000, 2000);
	dsi_dcs_write_seq(dsi, 0xb2, 0x13, 0x12, 0x40, 0xd0, 0xff, 0xff, 0x15);
	dsi_dcs_write_seq(dsi, 0xe3, 0x01);
	usleep_range(1000, 2000);
	dsi_dcs_write_seq(dsi, 0xf3, 0x03, 0x00, 0x00);

	ret = mipi_dsi_dcs_set_display_brightness(dsi, 0x0020);
	if (ret < 0) {
		dev_err(dev, "Failed to set display brightness: %d\n", ret);
		return ret;
	}

	dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20);
	dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	dsi_dcs_write_seq(dsi, 0xb0, 0x00);
	usleep_range(1000, 2000);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(120);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}
	msleep(20);

	return 0;
}

static int sw43101_off(struct sw43101_device *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}
	usleep_range(2000, 3000);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	msleep(128);

	return 0;
}

static int sw43101_prepare(struct drm_panel *panel)
{
	struct sw43101_device *ctx = to_sw43101_device(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->prepared)
		return 0;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	sw43101_reset(ctx);

	ret = sw43101_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	ctx->prepared = true;
	return 0;
}

static int sw43101_unprepare(struct drm_panel *panel)
{
	struct sw43101_device *ctx = to_sw43101_device(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = sw43101_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	ret = regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to disable regulators: %d\n", ret);
		return ret;
	}

	ctx->prepared = false;
	return 0;
}

static const struct drm_display_mode sw43101_mode = {
	.clock = (1080 + 56 + 8 + 60) * (1920 + 40 + 8 + 48) * 58 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 56,
	.hsync_end = 1080 + 56 + 8,
	.htotal = 1080 + 56 + 8 + 60,
	.vdisplay = 1920,
	.vsync_start = 1920 + 40,
	.vsync_end = 1920 + 40 + 8,
	.vtotal = 1920 + 40 + 8 + 48,
	.width_mm = 71,
	.height_mm = 126,
};

static int sw43101_get_modes(struct drm_panel *panel,
				     struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &sw43101_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs sw43101_panel_funcs = {
	.prepare = sw43101_prepare,
	.unprepare = sw43101_unprepare,
	.get_modes = sw43101_get_modes,
};

static int sw43101_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = bl->props.brightness;
	int ret;

	if (bl->props.power != FB_BLANK_UNBLANK ||
	    bl->props.fb_blank != FB_BLANK_UNBLANK ||
	    bl->props.state & (BL_CORE_SUSPENDED | BL_CORE_FBBLANK))
		brightness = 0;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static int sw43101_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = bl->props.brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness & 0xff;
}

static const struct backlight_ops sw43101_bl_ops = {
	.update_status = sw43101_bl_update_status,
	.get_brightness = sw43101_bl_get_brightness,
};

static struct backlight_device *
sw43101_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 255,
		.max_brightness = 255,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &sw43101_bl_ops, &props);
}

static int sw43101_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct sw43101_device *ctx;
	size_t i;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	for (i = 0; i < ARRAY_SIZE(ctx->supplies); ++i)
		ctx->supplies[i].supply = regulator_names[i];

	ret = devm_regulator_bulk_get(&dsi->dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0) {
		dev_err(&dsi->dev, "Failed to get regulators\n");
		return ret;
	}

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &sw43101_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ctx->panel.backlight = sw43101_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to attach to DSI host: %d\n", ret);
		return ret;
	}

	return 0;
}

static int sw43101_remove(struct mipi_dsi_device *dsi)
{
	struct sw43101_device *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id sw43101_of_match[] = {
	{ .compatible = "lgphilips,sw43101" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sw43101_of_match);

static struct mipi_dsi_driver sw43101_driver = {
	.probe = sw43101_probe,
	.remove = sw43101_remove,
	.driver = {
		.name = "panel-lgphilips-sw43101",
		.of_match_table = sw43101_of_match,
	},
};
module_mipi_dsi_driver(sw43101_driver);

MODULE_AUTHOR("Yassine Oudjana <y.oudjana@protonmail.com>");
MODULE_DESCRIPTION("LG.Philips SW43101 DSI video mode panel driver");
MODULE_LICENSE("GPL v2");
