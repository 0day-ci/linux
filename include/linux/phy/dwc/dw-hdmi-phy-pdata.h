/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 - present Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare HDMI PHY platform data
 *
 * Author: Jose Abreu <jose.abreu@synopsys.com>
 * Author: Nelson Costa <nelson.costa@synopsys.com>
 */

#ifndef __DW_HDMI_PHY_PDATA_H__
#define __DW_HDMI_PHY_PDATA_H__

#define DW_PHY_E40X_DRVNAME	"phy-dw-hdmi-e40x"

/**
 * struct dw_phy_funcs - Set of callbacks used to communicate between phy
 * and hdmi controller. Controller must correctly fill these callbacks
 * before probbing the phy driver.
 *
 * @write: write callback. Write value 'val' into address 'addr' of phy.
 *
 * @read: read callback. Read address 'addr' and return the value.
 *
 * @reset: reset callback. Activate phy reset. Active high.
 *
 * @pddq: pddq callback. Activate phy configuration mode. Active high.
 *
 * @svsmode: svsmode callback. Activate phy retention mode. Active low.
 *
 * @zcal_reset: zcal reset callback. Restart the impedance calibration
 * procedure. Active high. This is only used in prototyping and not in real
 * ASIC. Callback shall be empty (but non NULL) in ASIC cases.
 *
 * @zcal_done: zcal done callback. Return the current status of impedance
 * calibration procedure. This is only used in prototyping and not in real
 * ASIC. Shall return always true in ASIC cases.
 *
 * @tmds_valid: TMDS valid callback. Return the current status of TMDS signal
 * that comes from phy and feeds controller. This is read from a controller
 * register.
 */
struct dw_phy_funcs {
	void (*write)(void *arg, u16 val, u16 addr);
	u16 (*read)(void *arg, u16 addr);
	void (*reset)(void *arg, int enable);
	void (*pddq)(void *arg, int enable);
	void (*svsmode)(void *arg, int enable);
	void (*zcal_reset)(void *arg);
	bool (*zcal_done)(void *arg);
	bool (*tmds_valid)(void *arg);
};

/**
 * struct dw_phy_pdata - Platform data definition for Synopsys HDMI PHY.
 *
 * @version: The version of the phy.
 *
 * @cfg_clk: Configuration clock.
 *
 * @funcs: set of callbacks that must be correctly filled and supplied to phy.
 * See @dw_phy_funcs.
 *
 * @funcs_arg: parameter that is supplied to callbacks along with the function
 * parameters.
 */
struct dw_phy_pdata {
	unsigned int version;
	unsigned int cfg_clk;
	const struct dw_phy_funcs *funcs;
	void *funcs_arg;
};

#endif /* __DW_HDMI_PHY_PDATA_H__ */
