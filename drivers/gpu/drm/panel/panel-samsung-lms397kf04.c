// SPDX-License-Identifier: GPL-2.0
/*
 * Panel driver for the Samsung LMS397KF04 480x800 DPI RGB panel.
 * According to the data sheet the display controller is called DB7430
 * Linus Walleij <linus.walleij@linaro.org>
 */
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/media-bus-format.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include <video/mipi_display.h>

#define LMS397_MANUFACTURER_CMD		0xb0
#define LMS397_UNKNOWN_B4		0xb4
#define LMS397_USER_SELECT		0xb5
#define LMS397_UNKNOWN_B7		0xb7
#define LMS397_UNKNOWN_B8		0xb8
#define LMS397_PANEL_DRIVING		0xc0
#define LMS397_SOURCE_CONTROL		0xc1
#define LMS397_GATE_INTERFACE		0xc4
#define LMS397_DISPLAY_H_TIMING		0xc5
#define LMS397_RGB_SYNC_OPTION		0xc6
#define LMS397_GAMMA_SET_RED		0xc8
#define LMS397_GAMMA_SET_GREEN		0xc9
#define LMS397_GAMMA_SET_BLUE		0xca
#define LMS397_BIAS_CURRENT_CTRL	0xd1
#define LMS397_DDV_CTRL			0xd2
#define LMS397_GAMMA_CTRL_REF		0xd3
#define LMS397_UNKNOWN_D4		0xd4
#define LMS397_DCDC_CTRL		0xd5
#define LMS397_VCL_CTRL			0xd6
#define LMS397_UNKNOWN_F8		0xf8
#define LMS397_UNKNOWN_FC		0xfc

#define DATA_MASK	0x100

/**
 * struct lms397kf04 - state container for the LMS397kf04 panel
 */
struct lms397kf04 {
	/**
	 * @dev: the container device
	 */
	struct device *dev;
	/**
	 * @spi: the corresponding SPI device
	 */
	struct spi_device *spi;
	/**
	 * @panel: the DRM panel instance for this device
	 */
	struct drm_panel panel;
	/**
	 * @width: the width of this panel in mm
	 */
	u32 width;
	/**
	 * @height: the height of this panel in mm
	 */
	u32 height;
	/**
	 * @reset: reset GPIO line
	 */
	struct gpio_desc *reset;
	/**
	 * @regulators: VCCIO and VIO supply regulators
	 */
	struct regulator_bulk_data regulators[2];
};

static const struct drm_display_mode lms397kf04_mode = {
	/*
	 * 31 ns period min (htotal*vtotal*vrefresh)/1000
	 * gives a Vrefresh of ~71 Hz.
	 */
	.clock = 32258,
	.hdisplay = 480,
	.hsync_start = 480 + 10,
	.hsync_end = 480 + 10 + 4,
	.htotal = 480 + 10 + 4 + 40,
	.vdisplay = 800,
	.vsync_start = 800 + 6,
	.vsync_end = 800 + 6 + 1,
	.vtotal = 800 + 6 + 1 + 7,
	.width_mm = 53,
	.height_mm = 87,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static inline struct lms397kf04 *to_lms397kf04(struct drm_panel *panel)
{
	return container_of(panel, struct lms397kf04, panel);
}

static int lms397kf04_write_word(struct lms397kf04 *lms, u16 data)
{
	/* SPI buffers are always in CPU order */
	return spi_write(lms->spi, &data, 2);
}

static int lms397kf04_dcs_write(struct lms397kf04 *lms, const u8 *data, size_t len)
{
	int ret;

	dev_dbg(lms->dev, "SPI writing dcs seq: %*ph\n", (int)len, data);

	/*
	 * This sends 9 bits with the first bit (bit 8) set to 0
	 * This indicates that this is a command. Anything after the
	 * command is data.
	 */
	ret = lms397kf04_write_word(lms, *data);

	while (!ret && --len) {
		++data;
		/* This sends 9 bits with the first bit (bit 8) set to 1 */
		ret = lms397kf04_write_word(lms, *data | DATA_MASK);
	}

	if (ret) {
		dev_err(lms->dev, "SPI error %d writing dcs seq: %*ph\n", ret,
			(int)len, data);
	}

	return ret;
}

#define lms397kf04_dcs_write_seq_static(ctx, seq ...) \
	({ \
		static const u8 d[] = { seq }; \
		lms397kf04_dcs_write(ctx, d, ARRAY_SIZE(d)); \
	})

static int lms397kf04_power_on(struct lms397kf04 *lms)
{
	int ret;

	/* Power up */
	ret = regulator_bulk_enable(ARRAY_SIZE(lms->regulators),
				    lms->regulators);
	if (ret) {
		dev_err(lms->dev, "failed to enable regulators: %d\n", ret);
		return ret;
	}
	msleep(50);

	/* Assert reset >=1 ms */
	gpiod_set_value_cansleep(lms->reset, 1);
	msleep(1);
	/* De-assert reset */
	gpiod_set_value_cansleep(lms->reset, 0);
	/* Wait >= 10 ms */
	msleep(10);
	dev_info(lms->dev, "de-asserted RESET\n");

	/*
	 * This is set to 0x0a (RGB/BGR order + horizontal flip) in order
	 * to make the display behave normally. If this is not set the displays
	 * normal output behaviour is horizontally flipped and BGR ordered. Do
	 * it twice because the first message doesn't always "take".
	 */
	lms397kf04_dcs_write_seq_static(lms, MIPI_DCS_SET_ADDRESS_MODE, 0x0a);
	lms397kf04_dcs_write_seq_static(lms, MIPI_DCS_SET_ADDRESS_MODE, 0x0a);
	/* Called "Access protection off" in vendor code */
	lms397kf04_dcs_write_seq_static(lms, LMS397_MANUFACTURER_CMD, 0x00);
	lms397kf04_dcs_write_seq_static(lms, LMS397_PANEL_DRIVING, 0x28, 0x08);
	lms397kf04_dcs_write_seq_static(lms, LMS397_SOURCE_CONTROL,
					0x01, 0x30, 0x15, 0x05, 0x22);
	lms397kf04_dcs_write_seq_static(lms, LMS397_GATE_INTERFACE,
					0x10, 0x01, 0x00);
	lms397kf04_dcs_write_seq_static(lms, LMS397_DISPLAY_H_TIMING,
					0x06, 0x55, 0x03, 0x07, 0x0b,
					0x33, 0x00, 0x01, 0x03);
	/*
	 * 0x00 in datasheet 0x01 in vendor code 0x00, it seems 0x01 means
	 * DE active high and 0x00 means DE active low.
	 */
	lms397kf04_dcs_write_seq_static(lms, LMS397_RGB_SYNC_OPTION, 0x01);
	lms397kf04_dcs_write_seq_static(lms, LMS397_GAMMA_SET_RED,
		/* R positive gamma */ 0x00,
		0x0A, 0x31, 0x3B, 0x4E, 0x58, 0x59, 0x5B, 0x58, 0x5E, 0x62,
		0x60, 0x61, 0x5E, 0x62, 0x55, 0x55, 0x7F, 0x08,
		/* R negative gamma */ 0x00,
		0x0A, 0x31, 0x3B, 0x4E, 0x58, 0x59, 0x5B, 0x58, 0x5E, 0x62,
		0x60, 0x61, 0x5E, 0x62, 0x55, 0x55, 0x7F, 0x08);
	lms397kf04_dcs_write_seq_static(lms, LMS397_GAMMA_SET_GREEN,
		/* G positive gamma */ 0x00,
		0x25, 0x15, 0x28, 0x3D, 0x4A, 0x48, 0x4C, 0x4A, 0x52, 0x59,
		0x59, 0x5B, 0x56, 0x60, 0x5D, 0x55, 0x7F, 0x0A,
		/* G negative gamma */ 0x00,
		0x25, 0x15, 0x28, 0x3D, 0x4A, 0x48, 0x4C, 0x4A, 0x52, 0x59,
		0x59, 0x5B, 0x56, 0x60, 0x5D, 0x55, 0x7F, 0x0A);
	lms397kf04_dcs_write_seq_static(lms, LMS397_GAMMA_SET_BLUE,
		/* B positive gamma */ 0x00,
		0x48, 0x10, 0x1F, 0x2F, 0x35, 0x38, 0x3D, 0x3C, 0x45, 0x4D,
		0x4E, 0x52, 0x51, 0x60, 0x7F, 0x7E, 0x7F, 0x0C,
		/* B negative gamma */ 0x00,
		0x48, 0x10, 0x1F, 0x2F, 0x35, 0x38, 0x3D, 0x3C, 0x45, 0x4D,
		0x4E, 0x52, 0x51, 0x60, 0x7F, 0x7E, 0x7F, 0x0C);
	lms397kf04_dcs_write_seq_static(lms, LMS397_BIAS_CURRENT_CTRL,
					0x33, 0x13);
	lms397kf04_dcs_write_seq_static(lms, LMS397_DDV_CTRL,
					0x11, 0x00, 0x00);
	lms397kf04_dcs_write_seq_static(lms, LMS397_GAMMA_CTRL_REF,
					0x50, 0x50);
	lms397kf04_dcs_write_seq_static(lms, LMS397_DCDC_CTRL,
					0x2f, 0x11, 0x1e, 0x46);
	lms397kf04_dcs_write_seq_static(lms, LMS397_VCL_CTRL,
					0x11, 0x0a);

	return 0;
}

static void lms397kf04_power_off(struct lms397kf04 *lms)
{
	/* Go into RESET and disable regulators */
	gpiod_set_value_cansleep(lms->reset, 1);
	regulator_bulk_disable(ARRAY_SIZE(lms->regulators),
			       lms->regulators);
}

static int lms397kf04_unprepare(struct drm_panel *panel)
{
	struct lms397kf04 *lms = to_lms397kf04(panel);

	lms397kf04_power_off(lms);

	return 0;
}

static int lms397kf04_disable(struct drm_panel *panel)
{
	struct lms397kf04 *lms = to_lms397kf04(panel);

	lms397kf04_dcs_write_seq_static(lms, MIPI_DCS_SET_DISPLAY_OFF);
	msleep(25);
	lms397kf04_dcs_write_seq_static(lms, MIPI_DCS_ENTER_SLEEP_MODE);
	msleep(120);

	return 0;
}

static int lms397kf04_prepare(struct drm_panel *panel)
{
	struct lms397kf04 *lms = to_lms397kf04(panel);
	int ret;

	ret = lms397kf04_power_on(lms);
	if (ret)
		return ret;

	return 0;
}

static int lms397kf04_enable(struct drm_panel *panel)
{
	struct lms397kf04 *lms = to_lms397kf04(panel);

	/* Exit sleep mode */
	lms397kf04_dcs_write_seq_static(lms, MIPI_DCS_EXIT_SLEEP_MODE);
	msleep(20);

	/* NVM (non-volatile memory) load sequence */
	lms397kf04_dcs_write_seq_static(lms, LMS397_UNKNOWN_D4,
					0x52, 0x5e);
	lms397kf04_dcs_write_seq_static(lms, LMS397_UNKNOWN_F8,
					0x01, 0xf5, 0xf2, 0x71, 0x44);
	lms397kf04_dcs_write_seq_static(lms, LMS397_UNKNOWN_FC,
					0x00, 0x08);
	msleep(150);

	/* CABC turn on sequence (BC = backlight control) */
	lms397kf04_dcs_write_seq_static(lms, LMS397_UNKNOWN_B4,
					0x0f, 0x00, 0x50);
	lms397kf04_dcs_write_seq_static(lms, LMS397_USER_SELECT, 0x80);
	lms397kf04_dcs_write_seq_static(lms, LMS397_UNKNOWN_B7, 0x24);
	lms397kf04_dcs_write_seq_static(lms, LMS397_UNKNOWN_B8, 0x01);

	/* Turn on display */
	lms397kf04_dcs_write_seq_static(lms, MIPI_DCS_SET_DISPLAY_ON);

	/* Update brightness */

	return 0;
}

/**
 * lms397kf04_get_modes() - return the mode
 * @panel: the panel to get the mode for
 * @connector: reference to the central DRM connector control structure
 */
static int lms397kf04_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	struct lms397kf04 *lms = to_lms397kf04(panel);
	struct drm_display_mode *mode;
	static const u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	mode = drm_mode_duplicate(connector->dev, &lms397kf04_mode);
	if (!mode) {
		dev_err(lms->dev, "failed to add mode\n");
		return -ENOMEM;
	}

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;
	connector->display_info.bus_flags =
		DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE;
	drm_display_info_set_bus_formats(&connector->display_info,
					 &bus_format, 1);

	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	drm_mode_probed_add(connector, mode);

	return 1;
}

static const struct drm_panel_funcs lms397kf04_drm_funcs = {
	.disable = lms397kf04_disable,
	.unprepare = lms397kf04_unprepare,
	.prepare = lms397kf04_prepare,
	.enable = lms397kf04_enable,
	.get_modes = lms397kf04_get_modes,
};

static int lms397kf04_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct lms397kf04 *lms;
	int ret;

	lms = devm_kzalloc(dev, sizeof(*lms), GFP_KERNEL);
	if (!lms)
		return -ENOMEM;
	lms->dev = dev;

	/*
	 * VCI   is the analog voltage supply
	 * VCCIO is the digital I/O voltage supply
	 */
	lms->regulators[0].supply = "vci";
	lms->regulators[1].supply = "vccio";
	ret = devm_regulator_bulk_get(dev,
				      ARRAY_SIZE(lms->regulators),
				      lms->regulators);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get regulators\n");

	/* This asserts the RESET signal, putting the display into reset */
	lms->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(lms->reset)) {
		dev_err(dev, "no RESET GPIO\n");
		return -ENODEV;
	}

	spi->bits_per_word = 9;
	/* Preserve e.g. SPI_3WIRE setting */
	spi->mode |= SPI_MODE_3;
	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(dev, "spi setup failed.\n");
		return ret;
	}
	lms->spi = spi;

	drm_panel_init(&lms->panel, dev, &lms397kf04_drm_funcs,
		       DRM_MODE_CONNECTOR_DPI);

	/* FIXME: if no external backlight, use internal backlight */
	ret = drm_panel_of_backlight(&lms->panel);
	if (ret) {
		dev_info(dev, "failed to add backlight\n");
		return ret;
	}

	spi_set_drvdata(spi, lms);

	drm_panel_add(&lms->panel);
	dev_info(dev, "added panel\n");

	return 0;
}

static int lms397kf04_remove(struct spi_device *spi)
{
	struct lms397kf04 *lms = spi_get_drvdata(spi);

	drm_panel_remove(&lms->panel);
	return 0;
}

static const struct of_device_id lms397kf04_match[] = {
	{ .compatible = "samsung,lms397kf04", },
	{},
};
MODULE_DEVICE_TABLE(of, lms397kf04_match);

static struct spi_driver lms397kf04_driver = {
	.probe		= lms397kf04_probe,
	.remove		= lms397kf04_remove,
	.driver		= {
		.name	= "lms397kf04-panel",
		.of_match_table = lms397kf04_match,
	},
};
module_spi_driver(lms397kf04_driver);

MODULE_AUTHOR("Linus Walleij <linus.walleij@linaro.org>");
MODULE_DESCRIPTION("Samsung LMS397KF04 panel driver");
MODULE_LICENSE("GPL v2");
