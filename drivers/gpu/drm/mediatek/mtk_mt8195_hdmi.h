/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef _MTK_HDMI_CTRL_H
#define _MTK_HDMI_CTRL_H

#include <linux/hdmi.h>
#include <drm/drm_bridge.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <sound/hdmi-codec.h>
#include <linux/clk.h>
#include <linux/mutex.h>

#define RGB444_8bit BIT(0)
#define RGB444_10bit BIT(1)
#define RGB444_12bit BIT(2)
#define RGB444_16bit BIT(3)
#define YCBCR444_8bit BIT(4)
#define YCBCR444_10bit BIT(5)
#define YCBCR444_12bit BIT(6)
#define YCBCR444_16bit BIT(7)
#define YCBCR422_8bit_NO_SUPPORT BIT(8)
#define YCBCR422_10bit_NO_SUPPORT BIT(9)
#define YCBCR422_12bit BIT(10)
#define YCBCR422_16bit_NO_SUPPORT BIT(11)
#define YCBCR420_8bit BIT(12)
#define YCBCR420_10bit BIT(13)
#define YCBCR420_12bit BIT(14)
#define YCBCR420_16bit BIT(15)

enum hdmi_color_depth { HDMI_8_BIT, HDMI_10_BIT, HDMI_12_BIT, HDMI_16_BIT };

enum mtk_hdmi_clk_id {
	MTK_HDMI_CLK_UNIVPLL_D6D4,
	MTK_HDMI_CLK_MSDCPLL_D2,
	MTK_HDMI_CLK_HDMI_APB_SEL,
	MTK_HDMI_UNIVPLL_D4D8,
	MTK_HDIM_HDCP_SEL,
	MTK_HDMI_HDCP_24M_SEL,
	MTK_HDMI_VPP_SPLIT_HDMI,
	MTK_HDMI_CLK_COUNT,
};

struct mtk_hdmi_edid {
	unsigned char edid[EDID_LENGTH * 4];
	unsigned char blk_num;
};

enum HDMI_HPD_STATE {
	HDMI_PLUG_OUT = 0,
	HDMI_PLUG_IN_AND_SINK_POWER_ON,
	HDMI_PLUG_IN_ONLY,
};

struct hdmi_audio_param;

struct mtk_hdmi {
	struct drm_bridge bridge;
	struct drm_connector conn;
	struct device *dev;
	struct phy *phy;
	struct device *cec_dev;
	struct cec_notifier *notifier;
	struct i2c_adapter *ddc_adpt;
	struct clk *clk[MTK_HDMI_CLK_COUNT];
	struct drm_display_mode mode;
	struct mtk_edid_params *edid_params;
	struct mtk_hdmi_sink_av_cap *sink_avcap;
	bool dvi_mode;
	u32 max_hdisplay;
	u32 max_vdisplay;
	void __iomem *regs;
	spinlock_t property_lock;
	struct drm_property *hdmi_info_blob;
	struct drm_property_blob *hdmi_info_blob_ptr;
	struct drm_property *csp_depth_prop;
	uint64_t support_csp_depth;
	uint64_t set_csp_depth;
	enum hdmi_colorspace csp;
	enum hdmi_color_depth color_depth;
	enum hdmi_colorimetry colorimtery;
	enum hdmi_extended_colorimetry extended_colorimetry;
	enum hdmi_quantization_range quantization_range;
	enum hdmi_ycc_quantization_range ycc_quantization_range;
	struct mtk_hdmi_edid raw_edid;

	struct hdmi_audio_param *aud_param;
	bool audio_enable;
	struct device *codec_dev;
	hdmi_codec_plugged_cb plugged_cb;

	bool powered;
	bool enabled;
	unsigned int hdmi_irq;
	enum HDMI_HPD_STATE hpd;
	struct workqueue_struct *hdmi_wq;
	struct delayed_work hpd_work;
	struct delayed_work hdr10_delay_work;
	struct delayed_work hdr10vsif_delay_work;
	struct mutex hdr_mutex;

	bool hdmi_enabled;
	bool power_clk_enabled;
	bool irq_registered;
};

extern struct platform_driver mtk_hdmi_mt8195_ddc_driver;

#if defined(CONFIG_DRM_MEDIATEK_HDMI) ||                                       \
	defined(CONFIG_DRM_MEDIATEK_HDMI_MODULE)
unsigned int get_hdmi_colorspace_colorimetry(
	struct drm_bridge *bridge, enum hdmi_colorspace *colorspace,
	enum hdmi_colorimetry *colorimtery,
	enum hdmi_extended_colorimetry *extended_colorimetry,
	enum hdmi_quantization_range *quantization_range,
	enum hdmi_ycc_quantization_range *ycc_quantization_range);
#else
inline unsigned int get_hdmi_colorspace_colorimetry(
	struct drm_bridge *bridge, enum hdmi_colorspace *colorspace,
	enum hdmi_colorimetry *colorimtery,
	enum hdmi_extended_colorimetry *extended_colorimetry,
	enum hdmi_quantization_range *quantization_range,
	enum hdmi_ycc_quantization_range *ycc_quantization_range)
{
	return 0;
}
#endif

/* struct mtk_hdmi_info is used to propagate blob property to userspace */
struct mtk_hdmi_info {
	unsigned short edid_sink_colorimetry;
	unsigned char edid_sink_rgb_color_bit;
	unsigned char edid_sink_ycbcr_color_bit;
	unsigned char ui1_sink_dc420_color_bit;
	unsigned short edid_sink_max_tmds_clock;
	unsigned short edid_sink_max_tmds_character_rate;
	unsigned char edid_sink_support_dynamic_hdr;
};

#endif /* _MTK_HDMI_CTRL_H */
