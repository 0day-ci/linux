/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 - present Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare HDMI Receiver controller platform data
 *
 * Author: Jose Abreu <jose.abreu@synopsys.com>
 * Author: Nelson Costa <nelson.costa@synopsys.com>
 */

#ifndef __DW_HDMI_RX_PDATA_H__
#define __DW_HDMI_RX_PDATA_H__

#define DW_HDMI_RX_DRVNAME			"dw-hdmi-rx"

/* Notify events */
#define DW_HDMI_NOTIFY_IS_OFF		1
#define DW_HDMI_NOTIFY_INPUT_CHANGED	2
#define DW_HDMI_NOTIFY_AUDIO_CHANGED	3
#define DW_HDMI_NOTIFY_IS_STABLE	4

/* HDCP 1.4 */
#define DW_HDMI_HDCP14_BKSV_SIZE	2
#define DW_HDMI_HDCP14_KEYS_SIZE	(2 * 40)

/**
 * struct dw_hdmi_phy_config - Phy configuration for HDMI receiver.
 *
 * @name: The name of the phy.
 *
 * @drv_name: Driver name of the phy.
 *
 * @gen: The generation of the phy.
 *
 * @version: The version of the phy.
 *
 * @cfg_clk: The configuration clock used for phy.
 *
 * @input_count: Number of input ports supported by the phy.
 *
 * @jtag_addr: The JTAG address of phy.
 */
struct dw_hdmi_phy_config {
	const char *name;
	const char *drv_name;
	unsigned int gen;
	unsigned int version;
	unsigned int cfg_clk;
	unsigned int input_count;
	u8 jtag_addr;
};

/**
 * struct dw_hdmi_rx_pdata - Platform Data configuration for HDMI receiver.
 *
 * @phy: Phy configuration parameters.
 *
 * @iref_clk: Configuration clock.
 *
 * @dw_5v_status: 5v status callback. Shall return the status of the given
 * input, i.e. shall be true if a cable is connected to the specified input.
 *
 * @dw_5v_detected: 5v detected callback. Shall return the status changes of
 * the given input, i.e. shall be true if a cable was (dis)connected to a
 * specified input.
 *
 * @dw_5v_disable: 5v disable callback. Shall clear the interrupt associated
 * with the 5v sense controller.
 *
 * @dw_5v_enable: 5v enable callback. Shall enable the interrupt associated with
 * the 5v sense controller.
 *
 * @dw_5v_arg: Argument to be used with the 5v sense callbacks.
 *
 * @dw_zcal_reset: Impedance calibration reset callback. Shall be called when
 * the impedance calibration needs to be restarted. This is used by phy driver
 * only.
 *
 * @dw_zcal_done: Impedance calibration status callback. Shall return true if
 * the impedance calibration procedure has ended. This is used by phy driver
 * only.
 *
 * @dw_zcal_arg: Argument to be used with the ZCAL calibration callbacks.
 *
 * @dw_edid_read: EDID read callback.
 *
 * @dw_edid_write: EDID write callback.
 *
 * @dw_edid_4blocks_le: EDID byte ordering callback.
 *
 * @dw_edid_arg: Argument to be used with the EDID callbacks.
 *
 * @dw_reset_all: Reset all callback.
 *
 * @dw_reset_arg: Argument to be used with Reset callbacks.
 */
struct dw_hdmi_rx_pdata {
	/* Phy configuration */
	struct dw_hdmi_phy_config *phy;
	/* Controller configuration */
	unsigned int iref_clk; /* MHz */

	/* 5V sense interface */
	bool (*dw_5v_status)(void __iomem *regs, int input);
	bool (*dw_5v_detected)(void __iomem *regs, int input);
	void (*dw_5v_disable)(void __iomem *regs, int input);
	void (*dw_5v_enable)(void __iomem *regs, int input);
	void __iomem *dw_5v_arg;

	/* Zcal interface */
	void (*dw_zcal_reset)(void __iomem *regs);
	bool (*dw_zcal_done)(void __iomem *regs);
	void __iomem *dw_zcal_arg;

	/* EDID */
	u32 (*dw_edid_read)(void __iomem *regs, int input, u32 offset);
	int (*dw_edid_write)(void __iomem *regs, int input, u32 *edid,
			     int size);
	u32 (*dw_edid_4blocks_le)(void __iomem *regs);
	void __iomem *dw_edid_arg;

	/* Reset functions */
	void (*dw_reset_all)(void __iomem *regs);
	void __iomem *dw_reset_arg;
};

#endif /* __DW_HDMI_RX_PDATA_H__ */
