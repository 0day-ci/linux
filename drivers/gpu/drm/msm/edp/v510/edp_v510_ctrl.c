// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/rational.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <drm/drm_crtc.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_edid.h>
#include <linux/delay.h>

#include "edp_v510.h"
#include "edp_v510_reg.h"

#define VDDA_UA_ON_LOAD		21800	/* uA units */
#define VDDA_UA_OFF_LOAD	4	/* uA units */
#define LVL_UA_ON_LOAD		36000	/* uA units */
#define LVL_UA_OFF_LOAD		32	/* uA units */

#define DPCD_LINK_VOLTAGE_MAX		4
#define DPCD_LINK_PRE_EMPHASIS_MAX	4

#define EDP_LINK_BW_MAX		DP_LINK_BW_5_4

/* Link training return value */
#define EDP_TRAIN_FAIL		-1
#define EDP_TRAIN_SUCCESS	0
#define EDP_TRAIN_RECONFIG	1

#define EDP_CLK_MASK_AHB		BIT(0)
#define EDP_CLK_MASK_AUX		BIT(1)
#define EDP_CLK_MASK_LINK		BIT(2)
#define EDP_CLK_MASK_PIXEL		BIT(3)
#define EDP_CLK_MASK_MDP_CORE		BIT(4)
#define EDP_CLK_MASK_LINK_CHAN	(EDP_CLK_MASK_LINK | EDP_CLK_MASK_PIXEL)
#define EDP_CLK_MASK_AUX_CHAN	\
	(EDP_CLK_MASK_AHB | EDP_CLK_MASK_AUX | EDP_CLK_MASK_MDP_CORE)
#define EDP_CLK_MASK_ALL	(EDP_CLK_MASK_AUX_CHAN | EDP_CLK_MASK_LINK_CHAN)

#define EDP_BACKLIGHT_MAX	255

#define EDP_INTERRUPT_STATUS_ACK_SHIFT	1
#define EDP_INTERRUPT_STATUS_MASK_SHIFT	2

#define EDP_INTERRUPT_STATUS1 \
	(EDP_INTR_AUX_I2C_DONE| \
	EDP_INTR_WRONG_ADDR | EDP_INTR_TIMEOUT | \
	EDP_INTR_NACK_DEFER | EDP_INTR_WRONG_DATA_CNT | \
	EDP_INTR_I2C_NACK | EDP_INTR_I2C_DEFER | \
	EDP_INTR_PLL_UNLOCKED | EDP_INTR_AUX_ERROR)

#define EDP_INTERRUPT_STATUS1_ACK \
	(EDP_INTERRUPT_STATUS1 << EDP_INTERRUPT_STATUS_ACK_SHIFT)
#define EDP_INTERRUPT_STATUS1_MASK \
	(EDP_INTERRUPT_STATUS1 << EDP_INTERRUPT_STATUS_MASK_SHIFT)

#define EDP_INTERRUPT_STATUS2 \
	(EDP_INTR_READY_FOR_VIDEO | EDP_INTR_IDLE_PATTERN_SENT | \
	EDP_INTR_FRAME_END | EDP_INTR_CRC_UPDATED | EDP_INTR_SST_FIFO_UNDERFLOW)

#define EDP_INTERRUPT_STATUS2_ACK \
	(EDP_INTERRUPT_STATUS2 << EDP_INTERRUPT_STATUS_ACK_SHIFT)
#define EDP_INTERRUPT_STATUS2_MASK \
	(EDP_INTERRUPT_STATUS2 << EDP_INTERRUPT_STATUS_MASK_SHIFT)

enum edp_pm_type {
	EDP_CORE_PM,
	EDP_CTRL_PM,
	EDP_STREAM_PM,
	EDP_PHY_PM,
	EDP_MAX_PM
};

struct edp_ctrl {
	struct platform_device *pdev;

	void __iomem *base;
	void __iomem *phy_base;

	/* regulators */
	struct regulator *vdda_vreg;	/* 1.8 V */
	struct regulator *lvl_vreg;

	/* clocks */
	struct dss_module_power mp[EDP_MAX_PM];
	bool core_clks_on;
	bool link_clks_on;
	bool stream_clks_on;

	/* gpios */
	struct gpio_desc *panel_en_gpio;
	struct gpio_desc *panel_hpd_gpio;
	struct gpio_desc *panel_bklt1_gpio;
	struct gpio_desc *panel_bklt2_gpio;
	struct gpio_desc *panel_pwm_gpio;

	/* completion and mutex */
	struct completion idle_comp;
	struct mutex dev_mutex; /* To protect device power status */

	/* work queue */
	struct work_struct on_work;
	struct work_struct off_work;
	struct workqueue_struct *workqueue;

	/* Interrupt register lock */
	spinlock_t irq_lock;

	bool edp_connected;
	bool power_on;
	bool core_initialized;

	/* edid raw data */
	struct edid *edid;

	struct drm_dp_aux *drm_aux;

	/* dpcd raw data */
	u8 dpcd[DP_RECEIVER_CAP_SIZE];

	/* Link status */
	u8 link_rate;
	u8 lane_cnt;
	u8 v_level;
	u8 p_level;
	struct edp_phy_opts edp_opts;

	/* Timing status */
	u8 interlaced;
	u32 pixel_rate; /* in kHz */
	u32 color_depth;
	struct drm_display_mode drm_mode;

	struct edp_aux *aux;
	struct edp_phy *phy;
};

struct edp_ctrl_tu {
	u32 rate;
	u32 edp_tu;
	u32 valid_boundary;
	u32 valid_boundary2;
};

#define MAX_TU_TABLE 1
static const struct edp_ctrl_tu tu[MAX_TU_TABLE] = {
	{285550, 0x20, 0x13001B, 0x920035}, /* 1920x1080@120Hz CVT RB1 */
};

static inline bool edp_check_prefix(const char *clk_prefix,
						const char *clk_name)
{
	return !strncmp(clk_prefix, clk_name, strlen(clk_prefix));
}

static int edp_init_clk_data(struct edp_ctrl *ctrl)
{
	int num_clk, i, rc;
	int core_clk_count = 0, ctrl_clk_count = 0, stream_clk_count = 0;
	const char *clk_name;
	struct device *dev = &ctrl->pdev->dev;
	struct dss_module_power *core_power = &ctrl->mp[EDP_CORE_PM];
	struct dss_module_power *ctrl_power = &ctrl->mp[EDP_CTRL_PM];
	struct dss_module_power *stream_power = &ctrl->mp[EDP_STREAM_PM];

	num_clk = of_property_count_strings(dev->of_node, "clock-names");
	if (num_clk <= 0) {
		DRM_ERROR("no clocks are defined\n");
		return -EINVAL;
	}

	for (i = 0; i < num_clk; i++) {
		rc = of_property_read_string_index(dev->of_node,
				"clock-names", i, &clk_name);
		if (rc < 0)
			return rc;

		if (edp_check_prefix("core", clk_name))
			core_clk_count++;

		if (edp_check_prefix("ctrl", clk_name))
			ctrl_clk_count++;

		if (edp_check_prefix("stream", clk_name))
			stream_clk_count++;
	}

	/* Initialize the CORE power module */
	if (core_clk_count == 0) {
		DRM_ERROR("no core clocks are defined\n");
		return -EINVAL;
	}

	core_power->num_clk = core_clk_count;
	core_power->clk_config = devm_kzalloc(dev,
			sizeof(struct dss_clk) * core_power->num_clk,
			GFP_KERNEL);
	if (!core_power->clk_config)
		return -EINVAL;

	/* Initialize the CTRL power module */
	if (ctrl_clk_count == 0) {
		DRM_ERROR("no ctrl clocks are defined\n");
		return -EINVAL;
	}

	ctrl_power->num_clk = ctrl_clk_count;
	ctrl_power->clk_config = devm_kzalloc(dev,
			sizeof(struct dss_clk) * ctrl_power->num_clk,
			GFP_KERNEL);
	if (!ctrl_power->clk_config) {
		ctrl_power->num_clk = 0;
		return -EINVAL;
	}

	/* Initialize the STREAM power module */
	if (stream_clk_count == 0) {
		DRM_ERROR("no stream (pixel) clocks are defined\n");
		return -EINVAL;
	}

	stream_power->num_clk = stream_clk_count;
	stream_power->clk_config = devm_kzalloc(dev,
			sizeof(struct dss_clk) * stream_power->num_clk,
			GFP_KERNEL);
	if (!stream_power->clk_config) {
		stream_power->num_clk = 0;
		return -EINVAL;
	}

	return 0;
}

static int edp_clk_init(struct edp_ctrl *ctrl)
{
	int rc = 0, i = 0;
	int num_clk = 0;
	int core_clk_index = 0, ctrl_clk_index = 0, stream_clk_index = 0;
	int core_clk_count = 0, ctrl_clk_count = 0, stream_clk_count = 0;
	const char *clk_name;
	struct device *dev = &ctrl->pdev->dev;
	struct dss_module_power *core_power = &ctrl->mp[EDP_CORE_PM];
	struct dss_module_power *ctrl_power = &ctrl->mp[EDP_CTRL_PM];
	struct dss_module_power *stream_power = &ctrl->mp[EDP_STREAM_PM];

	rc =  edp_init_clk_data(ctrl);
	if (rc) {
		DRM_ERROR("failed to initialize power data %d\n", rc);
		return -EINVAL;
	}

	core_clk_count = core_power->num_clk;
	ctrl_clk_count = ctrl_power->num_clk;
	stream_clk_count = stream_power->num_clk;

	num_clk = core_clk_count + ctrl_clk_count + stream_clk_count;

	for (i = 0; i < num_clk; i++) {
		rc = of_property_read_string_index(dev->of_node, "clock-names",
				i, &clk_name);
		if (rc) {
			DRM_ERROR("error reading clock-names %d\n", rc);
			return rc;
		}
		if (edp_check_prefix("core", clk_name) &&
				core_clk_index < core_clk_count) {
			struct dss_clk *clk =
				&core_power->clk_config[core_clk_index];
			strscpy(clk->clk_name, clk_name, sizeof(clk->clk_name));
			clk->type = DSS_CLK_AHB;
			core_clk_index++;
		} else if (edp_check_prefix("stream", clk_name) &&
				stream_clk_index < stream_clk_count) {
			struct dss_clk *clk =
				&stream_power->clk_config[stream_clk_index];
			strscpy(clk->clk_name, clk_name, sizeof(clk->clk_name));
			clk->type = DSS_CLK_PCLK;
			stream_clk_index++;
		} else if (edp_check_prefix("ctrl", clk_name) &&
			   ctrl_clk_index < ctrl_clk_count) {
			struct dss_clk *clk =
				&ctrl_power->clk_config[ctrl_clk_index];
			strscpy(clk->clk_name, clk_name, sizeof(clk->clk_name));
			ctrl_clk_index++;
			if (edp_check_prefix("ctrl_link", clk_name) ||
			    edp_check_prefix("stream_pixel", clk_name))
				clk->type = DSS_CLK_PCLK;
			else
				clk->type = DSS_CLK_AHB;
		}
	}

	DRM_DEBUG_DP("clock parsing successful\n");

	rc = msm_dss_get_clk(dev, core_power->clk_config, core_power->num_clk);
	if (rc) {
		DRM_ERROR("failed to get core clk. err=%d\n", rc);
		return rc;
	}

	rc = msm_dss_get_clk(dev, ctrl_power->clk_config, ctrl_power->num_clk);
	if (rc) {
		DRM_ERROR("failed to get ctrl clk. err=%d\n", rc);
		msm_dss_put_clk(core_power->clk_config, core_power->num_clk);
		return -ENODEV;
	}

	rc = msm_dss_get_clk(dev, stream_power->clk_config, stream_power->num_clk);
	if (rc) {
		DRM_ERROR("failed to get strem clk. err=%d\n", rc);
		msm_dss_put_clk(core_power->clk_config, core_power->num_clk);
		return -ENODEV;
	}

	return 0;
}

static void edp_clk_deinit(struct edp_ctrl *ctrl)
{
	struct dss_module_power *core_power, *ctrl_power, *stream_power;

	core_power = &ctrl->mp[EDP_CORE_PM];
	ctrl_power = &ctrl->mp[EDP_CTRL_PM];
	stream_power = &ctrl->mp[EDP_STREAM_PM];

	if (!core_power || !ctrl_power || !stream_power)
		DRM_ERROR("invalid power_data\n");

	msm_dss_put_clk(ctrl_power->clk_config, ctrl_power->num_clk);
	msm_dss_put_clk(core_power->clk_config, core_power->num_clk);
	msm_dss_put_clk(stream_power->clk_config, stream_power->num_clk);
}

static int edp_clk_set_rate(struct edp_ctrl *ctrl,
		enum edp_pm_type module, bool enable)
{
	int rc = 0;
	struct dss_module_power *mp = &ctrl->mp[module];

	if (enable) {
		rc = msm_dss_clk_set_rate(mp->clk_config, mp->num_clk);
		if (rc) {
			DRM_ERROR("failed to set clks rate.\n");
			return rc;
		}
	}

	rc = msm_dss_enable_clk(mp->clk_config, mp->num_clk, enable);
	if (rc) {
		DRM_ERROR("failed to %d clks, err: %d\n", enable, rc);
		return rc;
	}

	return 0;
}

int edp_clk_enable(struct edp_ctrl *ctrl,
		enum edp_pm_type pm_type, bool enable)
{
	int rc = 0;

	if (pm_type != EDP_CORE_PM && pm_type != EDP_CTRL_PM &&
			pm_type != EDP_STREAM_PM) {
		DRM_ERROR("unsupported power module\n");
		return -EINVAL;
	}

	if (enable) {
		if (pm_type == EDP_CORE_PM && ctrl->core_clks_on) {
			DRM_DEBUG_DP("core clks already enabled\n");
			return 0;
		}

		if (pm_type == EDP_CTRL_PM && ctrl->link_clks_on) {
			DRM_DEBUG_DP("links clks already enabled\n");
			return 0;
		}

		if (pm_type == EDP_STREAM_PM && ctrl->stream_clks_on) {
			DRM_DEBUG_DP("pixel clks already enabled\n");
			return 0;
		}

		if ((pm_type == EDP_CTRL_PM) && (!ctrl->core_clks_on)) {
			DRM_DEBUG_DP("Enable core clks before link clks\n");

			rc = edp_clk_set_rate(ctrl, EDP_CORE_PM, enable);
			if (rc) {
				DRM_ERROR("fail to enable clks: core. err=%d\n",
					rc);
				return rc;
			}
			ctrl->core_clks_on = true;
		}
	}

	rc = edp_clk_set_rate(ctrl, pm_type, enable);
	if (rc) {
		DRM_ERROR("failed to '%s' clks. err=%d\n",
			enable ? "enable" : "disable", rc);
			return rc;
	}

	if (pm_type == EDP_CORE_PM)
		ctrl->core_clks_on = enable;
	else if (pm_type == EDP_STREAM_PM)
		ctrl->stream_clks_on = enable;
	else
		ctrl->link_clks_on = enable;

	DRM_DEBUG_DP("stream_clks:%s link_clks:%s core_clks:%s\n",
		ctrl->stream_clks_on ? "on" : "off",
		ctrl->link_clks_on ? "on" : "off",
		ctrl->core_clks_on ? "on" : "off");

	return 0;
}

static void edp_ctrl_set_clock_rate(struct edp_ctrl *ctrl,
			enum edp_pm_type module, char *name, unsigned long rate)
{
	u32 num = ctrl->mp[module].num_clk;
	struct dss_clk *cfg = ctrl->mp[module].clk_config;

	while (num && strcmp(cfg->clk_name, name)) {
		num--;
		cfg++;
	}

	DRM_DEBUG_DP("setting rate=%lu on clk=%s\n", rate, name);

	if (num)
		cfg->rate = rate;
	else
		DRM_ERROR("%s clock doesn't exit to set rate %lu\n",
				name, rate);
}

static int edp_regulator_init(struct edp_ctrl *ctrl)
{
	struct device *dev = &ctrl->pdev->dev;
	int ret;

	ctrl->vdda_vreg = devm_regulator_get(dev, "vdda");
	ret = PTR_ERR_OR_ZERO(ctrl->vdda_vreg);
	if (ret) {
		DRM_ERROR("%s: Could not get vdda reg, ret = %d\n", __func__,
				ret);
		ctrl->vdda_vreg = NULL;
		return ret;
	}
	ctrl->lvl_vreg = devm_regulator_get(dev, "lvl-vdd");
	ret = PTR_ERR_OR_ZERO(ctrl->lvl_vreg);
	if (ret) {
		DRM_ERROR("%s: Could not get lvl-vdd reg, ret = %d\n", __func__,
				ret);
		ctrl->lvl_vreg = NULL;
		return ret;
	}

	return 0;
}

static int edp_regulator_enable(struct edp_ctrl *ctrl)
{
	int ret;

	ret = regulator_set_load(ctrl->vdda_vreg, VDDA_UA_ON_LOAD);
	if (ret < 0) {
		DRM_ERROR("%s: vdda_vreg set regulator mode failed.\n", __func__);
		goto vdda_set_fail;
	}

	ret = regulator_enable(ctrl->vdda_vreg);
	if (ret) {
		DRM_ERROR("%s: Failed to enable vdda_vreg regulator.\n", __func__);
		goto vdda_enable_fail;
	}

	ret = regulator_set_load(ctrl->lvl_vreg, LVL_UA_ON_LOAD);
	if (ret < 0) {
		DRM_ERROR("%s: vdda_vreg set regulator mode failed.\n", __func__);
		goto vdda_set_fail;
	}

	ret = regulator_enable(ctrl->lvl_vreg);
	if (ret) {
		DRM_ERROR("Failed to enable lvl-vdd reg regulator, %d", ret);
		goto lvl_enable_fail;
	}

	return 0;

lvl_enable_fail:
	regulator_disable(ctrl->vdda_vreg);
vdda_enable_fail:
	regulator_set_load(ctrl->vdda_vreg, VDDA_UA_OFF_LOAD);
vdda_set_fail:
	return ret;
}

static void edp_regulator_disable(struct edp_ctrl *ctrl)
{
	regulator_disable(ctrl->lvl_vreg);
	regulator_set_load(ctrl->lvl_vreg, LVL_UA_OFF_LOAD);
	regulator_disable(ctrl->vdda_vreg);
	regulator_set_load(ctrl->vdda_vreg, VDDA_UA_OFF_LOAD);
}

static int edp_gpio_config(struct edp_ctrl *ctrl)
{
	struct device *dev = &ctrl->pdev->dev;
	int ret;

	ctrl->panel_hpd_gpio = devm_gpiod_get(dev, "panel-hpd", GPIOD_IN);
	if (IS_ERR(ctrl->panel_hpd_gpio)) {
		ret = PTR_ERR(ctrl->panel_hpd_gpio);
		ctrl->panel_hpd_gpio = NULL;
		DRM_ERROR("%s: cannot get panel-hpd-gpios, %d\n", __func__, ret);
		return ret;
	}

	ctrl->panel_en_gpio = devm_gpiod_get(dev, "panel-en", GPIOD_OUT_HIGH);
	if (IS_ERR(ctrl->panel_en_gpio)) {
		ret = PTR_ERR(ctrl->panel_en_gpio);
		ctrl->panel_en_gpio = NULL;
		DRM_ERROR("%s: cannot get panel-en-gpios, %d\n", __func__, ret);
		return ret;
	}

	ctrl->panel_bklt1_gpio = devm_gpiod_get(dev, "panel-bklt1",
			GPIOD_OUT_HIGH);
	if (IS_ERR(ctrl->panel_bklt1_gpio)) {
		ret = PTR_ERR(ctrl->panel_bklt1_gpio);
		ctrl->panel_bklt1_gpio = NULL;
		DRM_ERROR("%s: cannot get panel-bklt1-gpios, %d\n", __func__, ret);
		return ret;
	}

	ctrl->panel_bklt2_gpio = devm_gpiod_get(dev, "panel-bklt2",
			GPIOD_OUT_HIGH);
	if (IS_ERR(ctrl->panel_bklt2_gpio)) {
		ret = PTR_ERR(ctrl->panel_bklt2_gpio);
		ctrl->panel_bklt2_gpio = NULL;
		DRM_ERROR("%s: cannot get panel-bklt2-gpios, %d\n", __func__, ret);
		return ret;
	}

	ctrl->panel_pwm_gpio = devm_gpiod_get(dev, "panel-pwm", GPIOD_OUT_HIGH);
	if (IS_ERR(ctrl->panel_pwm_gpio)) {
		ret = PTR_ERR(ctrl->panel_pwm_gpio);
		ctrl->panel_pwm_gpio = NULL;
		DRM_ERROR("%s: cannot get panel-pwm-gpios, %d\n", __func__, ret);
		return ret;
	}

	DRM_INFO("gpio on");

	return 0;
}

static void edp_ctrl_irq_enable(struct edp_ctrl *ctrl, int enable)
{
	unsigned long flags;

	spin_lock_irqsave(&ctrl->irq_lock, flags);
	if (enable) {
		edp_write_ahb(ctrl->base, REG_EDP_INTR_STATUS,
				EDP_INTERRUPT_STATUS1_MASK);
		edp_write_ahb(ctrl->base, REG_EDP_INTR_STATUS2,
				EDP_INTERRUPT_STATUS2_MASK);
	} else {
		edp_write_ahb(ctrl->base, REG_EDP_INTR_STATUS,
				EDP_INTERRUPT_STATUS1_ACK);
		edp_write_ahb(ctrl->base, REG_EDP_INTR_STATUS2,
				EDP_INTERRUPT_STATUS2_ACK);
	}
	spin_unlock_irqrestore(&ctrl->irq_lock, flags);
}

static void edp_fill_link_cfg(struct edp_ctrl *ctrl)
{
	u32 prate;
	u32 bpp;
	u8 max_lane = drm_dp_max_lane_count(ctrl->dpcd);

	prate = ctrl->pixel_rate;
	bpp = ctrl->color_depth * 3;

	/*
	 * By default, use the maximum link rate and minimum lane count,
	 * so that we can do rate down shift during link training.
	 */
	ctrl->link_rate = ctrl->dpcd[DP_MAX_LINK_RATE];
	ctrl->lane_cnt = max_lane;
	DRM_INFO("rate=%d lane=%d", ctrl->link_rate, ctrl->lane_cnt);
}

static void edp_config_ctrl(struct edp_ctrl *ctrl)
{
	u32 config = 0, depth = 0;
	u8 *dpcd = ctrl->dpcd;

	/* Default-> LSCLK DIV: 1/4 LCLK  */
	config |= (2 << EDP_CONFIGURATION_CTRL_LSCLK_DIV_SHIFT);

	/* Scrambler reset enable */
	if (dpcd[DP_EDP_CONFIGURATION_CAP] & DP_ALTERNATE_SCRAMBLER_RESET_CAP)
		config |= EDP_CONFIGURATION_CTRL_ASSR;

	if (ctrl->color_depth == 8)
		depth = EDP_8BIT;
	else if (ctrl->color_depth == 10)
		depth = EDP_10BIT;
	else if (ctrl->color_depth == 12)
		depth = EDP_12BIT;
	else if (ctrl->color_depth == 16)
		depth = EDP_16BIT;
	config |= depth << EDP_CONFIGURATION_CTRL_BPC_SHIFT;

	/* Num of Lanes */
	config |= ((ctrl->lane_cnt - 1)
			<< EDP_CONFIGURATION_CTRL_NUM_OF_LANES_SHIFT);

	if (drm_dp_enhanced_frame_cap(dpcd))
		config |= EDP_CONFIGURATION_CTRL_ENHANCED_FRAMING;

	config |= EDP_CONFIGURATION_CTRL_P_INTERLACED; /* progressive video */

	/* sync clock & static Mvid */
	config |= EDP_CONFIGURATION_CTRL_STATIC_DYNAMIC_CN;
	config |= EDP_CONFIGURATION_CTRL_SYNC_ASYNC_CLK;

	edp_write_link(ctrl->base, REG_EDP_CONFIGURATION_CTRL, config);
}

static void edp_state_ctrl(struct edp_ctrl *ctrl, u32 state)
{
	edp_write_link(ctrl->base, REG_EDP_STATE_CTRL, state);
	/* Make sure H/W status is set */
	wmb();
}

static int edp_lane_set_write(struct edp_ctrl *ctrl,
	u8 voltage_level, u8 pre_emphasis_level)
{
	int i;
	u8 buf[4];

	if (voltage_level >= DPCD_LINK_VOLTAGE_MAX)
		voltage_level |= 0x04;

	if (pre_emphasis_level >= DPCD_LINK_PRE_EMPHASIS_MAX)
		pre_emphasis_level |= 0x04;

	pre_emphasis_level <<= 3;

	for (i = 0; i < 4; i++)
		buf[i] = voltage_level | pre_emphasis_level;

	DRM_INFO("%s: p|v=0x%x", __func__, voltage_level | pre_emphasis_level);
	if (drm_dp_dpcd_write(ctrl->drm_aux, 0x103, buf, 4) < 4) {
		DRM_ERROR("%s: Set sw/pe to panel failed\n", __func__);
		return -ENOLINK;
	}

	return 0;
}

static int edp_train_pattern_set_write(struct edp_ctrl *ctrl, u8 pattern)
{
	u8 p = pattern;

	DRM_DEBUG_DP("pattern=%x", p);
	if (drm_dp_dpcd_write(ctrl->drm_aux,
				DP_TRAINING_PATTERN_SET, &p, 1) < 1) {
		DRM_ERROR("%s: Set training pattern to panel failed\n", __func__);
		return -ENOLINK;
	}

	return 0;
}

static void edp_sink_train_set_adjust(struct edp_ctrl *ctrl,
	const u8 *link_status)
{
	int i;
	u8 max = 0;
	u8 data;

	/* use the max level across lanes */
	for (i = 0; i < ctrl->lane_cnt; i++) {
		data = drm_dp_get_adjust_request_voltage(link_status, i);
		DRM_DEBUG_DP("lane=%d req_voltage_swing=0x%x", i, data);
		if (max < data)
			max = data;
	}

	ctrl->v_level = max >> DP_TRAIN_VOLTAGE_SWING_SHIFT;

	/* use the max level across lanes */
	max = 0;
	for (i = 0; i < ctrl->lane_cnt; i++) {
		data = drm_dp_get_adjust_request_pre_emphasis(link_status, i);
		DRM_DEBUG_DP("lane=%d req_pre_emphasis=0x%x", i, data);
		if (max < data)
			max = data;
	}

	ctrl->p_level = max >> DP_TRAIN_PRE_EMPHASIS_SHIFT;
	DRM_DEBUG_DP("v_level=%d, p_level=%d", ctrl->v_level, ctrl->p_level);
}

static void edp_host_train_set(struct edp_ctrl *ctrl, u32 train)
{
	int cnt = 10;
	u32 data;
	u32 shift = train - 1;

	DRM_DEBUG_DP("train=%d", train);

	edp_state_ctrl(ctrl, EDP_STATE_CTRL_LINK_TRAINING_PATTERN1 << shift);
	while (--cnt) {
		data = edp_read_link(ctrl->base, REG_EDP_MAINLINK_READY);
		if (data & (EDP_MAINLINK_READY_TRAIN_PATTERN_1_READY << shift))
			break;
	}

	if (cnt == 0)
		DRM_DEBUG_DP("%s: set link_train=%d failed\n", __func__, train);
}

static int edp_voltage_pre_emphasis_set(struct edp_ctrl *ctrl)
{
	DRM_DEBUG_DP("v=%d p=%d", ctrl->v_level, ctrl->p_level);

	msm_edp_v510_phy_config(ctrl->phy, ctrl->v_level, ctrl->p_level);
	return edp_lane_set_write(ctrl, ctrl->v_level, ctrl->p_level);
}

static int edp_start_link_train_1(struct edp_ctrl *ctrl)
{
	u8 link_status[DP_LINK_STATUS_SIZE];
	u8 old_v_level;
	int tries;
	int ret;
	int rlen;

	edp_host_train_set(ctrl, DP_TRAINING_PATTERN_1);
	ret = edp_voltage_pre_emphasis_set(ctrl);
	if (ret)
		return ret;

	ret = edp_train_pattern_set_write(ctrl,
			DP_TRAINING_PATTERN_1 | DP_RECOVERED_CLOCK_OUT_EN);
	if (ret)
		return ret;

	tries = 0;
	old_v_level = ctrl->v_level;
	while (1) {
		drm_dp_link_train_clock_recovery_delay(ctrl->dpcd);

		rlen = drm_dp_dpcd_read_link_status(ctrl->drm_aux, link_status);
		if (rlen < DP_LINK_STATUS_SIZE) {
			DRM_ERROR("%s: read link status failed\n", __func__);
			return -ENOLINK;
		}
		if (drm_dp_clock_recovery_ok(link_status, ctrl->lane_cnt)) {
			ret = 0;
			break;
		}

		if (ctrl->v_level == DPCD_LINK_VOLTAGE_MAX) {
			ret = -1;
			break;
		}

		if (old_v_level == ctrl->v_level) {
			tries++;
			if (tries >= 5) {
				ret = -1;
				break;
			}
		} else {
			tries = 0;
			old_v_level = ctrl->v_level;
		}

		edp_sink_train_set_adjust(ctrl, link_status);
		ret = edp_voltage_pre_emphasis_set(ctrl);
		if (ret)
			return ret;
	}

	return ret;
}

static int edp_start_link_train_2(struct edp_ctrl *ctrl)
{
	u8 link_status[DP_LINK_STATUS_SIZE];
	int tries = 0;
	int ret;
	int rlen;

	edp_host_train_set(ctrl, DP_TRAINING_PATTERN_2);
	ret = edp_voltage_pre_emphasis_set(ctrl);
	if (ret)
		return ret;

	ret = edp_train_pattern_set_write(ctrl,
			DP_TRAINING_PATTERN_2 | DP_RECOVERED_CLOCK_OUT_EN);
	if (ret)
		return ret;

	while (1) {
		drm_dp_link_train_channel_eq_delay(ctrl->dpcd);

		rlen = drm_dp_dpcd_read_link_status(ctrl->drm_aux, link_status);
		if (rlen < DP_LINK_STATUS_SIZE) {
			DRM_ERROR("%s: read link status failed\n", __func__);
			return -ENOLINK;
		}
		if (drm_dp_channel_eq_ok(link_status, ctrl->lane_cnt)) {
			ret = 0;
			break;
		}

		tries++;
		if (tries > 10) {
			ret = -1;
			break;
		}

		edp_sink_train_set_adjust(ctrl, link_status);
		ret = edp_voltage_pre_emphasis_set(ctrl);
		if (ret)
			return ret;
	}

	return ret;
}

static int edp_link_rate_down_shift(struct edp_ctrl *ctrl)
{
	u32 prate, lrate, bpp;
	u8 rate, lane, max_lane;
	int changed = 0;

	rate = ctrl->link_rate;
	lane = ctrl->lane_cnt;
	max_lane = drm_dp_max_lane_count(ctrl->dpcd);

	bpp = ctrl->color_depth * 3;
	prate = ctrl->pixel_rate;
	prate *= bpp;
	prate /= 8; /* in kByte */

	if (rate > DP_LINK_BW_1_62 && rate <= EDP_LINK_BW_MAX) {
		rate -= 4;	/* reduce rate */
		changed++;
	}

	if (changed) {
		if (lane >= 1 && lane < max_lane)
			lane <<= 1;	/* increase lane */

		lrate = 270000; /* in kHz */
		lrate *= rate;
		lrate /= 10; /* kByte, 10 bits --> 8 bits */
		lrate *= lane;

		DRM_DEBUG_DP("new lrate=%u prate=%u(kHz) rate=%d lane=%d p=%u b=%d",
			lrate, prate, rate, lane,
			ctrl->pixel_rate,
			bpp);

		if (lrate > prate) {
			ctrl->link_rate = rate;
			ctrl->lane_cnt = lane;
			DRM_DEBUG_DP("new rate=%d %d", rate, lane);
			return 0;
		}
	}

	return -EINVAL;
}

static int edp_clear_training_pattern(struct edp_ctrl *ctrl)
{
	int ret;

	ret = edp_train_pattern_set_write(ctrl, 0);

	drm_dp_link_train_channel_eq_delay(ctrl->dpcd);

	return ret;
}

static int edp_do_link_train(struct edp_ctrl *ctrl)
{
	u8 values[2], edp_config = 0;
	int ret;

	/*
	 * Set the current link rate and lane cnt to panel. They may have been
	 * adjusted and the values are different from them in DPCD CAP
	 */
	values[0] = ctrl->lane_cnt;
	values[1] = ctrl->link_rate;

	if (drm_dp_enhanced_frame_cap(ctrl->dpcd))
		values[0] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;

	if (drm_dp_dpcd_write(ctrl->drm_aux, DP_LINK_BW_SET, &values[1], 1) < 0)
		return EDP_TRAIN_FAIL;

	drm_dp_dpcd_write(ctrl->drm_aux, DP_LANE_COUNT_SET, &values[0], 1);
	ctrl->v_level = 0; /* start from default level */
	ctrl->p_level = 0;

	values[0] = DP_SPREAD_AMP_0_5;
	values[1] = 1;
	drm_dp_dpcd_write(ctrl->drm_aux, DP_DOWNSPREAD_CTRL, &values[0], 1);
	drm_dp_dpcd_write(ctrl->drm_aux, DP_MAIN_LINK_CHANNEL_CODING_SET, &values[1], 1);

	edp_state_ctrl(ctrl, 0);
	if (edp_clear_training_pattern(ctrl))
		return EDP_TRAIN_FAIL;

	ret = edp_start_link_train_1(ctrl);
	if (ret < 0) {
		if (edp_link_rate_down_shift(ctrl) == 0) {
			DRM_ERROR("link reconfig");
			ret = EDP_TRAIN_RECONFIG;
			goto clear;
		} else {
			DRM_ERROR("%s: Training 1 failed", __func__);
			ret = EDP_TRAIN_FAIL;
			goto clear;
		}
	}
	DRM_INFO("Training 1 completed successfully");

	edp_state_ctrl(ctrl, 0);
	if (edp_clear_training_pattern(ctrl))
		return EDP_TRAIN_FAIL;

	ret = edp_start_link_train_2(ctrl);
	if (ret < 0) {
		if (edp_link_rate_down_shift(ctrl) == 0) {
			DRM_ERROR("link reconfig");
			ret = EDP_TRAIN_RECONFIG;
			goto clear;
		} else {
			DRM_ERROR("%s: Training 2 failed", __func__);
			ret = EDP_TRAIN_FAIL;
			goto clear;
		}
	}
	DRM_INFO("Training 2 completed successfully");

	edp_config = DP_ALTERNATE_SCRAMBLER_RESET_ENABLE;
	drm_dp_dpcd_write(ctrl->drm_aux, DP_EDP_CONFIGURATION_SET,
			&edp_config, 1);

	edp_state_ctrl(ctrl, EDP_STATE_CTRL_SEND_VIDEO);
clear:
	edp_clear_training_pattern(ctrl);

	return ret;
}

static void edp_ctrl_config_misc(struct edp_ctrl *ctrl)
{
	u32 misc_val;
	enum edp_color_depth depth = EDP_8BIT;

	misc_val = edp_read_link(ctrl->base, REG_EDP_MISC1_MISC0);

	if (ctrl->color_depth == 8)
		depth = EDP_8BIT;
	else if (ctrl->color_depth == 10)
		depth = EDP_10BIT;
	else if (ctrl->color_depth == 12)
		depth = EDP_12BIT;
	else if (ctrl->color_depth == 16)
		depth = EDP_16BIT;

	/* clear bpp bits */
	misc_val &= ~(0x07 << EDP_MISC0_TEST_BITS_DEPTH_SHIFT);
	misc_val |= depth << EDP_MISC0_TEST_BITS_DEPTH_SHIFT;

	/* Configure clock to synchronous mode */
	misc_val |= EDP_MISC0_SYNCHRONOUS_CLK;

	DRM_DEBUG_DP("misc settings = 0x%x\n", misc_val);
	edp_write_link(ctrl->base, REG_EDP_MISC1_MISC0, misc_val);
}

static void edp_ctrl_config_msa(struct edp_ctrl *ctrl)
{
	u32 pixel_m, pixel_n;
	u32 mvid, nvid, pixel_div = 0, dispcc_input_rate;
	unsigned long den, num;
	u8 rate = ctrl->link_rate;
	u32 stream_rate_khz = ctrl->pixel_rate;

	if (rate == DP_LINK_BW_8_1)
		pixel_div = 6;
	else if (rate == DP_LINK_BW_1_62 || rate == DP_LINK_BW_2_7)
		pixel_div = 2;
	else if (rate == DP_LINK_BW_5_4)
		pixel_div = 4;
	else
		DRM_ERROR("Invalid pixel mux divider\n");

	dispcc_input_rate = (drm_dp_bw_code_to_link_rate(rate) * 10) / pixel_div;

	rational_best_approximation(dispcc_input_rate, stream_rate_khz,
			(unsigned long)(1 << 16) - 1,
			(unsigned long)(1 << 16) - 1, &den, &num);

	den = ~(den - num);
	den = den & 0xFFFF;
	pixel_m = num;
	pixel_n = den;

	mvid = (pixel_m & 0xFFFF) * 5;
	nvid = (0xFFFF & (~pixel_n)) + (pixel_m & 0xFFFF);

	if (rate == DP_LINK_BW_5_4)
		nvid *= 2;

	if (rate == DP_LINK_BW_8_1)
		nvid *= 3;

	DRM_DEBUG_DP("mvid=0x%x, nvid=0x%x\n", mvid, nvid);
	edp_write_link(ctrl->base, REG_EDP_SOFTWARE_MVID, mvid);
	edp_write_link(ctrl->base, REG_EDP_SOFTWARE_NVID, nvid);
	edp_write_p0(ctrl->base, REG_EDP_DSC_DTO, 0x0);
}

static void edp_ctrl_config_TU(struct edp_ctrl *ctrl)
{
	int i;

	for (i = 0; i < MAX_TU_TABLE; i++) {
		if (tu[i].rate == ctrl->pixel_rate)
			break;
	}

	edp_write_link(ctrl->base, REG_EDP_VALID_BOUNDARY,
			tu[i].valid_boundary);

	edp_write_link(ctrl->base, REG_EDP_TU, tu[i].edp_tu);

	edp_write_link(ctrl->base, REG_EDP_VALID_BOUNDARY_2,
			tu[i].valid_boundary2);
}

static void edp_ctrl_timing_cfg(struct edp_ctrl *ctrl)
{
	struct drm_display_mode *mode = &ctrl->drm_mode;
	u32 hstart_from_sync, vstart_from_sync;
	u32 data;

	/* Configure eDP timing to HW */
	edp_write_link(ctrl->base, REG_EDP_TOTAL_HOR_VER,
		EDP_TOTAL_HOR_VER_HORIZ(mode->htotal) |
		EDP_TOTAL_HOR_VER_VERT(mode->vtotal));

	vstart_from_sync = mode->vtotal - mode->vsync_start;
	hstart_from_sync = mode->htotal - mode->hsync_start;
	edp_write_link(ctrl->base, REG_EDP_START_HOR_VER_FROM_SYNC,
		EDP_START_HOR_VER_FROM_SYNC_HORIZ(hstart_from_sync) |
		EDP_START_HOR_VER_FROM_SYNC_VERT(vstart_from_sync));

	data = EDP_HSYNC_VSYNC_WIDTH_POLARITY_VERT(
			mode->vsync_end - mode->vsync_start);
	data |= EDP_HSYNC_VSYNC_WIDTH_POLARITY_HORIZ(
			mode->hsync_end - mode->hsync_start);
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		data |= EDP_HSYNC_VSYNC_WIDTH_POLARITY_NVSYNC;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		data |= EDP_HSYNC_VSYNC_WIDTH_POLARITY_NHSYNC;
	edp_write_link(ctrl->base, REG_EDP_HSYNC_VSYNC_WIDTH_POLARITY, data);

	edp_write_link(ctrl->base, REG_EDP_ACTIVE_HOR_VER,
		EDP_ACTIVE_HOR_VER_HORIZ(mode->hdisplay) |
		EDP_ACTIVE_HOR_VER_VERT(mode->vdisplay));

}

static void edp_mainlink_ctrl(struct edp_ctrl *ctrl, int enable)
{
	u32 data = 0;

	edp_write_link(ctrl->base, REG_EDP_MAINLINK_CTRL, EDP_MAINLINK_CTRL_RESET);
	/* Make sure fully reset */
	wmb();
	usleep_range(500, 1000);

	if (enable) {
		data = (EDP_MAINLINK_CTRL_ENABLE |
				EDP_MAINLINK_FB_BOUNDARY_SEL);
	}

	edp_write_link(ctrl->base, REG_EDP_MAINLINK_CTRL, data);
}

static void edp_ctrl_phy_enable(struct edp_ctrl *ctrl, int enable)
{
	if (enable) {
		edp_write_ahb(ctrl->base, REG_EDP_PHY_CTRL,
			EDP_PHY_CTRL_SW_RESET | EDP_PHY_CTRL_SW_RESET_PLL);
		usleep_range(1000, 1100);
		edp_write_ahb(ctrl->base, REG_EDP_PHY_CTRL, 0);

		msm_edp_v510_phy_enable(ctrl->phy);
	}
}

static void edp_ctrl_phy_aux_enable(struct edp_ctrl *ctrl, int enable)
{
	if (ctrl->core_initialized == enable)
		return;

	if (enable) {
		pm_runtime_get_sync(&ctrl->pdev->dev);
		edp_regulator_enable(ctrl);
		edp_clk_enable(ctrl, EDP_CORE_PM, 1);
		edp_ctrl_phy_enable(ctrl, 1);
		msm_edp_v510_aux_ctrl(ctrl->aux, 1);
		ctrl->core_initialized =  true;
	} else {
		msm_edp_v510_aux_ctrl(ctrl->aux, 0);
		edp_clk_enable(ctrl, EDP_CORE_PM, 0);
		edp_regulator_disable(ctrl);
		pm_runtime_put_sync(&ctrl->pdev->dev);
		ctrl->core_initialized =  false;
	}
}

static void edp_ctrl_link_enable(struct edp_ctrl *ctrl, int enable)
{
	unsigned long link_rate;

	link_rate = drm_dp_max_link_rate(ctrl->dpcd);
	ctrl->edp_opts.link_rate = link_rate;
	ctrl->edp_opts.lanes = drm_dp_max_lane_count(ctrl->dpcd);

	if (enable) {
		msm_edp_v510_phy_vm_pe_init(ctrl->phy, &ctrl->edp_opts);
		msm_edp_v510_phy_power_on(ctrl->phy);

		/* Enable link channel clocks */
		edp_ctrl_set_clock_rate(ctrl, EDP_CTRL_PM, "ctrl_link",
				link_rate * 1000);
		edp_clk_enable(ctrl, EDP_CTRL_PM, 1);

		edp_ctrl_set_clock_rate(ctrl, EDP_STREAM_PM, "stream_pixel",
				ctrl->pixel_rate * 1000);
		edp_clk_enable(ctrl, EDP_STREAM_PM, 1);

		edp_mainlink_ctrl(ctrl, 1);
		edp_config_ctrl(ctrl);
		edp_ctrl_config_misc(ctrl);
		edp_ctrl_timing_cfg(ctrl);
		edp_ctrl_config_msa(ctrl);
		edp_ctrl_config_TU(ctrl);

	} else {
		edp_mainlink_ctrl(ctrl, 0);
		edp_clk_enable(ctrl, EDP_STREAM_PM, 0);
		edp_clk_enable(ctrl, EDP_CTRL_PM, 0);
	}
}

static int edp_ctrl_training(struct edp_ctrl *ctrl)
{
	int ret;

	/* Do link training only when power is on */
	if (!ctrl->power_on)
		return -EINVAL;

train_start:
	ret = edp_do_link_train(ctrl);
	if (ret == EDP_TRAIN_RECONFIG) {
		/* Re-configure main link */
		edp_ctrl_irq_enable(ctrl, 0);
		edp_ctrl_link_enable(ctrl, 0);

		/* Make sure link is fully disabled */
		wmb();
		usleep_range(500, 1000);

		edp_ctrl_phy_enable(ctrl, 1);
		edp_ctrl_irq_enable(ctrl, 1);
		edp_ctrl_link_enable(ctrl, 1);
		goto train_start;
	}

	return ret;
}

static void edp_ctrl_on_worker(struct work_struct *work)
{
	struct edp_ctrl *ctrl = container_of(
				work, struct edp_ctrl, on_work);
	u8 value;
	int ret;

	mutex_lock(&ctrl->dev_mutex);

	if (ctrl->power_on) {
		DRM_INFO("already on");
		goto unlock_ret;
	}

	edp_ctrl_phy_aux_enable(ctrl, 1);
	edp_ctrl_irq_enable(ctrl, 1);
	edp_ctrl_link_enable(ctrl, 1);


	/* DP_SET_POWER register is only available on DPCD v1.1 and later */
	if (ctrl->dpcd[DP_DPCD_REV] >= 0x11) {
		ret = drm_dp_dpcd_readb(ctrl->drm_aux, DP_SET_POWER, &value);
		if (ret < 0)
			goto fail;

		value &= ~DP_SET_POWER_MASK;
		value |= DP_SET_POWER_D0;

		ret = drm_dp_dpcd_writeb(ctrl->drm_aux, DP_SET_POWER, value);
		if (ret < 0)
			goto fail;

		/*
		 * According to the DP 1.1 specification, a "Sink Device must
		 * exit the power saving state within 1 ms" (Section 2.5.3.1,
		 * Table 5-52, "Sink Control Field" (register 0x600).
		 */
		usleep_range(1000, 2000);
	}

	ctrl->power_on = true;

	/* Start link training */
	ret = edp_ctrl_training(ctrl);
	if (ret != EDP_TRAIN_SUCCESS)
		goto fail;

	DRM_INFO("DONE");
	goto unlock_ret;

fail:
	edp_ctrl_irq_enable(ctrl, 0);
	edp_ctrl_link_enable(ctrl, 0);
	edp_ctrl_phy_aux_enable(ctrl, 0);
	ctrl->power_on = false;
unlock_ret:
	mutex_unlock(&ctrl->dev_mutex);
}

static void edp_ctrl_off_worker(struct work_struct *work)
{
	struct edp_ctrl *ctrl = container_of(
				work, struct edp_ctrl, off_work);
	unsigned long time_left;

	mutex_lock(&ctrl->dev_mutex);

	if (!ctrl->power_on) {
		DRM_INFO("already off");
		goto unlock_ret;
	}

	reinit_completion(&ctrl->idle_comp);

	edp_state_ctrl(ctrl, EDP_STATE_CTRL_PUSH_IDLE);

	time_left = wait_for_completion_timeout(&ctrl->idle_comp,
						msecs_to_jiffies(500));
	if (!time_left)
		DRM_ERROR("%s: idle pattern timedout\n", __func__);

	edp_state_ctrl(ctrl, 0);

	/* DP_SET_POWER register is only available on DPCD v1.1 and later */
	if (ctrl->dpcd[DP_DPCD_REV] >= 0x11) {
		u8 value;
		int ret;

		ret = drm_dp_dpcd_readb(ctrl->drm_aux, DP_SET_POWER, &value);
		if (ret > 0) {
			value &= ~DP_SET_POWER_MASK;
			value |= DP_SET_POWER_D3;

			drm_dp_dpcd_writeb(ctrl->drm_aux, DP_SET_POWER, value);
		}
	}

	edp_ctrl_irq_enable(ctrl, 0);

	edp_ctrl_link_enable(ctrl, 0);

	edp_ctrl_phy_aux_enable(ctrl, 0);

	ctrl->power_on = false;

unlock_ret:
	mutex_unlock(&ctrl->dev_mutex);
}

irqreturn_t msm_edp_v510_ctrl_irq(struct edp_ctrl *ctrl)
{
	u32 isr1, isr2, mask1, mask2;
	u32 ack;

	spin_lock(&ctrl->irq_lock);
	isr1 = edp_read_ahb(ctrl->base, REG_EDP_INTR_STATUS);
	isr2 = edp_read_ahb(ctrl->base, REG_EDP_INTR_STATUS2);

	mask1 = isr1 & EDP_INTERRUPT_STATUS1_MASK;
	mask2 = isr2 & EDP_INTERRUPT_STATUS2_MASK;

	isr1 &= ~mask1;	/* remove masks bit */
	isr2 &= ~mask2;

	DRM_DEBUG_DP("isr=%x mask=%x isr2=%x mask2=%x",
			isr1, mask1, isr2, mask2);

	ack = isr1 & EDP_INTERRUPT_STATUS1;
	ack <<= 1;	/* ack bits */
	ack |= mask1;
	edp_write_ahb(ctrl->base, REG_EDP_INTR_STATUS, ack);

	ack = isr2 & EDP_INTERRUPT_STATUS2;
	ack <<= 1;	/* ack bits */
	ack |= mask2;
	edp_write_ahb(ctrl->base, REG_EDP_INTR_STATUS2, ack);
	spin_unlock(&ctrl->irq_lock);

	if (isr2 & EDP_INTR_READY_FOR_VIDEO)
		DRM_INFO("edp_video_ready");

	if (isr2 & EDP_INTR_IDLE_PATTERN_SENT) {
		DRM_INFO("idle_patterns_sent");
		complete(&ctrl->idle_comp);
	}

	msm_edp_v510_aux_irq(ctrl->aux, isr1);

	return IRQ_HANDLED;
}

void msm_edp_v510_ctrl_power(struct edp_ctrl *ctrl, bool on)
{
	if (on)
		queue_work(ctrl->workqueue, &ctrl->on_work);
	else
		queue_work(ctrl->workqueue, &ctrl->off_work);
}

int msm_edp_v510_ctrl_init(struct msm_edp_v510 *edp)
{
	struct edp_ctrl *ctrl = NULL;
	struct device *dev = &edp->base.pdev->dev;
	int ret;

	if (!edp) {
		DRM_ERROR("%s: edp is NULL!\n", __func__);
		return -EINVAL;
	}

	ctrl = devm_kzalloc(dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	edp->ctrl = ctrl;
	ctrl->pdev = edp->base.pdev;

	ctrl->base = msm_ioremap(ctrl->pdev, "edp_ctrl", "eDP_CTRL");
	if (IS_ERR(ctrl->base))
		return PTR_ERR(ctrl->base);

	ctrl->phy_base = msm_ioremap(ctrl->pdev, "edp_phy", "eDP_PHY");
	if (IS_ERR(ctrl->phy_base))
		return PTR_ERR(ctrl->phy_base);

	/* Get regulator, clock, gpio, pwm */
	ret = edp_regulator_init(ctrl);
	if (ret) {
		DRM_ERROR("%s:regulator init fail\n", __func__);
		return ret;
	}
	ret = edp_clk_init(ctrl);
	if (ret) {
		DRM_ERROR("%s:clk init fail\n", __func__);
		return ret;
	}
	ret = edp_gpio_config(ctrl);
	if (ret) {
		DRM_ERROR("%s:failed to configure GPIOs: %d", __func__, ret);
		return ret;
	}

	/* Init aux and phy */
	ctrl->aux = msm_edp_v510_aux_init(dev, ctrl->base, &ctrl->drm_aux);
	if (!ctrl->aux || !ctrl->drm_aux) {
		DRM_ERROR("%s:failed to init aux\n", __func__);
		return -ENOMEM;
	}

	ctrl->phy = msm_edp_v510_phy_init(dev, ctrl->phy_base, &ctrl->edp_opts);
	if (!ctrl->phy) {
		DRM_ERROR("%s:failed to init phy\n", __func__);
		ret = -ENOMEM;
		goto err_destory_aux;
	}

	pm_runtime_enable(dev);
	spin_lock_init(&ctrl->irq_lock);
	mutex_init(&ctrl->dev_mutex);
	init_completion(&ctrl->idle_comp);

	/* setup workqueue */
	ctrl->workqueue = alloc_ordered_workqueue("edp_drm_work", 0);
	INIT_WORK(&ctrl->on_work, edp_ctrl_on_worker);
	INIT_WORK(&ctrl->off_work, edp_ctrl_off_worker);

	return 0;

err_destory_aux:
	msm_edp_v510_aux_destroy(dev, ctrl->aux);
	ctrl->aux = NULL;
	return ret;
}

void msm_edp_v510_ctrl_destroy(struct edp_ctrl *ctrl)
{
	if (!ctrl)
		return;

	if (ctrl->workqueue) {
		flush_workqueue(ctrl->workqueue);
		destroy_workqueue(ctrl->workqueue);
		ctrl->workqueue = NULL;
	}

	if (ctrl->aux) {
		msm_edp_v510_aux_destroy(&ctrl->pdev->dev, ctrl->aux);
		ctrl->aux = NULL;
	}

	edp_clk_deinit(ctrl);

	kfree(ctrl->edid);
	ctrl->edid = NULL;

	mutex_destroy(&ctrl->dev_mutex);
}

bool msm_edp_v510_ctrl_panel_connected(struct edp_ctrl *ctrl)
{
	mutex_lock(&ctrl->dev_mutex);
	if (ctrl->edp_connected) {
		mutex_unlock(&ctrl->dev_mutex);
		return true;
	}

	if (!ctrl->power_on) {
		edp_ctrl_phy_aux_enable(ctrl, 1);
		edp_ctrl_irq_enable(ctrl, 1);
	}

	if (drm_dp_dpcd_read(ctrl->drm_aux, DP_DPCD_REV, ctrl->dpcd,
				DP_RECEIVER_CAP_SIZE) < DP_RECEIVER_CAP_SIZE) {
		DRM_ERROR("%s: AUX channel is NOT ready\n", __func__);
		memset(ctrl->dpcd, 0, DP_RECEIVER_CAP_SIZE);

		if (!ctrl->power_on) {
			edp_ctrl_irq_enable(ctrl, 0);
			edp_ctrl_phy_aux_enable(ctrl, 0);
		}

	} else {
		ctrl->edp_connected = true;
	}


	DRM_INFO("connect status=%d", ctrl->edp_connected);

	mutex_unlock(&ctrl->dev_mutex);

	return ctrl->edp_connected;
}

int msm_edp_v510_ctrl_get_panel_info(struct edp_ctrl *ctrl,
		struct drm_connector *connector, struct edid **edid)
{
	int ret = 0;

	mutex_lock(&ctrl->dev_mutex);

	if (ctrl->edid) {
		if (edid) {
			DRM_DEBUG_DP("Just return edid buffer");
			*edid = ctrl->edid;
		}
		goto unlock_ret;
	}

	if (!ctrl->power_on && !ctrl->edp_connected) {
		edp_ctrl_phy_aux_enable(ctrl, 1);
		edp_ctrl_irq_enable(ctrl, 1);
	}

	/* Initialize link rate as panel max link rate */
	ctrl->link_rate = ctrl->dpcd[DP_MAX_LINK_RATE];


	ctrl->edid = drm_get_edid(connector, &ctrl->drm_aux->ddc);
	if (!ctrl->edid) {
		DRM_ERROR("%s: edid read fail\n", __func__);
		if (!ctrl->power_on) {
			edp_ctrl_irq_enable(ctrl, 0);
			edp_ctrl_phy_aux_enable(ctrl, 0);
		}
		goto unlock_ret;
	}

	if (edid)
		*edid = ctrl->edid;

unlock_ret:
	mutex_unlock(&ctrl->dev_mutex);
	return ret;
}

int msm_edp_v510_ctrl_mode_set(struct edp_ctrl *ctrl,
				const struct drm_display_mode *mode,
				const struct drm_display_info *info)
{
	/*
	 * Need to keep color depth, pixel rate and
	 * interlaced information in ctrl context
	 */
	ctrl->color_depth = info->bpc;
	ctrl->pixel_rate = mode->clock;

	memcpy(&ctrl->drm_mode, mode, sizeof(*mode));

	ctrl->interlaced = !!(mode->flags & DRM_MODE_FLAG_INTERLACE);

	/* Fill initial link config based on passed in timing */
	edp_fill_link_cfg(ctrl);

	return 0;
}


bool msm_edp_v510_ctrl_pixel_clock_valid(struct edp_ctrl *ctrl, u32 pixel_rate)
{
	u32 link_clock = 0;
	unsigned long link_bw = 0, stream_bw = 0;

	link_clock = drm_dp_bw_code_to_link_rate(ctrl->link_rate);
	link_bw = link_clock * ctrl->lane_cnt;
	stream_bw = pixel_rate * ctrl->color_depth * 3 / 8;

	if (stream_bw > link_bw) {
		DRM_ERROR("pixel clock %d(kHz) not supported", pixel_rate);
		return false;
	}

	return true;
}
