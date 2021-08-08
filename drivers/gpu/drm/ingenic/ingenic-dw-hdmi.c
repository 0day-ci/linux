// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2011-2013 Freescale Semiconductor, Inc.
 * Copyright (C) 2019, 2020 Paul Boddie <paul@boddie.org.uk>
 *
 * Derived from dw_hdmi-imx.c with i.MX portions removed.
 * Probe and remove operations derived from rcar_dw_hdmi.c.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_of.h>

static const struct dw_hdmi_mpll_config ingenic_mpll_cfg[] = {
	{ 45250000,  { { 0x01e0, 0x0000 }, { 0x21e1, 0x0000 }, { 0x41e2, 0x0000 } } },
	{ 92500000,  { { 0x0140, 0x0005 }, { 0x2141, 0x0005 }, { 0x4142, 0x0005 } } },
	{ 148500000, { { 0x00a0, 0x000a }, { 0x20a1, 0x000a }, { 0x40a2, 0x000a } } },
	{ 216000000, { { 0x00a0, 0x000a }, { 0x2001, 0x000f }, { 0x4002, 0x000f } } },
	{ ~0UL,      { { 0x0000, 0x0000 }, { 0x0000, 0x0000 }, { 0x0000, 0x0000 } } }
};

static const struct dw_hdmi_curr_ctrl ingenic_cur_ctr[] = {
	/*pixelclk     bpp8    bpp10   bpp12 */
	{ 54000000,  { 0x091c, 0x091c, 0x06dc } },
	{ 58400000,  { 0x091c, 0x06dc, 0x06dc } },
	{ 72000000,  { 0x06dc, 0x06dc, 0x091c } },
	{ 74250000,  { 0x06dc, 0x0b5c, 0x091c } },
	{ 118800000, { 0x091c, 0x091c, 0x06dc } },
	{ 216000000, { 0x06dc, 0x0b5c, 0x091c } },
	{ ~0UL,      { 0x0000, 0x0000, 0x0000 } },
};

/*
 * Resistance term 133Ohm Cfg
 * PREEMP config 0.00
 * TX/CK level 10
 */
static const struct dw_hdmi_phy_config ingenic_phy_config[] = {
	/*pixelclk   symbol   term   vlev */
	{ 216000000, 0x800d, 0x0005, 0x01ad},
	{ ~0UL,      0x0000, 0x0000, 0x0000}
};

static enum drm_mode_status
ingenic_dw_hdmi_mode_valid(struct dw_hdmi *hdmi, void *data,
			   const struct drm_display_info *info,
			   const struct drm_display_mode *mode)
{
	if (mode->clock < 13500)
		return MODE_CLOCK_LOW;
	/* FIXME: Hardware is capable of 270MHz, but setup data is missing. */
	if (mode->clock > 216000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static bool
ingenic_dw_hdmi_mode_fixup(struct drm_bridge *bridge,
			   const struct drm_display_mode *mode,
			   struct drm_display_mode *adjusted_mode)
{
	adjusted_mode->flags |= (DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC);
	adjusted_mode->flags &= ~(DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC);

	return true;
}

static const struct drm_bridge_timings ingenic_dw_hdmi_timings = {
	.input_bus_flags = DRM_BUS_FLAG_PIXDATA_SAMPLE_NEGEDGE,
};

static struct dw_hdmi_plat_data ingenic_dw_hdmi_plat_data = {
	.mpll_cfg   = ingenic_mpll_cfg,
	.cur_ctr    = ingenic_cur_ctr,
	.phy_config = ingenic_phy_config,
	.mode_valid = ingenic_dw_hdmi_mode_valid,
	.mode_fixup = ingenic_dw_hdmi_mode_fixup,
	.timings    = &ingenic_dw_hdmi_timings,
};

static const struct of_device_id ingenic_dw_hdmi_dt_ids[] = {
	{ .compatible = "ingenic,jz4780-dw-hdmi" },
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, ingenic_dw_hdmi_dt_ids);

static int ingenic_dw_hdmi_probe(struct platform_device *pdev)
{
	struct dw_hdmi *hdmi;

	hdmi = dw_hdmi_probe(pdev, &ingenic_dw_hdmi_plat_data);
	if (IS_ERR(hdmi))
		return PTR_ERR(hdmi);

	platform_set_drvdata(pdev, hdmi);

	return 0;
}

static int ingenic_dw_hdmi_remove(struct platform_device *pdev)
{
	struct dw_hdmi *hdmi = platform_get_drvdata(pdev);

	dw_hdmi_remove(hdmi);

	return 0;
}

static struct platform_driver ingenic_dw_hdmi_driver = {
	.probe  = ingenic_dw_hdmi_probe,
	.remove = ingenic_dw_hdmi_remove,
	.driver = {
		.name = "dw-hdmi-ingenic",
		.of_match_table = ingenic_dw_hdmi_dt_ids,
	},
};

struct platform_driver *ingenic_dw_hdmi_driver_ptr = &ingenic_dw_hdmi_driver;
