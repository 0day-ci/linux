// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018 - present Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare HDMI Receiver controller driver
 *
 * Author: Jose Abreu <jose.abreu@synopsys.com>
 * Author: Nelson Costa <nelson.costa@synopsys.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/phy/phy.h>
#include <linux/phy/dwc/dw-hdmi-phy-pdata.h>
#include <linux/v4l2-dv-timings.h>
#include <linux/workqueue.h>
#include <linux/rational.h>
#include <linux/hdmi.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-event.h>
#include <media/v4l2-subdev.h>
#include <media/dwc/dw-hdmi-rx-pdata.h>

#include "dw-hdmi-rx.h"

#define DW_HDMI_DEFAULT_TIMING		V4L2_DV_BT_CEA_640X480P59_94
#define DW_HDMI_JTAG_TAP_ADDR_CMD	0
#define DW_HDMI_JTAG_TAP_WRITE_CMD	1
#define DW_HDMI_JTAG_TAP_READ_CMD	3
#define DW_HDMI_AUDIO_FREQ_RANGE	1000

/* EDID for HDMI RX */
static u32 dw_hdmi_edid[] = {
	/* V2 Support for                           */
	/*  - Video modes up-to 2.97Gbps            */
	/*  - RGB, Ycc444/422/420                   */
	/*  - Audio 2Ch L-PCM                       */
	/*  - SCDC CED                              */
	0x00FFFFFF, 0xFFFFFF00, 0x4F2E4A21, 0x00000000,
	0x331E0103, 0x80462878, 0x0A0DC9A0, 0x57479827,
	0x12484C20, 0x00000101, 0x01010101, 0x01010101,
	0x01010101, 0x0101023A, 0x80187138, 0x2D40582C,
	0x450020C2, 0x3100001E, 0x011D0072, 0x51D01E20,
	0x6E285500, 0x20C23100, 0x001E0000, 0x00FC0053,
	0x4E505320, 0x48444D49, 0x2052580A, 0x000000FD,
	0x0017780F, 0x871E000A, 0x20202020, 0x20200160,

	0x02035771, 0x83010000, 0x57696867, 0x6463625F,
	0x5E5D901F, 0x04131211, 0x0302015A, 0x59585756,
	0x55555453, 0x5251504F, 0x4E4D4C4B, 0x4A494847,
	0x46454443, 0x42412309, 0x07076E03, 0x0C001000,
	0xF83B2000, 0x80010203, 0x0467D85D, 0xC4010080,
	0x00E50E66, 0x65616000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x000000B6,
};

static const struct v4l2_dv_timings_cap dw_hdmi_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	.reserved = { 0 },
	V4L2_INIT_BT_TIMINGS(640, 4096,			/* min/max width */
			     480, 4455,			/* min/max height */
			     20000000, 600000000,	/* min/max pixelclock */
			     /* standards */
			     V4L2_DV_BT_STD_CEA861,
			     /* capabilities */
			     V4L2_DV_BT_CAP_PROGRESSIVE)
};

static const struct v4l2_event dw_hdmi_event_fmt = {
	.type = V4L2_EVENT_SOURCE_CHANGE,
	.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
};

enum dw_hdmi_state {
	HDMI_STATE_NO_INIT = 0,
	HDMI_STATE_POWER_OFF,
	HDMI_STATE_POWER_UP,
	HDMI_STATE_PHY_CONFIG,
	HDMI_STATE_HPD,
	HDMI_STATE_EQUALIZER,
	HDMI_STATE_DATAPATH,
	HDMI_STATE_VIDEO_UNSTABLE,
	HDMI_STATE_AUDIO,
	HDMI_STATE_POWER_ON,
};

struct dw_hdmi_infoframe_cfg {
	const char *desc;
	u8 header[3];
	u32 header_addr;
	u32 payload_addr;
	u32 payload_len;
	void *frame;
	unsigned int frame_size;
};

struct dw_hdmi_dev {
	struct device *dev;
	struct device_node *of_node;
	void __iomem *regs;
	struct clk *clk;

	/* Platform Data configuration */
	struct dw_hdmi_rx_pdata *config;

	/* Phy info */
	struct platform_device *phy_pdev;
	struct dw_phy_pdata phy_config;
	struct phy *phy;

	/*
	 * Used to prevent race conditions between multiple
	 * concurrent calls to handle the state machine changes
	 * and pending configurations.
	 */
	spinlock_t lock;
	/*
	 * Used to prevent race conditions between multiple
	 * concurrent calls to notify the audio changes.
	 */
	spinlock_t event_lock;

	/* Work queue to handle the state machine */
	struct workqueue_struct *wq;
	struct work_struct work;

	/* State machine variables */
	enum dw_hdmi_state state;
	u32 mbus_code;
	u32 old_mbus;
	u8 *curr_edid_blocks;
	u8 current_vic;
	bool current_vic_is_4k;
	bool registered;
	bool pending_config;
	bool force_off;
	bool is_hdmi2;
	bool is_scrambled;
	bool phy_eq_force;
	bool phy_eq_on;
	bool hw_reset_on_hot_plug;
	bool *input_connected;
	unsigned int selected_input;
	unsigned int configured_input;
	unsigned int input_stat;
	unsigned int audio_sf;
	unsigned int tmds_valid_wait_count;
	unsigned int has_clock_wait_ms;
	unsigned int video_stable_wait_ms;
	unsigned int reset_datapath_enable;

	/* Infoframes */
	union hdmi_infoframe aviif;
	union hdmi_infoframe spdif;
	union hdmi_infoframe audioif;
	union hdmi_infoframe vsif;

	/* v4l2 device */
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	struct v4l2_ctrl *detect_tx_5v_ctrl;
	struct v4l2_dv_timings timings;
};

static const char *get_state_name(enum dw_hdmi_state state)
{
	switch (state) {
	case HDMI_STATE_NO_INIT:
		return "NO_INIT";
	case HDMI_STATE_POWER_OFF:
		return "POWER_OFF";
	case HDMI_STATE_POWER_UP:
		return "POWER_UP";
	case HDMI_STATE_PHY_CONFIG:
		return "PHY_CONFIG";
	case HDMI_STATE_HPD:
		return "HPD";
	case HDMI_STATE_EQUALIZER:
		return "EQUALIZER";
	case HDMI_STATE_DATAPATH:
		return "DATAPATH";
	case HDMI_STATE_VIDEO_UNSTABLE:
		return "VIDEO_UNSTABLE";
	case HDMI_STATE_AUDIO:
		return "AUDIO";
	case HDMI_STATE_POWER_ON:
		return "POWER_ON";
	default:
		return "UNKNOWN";
	}
}

static inline void dw_hdmi_set_state(struct dw_hdmi_dev *dw_dev,
				     enum dw_hdmi_state new_state)
{
	unsigned long flags;

	spin_lock_irqsave(&dw_dev->lock, flags);
	dev_dbg(dw_dev->dev, "old_state=%s, new_state=%s\n",
		get_state_name(dw_dev->state),
		get_state_name(new_state));
	dw_dev->state = new_state;
	spin_unlock_irqrestore(&dw_dev->lock, flags);
}

static inline struct dw_hdmi_dev *to_dw_dev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct dw_hdmi_dev, sd);
}

static inline void hdmi_writel(struct dw_hdmi_dev *dw_dev, u32 val, int reg)
{
	writel(val, dw_dev->regs + reg);
}

static inline u32 hdmi_readl(struct dw_hdmi_dev *dw_dev, int reg)
{
	return readl(dw_dev->regs + reg);
}

static void hdmi_modl(struct dw_hdmi_dev *dw_dev, u32 data, u32 mask, int reg)
{
	u32 val = hdmi_readl(dw_dev, reg) & ~mask;

	val |= data & mask;
	hdmi_writel(dw_dev, val, reg);
}

static void hdmi_mask_writel(struct dw_hdmi_dev *dw_dev, u32 data, int reg,
			     u32 shift, u32 mask)
{
	hdmi_modl(dw_dev, data << shift, mask, reg);
}

static u32 hdmi_mask_readl(struct dw_hdmi_dev *dw_dev, int reg, u32 shift,
			   u32 mask)
{
	return (hdmi_readl(dw_dev, reg) & mask) >> shift;
}

static bool dw_hdmi_5v_status(struct dw_hdmi_dev *dw_dev, int input)
{
	void __iomem *arg = dw_dev->config->dw_5v_arg;

	if (dw_dev->config->dw_5v_status)
		return dw_dev->config->dw_5v_status(arg, input);

	return false;
}

static void dw_hdmi_5v_disable(struct dw_hdmi_dev *dw_dev, int input)
{
	void __iomem *arg = dw_dev->config->dw_5v_arg;

	if (!dw_dev->config->dw_5v_disable)
		return;

	dw_dev->config->dw_5v_disable(arg, input);
}

static void dw_hdmi_5v_enable(struct dw_hdmi_dev *dw_dev, int input)
{
	void __iomem *arg = dw_dev->config->dw_5v_arg;

	if (!dw_dev->config->dw_5v_enable)
		return;

	dw_dev->config->dw_5v_enable(arg, input);
}

static u32 dw_hdmi_edid_read(struct dw_hdmi_dev *dw_dev, int input, u32 offset)
{
	void __iomem *arg = dw_dev->config->dw_edid_arg;

	if (!dw_dev->config->dw_edid_read)
		return 0x0;

	return dw_dev->config->dw_edid_read(arg, input, offset);
}

static int dw_hdmi_edid_write(struct dw_hdmi_dev *dw_dev, int input, u32 *edid,
			      int size)
{
	void __iomem *arg = dw_dev->config->dw_edid_arg;

	if (!dw_dev->config->dw_edid_write)
		return 0;

	return dw_dev->config->dw_edid_write(arg, input, edid, size);
}

static u32 dw_hdmi_edid_4blocks_le(struct dw_hdmi_dev *dw_dev)
{
	void __iomem *arg = dw_dev->config->dw_edid_arg;

	if (!dw_dev->config->dw_edid_4blocks_le)
		return 0x0;

	return dw_dev->config->dw_edid_4blocks_le(arg);
}

static int dw_hdmi_update_edid(struct dw_hdmi_dev *dw_dev, int input,
			       u8 *edid, int size, u8 invert_bytes)
{
	unsigned int i, j;
	u32 *w_edid_srt;
	int ret;

	if (invert_bytes) {
		/* invert the order of bytes to register 32bit */
		w_edid_srt = devm_kzalloc(dw_dev->dev, size, GFP_KERNEL);
		if (!w_edid_srt) {
			devm_kfree(dw_dev->dev, w_edid_srt);
			return -ENOMEM;
		}

		for (i = 0; i < size / sizeof(u32); i++) {
			for (j = 0; j < 4; j++) {
				w_edid_srt[i] |=
					edid[i * 4 + j] << (8 * (3 - j));
			}
		}

		ret = dw_hdmi_edid_write(dw_dev, input, (u32 *)w_edid_srt,
					 size / sizeof(u32));
		devm_kfree(dw_dev->dev, w_edid_srt);
	} else {
		/* no need to invert bytes */
		ret = dw_hdmi_edid_write(dw_dev, input, (u32 *)edid,
					 size / sizeof(u32));
	}
	dw_dev->curr_edid_blocks[input] = size / 128;

	return ret;
}

static void dw_hdmi_main_reset(struct dw_hdmi_dev *dw_dev)
{
	void __iomem *arg = dw_dev->config->dw_reset_arg;

	if (!dw_dev->config->dw_reset_all)
		return;

	dev_dbg(dw_dev->dev, "%s: main reset\n", __func__);

	dw_dev->config->dw_reset_all(arg);
}

static void dw_hdmi_disable_hpd(struct dw_hdmi_dev *dw_dev);

static void dw_hdmi_reset(struct dw_hdmi_dev *dw_dev)
{
	dev_dbg(dw_dev->dev, "%s: reset\n", __func__);

	/* perform main reset */
	dw_hdmi_main_reset(dw_dev);

	dw_hdmi_disable_hpd(dw_dev);
}

static inline bool is_off(struct dw_hdmi_dev *dw_dev)
{
	return dw_dev->state <= HDMI_STATE_POWER_OFF;
}

static inline bool is_on(struct dw_hdmi_dev *dw_dev)
{
	return dw_dev->state == HDMI_STATE_POWER_ON;
}

static bool has_signal(struct dw_hdmi_dev *dw_dev, unsigned int input)
{
	return dw_dev->input_connected[input];
}

static inline bool is_hdmi2(struct dw_hdmi_dev *dw_dev)
{
	return hdmi_readl(dw_dev, DW_HDMI_SCDC_REGS0) &
			DW_HDMI_SCDC_TMDSBITCLKRATIO_MASK;
}

static inline bool is_scrambled(struct dw_hdmi_dev *dw_dev)
{
	return hdmi_readl(dw_dev, DW_HDMI_SCDC_REGS0) &
			DW_HDMI_SCDC_SCRAMBSTATUS_MASK;
}

static void hdmi_phy_jtag_send_pulse(struct dw_hdmi_dev *dw_dev, u8 tms, u8 tdi)
{
	u8 val;

	val = tms ? DW_HDMI_JTAG_TMS : 0;
	val |= tdi ? DW_HDMI_JTAG_TDI : 0;

	hdmi_writel(dw_dev, 0, DW_HDMI_JTAG_TAP_TCLK);
	hdmi_writel(dw_dev, val, DW_HDMI_JTAG_TAP_IN);
	hdmi_writel(dw_dev, 1, DW_HDMI_JTAG_TAP_TCLK);
}

static void hdmi_phy_jtag_shift_dr(struct dw_hdmi_dev *dw_dev)
{
	hdmi_phy_jtag_send_pulse(dw_dev, 1, 0);
	hdmi_phy_jtag_send_pulse(dw_dev, 0, 0);
	hdmi_phy_jtag_send_pulse(dw_dev, 0, 0);
}

static void hdmi_phy_jtag_shift_ir(struct dw_hdmi_dev *dw_dev)
{
	hdmi_phy_jtag_send_pulse(dw_dev, 1, 0);
	hdmi_phy_jtag_send_pulse(dw_dev, 1, 0);
	hdmi_phy_jtag_send_pulse(dw_dev, 0, 0);
	hdmi_phy_jtag_send_pulse(dw_dev, 0, 0);
}

static u16 hdmi_phy_jtag_send(struct dw_hdmi_dev *dw_dev, u8 cmd, u16 val)
{
	u32 in = (cmd << 16) | val;
	u16 out = 0;
	u8 i;

	for (i = 0; i < 16; i++) {
		hdmi_phy_jtag_send_pulse(dw_dev, 0, in & 0x1);
		out |= (hdmi_readl(dw_dev, DW_HDMI_JTAG_TAP_OUT) & 0x1) << i;
		in >>= 1;
	}

	hdmi_phy_jtag_send_pulse(dw_dev, 0, in & 0x1);
	in >>= 1;
	hdmi_phy_jtag_send_pulse(dw_dev, 1, in & 0x1);

	out |= (hdmi_readl(dw_dev, DW_HDMI_JTAG_TAP_OUT) & 0x1) << ++i;
	return out;
}

static void hdmi_phy_jtag_idle(struct dw_hdmi_dev *dw_dev)
{
	hdmi_phy_jtag_send_pulse(dw_dev, 1, 0);
	hdmi_phy_jtag_send_pulse(dw_dev, 0, 0);
}

static void hdmi_phy_jtag_init(struct dw_hdmi_dev *dw_dev, u8 addr)
{
	u8 i;

	hdmi_writel(dw_dev, addr, DW_HDMI_JTAG_ADDR);

	/* reset */
	hdmi_writel(dw_dev, 0x10, DW_HDMI_JTAG_TAP_IN);
	hdmi_writel(dw_dev, 0x0, DW_HDMI_JTAG_CONF);
	hdmi_writel(dw_dev, 0x1, DW_HDMI_JTAG_CONF);
	hdmi_phy_jtag_send_pulse(dw_dev, 0, 0);

	/* soft reset */
	for (i = 0; i < 5; i++)
		hdmi_phy_jtag_send_pulse(dw_dev, 1, 0);
	hdmi_phy_jtag_send_pulse(dw_dev, 0, 0);

	/* set slave address */
	hdmi_phy_jtag_shift_ir(dw_dev);
	for (i = 0; i < 7; i++) {
		hdmi_phy_jtag_send_pulse(dw_dev, 0, addr & 0x1);
		addr >>= 1;
	}
	hdmi_phy_jtag_send_pulse(dw_dev, 1, addr & 0x1);
	hdmi_phy_jtag_idle(dw_dev);
}

static void hdmi_phy_jtag_write(struct dw_hdmi_dev *dw_dev, u16 val, u16 addr)
{
	hdmi_phy_jtag_shift_dr(dw_dev);
	hdmi_phy_jtag_send(dw_dev, DW_HDMI_JTAG_TAP_ADDR_CMD, addr << 8);
	hdmi_phy_jtag_idle(dw_dev);
	hdmi_phy_jtag_shift_dr(dw_dev);
	hdmi_phy_jtag_send(dw_dev, DW_HDMI_JTAG_TAP_WRITE_CMD, val);
	hdmi_phy_jtag_idle(dw_dev);
}

static u16 hdmi_phy_jtag_read(struct dw_hdmi_dev *dw_dev, u16 addr)
{
	u16 val;

	hdmi_phy_jtag_shift_dr(dw_dev);
	hdmi_phy_jtag_send(dw_dev, DW_HDMI_JTAG_TAP_ADDR_CMD, addr << 8);
	hdmi_phy_jtag_idle(dw_dev);
	hdmi_phy_jtag_shift_dr(dw_dev);
	val = hdmi_phy_jtag_send(dw_dev, DW_HDMI_JTAG_TAP_READ_CMD, 0xFFFF);
	hdmi_phy_jtag_idle(dw_dev);

	return val;
}

static void dw_hdmi_phy_write(void *arg, u16 val, u16 addr)
{
	struct dw_hdmi_dev *dw_dev = arg;
	u16 rval;

	hdmi_phy_jtag_init(dw_dev, dw_dev->config->phy->jtag_addr);
	hdmi_phy_jtag_write(dw_dev, val, addr);
	rval = hdmi_phy_jtag_read(dw_dev, addr);

	if (rval != val) {
		dev_err(dw_dev->dev,
			"JTAG read-back failed: expected=0x%x, got=0x%x\n",
			val, rval);
	}
}

static u16 dw_hdmi_phy_read(void *arg, u16 addr)
{
	struct dw_hdmi_dev *dw_dev = arg;

	hdmi_phy_jtag_init(dw_dev, dw_dev->config->phy->jtag_addr);
	return hdmi_phy_jtag_read(dw_dev, addr);
}

static void dw_hdmi_phy_reset(void *arg, int enable)
{
	struct dw_hdmi_dev *dw_dev = arg;

	hdmi_mask_writel(dw_dev, enable, DW_HDMI_PHY_CTRL,
			 DW_HDMI_PHYRESET_OFFSET,
			 DW_HDMI_PHYRESET_MASK);
}

static void dw_hdmi_phy_pddq(void *arg, int enable)
{
	struct dw_hdmi_dev *dw_dev = arg;

	hdmi_mask_writel(dw_dev, enable, DW_HDMI_PHY_CTRL,
			 DW_HDMI_PHYPDDQ_OFFSET,
			 DW_HDMI_PHYPDDQ_MASK);
}

static void dw_hdmi_phy_svsmode(void *arg, int enable)
{
	struct dw_hdmi_dev *dw_dev = arg;

	hdmi_mask_writel(dw_dev, enable, DW_HDMI_PHY_CTRL,
			 DW_HDMI_PHYSVSRETMODEZ_OFFSET,
			 DW_HDMI_PHYSVSRETMODEZ_MASK);
}

static void dw_hdmi_zcal_reset(void *arg)
{
	struct dw_hdmi_dev *dw_dev = arg;

	if (dw_dev->config->dw_zcal_reset)
		dw_dev->config->dw_zcal_reset(dw_dev->config->dw_zcal_arg);
}

static bool dw_hdmi_zcal_done(void *arg)
{
	struct dw_hdmi_dev *dw_dev = arg;
	void __iomem *zcal_arg = dw_dev->config->dw_zcal_arg;

	if (dw_dev->config->dw_zcal_done)
		return dw_dev->config->dw_zcal_done(zcal_arg);

	return false;
}

static bool dw_hdmi_tmds_valid(void *arg)
{
	struct dw_hdmi_dev *dw_dev = arg;

	return (hdmi_readl(dw_dev, DW_HDMI_PLL_LCK_STS) & DW_HDMI_PLL_LOCKED);
}

static bool dw_hdmi_audio_valid(void *arg)
{
	struct dw_hdmi_dev *dw_dev = arg;

	return ((hdmi_readl(dw_dev, DW_HDMI_AUD_PLL_CTRL) &
			DW_HDMI_PLL_LOCK_STABLE_MASK) != 0);
}

static const struct dw_phy_funcs dw_hdmi_phy_funcs = {
	.write = dw_hdmi_phy_write,
	.read = dw_hdmi_phy_read,
	.reset = dw_hdmi_phy_reset,
	.pddq = dw_hdmi_phy_pddq,
	.svsmode = dw_hdmi_phy_svsmode,
	.zcal_reset = dw_hdmi_zcal_reset,
	.zcal_done = dw_hdmi_zcal_done,
	.tmds_valid = dw_hdmi_tmds_valid,
};

static const struct of_device_id dw_hdmi_supported_phys[] = {
	{ .compatible = "snps,dw-hdmi-phy-e405", .data = DW_PHY_E40X_DRVNAME, },
	{ .compatible = "snps,dw-hdmi-phy-e406", .data = DW_PHY_E40X_DRVNAME, },
	{ },
};

static struct device_node *
dw_hdmi_get_phy_of_node(struct dw_hdmi_dev *dw_dev,
			const struct of_device_id **found_id)
{
	struct device_node *child = NULL;
	const struct of_device_id *id;

	for_each_child_of_node(dw_dev->of_node, child) {
		id = of_match_node(dw_hdmi_supported_phys, child);
		if (id)
			break;
	}

	if (!id)
		return NULL;
	if (found_id)
		*found_id = id;

	return child;
}

static bool dw_hdmi_has_dt(struct dw_hdmi_dev *dw_dev)
{
	const struct of_device_id *of_id;

	if (!dw_dev->of_node ||
	    !dw_hdmi_get_phy_of_node(dw_dev, &of_id) ||
	    !of_id || !of_id->data) {
		return false;
	}

	return true;
}

static const struct dw_hdmi_phy_cfg {
	const char *drvname;
	const struct dw_phy_funcs *funcs;
	int (*extra_init)(struct dw_hdmi_dev *dw_dev);
} dw_hdmi_phys[] = {
	{
		.drvname = DW_PHY_E40X_DRVNAME,
		.funcs = &dw_hdmi_phy_funcs,
	},
};

static int dw_hdmi_phy_init_dt(struct dw_hdmi_dev *dw_dev)
{
	const struct dw_hdmi_phy_cfg *phy_config = NULL;
	struct dw_phy_pdata *phy = &dw_dev->phy_config;
	struct of_dev_auxdata lookup = { };
	const struct of_device_id *of_id;
	struct device_node *child;
	const char *drvname;
	unsigned int i;
	int ret;

	child = dw_hdmi_get_phy_of_node(dw_dev, &of_id);
	if (!child || !of_id || !of_id->data) {
		dev_err(dw_dev->dev, "no supported PHY found in DT\n");
		return -EINVAL;
	}

	drvname = of_id->data;
	for (i = 0; i < ARRAY_SIZE(dw_hdmi_phys); i++) {
		if (!strcmp(dw_hdmi_phys[i].drvname, drvname))
			phy_config = &dw_hdmi_phys[i];
	}

	if (!phy_config) {
		dev_err(dw_dev->dev, "failed to find PHY configuration\n");
		return -EINVAL;
	}

	if (phy_config->extra_init) {
		ret = phy_config->extra_init(dw_dev);
		if (ret)
			return ret;
	}

	phy->funcs = phy_config->funcs;
	phy->funcs_arg = dw_dev;

	lookup.compatible = (char *)of_id->compatible;
	lookup.platform_data = phy;

	request_module(drvname);

	ret = of_platform_populate(dw_dev->of_node, NULL, &lookup, dw_dev->dev);
	if (ret) {
		dev_err(dw_dev->dev, "failed to populate PHY driver\n");
		return ret;
	}

	return 0;
}

static int dw_hdmi_phy_init_pd(struct dw_hdmi_dev *dw_dev)
{
	const char *drv_name = dw_dev->config->phy->drv_name;
	const struct dw_hdmi_phy_cfg *phy_config = NULL;
	struct platform_device_info pdevinfo;
	unsigned int i;
	int ret;

	memset(&pdevinfo, 0, sizeof(pdevinfo));

	for (i = 0; i < ARRAY_SIZE(dw_hdmi_phys); i++) {
		if (!strcmp(dw_hdmi_phys[i].drvname, drv_name))
			phy_config = &dw_hdmi_phys[i];
	}

	if (!phy_config) {
		dev_err(dw_dev->dev, "failed to find PHY configuration\n");
		return -EINVAL;
	}

	if (phy_config->extra_init) {
		ret = phy_config->extra_init(dw_dev);
		if (ret)
			return ret;
	}

	dw_dev->phy_config.version = dw_dev->config->phy->version;
	dw_dev->phy_config.cfg_clk = dw_dev->config->phy->cfg_clk;
	dw_dev->phy_config.funcs = phy_config->funcs;
	dw_dev->phy_config.funcs_arg = dw_dev;

	pdevinfo.parent = dw_dev->dev;
	pdevinfo.id = PLATFORM_DEVID_NONE;
	pdevinfo.name = drv_name;
	pdevinfo.data = &dw_dev->phy_config;
	pdevinfo.size_data = sizeof(dw_dev->phy_config);
	pdevinfo.dma_mask = DMA_BIT_MASK(32);

	request_module(pdevinfo.name);

	dw_dev->phy_pdev = platform_device_register_full(&pdevinfo);
	if (IS_ERR(dw_dev->phy_pdev)) {
		dev_err(dw_dev->dev, "failed to register PHY device\n");
		return PTR_ERR(dw_dev->phy_pdev);
	}

	return 0;
}

static int dw_hdmi_phy_init(struct dw_hdmi_dev *dw_dev)
{
	int ret;

	if (dw_hdmi_has_dt(dw_dev)) {
		/* init PHY based on device tree */
		ret = dw_hdmi_phy_init_dt(dw_dev);
	} else {
		/* init PHY based on platform device */
		ret = dw_hdmi_phy_init_pd(dw_dev);
	}

	/* get the HDMI PHY reference */
	dw_dev->phy = devm_phy_get(dw_dev->dev, "hdmi-phy");
	if (IS_ERR(dw_dev->phy)) {
		if (PTR_ERR(dw_dev->phy) != -EPROBE_DEFER)
			dev_err(dw_dev->dev, "Couldn't get the HDMI PHY\n");
		return PTR_ERR(dw_dev->phy);
	}

	return ret;
}

static void dw_hdmi_phy_exit(struct dw_hdmi_dev *dw_dev)
{
	if (dw_hdmi_has_dt(dw_dev)) {
		/* exit PHY based on device tree */
		of_platform_depopulate(dw_dev->dev);
	} else {
		/* exit PHY based on platform device */
		if (!IS_ERR(dw_dev->phy_pdev))
			platform_device_unregister(dw_dev->phy_pdev);
	}
}

static int dw_hdmi_phy_eq_init(struct dw_hdmi_dev *dw_dev, u16 acq, bool force)
{
	union phy_configure_opts opts;
	struct phy_configure_opts_hdmi *hdmi_opts = &opts.hdmi;
	int ret = 0;

	/* load the required options for calibration */
	hdmi_opts->calibration_acq = acq;
	hdmi_opts->calibration_force = force;
	/* to avoid other reconfigurations when is to calibrate */
	hdmi_opts->set_color_depth = 0;
	hdmi_opts->set_tmds_bit_clock_ratio = 0;
	hdmi_opts->set_scrambling = 0;

	/* set PHY configuration */
	ret = phy_configure(dw_dev->phy, &opts);
	if (ret) {
		dev_err(dw_dev->dev, "%s: Couldn't PHY configure (err: %d)\n",
			__func__, ret);
		goto err;
	}

	/* call PHY calibrate */
	ret = phy_calibrate(dw_dev->phy);
	if (ret) {
		dev_err(dw_dev->dev, "%s: Couldn't PHY calibrate (err: %d)\n",
			__func__, ret);
		goto err;
	}

err:
	return ret;
}

static int dw_hdmi_phy_config(struct dw_hdmi_dev *dw_dev,
			      unsigned char color_depth, bool hdmi2,
			      bool scrambling)
{
	union phy_configure_opts opts;
	struct phy_configure_opts_hdmi *hdmi_opts = &opts.hdmi;
	int ret = 0;

	hdmi_mask_writel(dw_dev, 0x1, DW_HDMI_CBUSIOCTRL,
			 DW_HDMI_DATAPATH_CBUSZ_OFFSET,
			 DW_HDMI_DATAPATH_CBUSZ_MASK);
	hdmi_mask_writel(dw_dev, 0x1, DW_HDMI_CBUSIOCTRL,
			 DW_HDMI_CBUS_SVSRETMODEZ_OFFSET,
			 DW_HDMI_CBUS_SVSRETMODEZ_MASK);
	hdmi_mask_writel(dw_dev, 0x1, DW_HDMI_CBUSIOCTRL,
			 DW_HDMI_CBUS_PDDQ_OFFSET,
			 DW_HDMI_CBUS_PDDQ_MASK);
	hdmi_mask_writel(dw_dev, 0x1, DW_HDMI_CBUSIOCTRL,
			 DW_HDMI_CBUS_RESET_OFFSET,
			 DW_HDMI_CBUS_RESET_MASK);

	/* load the required options for power on */
	hdmi_opts->color_depth = color_depth;
	hdmi_opts->tmds_bit_clock_ratio = hdmi2;
	hdmi_opts->scrambling = scrambling;
	/* to avoid color depth reconfiguration before the power on */
	hdmi_opts->set_color_depth = 0;

	/* set PHY configuration */
	ret = phy_configure(dw_dev->phy, &opts);
	if (ret) {
		dev_err(dw_dev->dev, "%s: Couldn't PHY configure (err: %d)\n",
			__func__, ret);
		goto err;
	}

	/* call PHY power off if needed */
	if (dw_dev->phy->power_count > 0) {
		ret = phy_power_off(dw_dev->phy);
		if (ret) {
			dev_err(dw_dev->dev, "%s: Couldn't PHY power off (err: %d)\n",
				__func__, ret);
		}
	}

	/* call PHY power on */
	ret = phy_power_on(dw_dev->phy);
	if (ret) {
		dev_err(dw_dev->dev, "%s: Couldn't PHY power on (err: %d)\n",
			__func__, ret);
		goto err;
	}

err:
	return ret;
}

static int dw_hdmi_phy_set_color_depth(struct dw_hdmi_dev *dw_dev,
				       u8 color_depth)
{
	union phy_configure_opts opts;
	struct phy_configure_opts_hdmi *hdmi_opts = &opts.hdmi;
	int ret = 0;

	/* load the required options for color depth reconfiguration */
	hdmi_opts->color_depth = color_depth;
	/* to avoid other reconfigurations when is to set only color depth */
	hdmi_opts->set_tmds_bit_clock_ratio = 0;
	hdmi_opts->set_scrambling = 0;

	/* set PHY configuration */
	ret = phy_configure(dw_dev->phy, &opts);
	if (ret) {
		dev_err(dw_dev->dev, "%s: Couldn't PHY configure (err: %d)\n",
			__func__, ret);
		goto err;
	}

err:
	return ret;
}

static void dw_hdmi_event_source_change(struct dw_hdmi_dev *dw_dev)
{
	if (dw_dev->registered)
		v4l2_subdev_notify_event(&dw_dev->sd, &dw_hdmi_event_fmt);
}

static int dw_hdmi_reset_ceavid(struct dw_hdmi_dev *dw_dev)
{
	hdmi_mask_writel(dw_dev, 0x1, DW_HDMI_CEAVID_CONFIG,
			 DW_HDMI_CEAVID_RST_OFFSET,
			 DW_HDMI_CEAVID_RST_MASK);
	msleep(100);
	hdmi_mask_writel(dw_dev, 0x0, DW_HDMI_CEAVID_CONFIG,
			 DW_HDMI_CEAVID_RST_OFFSET,
			 DW_HDMI_CEAVID_RST_MASK);

	return 0;
}

static void dw_hdmi_update_avmute(struct dw_hdmi_dev *dw_dev, u32 mbus_code)
{
	u32 val_l = 0x0, val_h = 0x0;

	switch (mbus_code) {
	case MEDIA_BUS_FMT_YUYV8_1X16: /* YCbCr 4:2:2 */
		val_h = 0x00008000;
		val_l = 0x00008000;
		break;
	case MEDIA_BUS_FMT_YUYV12_1X24: /* YCbCr 4:4:4 */
		val_h = 0x00008000;
		val_l = 0x00008000;
		break;
	case MEDIA_BUS_FMT_YVYU8_1X16: /* YCbCr 4:2:0 */
		val_h = 0x00000f00;
		val_l = 0x0f007f00;
		break;
	case MEDIA_BUS_FMT_RGB888_2X12_BE: /* RGB */
	default:
		break;
	}

	hdmi_writel(dw_dev, val_l, DW_HDMI_VM_CFG_CH_0_1);
	hdmi_mask_writel(dw_dev, val_h, DW_HDMI_VM_CFG_CH2,
			 DW_HDMI_VM_CH2_COL_VALUE_OFFSET,
			 DW_HDMI_VM_CH2_COL_VALUE_MASK);
}

static u32 dw_hdmi_get_mbus_code(struct dw_hdmi_dev *dw_dev)
{
	enum hdmi_colorspace cs = dw_dev->aviif.avi.colorspace;

	switch (cs) {
	case HDMI_COLORSPACE_RGB: /* RGB */
		return MEDIA_BUS_FMT_RGB888_2X12_BE;
	case HDMI_COLORSPACE_YUV422: /* YCbCr 4:2:2 */
		return MEDIA_BUS_FMT_YUYV8_1X16;
	case HDMI_COLORSPACE_YUV444: /* YCbCr 4:4:4 */
		return MEDIA_BUS_FMT_YUYV12_1X24;
	case HDMI_COLORSPACE_YUV420: /* YCbCr 4:2:0 */
		return MEDIA_BUS_FMT_YVYU8_1X16;
	default:
		return MEDIA_BUS_FMT_RGB888_2X12_BE;
	}
}

static u8 dw_hdmi_infoframe_checksum(u8 *ptr, size_t size)
{
	u8 csum = 0;
	size_t i;

	for (i = 0; i < size; i++)
		csum += ptr[i];
	return 256 - csum;
}

static void dw_hdmi_get_raw_infoframe_avi(struct dw_hdmi_dev *dw_dev,
					  struct dw_hdmi_infoframe_cfg *fcfg)
{
	u32 pb, ph = hdmi_readl(dw_dev, fcfg->header_addr);
	union hdmi_infoframe *frame = fcfg->frame;
	u8 packet_hlen = sizeof(fcfg->header);
	u8 packet_len = (ph >> 8) & 0xff;
	unsigned int i, j, pos = 0;
	u8 packet[35] = {0};

	dev_dbg(dw_dev->dev, "%s[%s]: packet_header=0x%x\n", __func__,
		fcfg->desc, ph);

	if (!ph) /* Fail silently if there is no packet */
		return;

	if ((packet_len + packet_hlen + 1) > sizeof(packet)) {
		dev_dbg(dw_dev->dev, "%s: invalid length\n", __func__);
		goto out;
	}

	memcpy(packet, fcfg->header, packet_hlen);
	packet[2] = packet_len; /* Replace fake header size by real header */
	pos += packet_hlen + 1;

	for (i = 0; i < fcfg->payload_len; i++) {
		j = 0;

		/* specific for AVI */
		if (fcfg->header[0] == HDMI_INFOFRAME_TYPE_AVI && i == 1) {
			/* read YQx, CNx, PRx from AVI PH register */
			u32 offset = DW_HDMI_PIX_REP_FACTOR_OFFSET;
			u32 mask = DW_HDMI_QUANT_RANGE_MASK |
				DW_HDMI_CONTENT_TYPE_MASK |
				DW_HDMI_PIX_REP_FACTOR_MASK;

			pb = hdmi_mask_readl(dw_dev, fcfg->header_addr, offset,
					     mask);
			packet[pos++] = pb & 0xff;
			j = 1;
		}

		/* the registers are aligned with the standard raw packet */
		pb = hdmi_readl(dw_dev, fcfg->payload_addr + 4 * i);
		for (; j < 4; j++) {
			if (pos >= (packet_len + packet_hlen + 1))
				break;
			packet[pos++] = (pb >> (8 * j)) & 0xff;
		}
	}

	packet[3] = dw_hdmi_infoframe_checksum(packet, packet_len +
			packet_hlen + 1);

	/*print all packet bytes */
	for (j = 0; j < pos; j++) {
		dev_dbg(dw_dev->dev, "%s[%s]: packet=0x%x\n", __func__,
			fcfg->desc, packet[j]);
	}

	if (hdmi_infoframe_unpack(frame, packet, sizeof(packet))) {
		dev_dbg(dw_dev->dev, "%s[%s]: failed to unpack\n",
			__func__, fcfg->desc);
		goto out;
	}

	return;
out:
	dev_err(dw_dev->dev, "[INVALID INFOFRAME]\n");
}

static void dw_hdmi_get_raw_infoframe_aud(struct dw_hdmi_dev *dw_dev,
					  struct dw_hdmi_infoframe_cfg *fcfg)
{
	u32 pb, ph = hdmi_readl(dw_dev, fcfg->header_addr);
	union hdmi_infoframe *frame = fcfg->frame;
	u8 packet_hlen = sizeof(fcfg->header);
	u8 packet_len = (ph >> 8) & 0xff;
	unsigned int i, j, pos = 0;
	u8 packet[35] = {0};

	dev_dbg(dw_dev->dev, "%s[%s]: packet_header=0x%x\n", __func__,
		fcfg->desc, ph);

	if (!ph) /* Fail silently if there is no packet */
		return;

	if ((packet_len + packet_hlen + 1) > sizeof(packet)) {
		dev_dbg(dw_dev->dev, "%s: invalid length\n", __func__);
		goto out;
	}

	memcpy(packet, fcfg->header, packet_hlen);
	packet[2] = packet_len; /* Replace fake header size by real header */
	pos += packet_hlen + 1;

	for (i = 0; i < fcfg->payload_len; i++) {
		j = 0;

		/* specific for audio */
		if (fcfg->header[0] == HDMI_INFOFRAME_TYPE_AUDIO && i == 1) {
			pb = hdmi_readl(dw_dev, fcfg->payload_addr + 4 * i);
			packet[pos++] = (pb |
			    ((pb & DW_HDMI_LFE_PLAYBACK_LEVEL_MASK)
			    >> DW_HDMI_LFE_PLAYBACK_LEVEL_OFFSET)) &
			    0xff;
			break;
		}

		/* the registers are aligned with the standard raw packet */
		pb = hdmi_readl(dw_dev, fcfg->payload_addr + 4 * i);
		for (; j < 4; j++) {
			if (pos >= (packet_len + packet_hlen + 1))
				break;
			packet[pos++] = (pb >> (8 * j)) & 0xff;
		}
	}

	packet[3] = dw_hdmi_infoframe_checksum(packet, packet_len +
			packet_hlen + 1);

	/*print all packet bytes */
	for (j = 0; j < pos; j++) {
		dev_dbg(dw_dev->dev, "%s[%s]: packet=0x%x\n", __func__,
			fcfg->desc, packet[j]);
	}

	if (hdmi_infoframe_unpack(frame, packet, sizeof(packet))) {
		dev_dbg(dw_dev->dev, "%s[%s]: failed to unpack\n",
			__func__, fcfg->desc);
		goto out;
	}

	return;
out:
	dev_err(dw_dev->dev, "[INVALID INFOFRAME]\n");
}

static void dw_hdmi_get_raw_infoframe_vs(struct dw_hdmi_dev *dw_dev,
					 struct dw_hdmi_infoframe_cfg *fcfg)
{
	u32 pb, ph = hdmi_mask_readl(dw_dev, DW_HDMI_PDEC_VSI_ST1,
				     DW_HDMI_LENGTH_OFFSET,
				     DW_HDMI_LENGTH_MASK);
	union hdmi_infoframe *frame = fcfg->frame;
	u8 packet_hlen = sizeof(fcfg->header);
	unsigned int j, pos = 0;
	u8 packet[35] = {0};
	u8 packet_len = ph;

	dev_dbg(dw_dev->dev, "%s[%s]: packet_header=0x%x\n", __func__,
		fcfg->desc, ph);

	if (!ph) /* Fail silently if there is no packet */
		return;

	if ((packet_len + packet_hlen + 1) > sizeof(packet)) {
		dev_dbg(dw_dev->dev, "%s: invalid length\n", __func__);
		goto out;
	}

	memcpy(packet, fcfg->header, packet_hlen);
	packet[2] = packet_len; /* Replace fake header size by real header */
	pos += packet_hlen + 1;

	/* 24bit IEEE Registration identifier */
	pb = hdmi_mask_readl(dw_dev, DW_HDMI_PDEC_VSI_ST0,
			     DW_HDMI_IEEE_REG_ID_OFFSET,
			     DW_HDMI_IEEE_REG_ID_MASK);
	for (j = 0; j < 3; j++) {
		if (pos >= (packet_len + packet_hlen + 1))
			break;
		packet[pos++] = (pb >> (8 * j)) & 0xff;
	}

	/* HDMI video format */
	pb = hdmi_mask_readl(dw_dev, DW_HDMI_PDEC_VSI_ST1,
			     DW_HDMI_HDMI_VIDEO_FORMAT_OFFSET,
			     DW_HDMI_HDMI_VIDEO_FORMAT_MASK);
	packet[pos++] = (pb << DW_HDMI_HDMI_VIDEO_FORMAT_OFFSET) &
			0xff;

	/* HDMI vic */
	pb = hdmi_mask_readl(dw_dev, DW_HDMI_PDEC_VSI_ST1,
			     DW_HDMI_HDMI_VIC_OFFSET,
			     DW_HDMI_HDMI_VIC_MASK);
	packet[pos++] = pb & 0xff;

	/* 3d structure */
	pb = hdmi_mask_readl(dw_dev, DW_HDMI_PDEC_VSI_ST1,
			     DW_HDMI_H3D_STRUCTURE_OFFSET,
			     DW_HDMI_H3D_STRUCTURE_MASK);
	packet[pos++] = (pb << 4) & 0xff;

	/* 3d ext data */
	pb = hdmi_mask_readl(dw_dev, DW_HDMI_PDEC_VSI_ST1,
			     DW_HDMI_H3D_EXT_DATA_OFFSET,
			     DW_HDMI_H3D_EXT_DATA_MASK);
	packet[pos++] = (pb << 4) & 0xff;

	packet[3] = dw_hdmi_infoframe_checksum(packet, packet_len +
			packet_hlen + 1);

	/*print all packet bytes */
	for (j = 0; j < pos; j++) {
		dev_dbg(dw_dev->dev, "%s[%s]: packet=0x%x\n", __func__,
			fcfg->desc, packet[j]);
	}

	if (hdmi_infoframe_unpack(frame, packet, sizeof(packet))) {
		dev_dbg(dw_dev->dev, "%s[%s]: failed to unpack\n",
			__func__, fcfg->desc);
		goto out;
	}

	return;
out:
	dev_err(dw_dev->dev, "[INVALID INFOFRAME]\n");
}

static void dw_hdmi_get_raw_infoframe(struct dw_hdmi_dev *dw_dev,
				      struct dw_hdmi_infoframe_cfg *fcfg)
{
	switch (fcfg->header[0]) {
	case HDMI_INFOFRAME_TYPE_AVI:
		dw_hdmi_get_raw_infoframe_avi(dw_dev, fcfg);
		break;
	case HDMI_INFOFRAME_TYPE_SPD:
		dev_dbg(dw_dev->dev,
			"%s[%s]: not processed infoframe packet type %d\n",
			__func__, fcfg->desc, fcfg->header[0]);
		break;
	case HDMI_INFOFRAME_TYPE_AUDIO:
		dw_hdmi_get_raw_infoframe_aud(dw_dev, fcfg);
		break;
	case HDMI_INFOFRAME_TYPE_VENDOR:
		dw_hdmi_get_raw_infoframe_vs(dw_dev, fcfg);
		break;
	case HDMI_INFOFRAME_TYPE_DRM:
		dev_dbg(dw_dev->dev,
			"%s[%s]: not processed infoframe packet type %d\n",
			__func__, fcfg->desc, fcfg->header[0]);
		break;
	default:
		dev_dbg(dw_dev->dev,
			"%s[%s]: invalid infoframe packet type %d\n",
			__func__, fcfg->desc, fcfg->header[0]);
		break;
	}
}

/* Forward declaration needed because of color encoding change */
static void dw_hdmi_power_off(struct dw_hdmi_dev *dw_dev);
static void dw_hdmi_controller_power_off(struct dw_hdmi_dev *dw_dev);
static int dw_hdmi_power_on(struct dw_hdmi_dev *dw_dev, unsigned int input);

static void dw_hdmi_get_infoframes(struct dw_hdmi_dev *dw_dev)
{
	struct dw_hdmi_infoframe_cfg ifs[] = {
		{
			.desc = "AVI",
			.header = {HDMI_INFOFRAME_TYPE_AVI, 2,
				   HDMI_AVI_INFOFRAME_SIZE},
			.header_addr = DW_HDMI_PDEC_AVI_HB,
			.payload_addr = DW_HDMI_PDEC_AVI_PB,
			.payload_len = DW_HDMI_PDEC_AVI_PBLEN,
			.frame = &dw_dev->aviif,
			.frame_size = sizeof(dw_dev->aviif),
		}, {
			.desc = "Audio",
			.header = {HDMI_INFOFRAME_TYPE_AUDIO, 1,
				   HDMI_AUDIO_INFOFRAME_SIZE},
			.header_addr = DW_HDMI_PDEC_AIF_HB,
			.payload_addr = DW_HDMI_PDEC_AIF_PB0,
			.payload_len = DW_HDMI_PDEC_AIF_PBLEN,
			.frame = &dw_dev->audioif,
			.frame_size = sizeof(dw_dev->audioif),
		}, {
			.desc = "Vendor Specific",
			.header = {HDMI_INFOFRAME_TYPE_VENDOR, 1,
				   HDMI_VENDOR_INFOFRAME_SIZE},
			.frame = &dw_dev->vsif,
			.frame_size = sizeof(dw_dev->vsif),
		},
	};
	union hdmi_vendor_any_infoframe *vendor;
	struct hdmi_avi_infoframe *avi;
	unsigned int i;
	u32 old_mbus;

	for (i = 0; i < ARRAY_SIZE(ifs); i++) {
		memset(ifs[i].frame, 0, ifs[i].frame_size);
		dw_hdmi_get_raw_infoframe(dw_dev, &ifs[i]);
	}

	/* Update color space */
	old_mbus = dw_dev->mbus_code;
	dw_dev->mbus_code = dw_hdmi_get_mbus_code(dw_dev);
	if (dw_dev->mbus_code != old_mbus && is_on(dw_dev)) {
		dw_hdmi_power_off(dw_dev);
		if (has_signal(dw_dev, dw_dev->configured_input))
			dw_hdmi_power_on(dw_dev, dw_dev->configured_input);
	}

	/* Update AVMute value */
	dw_hdmi_update_avmute(dw_dev, dw_dev->mbus_code);

	vendor = &dw_dev->vsif.vendor;
	avi = &dw_dev->aviif.avi;

	/*
	 * Update current VIC: When transmitting any extended video format
	 * indicated through use of the HDMI_VIC field in the HDMI Vendor
	 * Specific InfoFrame or any other format which is not described in
	 * the above cases, an HDMI Source shall set the AVI InfoFrame VIC
	 * field to zero.
	 */
	if (vendor->hdmi.vic && !avi->video_code) {
		dw_dev->current_vic = vendor->hdmi.vic;
		dw_dev->current_vic_is_4k = true;
	} else {
		dw_dev->current_vic = avi->video_code;
		dw_dev->current_vic_is_4k = false;
	}
}

static int dw_hdmi_wait_phy_lock_poll(struct dw_hdmi_dev *dw_dev)
{
	int timeout = dw_dev->tmds_valid_wait_count;

	while (!dw_hdmi_tmds_valid(dw_dev) && timeout-- && !dw_dev->force_off)
		usleep_range(5000, 10000);

	if (!dw_hdmi_tmds_valid(dw_dev))
		return -ETIMEDOUT;

	return 0;
}

static void dw_hdmi_reset_datapath(struct dw_hdmi_dev *dw_dev)
{
	u32 val = DW_HDMI_TMDS_SWRESET |
		DW_HDMI_HDCP_SWRESET |
		DW_HDMI_VID_SWRESET |
		DW_HDMI_PIXEL_SWRESET |
		DW_HDMI_CEC_SWRESET |
		DW_HDMI_AUD_SWRESET |
		DW_HDMI_BUS_SWRESET |
		DW_HDMI_HDMI_SWRESET |
		DW_HDMI_MODET_SWRESET;

	hdmi_writel(dw_dev, val & dw_dev->reset_datapath_enable,
		    DW_HDMI_DMI_SW_RST);
}

static void dw_hdmi_reset_audio(struct dw_hdmi_dev *dw_dev)
{
	hdmi_writel(dw_dev, DW_HDMI_AUD_SWRESET, DW_HDMI_DMI_SW_RST);
}

static void dw_hdmi_restart_audio_fifo(struct dw_hdmi_dev *dw_dev)
{
	hdmi_mask_writel(dw_dev, 0x1, DW_HDMI_AUD_FIFO_CTRL,
			 DW_HDMI_AFIF_INIT_OFFSET,
			 DW_HDMI_AFIF_INIT_MASK);
	hdmi_mask_writel(dw_dev, 0x0, DW_HDMI_AUD_FIFO_CTRL,
			 DW_HDMI_AFIF_INIT_OFFSET,
			 DW_HDMI_AFIF_INIT_MASK);
}

static int dw_hdmi_wait_audio_lock_poll(struct dw_hdmi_dev *dw_dev)
{
	int timeout = 10;

	while (!dw_hdmi_audio_valid(dw_dev) && timeout-- && !dw_dev->force_off)
		usleep_range(5000, 10000);

	if (!dw_hdmi_audio_valid(dw_dev))
		return -ETIMEDOUT;

	return 0;
}

static int dw_hdmi_config_audio(struct dw_hdmi_dev *dw_dev)
{
	int ret;

	ret = dw_hdmi_wait_audio_lock_poll(dw_dev);
	if (ret) {
		dev_err(dw_dev->dev, "failed to wait for audio pll lock\n");
		return ret;
	}

	/* trigger offset for N and CTS interrupts */
	hdmi_mask_writel(dw_dev, 0x05, DW_HDMI_PDEC_ACRM_CTRL,
			 DW_HDMI_DELTACTS_IRQTRIG_OFFSET,
			 DW_HDMI_DELTACTS_IRQTRIG_MASK);

	/* Config */
	hdmi_mask_writel(dw_dev, 0x01, DW_HDMI_AUD_MUTE_CTRL,
			 DW_HDMI_AUD_MUTE_SEL_OFFSET,
			 DW_HDMI_AUD_MUTE_SEL_MASK);

	/* enable all outputs and select 16-bit for I2S */
	hdmi_writel(dw_dev, 0x00, DW_HDMI_AUD_SAO_CTRL);

	/* Start */
	dw_hdmi_restart_audio_fifo(dw_dev);
	return 0;
}

static void dw_hdmi_config_packet(struct dw_hdmi_dev *dw_dev)
{
	hdmi_mask_writel(dw_dev, 0x1, DW_HDMI_PDEC_CTRL,
			 DW_HDMI_PFIFO_STORE_FILTER_EN_OFFSET,
			 DW_HDMI_PFIFO_STORE_FILTER_EN_MASK);

	hdmi_mask_writel(dw_dev, 0x1, DW_HDMI_PDEC_ASP_CTRL,
			 DW_HDMI_AUTO_VMUTE_OFFSET,
			 DW_HDMI_AUTO_VMUTE_MASK);

	hdmi_mask_writel(dw_dev, 0x1, DW_HDMI_PDEC_ASP_CTRL,
			 DW_HDMI_AUTO_SPFLAT_MUTE_OFFSET,
			 DW_HDMI_AUTO_SPFLAT_MUTE_MASK);

	hdmi_mask_writel(dw_dev, 0x1, DW_HDMI_PDEC_CTRL,
			 DW_HDMI_PD_FIFO_CLR_OFFSET,
			 DW_HDMI_PD_FIFO_CLR_MASK);
}

static void dw_hdmi_wait_video_stable(struct dw_hdmi_dev *dw_dev)
{
	/*
	 * Empiric value. Video should be stable way longer before the
	 * end of this sleep time. Though, we can have some video change
	 * interrupts before the video is stable so filter them by sleeping.
	 */
	msleep(dw_dev->video_stable_wait_ms);
}

static void dw_hdmi_enable_ints(struct dw_hdmi_dev *dw_dev)
{
	u32 pdec_ints = 0;

	/* video interrupts */
	hdmi_writel(dw_dev, DW_HDMI_CLK_CHANGE_ISTS | DW_HDMI_PLL_LCK_CHG_ISTS |
			DW_HDMI_DCM_CURRENT_MODE_CHG_ISTS, DW_HDMI_IEN_SET);
	hdmi_writel(dw_dev, (DW_HDMI_VACT_LIN_ISTS | DW_HDMI_HACT_PIX_ISTS),
		    DW_HDMI_MD_IEN_SET);

	/* infoframes interrupts */
	pdec_ints = (DW_HDMI_VSI_CKS_CHG_ISTS |
		     DW_HDMI_DRM_CKS_CHG_ISTS |
		     DW_HDMI_AVI_CKS_CHG_ISTS);

	/* audio interrupts */
	pdec_ints |= (DW_HDMI_AUD_TYPE_CHG_ISTS |
		      DW_HDMI_AIF_CKS_CHG_ISTS |
		      DW_HDMI_ACR_N_CHG_ISTS |
		      DW_HDMI_ACR_CTS_CHG_ISTS |
		      DW_HDMI_GCP_AV_MUTE_CHG_ISTS);

	hdmi_writel(dw_dev, pdec_ints, DW_HDMI_PDEC_IEN_SET);
	hdmi_writel(dw_dev, (DW_HDMI_AFIF_OVERFL_ISTS |
			DW_HDMI_AFIF_UNDERFL_ISTS |
			DW_HDMI_AFIF_THS_PASS_ISTS),
		    DW_HDMI_AUD_FIFO_IEN_SET);
}

static void dw_hdmi_disable_ints(struct dw_hdmi_dev *dw_dev)
{
	hdmi_writel(dw_dev, ~0x0, DW_HDMI_IEN_CLR);
	hdmi_writel(dw_dev, ~0x0, DW_HDMI_MD_IEN_CLR);

	hdmi_writel(dw_dev, ~0x0, DW_HDMI_PDEC_IEN_CLR);
	hdmi_writel(dw_dev, ~0x0, DW_HDMI_AUD_FIFO_IEN_CLR);
}

static void dw_hdmi_clear_ints(struct dw_hdmi_dev *dw_dev)
{
	hdmi_writel(dw_dev, ~0x0, DW_HDMI_ICLR);
	hdmi_writel(dw_dev, ~0x0, DW_HDMI_MD_ICLR);

	hdmi_writel(dw_dev, ~0x0, DW_HDMI_PDEC_ICLR);
	hdmi_writel(dw_dev, ~0x0, DW_HDMI_AUD_FIFO_ICLR);
}

static u32 dw_hdmi_get_int_val(struct dw_hdmi_dev *dw_dev, u32 ists, u32 ien)
{
	return hdmi_readl(dw_dev, ists) & hdmi_readl(dw_dev, ien);
}

static u8 dw_hdmi_get_curr_vic(struct dw_hdmi_dev *dw_dev, bool *is_hdmi_vic)
{
	u8 vic = hdmi_mask_readl(dw_dev, DW_HDMI_PDEC_AVI_PB,
				 DW_HDMI_VID_IDENT_CODE_OFFSET,
				 DW_HDMI_VID_IDENT_CODE_MASK);

	if (!vic) {
		vic = hdmi_mask_readl(dw_dev, DW_HDMI_PDEC_VSI_PAYLOAD0,
				      DW_HDMI_VSI_PAYLOAD1_HDMI_VIC_OFFSET,
				      DW_HDMI_VSI_PAYLOAD1_HDMI_VIC_MASK);
		if (is_hdmi_vic)
			*is_hdmi_vic = true;
	} else {
		if (is_hdmi_vic)
			*is_hdmi_vic = false;
	}

	return vic;
}

static u32 dw_hdmi_get_evaltime(struct dw_hdmi_dev *dw_dev)
{
	return hdmi_mask_readl(dw_dev, DW_HDMI_CKM_EVLTM,
			       DW_HDMI_EVAL_TIME_OFFSET,
			       DW_HDMI_EVAL_TIME_MASK);
}

static u32 dw_hdmi_get_clkrate(struct dw_hdmi_dev *dw_dev)
{
	return hdmi_mask_readl(dw_dev, DW_HDMI_CKM_RESULT,
			       DW_HDMI_CLKRATE_OFFSET,
			       DW_HDMI_CLKRATE_MASK);
}

static u32 dw_hdmi_get_cts(struct dw_hdmi_dev *dw_dev)
{
	return hdmi_mask_readl(dw_dev, DW_HDMI_PDEC_ACR_CTS,
			       DW_HDMI_CTS_DECODED_OFFSET,
			       DW_HDMI_CTS_DECODED_MASK);
}

static u32 dw_hdmi_get_n(struct dw_hdmi_dev *dw_dev)
{
	return hdmi_mask_readl(dw_dev, DW_HDMI_PDEC_ACR_N,
			       DW_HDMI_N_DECODED_OFFSET,
			       DW_HDMI_N_DECODED_MASK);
}

static u32 dw_hdmi_get_tmds_clk(struct dw_hdmi_dev *dw_dev)
{
	u32 rate = dw_hdmi_get_clkrate(dw_dev);
	u64 tmp = (u64)rate * (u64)dw_dev->config->iref_clk * 1000000;
	u32 evaltime = dw_hdmi_get_evaltime(dw_dev);

	do_div(tmp, evaltime);
	return tmp;
}

static u32 dw_hdmi_get_colordepth(struct dw_hdmi_dev *dw_dev)
{
	u32 dcm = hdmi_mask_readl(dw_dev, DW_HDMI_STS,
				  DW_HDMI_DCM_CURRENT_MODE_OFFSET,
				  DW_HDMI_DCM_CURRENT_MODE_MASK);

	switch (dcm) {
	case 0x4:
		return 24;
	case 0x5:
		return 30;
	case 0x6:
		return 36;
	case 0x7:
		return 48;
	default:
		return 24;
	}
}

static u64 dw_hdmi_get_pixelclk(struct dw_hdmi_dev *dw_dev)
{
	u32 tmds_clk = dw_hdmi_get_tmds_clk(dw_dev);
	u32 cd = dw_hdmi_get_colordepth(dw_dev);
	u32 pix_clk = 0;

	switch (cd) {
	case 24:
		pix_clk = tmds_clk;
		break;
	case 30:
		pix_clk = (tmds_clk * 100) / 125;
		break;
	case 36:
		pix_clk = (tmds_clk * 10) / 15;
		break;
	case 48:
		pix_clk = tmds_clk / 2;
		break;
	default:
		break;
	}

	return pix_clk;
}

static void dw_hdmi_set_input(struct dw_hdmi_dev *dw_dev, u32 input)
{
	hdmi_mask_writel(dw_dev, input, DW_HDMI_PHY_CTRL,
			 DW_HDMI_PORTSELECT_OFFSET,
			 DW_HDMI_PORTSELECT_MASK);
	dw_dev->configured_input = input;
	dw_dev->selected_input = input;
	v4l2_subdev_notify(&dw_dev->sd, DW_HDMI_NOTIFY_INPUT_CHANGED,
			   &dw_dev->configured_input);
}

static void dw_hdmi_enable_hpd(struct dw_hdmi_dev *dw_dev, u32 input_mask)
{
	hdmi_mask_writel(dw_dev, input_mask, DW_HDMI_SETUP_CTRL,
			 DW_HDMI_HOT_PLUG_DETECT_INPUT_X_OFFSET,
			 DW_HDMI_HOT_PLUG_DETECT_INPUT_X_MASK);
	hdmi_mask_writel(dw_dev, 0x1, DW_HDMI_SETUP_CTRL,
			 DW_HDMI_HOT_PLUG_DETECT_OFFSET,
			 DW_HDMI_HOT_PLUG_DETECT_MASK);
}

static void dw_hdmi_disable_hpd(struct dw_hdmi_dev *dw_dev)
{
	hdmi_mask_writel(dw_dev, 0x0, DW_HDMI_SETUP_CTRL,
			 DW_HDMI_HOT_PLUG_DETECT_INPUT_X_OFFSET,
			 DW_HDMI_HOT_PLUG_DETECT_INPUT_X_MASK);
	hdmi_mask_writel(dw_dev, 0x0, DW_HDMI_SETUP_CTRL,
			 DW_HDMI_HOT_PLUG_DETECT_OFFSET,
			 DW_HDMI_HOT_PLUG_DETECT_MASK);
}

static void dw_hdmi_enable_scdc(struct dw_hdmi_dev *dw_dev)
{
	hdmi_mask_writel(dw_dev, 0x1, DW_HDMI_SCDC_CONFIG,
			 DW_HDMI_POWERPROVIDED_OFFSET,
			 DW_HDMI_POWERPROVIDED_MASK);
	hdmi_mask_writel(dw_dev, 0x1, DW_HDMI_IEN_SET,
			 DW_HDMI_SCDCTMDSCFGCHANGE_ISTS_OFFSET,
			 DW_HDMI_SCDCTMDSCFGCHANGE_ISTS_MASK);
}

static void dw_hdmi_disable_scdc(struct dw_hdmi_dev *dw_dev)
{
	hdmi_mask_writel(dw_dev, 0x0, DW_HDMI_SCDC_CONFIG,
			 DW_HDMI_POWERPROVIDED_OFFSET,
			 DW_HDMI_POWERPROVIDED_MASK);
}

static void dw_hdmi_handle_audio_mute_change(struct dw_hdmi_dev *dw_dev);

static void dw_hdmi_enable_hdmi_domain(struct dw_hdmi_dev *dw_dev, bool enable)
{
	hdmi_mask_writel(dw_dev, enable, DW_HDMI_DMI_DISABLE_IF,
			 DW_HDMI_HDMI_ENABLE_OFFSET,
			 DW_HDMI_HDMI_ENABLE_MASK);
}

static int dw_hdmi_initial_config(struct dw_hdmi_dev *dw_dev, u32 input);

static int dw_hdmi_config(struct dw_hdmi_dev *dw_dev, u32 input)
{
	u32 in_state = dw_dev->state;
	u32 cd1 = 0, cd2 = 0;
	int eqret, ret = 0;

	while (1) {
		/* Give up silently if we are forcing off */
		if (dw_dev->force_off) {
			ret = 0;
			goto out;
		}
		/* Give up silently if input has disconnected */
		if (!has_signal(dw_dev, input)) {
			ret = 0;
			goto out;
		}

		switch (dw_dev->state) {
		case HDMI_STATE_POWER_OFF:
			dw_hdmi_disable_ints(dw_dev);
			dw_hdmi_set_state(dw_dev, HDMI_STATE_PHY_CONFIG);
			break;
		case HDMI_STATE_POWER_UP:
			/* when connect the cable */
			dw_hdmi_disable_ints(dw_dev);
			/* reset */
			if (dw_dev->hw_reset_on_hot_plug)
				dw_hdmi_reset(dw_dev);
			/* initial configuration */
			dw_hdmi_initial_config(dw_dev, input);
			dw_hdmi_set_state(dw_dev, HDMI_STATE_PHY_CONFIG);
			break;
		case HDMI_STATE_PHY_CONFIG:
			cd1 = 24;
			dw_dev->is_hdmi2 = is_hdmi2(dw_dev);
			dw_dev->is_scrambled = is_scrambled(dw_dev);
			dw_hdmi_phy_config(dw_dev, cd1, dw_dev->is_hdmi2,
					   dw_dev->is_scrambled);
			dw_hdmi_set_state(dw_dev, HDMI_STATE_HPD);
			break;
		case HDMI_STATE_HPD:
			/* disable HDMI domain to avoid the DCM INTs */
			dw_hdmi_enable_hdmi_domain(dw_dev, false);
			dw_hdmi_enable_scdc(dw_dev);
			dw_hdmi_enable_hpd(dw_dev, dw_dev->input_stat);
			dw_hdmi_set_state(dw_dev, HDMI_STATE_EQUALIZER);
			break;
		case HDMI_STATE_EQUALIZER:
			if (dw_dev->phy_eq_on) {
				bool phy_eq_force = dw_dev->phy_eq_force;

				eqret = dw_hdmi_phy_eq_init(dw_dev, 5,
							    phy_eq_force);
			} else {
				/* Clear equalizer error status if not on */
				eqret = 0;
			}

			ret = dw_hdmi_wait_phy_lock_poll(dw_dev);

			/* Do not force equalizer */
			dw_dev->phy_eq_force = false;

			if (ret || eqret) {
				if (ret || eqret == -ETIMEDOUT) {
					/* No TMDSVALID signal:
					 * - force equalizer
					 */
					dw_dev->phy_eq_force = true;
				}
				break;
			}

			dw_hdmi_set_state(dw_dev, HDMI_STATE_DATAPATH);
			break;
		case HDMI_STATE_DATAPATH:
			dw_hdmi_reset_datapath(dw_dev);
			/* reenable HDMI domain */
			dw_hdmi_enable_hdmi_domain(dw_dev, true);
			dw_hdmi_set_state(dw_dev, HDMI_STATE_VIDEO_UNSTABLE);
			break;
		case HDMI_STATE_VIDEO_UNSTABLE:
			dw_hdmi_wait_video_stable(dw_dev);
			dw_hdmi_set_state(dw_dev, HDMI_STATE_AUDIO);
			break;
		case HDMI_STATE_AUDIO:
			ret = dw_hdmi_config_audio(dw_dev);
			dw_hdmi_config_packet(dw_dev);

			if (in_state != HDMI_STATE_EQUALIZER)
				dw_hdmi_clear_ints(dw_dev);

			dw_hdmi_get_infoframes(dw_dev);

			/* check if there was deep color changes */
			if (cd1) {
				cd2 = dw_hdmi_get_colordepth(dw_dev);
				if (cd1 != cd2)
					dw_hdmi_phy_set_color_depth(dw_dev,
								    cd2);
			}

			/* reset CEA video */
			dw_hdmi_reset_ceavid(dw_dev);

			dw_hdmi_enable_ints(dw_dev);
			dw_hdmi_set_state(dw_dev, HDMI_STATE_POWER_ON);
			break;
		case HDMI_STATE_POWER_ON:
			break;
		default:
			dev_err(dw_dev->dev, "%s called with state (%d)\n",
				__func__, dw_dev->state);
			ret = -EINVAL;
			goto out;
		}

		if (dw_dev->state == HDMI_STATE_POWER_ON) {
			dev_info(dw_dev->dev, "HDMI-RX configured\n");
			dw_hdmi_event_source_change(dw_dev);
			dw_hdmi_handle_audio_mute_change(dw_dev);
			return 0;
		}
	}

out:
	dw_hdmi_set_state(dw_dev, HDMI_STATE_POWER_OFF);
	v4l2_subdev_notify(&dw_dev->sd, DW_HDMI_NOTIFY_IS_OFF, NULL);
	return ret;
}

static int __dw_hdmi_power_on(struct dw_hdmi_dev *dw_dev, u32 input)
{
	unsigned long flags;
	int ret;

	ret = dw_hdmi_config(dw_dev, input);

	spin_lock_irqsave(&dw_dev->lock, flags);
	dw_dev->pending_config = false;
	spin_unlock_irqrestore(&dw_dev->lock, flags);

	return ret;
}

static void dw_hdmi_work_handler(struct work_struct *work)
{
	struct dw_hdmi_dev *dw_dev = container_of(work, struct dw_hdmi_dev,
						  work);

	__dw_hdmi_power_on(dw_dev, dw_dev->configured_input);
}

static int dw_hdmi_power_on(struct dw_hdmi_dev *dw_dev, u32 input)
{
	unsigned long flags;

	spin_lock_irqsave(&dw_dev->lock, flags);
	if (dw_dev->pending_config) {
		spin_unlock_irqrestore(&dw_dev->lock, flags);
		return 0;
	}

	INIT_WORK(&dw_dev->work, dw_hdmi_work_handler);
	dw_dev->configured_input = input;
	dw_dev->pending_config = true;
	queue_work(dw_dev->wq, &dw_dev->work);
	spin_unlock_irqrestore(&dw_dev->lock, flags);
	return 0;
}

static void dw_hdmi_power_off(struct dw_hdmi_dev *dw_dev)
{
	unsigned long flags;

	v4l2_subdev_notify(&dw_dev->sd, DW_HDMI_NOTIFY_IS_OFF, NULL);

	dw_dev->force_off = true;
	flush_workqueue(dw_dev->wq);
	dw_dev->force_off = false;

	spin_lock_irqsave(&dw_dev->lock, flags);
	dw_dev->pending_config = false;
	dw_dev->state = HDMI_STATE_POWER_OFF;
	spin_unlock_irqrestore(&dw_dev->lock, flags);

	/* Reset variables */
	dw_dev->phy_eq_force = true;
	dw_dev->audio_sf = 0;
}

static void dw_hdmi_force_off(struct dw_hdmi_dev *dw_dev)
{
	unsigned long flags;

	v4l2_subdev_notify(&dw_dev->sd, DW_HDMI_NOTIFY_IS_OFF, NULL);

	dw_dev->force_off = true;
	flush_workqueue(dw_dev->wq);
	dw_dev->force_off = false;

	spin_lock_irqsave(&dw_dev->lock, flags);
	dw_dev->pending_config = false;
	spin_unlock_irqrestore(&dw_dev->lock, flags);
}

static void dw_hdmi_power_up(struct dw_hdmi_dev *dw_dev)
{
	dw_hdmi_force_off(dw_dev);
	dw_hdmi_set_state(dw_dev, HDMI_STATE_POWER_UP);

	/* Reset variables */
	dw_dev->phy_eq_force = true;
	dw_dev->audio_sf = 0;
}

static void dw_hdmi_controller_power_off(struct dw_hdmi_dev *dw_dev)
{
	dw_hdmi_force_off(dw_dev);
	dw_hdmi_set_state(dw_dev, HDMI_STATE_EQUALIZER);

	dw_dev->phy_eq_force = false;
	dw_dev->audio_sf = 0;
}

static int dw_hdmi_query_dv_timings(struct v4l2_subdev *sd,
				    struct v4l2_dv_timings *timings);

static void dw_hdmi_handle_video_change(struct dw_hdmi_dev *dw_dev)
{
	struct v4l2_dv_timings timings;

	if (is_on(dw_dev)) {
		dev_dbg(dw_dev->dev, "[VIDEO] video change interrupt\n");
		dw_hdmi_query_dv_timings(&dw_dev->sd, &timings);
		dw_hdmi_controller_power_off(dw_dev);
		dw_hdmi_power_on(dw_dev, dw_dev->configured_input);
	}
}

static u32 dw_hdmi_round_freq(int freq)
{
	static const u32 base_freqs[] = { 32000, 44100, 48000, 0 };
	unsigned int i;

	for (i = 0; base_freqs[i]; i++) {
		if ((freq <= (base_freqs[i] + DW_HDMI_AUDIO_FREQ_RANGE)) &&
		    (freq >= (base_freqs[i] - DW_HDMI_AUDIO_FREQ_RANGE))) {
			return base_freqs[i];
		}
	}

	return 0;
}

static unsigned int dw_hdmi_get_sample_freq(struct dw_hdmi_dev *dw_dev)
{
	u64 tmp;
	u32 cts;
	u32 sf;
	u32 n;

	n = dw_hdmi_get_n(dw_dev);
	cts = dw_hdmi_get_cts(dw_dev);

	if (!n || !cts) {
		dev_dbg(dw_dev->dev, "%s: cts: %d, n: %d\n", __func__, cts, n);
		return 0;
	}

	dev_dbg(dw_dev->dev, "%s: tmds_clk: %d\n",
		__func__, dw_hdmi_get_tmds_clk(dw_dev));

	/* regenerate the audio clock from tmds clock */
	tmp = (u64)dw_hdmi_get_tmds_clk(dw_dev) * (u64)n;
	do_div(tmp, cts);
	do_div(tmp, 128);
	sf = tmp;

	dev_dbg(dw_dev->dev, "%s: sf: %d\n", __func__, sf);
	sf = dw_hdmi_round_freq(sf);
	dev_dbg(dw_dev->dev, "%s: sf(round): %d\n", __func__, sf);

	return sf;
}

static void dw_hdmi_handle_audio_mute_change(struct dw_hdmi_dev *dw_dev)
{
	unsigned long flags;
	unsigned int sf;

	spin_lock_irqsave(&dw_dev->event_lock, flags);
	sf = dw_hdmi_get_sample_freq(dw_dev);
	v4l2_subdev_notify(&dw_dev->sd, DW_HDMI_NOTIFY_AUDIO_CHANGED, &sf);
	dw_dev->audio_sf = sf;
	spin_unlock_irqrestore(&dw_dev->event_lock, flags);
}

static void dw_hdmi_handle_audio_change(struct dw_hdmi_dev *dw_dev,
					u32 afif_stat, u32 pdec_stat)
{
	bool restart = true;

	if (pdec_stat & DW_HDMI_GCP_AV_MUTE_CHG_ISTS) {
		dev_dbg(dw_dev->dev, "[interrupt:audio] AV Mute change\n");
		dw_hdmi_handle_audio_mute_change(dw_dev);
		restart = false;
	}

	if (pdec_stat & DW_HDMI_AUD_TYPE_CHG_ISTS) {
		dev_dbg(dw_dev->dev, "[interrupt:audio] type change\n");
		restart = true;
	}
	if (pdec_stat & DW_HDMI_AIF_CKS_CHG_ISTS) {
		dev_dbg(dw_dev->dev, "[interrupt:audio] aif change\n");
		restart = true;
	}
	if (pdec_stat & DW_HDMI_ACR_N_CHG_ISTS) {
		dev_dbg(dw_dev->dev, "[interrupt:audio] N change\n");
		restart = true;
	}
	if (pdec_stat & DW_HDMI_ACR_CTS_CHG_ISTS) {
		dev_dbg(dw_dev->dev, "[interrupt:audio] CTS change\n");
		restart = true;
	}
	if (afif_stat & DW_HDMI_AFIF_UNDERFL_ISTS) {
		dev_dbg(dw_dev->dev, "[interrupt:audio] fifo underflow\n");
		restart = true;
	}
	if (afif_stat & DW_HDMI_AFIF_OVERFL_ISTS) {
		dev_dbg(dw_dev->dev, "[interrupt:audio] fifo overflow\n");
		restart = true;
	}
	if (afif_stat & DW_HDMI_AFIF_THS_PASS_ISTS) {
		dev_dbg(dw_dev->dev, "[interrupt:audio] TRH Pass\n");
		dw_hdmi_handle_audio_mute_change(dw_dev);
		restart = false;
	}

	if (restart) {
		dw_hdmi_handle_audio_mute_change(dw_dev);
		dw_hdmi_wait_audio_lock_poll(dw_dev);
		dw_hdmi_reset_audio(dw_dev);
		dw_hdmi_restart_audio_fifo(dw_dev);
	}
}

static irqreturn_t dw_hdmi_irq_handler(int irq, void *dev_data)
{
	struct dw_hdmi_dev *dw_dev = dev_data;
	u32 hdmi_ists = dw_hdmi_get_int_val(dw_dev, DW_HDMI_ISTS, DW_HDMI_IEN);
	u32 md_ists = dw_hdmi_get_int_val(dw_dev, DW_HDMI_MD_ISTS,
					  DW_HDMI_MD_IEN);
	u32 pdec_ists = dw_hdmi_get_int_val(dw_dev, DW_HDMI_PDEC_ISTS,
					    DW_HDMI_PDEC_IEN);
	u32 afif_ists = dw_hdmi_get_int_val(dw_dev, DW_HDMI_AUD_FIFO_ISTS,
					    DW_HDMI_AUD_FIFO_IEN);
	u32 cd = 0;

	dw_hdmi_clear_ints(dw_dev);

	/* video handling */
	if (hdmi_ists & DW_HDMI_CLK_CHANGE_ISTS) {
		dev_dbg(dw_dev->dev, "[HDMI] clock rate change\n");
		dw_hdmi_power_off(dw_dev);
		if (has_signal(dw_dev, dw_dev->configured_input))
			dw_hdmi_power_on(dw_dev, dw_dev->configured_input);

		return IRQ_HANDLED;
	}
	if (hdmi_ists & DW_HDMI_PLL_LCK_CHG_ISTS) {
		dev_dbg(dw_dev->dev,
			"[PHY] PLL lock state changed (tmds_valid: %d)\n",
			dw_hdmi_tmds_valid(dw_dev));
		dw_hdmi_power_off(dw_dev);

		if (has_signal(dw_dev, dw_dev->configured_input))
			dw_hdmi_power_on(dw_dev, dw_dev->configured_input);

		return IRQ_HANDLED;
	}
	if (hdmi_ists & DW_HDMI_DCM_CURRENT_MODE_CHG_ISTS) {
		dev_dbg(dw_dev->dev,
			"[HDMI] deep color changed\n");

		cd = dw_hdmi_get_colordepth(dw_dev);
		if (cd)
			dw_hdmi_phy_set_color_depth(dw_dev, cd);

		/* reset CEA video */
		dw_hdmi_reset_ceavid(dw_dev);

		return IRQ_HANDLED;
	}
	if (md_ists || pdec_ists &
	    (DW_HDMI_VSI_CKS_CHG_ISTS |
	     DW_HDMI_DRM_CKS_CHG_ISTS |
	     DW_HDMI_AVI_CKS_CHG_ISTS)) {
		dw_hdmi_handle_video_change(dw_dev);
		dev_dbg(dw_dev->dev,
			"   md_ists: 0x%x, pdec_ists: 0x%x\n",
			md_ists, pdec_ists);
	}

	/* infoframes */
	if (pdec_ists & DW_HDMI_AIF_CKS_CHG_ISTS)
		dw_hdmi_get_infoframes(dw_dev);

	/* audio handling */
	if (pdec_ists & (DW_HDMI_AUD_TYPE_CHG_ISTS |
			 DW_HDMI_AIF_CKS_CHG_ISTS |
			 DW_HDMI_ACR_N_CHG_ISTS |
			 DW_HDMI_ACR_CTS_CHG_ISTS |
			 DW_HDMI_GCP_AV_MUTE_CHG_ISTS) ||
	    afif_ists & (DW_HDMI_AFIF_UNDERFL_ISTS |
			 DW_HDMI_AFIF_OVERFL_ISTS |
			 DW_HDMI_AFIF_THS_PASS_ISTS)) {
		dw_hdmi_handle_audio_change(dw_dev, afif_ists, pdec_ists);
	}

	/* scdc */
	if (hdmi_ists & DW_HDMI_SCDCTMDSCFGCHANGE_ISTS_MASK) {
		dev_dbg(dw_dev->dev, "[SCDC] hdmi2=%d->%d, scrambling=%d->%d\n",
			dw_dev->is_hdmi2, is_hdmi2(dw_dev),
			dw_dev->is_scrambled, is_scrambled(dw_dev));
		if (dw_dev->is_hdmi2 != is_hdmi2(dw_dev) ||
		    dw_dev->is_scrambled != is_scrambled(dw_dev)) {
			dw_dev->is_hdmi2 = is_hdmi2(dw_dev);
			dw_dev->is_scrambled = is_scrambled(dw_dev);
			dw_hdmi_power_off(dw_dev);
			dw_hdmi_power_on(dw_dev, dw_dev->configured_input);
		}
	}

	return IRQ_HANDLED;
}

static int dw_hdmi_detect_tx_5v_ctrl(struct dw_hdmi_dev *dw_dev)
{
	bool current_on = dw_hdmi_5v_status(dw_dev, dw_dev->configured_input);
	unsigned int input_count = dw_dev->config->phy->input_count;
	unsigned int old_input = dw_dev->configured_input;
	unsigned int new_input = old_input;
	bool pending_config = false;
	unsigned int stat = 0;
	unsigned int i;

	for (i = 0; i < input_count; i++) {
		bool on = dw_hdmi_5v_status(dw_dev, i);

		stat |= on << i;

		if (on && on != dw_dev->input_connected[i]) {
			dw_hdmi_disable_ints(dw_dev);
			dw_hdmi_power_off(dw_dev);
			dw_hdmi_power_up(dw_dev);
			dw_dev->input_connected[i] = true;
			dw_hdmi_power_on(dw_dev, i);
			dw_hdmi_set_input(dw_dev, i);
			new_input = i;
			pending_config = true;
		} else {
			dw_dev->input_connected[i] = on;
		}
	}

	dw_dev->input_stat = stat;

	if (!pending_config && !current_on) {
		dw_hdmi_disable_ints(dw_dev);
		dw_hdmi_disable_hpd(dw_dev);
		dw_hdmi_disable_scdc(dw_dev);
		dw_hdmi_power_off(dw_dev);
		phy_power_off(dw_dev->phy);
	}

	dev_dbg(dw_dev->dev, "%s: stat=0x%x, input=%d->%d\n", __func__,
		stat, old_input, new_input);
	return v4l2_ctrl_s_ctrl(dw_dev->detect_tx_5v_ctrl, stat);
}

static irqreturn_t dw_hdmi_5v_irq_handler(int irq, void *dev_data)
{
	struct dw_hdmi_dev *dw_dev = dev_data;

	dw_hdmi_detect_tx_5v_ctrl(dw_dev);
	return IRQ_HANDLED;
}

static irqreturn_t dw_hdmi_5v_hard_irq_handler(int irq, void *dev_data)
{
	struct dw_hdmi_dev *dw_dev = dev_data;
	unsigned int input_count = dw_dev->config->phy->input_count;
	u32 stat = 0x0;
	unsigned int i;

	/* Clear interrupts */
	for (i = 0; i < input_count; i++) {
		dw_hdmi_5v_disable(dw_dev, i);
		dw_hdmi_5v_enable(dw_dev, i);
		stat |= dw_hdmi_5v_status(dw_dev, i) << i;
	}

	if (!stat) {
		/*
		 * If there are no connected ports disable whole HPD and SCDC
		 * also.
		 */
		dw_hdmi_disable_hpd(dw_dev);
		dw_hdmi_disable_scdc(dw_dev);
	}

	return IRQ_WAKE_THREAD;
}

static int dw_hdmi_s_routing(struct v4l2_subdev *sd, u32 input, u32 output,
			     u32 config)
{
	struct dw_hdmi_dev *dw_dev = to_dw_dev(sd);
	int ret;

	if (!has_signal(dw_dev, input))
		return -EINVAL;

	dw_dev->selected_input = input;
	if (input == dw_dev->configured_input)
		return 0;

	dw_hdmi_power_off(dw_dev);
	ret = dw_hdmi_power_on(dw_dev, input);
	dw_hdmi_set_input(dw_dev, input);
	return ret;
}

static int dw_hdmi_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	struct dw_hdmi_dev *dw_dev = to_dw_dev(sd);

	*status = 0;
	if (!has_signal(dw_dev, dw_dev->selected_input))
		*status |= V4L2_IN_ST_NO_POWER;
	if (!is_on(dw_dev))
		*status |= V4L2_IN_ST_NO_SIGNAL;

	dev_dbg(dw_dev->dev, "%s: status=0x%x\n", __func__, *status);
	return 0;
}

static int dw_hdmi_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *ival)
{
	struct dw_hdmi_dev *dw_dev = to_dw_dev(sd);
	u32 htot, vtot, fps;
	unsigned long n, d;
	u64 pclk;

	htot = hdmi_mask_readl(dw_dev, DW_HDMI_MD_HT1,
			       DW_HDMI_HTOT_PIX_OFFSET,
			       DW_HDMI_HTOT_PIX_MASK);
	vtot = hdmi_readl(dw_dev, DW_HDMI_MD_VTL);
	pclk = dw_hdmi_get_pixelclk(dw_dev);

	fps = (htot * vtot) > 0 ? div_u64((100 * pclk), (htot * vtot)) : 0;
	if (!fps)
		return 0;

	rational_best_approximation(fps, 100, fps, 100, &n, &d);

	ival->interval.numerator = d;
	ival->interval.denominator = n;

	dev_dbg(dw_dev->dev, "%s: %lu / %lu\n", __func__, d, n);

	return 0;
}

static int dw_hdmi_s_dv_timings(struct v4l2_subdev *sd,
				struct v4l2_dv_timings *timings)
{
	struct dw_hdmi_dev *dw_dev = to_dw_dev(sd);

	if (!v4l2_valid_dv_timings(timings, &dw_hdmi_timings_cap, NULL, NULL))
		return -EINVAL;
	if (v4l2_match_dv_timings(timings, &dw_dev->timings, 0, false))
		return 0;

	dw_dev->timings = *timings;
	return 0;
}

static int dw_hdmi_g_dv_timings(struct v4l2_subdev *sd,
				struct v4l2_dv_timings *timings)
{
	struct dw_hdmi_dev *dw_dev = to_dw_dev(sd);

	*timings = dw_dev->timings;
	return 0;
}

static u32 dw_hdmi_get_width(struct dw_hdmi_dev *dw_dev)
{
	u32 width = hdmi_readl(dw_dev, DW_HDMI_MD_HACT_PX);
	u32 cd = dw_hdmi_get_colordepth(dw_dev);

	switch (cd) {
	case 30:
		width = (width * 100) / 125;
		break;
	case 36:
		width = (width * 10) / 15;
		break;
	case 48:
		width /= 2;
		break;
	case 24:
	default:
		break;
	}

	if (dw_hdmi_get_mbus_code(dw_dev) == MEDIA_BUS_FMT_YVYU8_1X16)
		width *= 2;

	return width;
}

static int dw_hdmi_query_dv_timings(struct v4l2_subdev *sd,
				    struct v4l2_dv_timings *timings)
{
	struct dw_hdmi_dev *dw_dev = to_dw_dev(sd);
	struct v4l2_bt_timings *bt = &timings->bt;
	bool is_hdmi_vic;
	u32 htot, hofs;
	u32 vtot;
	u8 vic;

	memset(timings, 0, sizeof(*timings));

	if (is_off(dw_dev)) {
		dev_dbg(dw_dev->dev, "%s: controller is off\n", __func__);
		return -ENOLINK;
	}

	if (!is_on(dw_dev)) {
		dev_dbg(dw_dev->dev, "%s: controller is being configured\n",
			__func__);
		return -EAGAIN;
	}

	timings->type = V4L2_DV_BT_656_1120;
	bt->width = dw_hdmi_get_width(dw_dev);
	bt->height = hdmi_readl(dw_dev, DW_HDMI_MD_VAL);
	bt->interlaced =
		hdmi_readl(dw_dev, DW_HDMI_MD_STS) & DW_HDMI_ILACE_STS ?
			V4L2_DV_INTERLACED : V4L2_DV_PROGRESSIVE;

	if (hdmi_readl(dw_dev, DW_HDMI_ISTS) & DW_HDMI_VS_POL_ADJ_ISTS)
		bt->polarities |= V4L2_DV_VSYNC_POS_POL;
	if (hdmi_readl(dw_dev, DW_HDMI_ISTS) & DW_HDMI_HS_POL_ADJ_ISTS)
		bt->polarities |= V4L2_DV_HSYNC_POS_POL;

	bt->pixelclock = dw_hdmi_get_pixelclk(dw_dev);

	/* HTOT = HACT + HFRONT + HSYNC + HBACK */
	htot = hdmi_mask_readl(dw_dev, DW_HDMI_MD_HT1,
			       DW_HDMI_HTOT_PIX_OFFSET,
			       DW_HDMI_HTOT_PIX_MASK);
	/* HOFS = HSYNC + HBACK */
	hofs = hdmi_mask_readl(dw_dev, DW_HDMI_MD_HT1,
			       DW_HDMI_HOFS_PIX_OFFSET,
			       DW_HDMI_HOFS_PIX_MASK);

	bt->hfrontporch = htot - hofs - bt->width;
	bt->hsync = hdmi_mask_readl(dw_dev, DW_HDMI_MD_HT0,
				    DW_HDMI_HS_CLK_OFFSET,
				    DW_HDMI_HS_CLK_MASK);
	bt->hbackporch = hofs - bt->hsync;

	/* VTOT = VACT + VFRONT + VSYNC + VBACK */
	vtot = hdmi_readl(dw_dev, DW_HDMI_MD_VTL);

	bt->vsync = hdmi_readl(dw_dev, DW_HDMI_MD_VOL);

	bt->vbackporch = hdmi_readl(dw_dev, DW_HDMI_MD_VOL);
	bt->vfrontporch = vtot - bt->height - bt->vsync - bt->vbackporch;

	if (bt->interlaced == V4L2_DV_INTERLACED) {
		hdmi_mask_writel(dw_dev, 0x1, DW_HDMI_MD_VCTRL,
				 DW_HDMI_V_MODE_OFFSET,
				 DW_HDMI_V_MODE_MASK);
		msleep(100); /* Wait for 2 fields */

		vtot = hdmi_readl(dw_dev, DW_HDMI_MD_VTL);
		hdmi_mask_writel(dw_dev, 0x1, DW_HDMI_MD_VCTRL,
				 DW_HDMI_V_OFFS_LIN_MODE_OFFSET,
				 DW_HDMI_V_OFFS_LIN_MODE_MASK);
		msleep(50); /* Wait for 1 field */
		bt->il_vsync = hdmi_readl(dw_dev, DW_HDMI_MD_VOL);

		hdmi_mask_writel(dw_dev, 0x0, DW_HDMI_MD_VCTRL,
				 DW_HDMI_V_OFFS_LIN_MODE_OFFSET,
				 DW_HDMI_V_OFFS_LIN_MODE_MASK);
		msleep(50);
		bt->il_vbackporch = hdmi_readl(dw_dev, DW_HDMI_MD_VOL);
		bt->il_vfrontporch = vtot - bt->height - bt->il_vsync -
			bt->il_vbackporch;

		hdmi_mask_writel(dw_dev, 0x0, DW_HDMI_MD_VCTRL,
				 DW_HDMI_V_MODE_OFFSET,
				 DW_HDMI_V_MODE_MASK);
	}

	bt->standards = V4L2_DV_BT_STD_CEA861;

	vic = dw_hdmi_get_curr_vic(dw_dev, &is_hdmi_vic);
	if (vic) {
		if (is_hdmi_vic) {
			bt->flags |= V4L2_DV_FL_HAS_HDMI_VIC;
			bt->hdmi_vic = vic;
			bt->cea861_vic = 0;
		} else {
			bt->flags |= V4L2_DV_FL_HAS_CEA861_VIC;
			bt->hdmi_vic = 0;
			bt->cea861_vic = vic;
		}
	}

	dev_dbg(dw_dev->dev, "%s: width=%u, height=%u, mbuscode=%u\n", __func__,
		bt->width, bt->height, dw_hdmi_get_mbus_code(dw_dev));

	return 0;
}

static int dw_hdmi_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct dw_hdmi_dev *dw_dev = to_dw_dev(sd);

	if (code->index != 0)
		return -EINVAL;

	code->code = dw_dev->mbus_code;
	return 0;
}

static int dw_hdmi_fill_format(struct dw_hdmi_dev *dw_dev,
			       struct v4l2_mbus_framefmt *format)
{
	enum hdmi_extended_colorimetry extcol;
	enum hdmi_colorimetry col;
	int ret;

	memset(format, 0, sizeof(*format));

	/* Update timings */
	ret = dw_hdmi_query_dv_timings(&dw_dev->sd, &dw_dev->timings);
	if (ret)
		return ret;

	/* Update infoframe contents */
	dw_hdmi_get_infoframes(dw_dev);

	col = dw_dev->aviif.avi.colorimetry;
	extcol = dw_dev->aviif.avi.extended_colorimetry;

	switch (col) {
	case HDMI_COLORIMETRY_ITU_601:
		format->colorspace = V4L2_COLORSPACE_SMPTE170M;
		break;
	case HDMI_COLORIMETRY_EXTENDED:
		switch (extcol) {
		case HDMI_EXTENDED_COLORIMETRY_XV_YCC_601:
			format->colorspace = V4L2_COLORSPACE_SMPTE170M;
			break;
		case HDMI_EXTENDED_COLORIMETRY_XV_YCC_709:
		case HDMI_EXTENDED_COLORIMETRY_S_YCC_601:
		case HDMI_EXTENDED_COLORIMETRY_OPYCC_601:
		case HDMI_EXTENDED_COLORIMETRY_OPRGB:
		case HDMI_EXTENDED_COLORIMETRY_BT2020_CONST_LUM:
		case HDMI_EXTENDED_COLORIMETRY_BT2020:
		case HDMI_EXTENDED_COLORIMETRY_RESERVED:
		default:
			format->colorspace = V4L2_COLORSPACE_REC709;
			break;
		}

		break;
	case HDMI_COLORIMETRY_NONE:
	case HDMI_COLORIMETRY_ITU_709:
	default:
		format->colorspace = V4L2_COLORSPACE_REC709;
		break;
	}

	format->width = dw_dev->timings.bt.width;
	format->height = dw_dev->timings.bt.height;
	format->code = dw_dev->mbus_code;
	if (dw_dev->timings.bt.interlaced)
		format->field = V4L2_FIELD_ALTERNATE;
	else
		format->field = V4L2_FIELD_NONE;

	return 0;
}

static int dw_hdmi_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *format)
{
	struct dw_hdmi_dev *dw_dev = to_dw_dev(sd);

	return dw_hdmi_fill_format(dw_dev, &format->format);
}

static int dw_hdmi_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *format)
{
	struct dw_hdmi_dev *dw_dev = to_dw_dev(sd);

	if (format->format.code != dw_dev->mbus_code) {
		dev_dbg(dw_dev->dev, "invalid format\n");
		return -EINVAL;
	}

	return dw_hdmi_get_fmt(sd, cfg, format);
}

static int dw_hdmi_get_edid(struct v4l2_subdev *sd, struct v4l2_edid *edid)
{
	struct dw_hdmi_dev *dw_dev = to_dw_dev(sd);
	unsigned int input_count = dw_dev->config->phy->input_count;
	unsigned int i, j, start, end;
	u8 *ptr = edid->edid;
	u8 blocks;

	memset(edid->reserved, 0, sizeof(edid->reserved));

	if (edid->pad >= input_count || !ptr)
		return -EINVAL;

	blocks = dw_dev->curr_edid_blocks[edid->pad];

	if (!edid->start_block && !edid->blocks) {
		edid->blocks = blocks;
		return 0;
	}
	if (!blocks)
		return -ENODATA;
	if (edid->start_block >= blocks)
		return -EINVAL;
	if ((edid->start_block + edid->blocks) > blocks)
		edid->blocks = blocks - edid->start_block;

	start = (edid->start_block * 128) / sizeof(u32);
	end = start + (edid->blocks * 128) / sizeof(u32);

	for (i = start; i < end; i++) {
		u32 raw = dw_hdmi_edid_read(dw_dev, edid->pad, i * sizeof(u32));

		if (!dw_hdmi_edid_4blocks_le(dw_dev)) {
			u32 raw_srt = 0;
			/* little endian representation, need to invert */
			for (j = 0; j < 4; j++) {
				raw_srt |= ((raw >> (8 * (3 - j))) & 0xff)
					<< (j * 8);
			}
			raw = raw_srt;
		}
		memcpy(ptr, &raw, sizeof(u32));
		ptr += sizeof(u32);
	}

	return 0;
}

static int dw_hdmi_set_edid(struct v4l2_subdev *sd, struct v4l2_edid *edid)
{
	struct dw_hdmi_dev *dw_dev = to_dw_dev(sd);
	int input_count = dw_dev->config->phy->input_count;
	int size, ret;
	u32 *tmp;

	memset(edid->reserved, 0, sizeof(edid->reserved));

	if (edid->pad >= input_count || !edid->edid || !edid->blocks)
		return -EINVAL;
	if (edid->start_block != 0)
		return -EINVAL;

	/* Clear old EDID */
	size = dw_dev->curr_edid_blocks[edid->pad] * 128;
	tmp = devm_kzalloc(dw_dev->dev, size, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	ret = dw_hdmi_edid_write(dw_dev, edid->pad, tmp, size / sizeof(u32));
	devm_kfree(dw_dev->dev, tmp);

	if (ret)
		return ret;

	dw_dev->curr_edid_blocks[edid->pad] = 0;

	/* Set new EDID */
	if (dw_hdmi_edid_4blocks_le(dw_dev)) {
		/* little endian representation, no need to invert bytes */
		ret = dw_hdmi_update_edid(dw_dev, edid->pad, (u8 *)edid->edid,
					  (edid->blocks * 128), false);
	} else {
		/* invert the order of bytes to register 32bit */
		ret = dw_hdmi_update_edid(dw_dev, edid->pad, (u8 *)edid->edid,
					  (edid->blocks * 128), true);
	}
	if (ret)
		return ret;

	dw_dev->curr_edid_blocks[edid->pad] = edid->blocks;
	return 0;
}

static int dw_hdmi_dv_timings_cap(struct v4l2_subdev *sd,
				  struct v4l2_dv_timings_cap *cap)
{
	unsigned int pad = cap->pad;

	*cap = dw_hdmi_timings_cap;
	cap->pad = pad;
	return 0;
}

static int dw_hdmi_enum_dv_timings(struct v4l2_subdev *sd,
				   struct v4l2_enum_dv_timings *timings)
{
	return v4l2_enum_dv_timings_cap(timings, &dw_hdmi_timings_cap,
					NULL, NULL);
}

static int dw_hdmi_log_status(struct v4l2_subdev *sd)
{
	struct dw_hdmi_dev *dw_dev = to_dw_dev(sd);
	struct v4l2_dv_timings timings;

	v4l2_info(sd, "--- Chip configuration ---\n");
	v4l2_info(sd, "iref_clk=%dMHz\n", dw_dev->config->iref_clk);
	v4l2_info(sd, "phy_drv=%s, phy_jtag_addr=0x%x\n",
		  dw_dev->phy ? "present" : "not present",
		  dw_dev->config->phy->jtag_addr);

	v4l2_info(sd, "--- Chip status ---\n");
	v4l2_info(sd, "selected_input=%d: signal=%d\n", dw_dev->selected_input,
		  has_signal(dw_dev, dw_dev->selected_input));
	v4l2_info(sd, "configured_input=%d: signal=%d\n",
		  dw_dev->configured_input,
		  has_signal(dw_dev, dw_dev->configured_input));

	v4l2_info(sd, "--- Video status ---\n");
	v4l2_info(sd, "type=%s, color_depth=%dbits",
		  hdmi_readl(dw_dev, DW_HDMI_PDEC_STS) &
		  DW_HDMI_DVIDET ? "dvi" : "hdmi",
		  dw_hdmi_get_colordepth(dw_dev));

	v4l2_info(sd, "--- Video timings ---\n");
	if (dw_hdmi_query_dv_timings(sd, &timings))
		v4l2_info(sd, "No video detected\n");
	else
		v4l2_print_dv_timings(sd->name, "Detected format: ",
				      &timings, true);
	v4l2_print_dv_timings(sd->name, "Configured format: ",
			      &dw_dev->timings, true);

	v4l2_ctrl_subdev_log_status(sd);
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static void dw_hdmi_invalid_register(struct dw_hdmi_dev *dw_dev, u64 reg)
{
	dev_err(dw_dev->dev, "register 0x%llx not supported\n", reg);
	dev_err(dw_dev->dev, "0x0000-0x7fff: Main controller map\n");
	dev_err(dw_dev->dev, "0x8000-0x8fff: Debug registers\n");
	dev_err(dw_dev->dev, "  0x8000: TMDSVALID wait count\n");
	dev_err(dw_dev->dev, "  0x8001: SW state\n");
	dev_err(dw_dev->dev, "  0x8002: Equalizer ON/OFF\n");
	dev_err(dw_dev->dev, "  0x8003: PHY Version\n");
	dev_err(dw_dev->dev, "  0x8004: Video Stable Wait Time (ms)\n");
	dev_err(dw_dev->dev, "  0x8005: Clock wait time (ms)\n");
	dev_err(dw_dev->dev, "  0x8006: iref_clk value\n");
	dev_err(dw_dev->dev, "  0x8007: reset_datapath_enable mask value\n");
	dev_err(dw_dev->dev, "  0x8008: audio sample frequency (read only)\n");
	dev_err(dw_dev->dev, "  0x8009: hw_reset_on_hot_plug (1=enabled, 0=disabled)");
	dev_err(dw_dev->dev, "0x10000-0x100ff: PHY map\n");
}

static bool dw_hdmi_is_reserved_register(struct dw_hdmi_dev *dw_dev, u32 reg)
{
	/*
	 * NOTE: Some of the HDCP registers are write only. This means that
	 * a read from these registers will never return and can block the bus
	 * in some architectures. Disable the read to these registers and also
	 * disable the write as a safety measure because userspace should not
	 * be able to set HDCP registers.
	 */
	if (reg >= DW_HDMI_HDCP_CTRL && reg <= DW_HDMI_HDCP_STS)
		return true;
	if (reg == DW_HDMI_HDCP22_CONTROL)
		return true;
	if (reg == DW_HDMI_HDCP22_STATUS)
		return true;
	return false;
}

static int dw_hdmi_g_register(struct v4l2_subdev *sd,
			      struct v4l2_dbg_register *reg)
{
	struct dw_hdmi_dev *dw_dev = to_dw_dev(sd);

	switch (reg->reg >> 15) {
	case 0: /* Controller core read */
		if (dw_hdmi_is_reserved_register(dw_dev, reg->reg & 0x7fff))
			return -EINVAL;

		reg->size = 4;
		reg->val = hdmi_readl(dw_dev, reg->reg & 0x7fff);
		return 0;
	case 1: /* Debug registers */
		reg->size = 4;

		switch (reg->reg & GENMASK(14, 0)) {
		case 0:
			reg->val = dw_dev->tmds_valid_wait_count;
			return 0;
		case 1:
			reg->val = dw_dev->state;
			return 0;
		case 2:
			reg->val = dw_dev->phy_eq_on;
			return 0;
		case 3:
			reg->val = dw_dev->config->phy->version;
			return 0;
		case 4:
			reg->val = dw_dev->video_stable_wait_ms;
			return 0;
		case 5:
			reg->val = dw_dev->has_clock_wait_ms;
			return 0;
		case 6:
			reg->val = dw_dev->config->iref_clk;
			return 0;
		case 7:
			reg->val = dw_dev->reset_datapath_enable;
			return 0;
		case 8:
			reg->val = dw_hdmi_get_sample_freq(dw_dev);
			return 0;
		case 9:
			reg->val = dw_dev->hw_reset_on_hot_plug;
			return 0;
		default:
			break;
		}
		break;
	case 2: /* PHY read */
		if ((reg->reg & ~0xff) != BIT(16))
			break;

		reg->size = 2;
		reg->val = dw_hdmi_phy_read(dw_dev, reg->reg & 0xff);
		return 0;
	default:
		break;
	}

	dw_hdmi_invalid_register(dw_dev, reg->reg);
	return 0;
}

static int dw_hdmi_s_register(struct v4l2_subdev *sd,
			      const struct v4l2_dbg_register *reg)
{
	struct dw_hdmi_dev *dw_dev = to_dw_dev(sd);

	switch (reg->reg >> 15) {
	case 0: /* Controller core write */
		if (dw_hdmi_is_reserved_register(dw_dev, reg->reg & 0x7fff))
			return -EINVAL;

		hdmi_writel(dw_dev, reg->val & GENMASK(31, 0),
			    reg->reg & 0x7fff);
		return 0;
	case 1: /* Debug registers */
		switch (reg->reg & GENMASK(14, 0)) {
		case 0:
			dw_dev->tmds_valid_wait_count = reg->val;
			return 0;
		case 1:
			dw_hdmi_set_state(dw_dev, reg->val & 0xff);
			return 0;
		case 2:
			dw_dev->phy_eq_on = reg->val;
			return 0;
		case 4:
			dw_dev->video_stable_wait_ms = reg->val;
			return 0;
		case 5:
			dw_dev->has_clock_wait_ms = reg->val;
			return 0;
		case 7:
			dw_dev->reset_datapath_enable = reg->val;
			return 0;
		/* case 8 is read-only */
		case 9:
			dw_dev->hw_reset_on_hot_plug = reg->val;
			return 0;
		case 400:
			dev_warn(dw_dev->dev, "synmp debug select timeout\n");
			return 0;
		default:
			break;
		}
		break;
	case 2: /* PHY write */
		if ((reg->reg & ~0xff) != BIT(16))
			break;
		dw_hdmi_phy_write(dw_dev, reg->val & 0xffff, reg->reg & 0xff);
		return 0;
	default:
		break;
	}

	dw_hdmi_invalid_register(dw_dev, reg->reg);
	return 0;
}
#endif

static int dw_hdmi_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				   struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subdev_subscribe(sd, fh, sub);
	default:
		return v4l2_ctrl_subdev_subscribe_event(sd, fh, sub);
	}
}

static int dw_hdmi_registered(struct v4l2_subdev *sd)
{
	struct dw_hdmi_dev *dw_dev = to_dw_dev(sd);

	dw_dev->registered = true;
	return 0;
}

static void dw_hdmi_unregistered(struct v4l2_subdev *sd)
{
	struct dw_hdmi_dev *dw_dev = to_dw_dev(sd);

	dw_dev->registered = false;
}

static const struct v4l2_subdev_core_ops dw_hdmi_sd_core_ops = {
	.log_status = dw_hdmi_log_status,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = dw_hdmi_g_register,
	.s_register = dw_hdmi_s_register,
#endif
	.subscribe_event = dw_hdmi_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops dw_hdmi_sd_video_ops = {
	.s_routing = dw_hdmi_s_routing,
	.g_input_status = dw_hdmi_g_input_status,
	.g_frame_interval = dw_hdmi_g_frame_interval,
	.s_dv_timings = dw_hdmi_s_dv_timings,
	.g_dv_timings = dw_hdmi_g_dv_timings,
	.query_dv_timings = dw_hdmi_query_dv_timings,
};

static const struct v4l2_subdev_pad_ops dw_hdmi_sd_pad_ops = {
	.enum_mbus_code = dw_hdmi_enum_mbus_code,
	.get_fmt = dw_hdmi_get_fmt,
	.set_fmt = dw_hdmi_set_fmt,
	.get_edid = dw_hdmi_get_edid,
	.set_edid = dw_hdmi_set_edid,
	.dv_timings_cap = dw_hdmi_dv_timings_cap,
	.enum_dv_timings = dw_hdmi_enum_dv_timings,
};

static const struct v4l2_subdev_ops dw_hdmi_sd_ops = {
	.core = &dw_hdmi_sd_core_ops,
	.video = &dw_hdmi_sd_video_ops,
	.pad = &dw_hdmi_sd_pad_ops,
};

static const struct v4l2_subdev_internal_ops dw_hdmi_internal_ops = {
	.registered = dw_hdmi_registered,
	.unregistered = dw_hdmi_unregistered,
};

static int dw_hdmi_parse_pd(struct dw_hdmi_dev *dw_dev)
{
	/* PHY address already comes from platform data */
	if (!dw_dev->config->phy->jtag_addr) {
		dev_err(dw_dev->dev, "missing PHY jtag address in PD\n");
		return -EINVAL;
	}

	/* clock already comes from platform data */
	if (!dw_dev->config->iref_clk) {
		dev_err(dw_dev->dev, "invalid cfg clock frequency in PD\n");
		return -EINVAL;
	}

	/* PHY input count already comes from platform data */
	if (!dw_dev->config->phy->input_count) {
		dev_err(dw_dev->dev, "invalid PHY input count\n");
		return -EINVAL;
	}

	return 0;
}

static int dw_hdmi_parse_dt(struct dw_hdmi_dev *dw_dev)
{
	struct device_node *phy_node, *np = dw_dev->of_node;
	u32 tmp = 0;
	int ret;

	if (!np) {
		dev_err(dw_dev->dev, "missing DT node\n");
		return -EINVAL;
	}

	/* PHY properties parsing */
	phy_node = dw_hdmi_get_phy_of_node(dw_dev, NULL);
	ret = of_property_read_u32(phy_node, "reg", &tmp);
	if (ret) {
		dev_err(dw_dev->dev, "missing PHY jtag address in DT\n");
		return ret;
	}

	dw_dev->config->phy->jtag_addr = tmp & 0xff;

	/* Get config clock value */
	dw_dev->clk = devm_clk_get(dw_dev->dev, "cfg");
	if (IS_ERR(dw_dev->clk)) {
		dev_err(dw_dev->dev, "failed to get cfg clock\n");
		return PTR_ERR(dw_dev->clk);
	}

	ret = clk_prepare_enable(dw_dev->clk);
	if (ret) {
		dev_err(dw_dev->dev, "failed to enable cfg clock\n");
		return ret;
	}

	dw_dev->config->iref_clk = clk_get_rate(dw_dev->clk) / 1000000U;
	if (!dw_dev->config->iref_clk) {
		dev_err(dw_dev->dev, "invalid cfg clock frequency\n");
		ret = -EINVAL;
		goto err_clk;
	}

	/* Get PHY input count */
	tmp = 0;
	of_property_read_u32(phy_node, "input-count", &tmp);
	dw_dev->config->phy->input_count = tmp;
	if (!dw_dev->config->phy->input_count) {
		dev_err(dw_dev->dev, "invalid PHY input count\n");
		ret = -EINVAL;
		goto err_clk;
	}

	return 0;

err_clk:
	clk_disable_unprepare(dw_dev->clk);
	return ret;
}

static void dw_hdmi_config_ced(struct dw_hdmi_dev *dw_dev)
{
	hdmi_mask_writel(dw_dev, 0x1f, DW_HDMI_HDMI20_CONTROL,
			 DW_HDMI_CTRLCHECKEN_OFFSET,
			 DW_HDMI_VIDDATACHECKEN_MASK |
			 DW_HDMI_DATAISCHECKEN_MASK |
			 DW_HDMI_GBCHECKEN_MASK |
			 DW_HDMI_PREAMBCHECKEN_MASK |
			 DW_HDMI_CTRLCHECKEN_MASK);
}

static void dw_hdmi_config_mode_recover(struct dw_hdmi_dev *dw_dev)
{
	/*NOTE: avoid instability of md_ists interrupts */

	/* set HDMI_MODE_HYST */
	hdmi_mask_writel(dw_dev, 0x1, DW_HDMI_MODE_RECOVER,
			 DW_HDMI_HDMI_MODE_HYST_OFFSET,
			 DW_HDMI_HDMI_MODE_HYST_MASK);
	/* set DVI_MODE_HYST */
	hdmi_mask_writel(dw_dev, 0x8, DW_HDMI_MODE_RECOVER,
			 DW_HDMI_DVI_MODE_HYST_OFFSET,
			 DW_HDMI_DVI_MODE_HYST_MASK);
	/* set SPIKE_FILTER_EN */
	hdmi_mask_writel(dw_dev, 0x1, DW_HDMI_MODE_RECOVER,
			 DW_HDMI_SPIKE_FILTER_EN_OFFSET,
			 DW_HDMI_SPIKE_FILTER_EN_MASK);

	/* enable BCH error correction */
	hdmi_mask_writel(dw_dev, 0x1, DW_HDMI_PDEC_CTRL,
			 DW_HDMI_PDEC_BCH_EN_OFFSET,
			 DW_HDMI_PDEC_BCH_EN_MASK);
}

static void dw_hdmi_config_scdc(struct dw_hdmi_dev *dw_dev)
{
	u32 chlock = dw_dev->config->iref_clk * 1000;

	/* set HDMI_CHLOCK_CONFIG */
	hdmi_mask_writel(dw_dev, chlock, DW_HDMI_CHLOCK_CONFIG,
			 DW_HDMI_MILISECTIMERLIMIT_OFFSET,
			 DW_HDMI_MILISECTIMERLIMIT_MASK);
}

static void dw_hdmi_config_ceavid(struct dw_hdmi_dev *dw_dev)
{
	/* set CEA YCC 422 IPI Mapping */
	hdmi_mask_writel(dw_dev, 0x1, DW_HDMI_CEAVID_CONFIG,
			 DW_HDMI_CEAVID_YCC422_IPIMAP_OFFSET,
			 DW_HDMI_CEAVID_YCC422_IPIMAP_MASK);
	/* set CEA YCC 420 IPI Mapping */
	hdmi_mask_writel(dw_dev, 0x1, DW_HDMI_CEAVID_CONFIG,
			 DW_HDMI_CEAVID_YCC420_IPIMAP_OFFSET,
			 DW_HDMI_CEAVID_YCC420_IPIMAP_MASK);
}

static int dw_hdmi_initial_config(struct dw_hdmi_dev *dw_dev, u32 input)
{
	/* disable interrupts */
	dw_hdmi_disable_ints(dw_dev);
	/* Disable HPD */
	dw_hdmi_disable_hpd(dw_dev);

	/* select PHY port */
	hdmi_mask_writel(dw_dev, input, DW_HDMI_PHY_CTRL,
			 DW_HDMI_PORTSELECT_OFFSET,
			 DW_HDMI_PORTSELECT_MASK);

	/* ced */
	dw_hdmi_config_ced(dw_dev);

	/* HDMI recover configurations */
	dw_hdmi_config_mode_recover(dw_dev);

	/* scdc configurations */
	dw_hdmi_config_scdc(dw_dev);

	/* ceavid configuration */
	dw_hdmi_config_ceavid(dw_dev);

	return 0;
}

static int dw_hdmi_rx_probe(struct platform_device *pdev)
{
	const struct v4l2_dv_timings timings_def = DW_HDMI_DEFAULT_TIMING;
	struct dw_hdmi_rx_pdata *pdata = pdev->dev.platform_data;
	struct device *dev = &pdev->dev;
	struct v4l2_ctrl_handler *hdl;
	struct dw_hdmi_dev *dw_dev;
	struct v4l2_subdev *sd;
	struct resource *res;
	u32 input_count;
	unsigned int i;
	int ret, irq;

	/* Resource allocation */
	dw_dev = devm_kzalloc(dev, sizeof(*dw_dev), GFP_KERNEL);
	if (!dw_dev)
		return -ENOMEM;

	/* Resource initialization */
	if (!pdata) {
		dev_err(dev, "missing platform data\n");
		return -EINVAL;
	}

	dw_dev->dev = dev;
	dw_dev->config = pdata;
	dw_dev->state = HDMI_STATE_NO_INIT;
	dw_dev->of_node = dev->of_node;
	dw_dev->tmds_valid_wait_count = 100;
	dw_dev->has_clock_wait_ms = 200;
	dw_dev->video_stable_wait_ms = 200;
	dw_dev->reset_datapath_enable = 0xFFFFFFFF;
	dw_dev->phy_eq_on = true;
	dw_dev->hw_reset_on_hot_plug = 1;
	spin_lock_init(&dw_dev->lock);
	spin_lock_init(&dw_dev->event_lock);

	if (dw_hdmi_has_dt(dw_dev)) {
		/* init PHY based on device tree */
		ret = dw_hdmi_parse_dt(dw_dev);
	} else {
		/* init PHY based on platform data */
		ret = dw_hdmi_parse_pd(dw_dev);
	}
	if (ret)
		return ret;

	input_count = dw_dev->config->phy->input_count;

	dw_dev->curr_edid_blocks =
		devm_kzalloc(dev,
			     sizeof(*dw_dev->curr_edid_blocks) * input_count,
			     GFP_KERNEL);
	if (!dw_dev->curr_edid_blocks)
		return -ENOMEM;

	dw_dev->input_connected =
		devm_kzalloc(dev,
			     sizeof(*dw_dev->input_connected) * input_count,
			     GFP_KERNEL);
	if (!dw_dev->input_connected)
		return -ENOMEM;

	/* Deferred work */
	dw_dev->wq = create_singlethread_workqueue(DW_HDMI_RX_DRVNAME);
	if (!dw_dev->wq) {
		dev_err(dev, "failed to create workqueue\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dw_dev->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(dw_dev->regs)) {
		dev_err(dev, "failed to remap resource\n");
		ret = PTR_ERR(dw_dev->regs);
		goto err_wq;
	}

	/* Disable 5V and write EDID */
	for (i = 0; i < input_count; i++) {
		dw_dev->curr_edid_blocks[i] = ARRAY_SIZE(dw_hdmi_edid) / 32;
		dw_hdmi_5v_disable(dw_dev, i);
		if (dw_hdmi_edid_4blocks_le(dw_dev)) {
			/* little endian representation, needs to invert */
			ret = dw_hdmi_update_edid(dw_dev, i, (u8 *)dw_hdmi_edid,
						  ARRAY_SIZE(dw_hdmi_edid) *
							sizeof(u32),
						  true);
		} else {
			/* no need to invert */
			ret = dw_hdmi_update_edid(dw_dev, i, (u8 *)dw_hdmi_edid,
						  ARRAY_SIZE(dw_hdmi_edid) *
							sizeof(u32),
						  false);
		}
		if (ret)
			goto err_wq;
	}

	dw_hdmi_initial_config(dw_dev, 0);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto err_wq;
	}

	ret = devm_request_threaded_irq(dev, irq, NULL, dw_hdmi_irq_handler,
					IRQF_ONESHOT, DW_HDMI_RX_DRVNAME,
					dw_dev);
	if (ret)
		goto err_wq;

	/* V4L2 initialization */
	sd = &dw_dev->sd;
	v4l2_subdev_init(sd, &dw_hdmi_sd_ops);
	strscpy(sd->name, dev_name(dev), sizeof(sd->name));
	sd->dev = dev;
	sd->internal_ops = &dw_hdmi_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_EVENTS | V4L2_SUBDEV_FL_HAS_DEVNODE;

	/* Control handlers */
	hdl = &dw_dev->hdl;
	v4l2_ctrl_handler_init(hdl, 1);
	dw_dev->detect_tx_5v_ctrl =
		v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_DV_RX_POWER_PRESENT, 0,
				  BIT(4) - 1, 0, 0);

	sd->ctrl_handler = hdl;
	if (hdl->error) {
		ret = hdl->error;
		goto err_hdl;
	}

	/* Wait for ctrl handler register before requesting 5v interrupt */
	irq = platform_get_irq(pdev, 1);
	if (irq < 0) {
		ret = irq;
		goto err_hdl;
	}

	ret = devm_request_threaded_irq(dev, irq, dw_hdmi_5v_hard_irq_handler,
					dw_hdmi_5v_irq_handler, IRQF_ONESHOT,
					DW_HDMI_RX_DRVNAME "-5v-handler",
					dw_dev);
	if (ret)
		goto err_hdl;

	/* PHY loading */
	ret = dw_hdmi_phy_init(dw_dev);
	if (ret)
		goto err_phy_exit;

	ret = v4l2_async_register_subdev(sd);
	if (ret) {
		dev_err(dev, "failed to register subdev\n");
		goto err_phy_exit;
	}

	/* Fill initial format settings */
	dw_dev->timings = timings_def;
	dw_dev->mbus_code = MEDIA_BUS_FMT_BGR888_1X24;

	dev_set_drvdata(dev, sd);
	dw_dev->state = HDMI_STATE_POWER_OFF;
	dw_dev->is_hdmi2 = is_hdmi2(dw_dev);
	dw_dev->is_scrambled = is_scrambled(dw_dev);
	dev_info(dev, "using PHY: %s (GEN%d)\n", pdata->phy->name,
		 pdata->phy->gen);
	dev_info(dev, "HDMI mode=%s\n", dw_dev->is_hdmi2 ? "2.x" : "1.4");

	/* Set initial input, if any */
	dw_hdmi_detect_tx_5v_ctrl(dw_dev);
	if (ret) {
		dev_err(dev, "failed to set 5V ctrl initial value\n");
		goto err_subdev;
	}

	dev_info(dev, "selected_input=%d, state=%s\n",
		 dw_dev->selected_input, get_state_name(dw_dev->state));

	for (i = 0; i < input_count; i++)
		dw_hdmi_5v_enable(dw_dev, i);

	dev_dbg(dev, "driver probed\n");
	return 0;

err_subdev:
	v4l2_async_unregister_subdev(sd);
err_phy_exit:
	dw_hdmi_phy_exit(dw_dev);
err_hdl:
	v4l2_ctrl_handler_free(hdl);
err_wq:
	destroy_workqueue(dw_dev->wq);
	if (dw_dev->clk)
		clk_disable_unprepare(dw_dev->clk);

	return ret;
}

static int dw_hdmi_rx_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct dw_hdmi_dev *dw_dev = to_dw_dev(sd);
	unsigned int i, input_count = dw_dev->config->phy->input_count;

	dw_hdmi_disable_ints(dw_dev);
	dw_hdmi_disable_hpd(dw_dev);
	dw_hdmi_disable_scdc(dw_dev);
	dw_hdmi_power_off(dw_dev);
	phy_power_off(dw_dev->phy);
	for (i = 0; i < input_count; i++)
		dw_hdmi_5v_disable(dw_dev, i);
	flush_workqueue(dw_dev->wq);
	destroy_workqueue(dw_dev->wq);
	dw_hdmi_phy_exit(dw_dev);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	clk_disable_unprepare(dw_dev->clk);
	dev_dbg(dev, "driver removed\n");
	return 0;
}

static const struct of_device_id dw_hdmi_rx_id[] = {
	{ .compatible = "snps,dw-hdmi-rx" },
	{ },
};
MODULE_DEVICE_TABLE(of, dw_hdmi_rx_id);

static struct platform_driver dw_hdmi_rx_driver = {
	.probe = dw_hdmi_rx_probe,
	.remove = dw_hdmi_rx_remove,
	.driver = {
		.name = DW_HDMI_RX_DRVNAME,
		.of_match_table = dw_hdmi_rx_id,
	}
};
module_platform_driver(dw_hdmi_rx_driver);

MODULE_AUTHOR("Jose Abreu <jose.abreu@synopsys.com>");
MODULE_AUTHOR("Nelson Costa <nelson.costa@synopsys.com>");
MODULE_DESCRIPTION("DesignWare HDMI Receiver Driver");
MODULE_LICENSE("GPL");
