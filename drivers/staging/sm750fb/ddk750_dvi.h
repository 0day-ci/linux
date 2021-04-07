/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DDK750_DVI_H__
#define DDK750_DVI_H__

/* dvi chip stuffs structros */

typedef long (*PFN_DVICTRL_INIT)(unsigned char edgeSelect,
				 unsigned char busSelect,
				 unsigned char dualEdgeClkSelect,
				 unsigned char hsyncEnable,
				 unsigned char vsyncEnable,
				 unsigned char deskewEnable,
				 unsigned char deskewSetting,
				 unsigned char continuousSyncEnable,
				 unsigned char pllFilterEnable,
				 unsigned char pllFilterValue);

typedef void (*PFN_DVICTRL_RESETCHIP)(void);
typedef char* (*PFN_DVICTRL_GETCHIPSTRING)(void);
typedef unsigned short (*PFN_DVICTRL_GETVENDORID)(void);
typedef unsigned short (*PFN_DVICTRL_GETDEVICEID)(void);
typedef void (*PFN_DVICTRL_SETPOWER)(unsigned char powerUp);
typedef void (*PFN_DVICTRL_HOTPLUGDETECTION)(unsigned char enableHotPlug);
typedef unsigned char (*PFN_DVICTRL_ISCONNECTED)(void);
typedef unsigned char (*PFN_DVICTRL_CHECKINTERRUPT)(void);
typedef void (*PFN_DVICTRL_CLEARINTERRUPT)(void);

/* Structure to hold all the function pointer to the DVI Controller. */
struct dvi_ctrl_device {
	PFN_DVICTRL_INIT		pfn_init;
	PFN_DVICTRL_RESETCHIP		pfn_reset_chip;
	PFN_DVICTRL_GETCHIPSTRING	pfn_get_chip_string;
	PFN_DVICTRL_GETVENDORID		pfn_get_vendor_id;
	PFN_DVICTRL_GETDEVICEID		pfn_get_device_id;
	PFN_DVICTRL_SETPOWER		pfn_set_power;
	PFN_DVICTRL_HOTPLUGDETECTION	pfn_enable_hot_plug_detection;
	PFN_DVICTRL_ISCONNECTED		pfn_is_connected;
	PFN_DVICTRL_CHECKINTERRUPT	pfn_check_interrupt;
	PFN_DVICTRL_CLEARINTERRUPT	pfn_clear_interrupt;
};

#define DVI_CTRL_SII164

/* dvi functions prototype */
int dviInit(unsigned char edgeSelect,
	    unsigned char busSelect,
	    unsigned char dualEdgeClkSelect,
	    unsigned char hsyncEnable,
	    unsigned char vsyncEnable,
	    unsigned char deskewEnable,
	    unsigned char deskewSetting,
	    unsigned char continuousSyncEnable,
	    unsigned char pllFilterEnable,
	    unsigned char pllFilterValue);

#endif

