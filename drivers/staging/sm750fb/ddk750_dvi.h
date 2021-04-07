/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DDK750_DVI_H__
#define DDK750_DVI_H__

/* dvi chip stuffs structros */

typedef long (*DVICTRL_INIT)(unsigned char edge_select,
				 unsigned char bus_select,
				 unsigned char dual_edge_clk_select,
				 unsigned char hsync_enable,
				 unsigned char vsync_enable,
				 unsigned char deskew_enable,
				 unsigned char deskew_setting,
				 unsigned char continuous_sync_enable,
				 unsigned char pll_filter_enable,
				 unsigned char pll_filter_value);

typedef void (*DVICTRL_RESETCHIP)(void);
typedef char* (*DVICTRL_GETCHIPSTRING)(void);
typedef unsigned short (*DVICTRL_GETVENDORID)(void);
typedef unsigned short (*DVICTRL_GETDEVICEID)(void);
typedef void (*DVICTRL_SETPOWER)(unsigned char power_up);
typedef void (*DVICTRL_HOTPLUGDETECTION)(unsigned char enable_hot_plug);
typedef unsigned char (*DVICTRL_ISCONNECTED)(void);
typedef unsigned char (*DVICTRL_CHECKINTERRUPT)(void);
typedef void (*DVICTRL_CLEARINTERRUPT)(void);

/* Structure to hold all the function pointer to the DVI Controller. */
struct dvi_ctrl_device {
	DVICTRL_INIT			init;
	DVICTRL_RESETCHIP		reset_chip;
	DVICTRL_GETCHIPSTRING		get_chip_string;
	DVICTRL_GETVENDORID		get_vendor_id;
	DVICTRL_GETDEVICEID		get_device_id;
	DVICTRL_SETPOWER		set_power;
	DVICTRL_HOTPLUGDETECTION	enable_hot_plug_detection;
	DVICTRL_ISCONNECTED		is_connected;
	DVICTRL_CHECKINTERRUPT	check_interrupt;
	DVICTRL_CLEARINTERRUPT	clear_interrupt;
};

#define DVI_CTRL_SII164

/* dvi functions prototype */
int dvi_init(unsigned char edge_select,
	    unsigned char bus_select,
	    unsigned char dual_edge_clk_select,
	    unsigned char hsync_enable,
	    unsigned char vsync_enable,
	    unsigned char deskew_enable,
	    unsigned char deskew_setting,
	    unsigned char continuous_sync_enable,
	    unsigned char pll_filter_enable,
	    unsigned char pll_filter_value);

#endif

