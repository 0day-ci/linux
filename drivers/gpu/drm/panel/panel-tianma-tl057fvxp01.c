// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 Julian Braha <julianbraha@gmail.com>
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>

#include <video/mipi_display.h>

#include "panel-mipi-dsi-common.h"

static void tianma_tl057fvxp01_reset(struct gpio_desc *reset_gpio)
{
	gpiod_set_value_cansleep(reset_gpio, 0);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int tianma_tl057fvxp01_on(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	int ret;

	dsi_dcs_write_seq(dsi, 0x00, 0x00);
	dsi_dcs_write_seq(dsi, 0xff, 0x19, 0x11, 0x01);
	dsi_dcs_write_seq(dsi, 0x00, 0x80);
	dsi_dcs_write_seq(dsi, 0xff, 0x19, 0x11);
	dsi_dcs_write_seq(dsi, 0x00, 0xb0);
	dsi_dcs_write_seq(dsi, 0xb3, 0x04, 0x38, 0x08, 0x70);
	dsi_dcs_write_seq(dsi, 0x00, 0x00);
	dsi_dcs_write_seq(dsi, 0xff, 0xff, 0xff, 0xff);
	dsi_dcs_write_seq(dsi, 0x35, 0x00);
	dsi_dcs_write_seq(dsi, 0x51, 0xcc, 0x08);
	dsi_dcs_write_seq(dsi, 0x53, 0x2c);
	dsi_dcs_write_seq(dsi, 0x55, 0x01);

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
	usleep_range(10000, 11000);

	return 0;
}

static const struct panel_mipi_dsi_info tianma_tl057fvxp01_info = {
	.mode = {
		.clock = (1080 + 53 + 4 + 53) * (2160 + 14 + 1 + 11) * 60 / 1000,
		.hdisplay = 1080,
		.hsync_start = 1080 + 53,
		.hsync_end = 1080 + 53 + 4,
		.htotal = 1080 + 53 + 4 + 53,
		.vdisplay = 2160,
		.vsync_start = 2160 + 14,
		.vsync_end = 2160 + 14 + 1,
		.vtotal = 2160 + 14 + 1 + 11,
		.width_mm = 62,
		.height_mm = 110,
	},

	.reset = tianma_tl057fvxp01_reset,
	.power_on = tianma_tl057fvxp01_on,

	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO
		    | MIPI_DSI_MODE_VIDEO_BURST
		    | MIPI_DSI_CLOCK_NON_CONTINUOUS
		    | MIPI_DSI_MODE_LPM
};

MIPI_DSI_PANEL_DRIVER(tianma_tl057fvxp01, "tianma-tl057fvxp01", "tianma,tl057fvxp01");

MODULE_AUTHOR("Julian Braha <julianbraha@gmail.com>");
MODULE_DESCRIPTION("Tianma TL057FVXP01 panel driver");
MODULE_LICENSE("GPL v2");
