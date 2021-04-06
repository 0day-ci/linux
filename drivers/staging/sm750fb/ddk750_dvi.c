// SPDX-License-Identifier: GPL-2.0
#define USE_DVICHIP
#ifdef USE_DVICHIP
#include "ddk750_chip.h"
#include "ddk750_reg.h"
#include "ddk750_dvi.h"
#include "ddk750_sii164.h"

/*
 * This global variable contains all the supported driver and its corresponding
 * function API. Please set the function pointer to NULL whenever the function
 * is not supported.
 */
static struct dvi_ctrl_device g_dcft_supported_dvi_controller[] = {
#ifdef DVI_CTRL_SII164
	{
		.pfn_init = sii164_init_chip,
		.pfn_get_vendor_id = sii164_get_vendor_id,
		.pfn_get_device_id = sii164_get_device_id,
#ifdef SII164_FULL_FUNCTIONS
		.pfn_reset_chip = sii164_reset_chip,
		.pfn_get_chip_string = sii164_get_chip_string,
		.pfn_set_power = sii164_set_power,
		.pfn_enable_hot_plug_detection = sii164_enable_hot_plug_detection,
		.pfn_is_connected = sii164_is_connected,
		.pfn_check_interrupt = sii164_check_interrupt,
		.pfn_clear_interrupt = sii164_clear_interrupt,
#endif
	},
#endif
};

int dvi_init(unsigned char edge_select,
	    unsigned char bus_select,
	    unsigned char dual_edge_clk_select,
	    unsigned char hsync_enable,
	    unsigned char vsync_enable,
	    unsigned char deskew_enable,
	    unsigned char deskew_setting,
	    unsigned char continuous_sync_enable,
	    unsigned char pll_filter_enable,
	    unsigned char pll_filter_value)
{
	struct dvi_ctrl_device *p_current_dvi_ctrl;

	p_current_dvi_ctrl = g_dcft_supported_dvi_controller;
	if (p_current_dvi_ctrl->pfn_init) {
		return p_current_dvi_ctrl->pfn_init(edge_select,
						bus_select,
						dual_edge_clk_select,
						hsync_enable,
						vsync_enable,
						deskew_enable,
						deskew_setting,
						continuous_sync_enable,
						pll_filter_enable,
						pll_filter_value);
	}
	return -1; /* error */
}

#endif
