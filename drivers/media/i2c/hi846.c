// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2021 Purism SPC

#include <asm/unaligned.h>
#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/pm.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>


#define HI846_MEDIA_BUS_FORMAT		MEDIA_BUS_FMT_SGBRG10_1X10
#define HI846_RGB_DEPTH			10
#define DEFAULT_FPS			30

/* Frame length lines / Vertical timings */
#define HI846_REG_FLL			0x0006
#define HI846_FLL_MAX			0xffff

/* Horizontal timing */
#define HI846_REG_LLP			0x0008
#define HI846_LINE_LENGTH		3800

#define HI846_REG_BINNING_MODE		0x000c

#define HI846_REG_IMAGE_ORIENTATION	0x000e

#define HI846_REG_UNKNOWN_0022		0x0022

#define HI846_REG_Y_ADDR_START_VACT_H	0x0026
#define HI846_REG_Y_ADDR_START_VACT_L	0x0027
#define HI846_REG_UNKNOWN_0028		0x0028

#define HI846_REG_Y_ADDR_END_VACT_H	0x002c
#define HI846_REG_Y_ADDR_END_VACT_L	0x002d

#define HI846_REG_Y_ODD_INC_FOBP	0x002e
#define HI846_REG_Y_EVEN_INC_FOBP	0x002f

#define HI846_REG_Y_ODD_INC_VACT	0x0032
#define HI846_REG_Y_EVEN_INC_VACT	0x0033

#define HI846_REG_GROUPED_PARA_HOLD	0x0046

#define HI846_REG_TG_ENABLE		0x004c

#define HI846_REG_UNKNOWN_005C		0x005c

#define HI846_REG_UNKNOWN_006A		0x006a

/* Long exposure time. Actually, exposure is a 20 bit value that
 * includes the lower 4 bits of 0x0073 too. only 16 bit are used
 * right now
 */
#define HI846_REG_EXPOSURE		0x0074
#define HI846_EXPOSURE_MIN		6
#define HI846_EXPOSURE_MAX_MARGIN	2
#define HI846_EXPOSURE_STEP		1

/* Analog gain controls from sensor */
#define HI846_REG_ANALOG_GAIN		0x0077
#define HI846_ANAL_GAIN_MIN		0
#define HI846_ANAL_GAIN_MAX		240
#define HI846_ANAL_GAIN_STEP		8

/* Digital gain controls from sensor */
#define HI846_REG_MWB_GR_GAIN_H		0x0078
#define HI846_REG_MWB_GR_GAIN_L		0x0079
#define HI846_REG_MWB_GB_GAIN_H		0x007a
#define HI846_REG_MWB_GB_GAIN_L		0x007b
#define HI846_REG_MWB_R_GAIN_H		0x007c
#define HI846_REG_MWB_R_GAIN_L		0x007d
#define HI846_REG_MWB_B_GAIN_H		0x007e
#define HI846_REG_MWB_B_GAIN_L		0x007f
#define HI846_DGTL_GAIN_MIN		0
#define HI846_DGTL_GAIN_MAX		8191
#define HI846_DGTL_GAIN_STEP		1
#define HI846_DGTL_GAIN_DEFAULT		256

#define HI846_REG_X_ADDR_START_HACT_H	0x0120
#define HI846_REG_X_ADDR_END_HACT_H	0x0122

#define HI846_REG_UNKNOWN_012A		0x012a

#define HI846_REG_UNKNOWN_0200		0x0200

#define HI846_REG_UNKNOWN_021C		0x021c
#define HI846_REG_UNKNOWN_021E		0x021e

#define HI846_REG_UNKNOWN_0402		0x0402
#define HI846_REG_UNKNOWN_0404		0x0404
#define HI846_REG_UNKNOWN_0408		0x0408
#define HI846_REG_UNKNOWN_0410		0x0410
#define HI846_REG_UNKNOWN_0412		0x0412
#define HI846_REG_UNKNOWN_0414		0x0414

#define HI846_REG_UNKNOWN_0418		0x0418

#define HI846_REG_UNKNOWN_051E		0x051e

/* Formatter */
#define HI846_REG_X_START_H		0x0804
#define HI846_REG_X_START_L		0x0805

/* MIPI */
#define HI846_REG_UNKNOWN_0900		0x0900
#define HI846_REG_MIPI_TX_OP_EN		0x0901
#define HI846_REG_MIPI_TX_OP_MODE	0x0902
#define HI846_RAW8			BIT(5)

#define HI846_REG_UNKNOWN_090C		0x090c
#define HI846_REG_UNKNOWN_090E		0x090e

#define HI846_REG_UNKNOWN_0914		0x0914
#define HI846_REG_TLPX			0x0915
#define HI846_REG_TCLK_PREPARE		0x0916
#define HI846_REG_TCLK_ZERO		0x0917
#define HI846_REG_UNKNOWN_0918		0x0918
#define HI846_REG_THS_PREPARE		0x0919
#define HI846_REG_THS_ZERO		0x091a
#define HI846_REG_THS_TRAIL		0x091b
#define HI846_REG_TCLK_POST		0x091c
#define HI846_REG_TCLK_TRAIL_MIN	0x091d
#define HI846_REG_UNKNOWN_091E		0x091e

#define HI846_REG_UNKNOWN_0954		0x0954
#define HI846_REG_UNKNOWN_0956		0x0956
#define HI846_REG_UNKNOWN_0958		0x0958
#define HI846_REG_UNKNOWN_095A		0x095a

/* ISP Common */
#define HI846_REG_MODE_SELECT		0x0a00
#define HI846_MODE_STANDBY		0x00
#define HI846_MODE_STREAMING		0x01
#define HI846_REG_FAST_STANDBY_MODE	0x0a02
#define HI846_REG_ISP_EN_H		0x0a04

/* Test Pattern Control */
#define HI846_REG_ISP			0x0a05
#define HI846_REG_ISP_TPG_EN		0x01
#define HI846_REG_TEST_PATTERN		0x020a /* 1-9 */

#define HI846_REG_UNKNOWN_0A0C		0x0a0c

/* Windowing */
#define HI846_REG_X_OUTPUT_SIZE_H	0x0a12
#define HI846_REG_X_OUTPUT_SIZE_L	0x0a13
#define HI846_REG_Y_OUTPUT_SIZE_H	0x0a14
#define HI846_REG_Y_OUTPUT_SIZE_L	0x0a15

/* ISP Common */
#define HI846_REG_PEDESTAL_EN		0x0a1a

#define HI846_REG_UNKNOWN_0A1E		0x0a1e

/* Horizontal Binning Mode */
#define HI846_REG_HBIN_MODE		0x0a22

#define HI846_REG_UNKNOWN_0A24		0x0a24
#define HI846_REG_UNKNOWN_0B02		0x0b02
#define HI846_REG_UNKNOWN_0B10		0x0b10
#define HI846_REG_UNKNOWN_0B12		0x0b12
#define HI846_REG_UNKNOWN_0B14		0x0b14

/* BLC (Black Level Calibration) */
#define HI846_REG_BLC_CTL0		0x0c00

#define HI846_REG_UNKNOWN_0C06		0x0c06
#define HI846_REG_UNKNOWN_0C10		0x0c10
#define HI846_REG_UNKNOWN_0C12		0x0c12
#define HI846_REG_UNKNOWN_0C14		0x0c14
#define HI846_REG_UNKNOWN_0C16		0x0c16

#define HI846_REG_UNKNOWN_0E04		0x0e04

#define HI846_REG_CHIP_ID_L		0x0f16
#define HI846_REG_CHIP_ID_H		0x0f17
#define HI846_CHIP_ID_L			0x46
#define HI846_CHIP_ID_H			0x08

#define HI846_REG_UNKNOWN_0F04		0x0f04
#define HI846_REG_UNKNOWN_0F08		0x0f08

/* PLL */
#define HI846_REG_PLL_CFG_MIPI2_H	0x0f2a
#define HI846_REG_PLL_CFG_MIPI2_L	0x0f2b

#define HI846_REG_UNKNOWN_0F30		0x0f30
#define HI846_REG_PLL_CFG_RAMP1_H	0x0f32
#define HI846_REG_UNKNOWN_0F36		0x0f36
#define HI846_REG_PLL_CFG_MIPI1_H	0x0f38

#define HI846_REG_UNKNOWN_2008		0x2008
#define HI846_REG_UNKNOWN_326E		0x326e

struct hi846_reg {
	u16 address;
	u16 val;
};

struct hi846_reg_list {
	u32 num_of_regs;
	const struct hi846_reg *regs;
};

struct hi846_mode {
	/* Frame width in pixels */
	u32 width;

	/* Frame height in pixels */
	u32 height;

	/* Horizontal timing size */
	u32 llp;

	/* Link frequency needed for this resolution */
	u8 link_freq_index;

	u16 fps;

	/* vertical timining size */
	u16 frame_len;

	const struct hi846_reg_list reg_list_config;
	const struct hi846_reg_list reg_list_2lane;
	const struct hi846_reg_list reg_list_4lane;
};

#define to_hi846(_sd) container_of(_sd, struct hi846, sd)

static const struct hi846_reg hi846_init_2lane[] = {
	{HI846_REG_MODE_SELECT,		0x0000},
	/* regs below are unknown */
	{0x2000, 0x100A},
	{0x2002, 0x00FF},
	{0x2004, 0x0007},
	{0x2006, 0x3FFF},
	{0x2008, 0x3FFF},
	{0x200A, 0xC216},
	{0x200C, 0x1292},
	{0x200E, 0xC01A},
	{0x2010, 0x403D},
	{0x2012, 0x000E},
	{0x2014, 0x403E},
	{0x2016, 0x0B80},
	{0x2018, 0x403F},
	{0x201A, 0x82AE},
	{0x201C, 0x1292},
	{0x201E, 0xC00C},
	{0x2020, 0x4130},
	{0x2022, 0x43E2},
	{0x2024, 0x0180},
	{0x2026, 0x4130},
	{0x2028, 0x7400},
	{0x202A, 0x5000},
	{0x202C, 0x0253},
	{0x202E, 0x0AD1},
	{0x2030, 0x2360},
	{0x2032, 0x0009},
	{0x2034, 0x5020},
	{0x2036, 0x000B},
	{0x2038, 0x0002},
	{0x203A, 0x0044},
	{0x203C, 0x0016},
	{0x203E, 0x1792},
	{0x2040, 0x7002},
	{0x2042, 0x154F},
	{0x2044, 0x00D5},
	{0x2046, 0x000B},
	{0x2048, 0x0019},
	{0x204A, 0x1698},
	{0x204C, 0x000E},
	{0x204E, 0x099A},
	{0x2050, 0x0058},
	{0x2052, 0x7000},
	{0x2054, 0x1799},
	{0x2056, 0x0310},
	{0x2058, 0x03C3},
	{0x205A, 0x004C},
	{0x205C, 0x064A},
	{0x205E, 0x0001},
	{0x2060, 0x0007},
	{0x2062, 0x0BC7},
	{0x2064, 0x0055},
	{0x2066, 0x7000},
	{0x2068, 0x1550},
	{0x206A, 0x158A},
	{0x206C, 0x0004},
	{0x206E, 0x1488},
	{0x2070, 0x7010},
	{0x2072, 0x1508},
	{0x2074, 0x0004},
	{0x2076, 0x0016},
	{0x2078, 0x03D5},
	{0x207A, 0x0055},
	{0x207C, 0x08CA},
	{0x207E, 0x2019},
	{0x2080, 0x0007},
	{0x2082, 0x7057},
	{0x2084, 0x0FC7},
	{0x2086, 0x5041},
	{0x2088, 0x12C8},
	{0x208A, 0x5060},
	{0x208C, 0x5080},
	{0x208E, 0x2084},
	{0x2090, 0x12C8},
	{0x2092, 0x7800},
	{0x2094, 0x0802},
	{0x2096, 0x040F},
	{0x2098, 0x1007},
	{0x209A, 0x0803},
	{0x209C, 0x080B},
	{0x209E, 0x3803},
	{0x20A0, 0x0807},
	{0x20A2, 0x0404},
	{0x20A4, 0x0400},
	{0x20A6, 0xFFFF},
	{0x20A8, 0xF0B2},
	{0x20AA, 0xFFEF},
	{0x20AC, 0x0A84},
	{0x20AE, 0x1292},
	{0x20B0, 0xC02E},
	{0x20B2, 0x4130},
	{0x23FE, 0xC056},
	{0x3232, 0xFC0C},
	{0x3236, 0xFC22},
	{0x3248, 0xFCA8},
	{0x326A, 0x8302},
	{0x326C, 0x830A},
	{0x326E, 0x0000},
	{0x32CA, 0xFC28},
	{0x32CC, 0xC3BC},
	{0x32CE, 0xC34C},
	{0x32D0, 0xC35A},
	{0x32D2, 0xC368},
	{0x32D4, 0xC376},
	{0x32D6, 0xC3C2},
	{0x32D8, 0xC3E6},
	{0x32DA, 0x0003},
	{0x32DC, 0x0003},
	{0x32DE, 0x00C7},
	{0x32E0, 0x0031},
	{0x32E2, 0x0031},
	{0x32E4, 0x0031},
	{0x32E6, 0xFC28},
	{0x32E8, 0xC3BC},
	{0x32EA, 0xC384},
	{0x32EC, 0xC392},
	{0x32EE, 0xC3A0},
	{0x32F0, 0xC3AE},
	{0x32F2, 0xC3C4},
	{0x32F4, 0xC3E6},
	{0x32F6, 0x0003},
	{0x32F8, 0x0003},
	{0x32FA, 0x00C7},
	{0x32FC, 0x0031},
	{0x32FE, 0x0031},
	{0x3300, 0x0031},
	{0x3302, 0x82CA},
	{0x3304, 0xC164},
	{0x3306, 0x82E6},
	{0x3308, 0xC19C},
	{0x330A, 0x001F},
	{0x330C, 0x001A},
	{0x330E, 0x0034},
	{0x3310, 0x0000},
	{0x3312, 0x0000},
	{0x3314, 0xFC94},
	{0x3316, 0xC3D8},
	/* regs above are unknown */
	{HI846_REG_MODE_SELECT,			0x0000},
	{HI846_REG_UNKNOWN_0E04,		0x0012},
	{HI846_REG_Y_ODD_INC_FOBP,		0x1111},
	{HI846_REG_Y_ODD_INC_VACT,		0x1111},
	{HI846_REG_UNKNOWN_0022,		0x0008},
	{HI846_REG_Y_ADDR_START_VACT_H,		0x0040},
	{HI846_REG_UNKNOWN_0028,		0x0017},
	{HI846_REG_Y_ADDR_END_VACT_H,		0x09CF},
	{HI846_REG_UNKNOWN_005C,		0x2101},
	{HI846_REG_FLL,				0x09DE},
	{HI846_REG_LLP,				0x0ED8},
	{HI846_REG_IMAGE_ORIENTATION,		0x0100},
	{HI846_REG_BINNING_MODE,		0x0022},
	{HI846_REG_HBIN_MODE,			0x0000},
	{HI846_REG_UNKNOWN_0A24,		0x0000},
	{HI846_REG_X_START_H,			0x0000},
	{HI846_REG_X_OUTPUT_SIZE_H,		0x0CC0},
	{HI846_REG_Y_OUTPUT_SIZE_H,		0x0990},
	{HI846_REG_EXPOSURE,			0x09D8},
	{HI846_REG_ANALOG_GAIN,			0x0000},
	{HI846_REG_GROUPED_PARA_HOLD,		0x0000},
	{HI846_REG_UNKNOWN_051E,		0x0000},
	{HI846_REG_UNKNOWN_0200,		0x0400},
	{HI846_REG_PEDESTAL_EN,			0x0C00},
	{HI846_REG_UNKNOWN_0A0C,		0x0010},
	{HI846_REG_UNKNOWN_0A1E,		0x0CCF},
	{HI846_REG_UNKNOWN_0402,		0x0110},
	{HI846_REG_UNKNOWN_0404,		0x00F4},
	{HI846_REG_UNKNOWN_0408,		0x0000},
	{HI846_REG_UNKNOWN_0410,		0x008D},
	{HI846_REG_UNKNOWN_0412,		0x011A},
	{HI846_REG_UNKNOWN_0414,		0x864C},
	{HI846_REG_UNKNOWN_021C,		0x0003},
	{HI846_REG_UNKNOWN_021E,		0x0235},
	{HI846_REG_BLC_CTL0,			0x9150},
	{HI846_REG_UNKNOWN_0C06,		0x0021},
	{HI846_REG_UNKNOWN_0C10,		0x0040},
	{HI846_REG_UNKNOWN_0C12,		0x0040},
	{HI846_REG_UNKNOWN_0C14,		0x0040},
	{HI846_REG_UNKNOWN_0C16,		0x0040},
	{HI846_REG_FAST_STANDBY_MODE,		0x0100},
	{HI846_REG_ISP_EN_H,			0x014A},
	{HI846_REG_UNKNOWN_0418,		0x0000},
	{HI846_REG_UNKNOWN_012A,		0x03B4},
	{HI846_REG_X_ADDR_START_HACT_H,		0x0046},
	{HI846_REG_X_ADDR_END_HACT_H,		0x0376},
	{HI846_REG_UNKNOWN_0B02,		0xE04D},
	{HI846_REG_UNKNOWN_0B10,		0x6821},
	{HI846_REG_UNKNOWN_0B12,		0x0120},
	{HI846_REG_UNKNOWN_0B14,		0x0001},
	{HI846_REG_UNKNOWN_2008,		0x38FD},
	{HI846_REG_UNKNOWN_326E,		0x0000},
	{HI846_REG_UNKNOWN_0900,		0x0320},
	{HI846_REG_MIPI_TX_OP_MODE,		0xC31A},
	{HI846_REG_UNKNOWN_0914,		0xC109},
	{HI846_REG_TCLK_PREPARE,		0x061A},
	{HI846_REG_UNKNOWN_0918,		0x0306},
	{HI846_REG_THS_ZERO,			0x0B09},
	{HI846_REG_TCLK_POST,			0x0C07},
	{HI846_REG_UNKNOWN_091E,		0x0A00},
	{HI846_REG_UNKNOWN_090C,		0x042A},
	{HI846_REG_UNKNOWN_090E,		0x006B},
	{HI846_REG_UNKNOWN_0954,		0x0089},
	{HI846_REG_UNKNOWN_0956,		0x0000},
	{HI846_REG_UNKNOWN_0958,		0xCA00},
	{HI846_REG_UNKNOWN_095A,		0x9240},
	{HI846_REG_UNKNOWN_0F08,		0x2F04},
	{HI846_REG_UNKNOWN_0F30,		0x001F},
	{HI846_REG_UNKNOWN_0F36,		0x001F},
	{HI846_REG_UNKNOWN_0F04,		0x3A00},
	{HI846_REG_PLL_CFG_RAMP1_H,		0x025A},
	{HI846_REG_PLL_CFG_MIPI1_H,		0x025A},
	{HI846_REG_PLL_CFG_MIPI2_H,		0x0024},
	{HI846_REG_UNKNOWN_006A,		0x0100},
	{HI846_REG_TG_ENABLE,			0x0100},
};

static const struct hi846_reg hi846_init_4lane[] = {
	{0x2000, 0x987A},
	{0x2002, 0x00FF},
	{0x2004, 0x0047},
	{0x2006, 0x3FFF},
	{0x2008, 0x3FFF},
	{0x200A, 0xC216},
	{0x200C, 0x1292},
	{0x200E, 0xC01A},
	{0x2010, 0x403D},
	{0x2012, 0x000E},
	{0x2014, 0x403E},
	{0x2016, 0x0B80},
	{0x2018, 0x403F},
	{0x201A, 0x82AE},
	{0x201C, 0x1292},
	{0x201E, 0xC00C},
	{0x2020, 0x4130},
	{0x2022, 0x43E2},
	{0x2024, 0x0180},
	{0x2026, 0x4130},
	{0x2028, 0x7400},
	{0x202A, 0x5000},
	{0x202C, 0x0253},
	{0x202E, 0x0AD1},
	{0x2030, 0x2360},
	{0x2032, 0x0009},
	{0x2034, 0x5020},
	{0x2036, 0x000B},
	{0x2038, 0x0002},
	{0x203A, 0x0044},
	{0x203C, 0x0016},
	{0x203E, 0x1792},
	{0x2040, 0x7002},
	{0x2042, 0x154F},
	{0x2044, 0x00D5},
	{0x2046, 0x000B},
	{0x2048, 0x0019},
	{0x204A, 0x1698},
	{0x204C, 0x000E},
	{0x204E, 0x099A},
	{0x2050, 0x0058},
	{0x2052, 0x7000},
	{0x2054, 0x1799},
	{0x2056, 0x0310},
	{0x2058, 0x03C3},
	{0x205A, 0x004C},
	{0x205C, 0x064A},
	{0x205E, 0x0001},
	{0x2060, 0x0007},
	{0x2062, 0x0BC7},
	{0x2064, 0x0055},
	{0x2066, 0x7000},
	{0x2068, 0x1550},
	{0x206A, 0x158A},
	{0x206C, 0x0004},
	{0x206E, 0x1488},
	{0x2070, 0x7010},
	{0x2072, 0x1508},
	{0x2074, 0x0004},
	{0x2076, 0x0016},
	{0x2078, 0x03D5},
	{0x207A, 0x0055},
	{0x207C, 0x08CA},
	{0x207E, 0x2019},
	{0x2080, 0x0007},
	{0x2082, 0x7057},
	{0x2084, 0x0FC7},
	{0x2086, 0x5041},
	{0x2088, 0x12C8},
	{0x208A, 0x5060},
	{0x208C, 0x5080},
	{0x208E, 0x2084},
	{0x2090, 0x12C8},
	{0x2092, 0x7800},
	{0x2094, 0x0802},
	{0x2096, 0x040F},
	{0x2098, 0x1007},
	{0x209A, 0x0803},
	{0x209C, 0x080B},
	{0x209E, 0x3803},
	{0x20A0, 0x0807},
	{0x20A2, 0x0404},
	{0x20A4, 0x0400},
	{0x20A6, 0xFFFF},
	{0x20A8, 0xF0B2},
	{0x20AA, 0xFFEF},
	{0x20AC, 0x0A84},
	{0x20AE, 0x1292},
	{0x20B0, 0xC02E},
	{0x20B2, 0x4130},
	{0x20B4, 0xF0B2},
	{0x20B6, 0xFFBF},
	{0x20B8, 0x2004},
	{0x20BA, 0x403F},
	{0x20BC, 0x00C3},
	{0x20BE, 0x4FE2},
	{0x20C0, 0x8318},
	{0x20C2, 0x43CF},
	{0x20C4, 0x0000},
	{0x20C6, 0x9382},
	{0x20C8, 0xC314},
	{0x20CA, 0x2003},
	{0x20CC, 0x12B0},
	{0x20CE, 0xCAB0},
	{0x20D0, 0x4130},
	{0x20D2, 0x12B0},
	{0x20D4, 0xC90A},
	{0x20D6, 0x4130},
	{0x20D8, 0x42D2},
	{0x20DA, 0x8318},
	{0x20DC, 0x00C3},
	{0x20DE, 0x9382},
	{0x20E0, 0xC314},
	{0x20E2, 0x2009},
	{0x20E4, 0x120B},
	{0x20E6, 0x120A},
	{0x20E8, 0x1209},
	{0x20EA, 0x1208},
	{0x20EC, 0x1207},
	{0x20EE, 0x1206},
	{0x20F0, 0x4030},
	{0x20F2, 0xC15E},
	{0x20F4, 0x4130},
	{0x20F6, 0x1292},
	{0x20F8, 0xC008},
	{0x20FA, 0x4130},
	{0x20FC, 0x42D2},
	{0x20FE, 0x82A1},
	{0x2100, 0x00C2},
	{0x2102, 0x1292},
	{0x2104, 0xC040},
	{0x2106, 0x4130},
	{0x2108, 0x1292},
	{0x210A, 0xC006},
	{0x210C, 0x42A2},
	{0x210E, 0x7324},
	{0x2110, 0x9382},
	{0x2112, 0xC314},
	{0x2114, 0x2011},
	{0x2116, 0x425F},
	{0x2118, 0x82A1},
	{0x211A, 0xF25F},
	{0x211C, 0x00C1},
	{0x211E, 0xF35F},
	{0x2120, 0x2406},
	{0x2122, 0x425F},
	{0x2124, 0x00C0},
	{0x2126, 0xF37F},
	{0x2128, 0x522F},
	{0x212A, 0x4F82},
	{0x212C, 0x7324},
	{0x212E, 0x425F},
	{0x2130, 0x82D4},
	{0x2132, 0xF35F},
	{0x2134, 0x4FC2},
	{0x2136, 0x01B3},
	{0x2138, 0x93C2},
	{0x213A, 0x829F},
	{0x213C, 0x2421},
	{0x213E, 0x403E},
	{0x2140, 0xFFFE},
	{0x2142, 0x40B2},
	{0x2144, 0xEC78},
	{0x2146, 0x831C},
	{0x2148, 0x40B2},
	{0x214A, 0xEC78},
	{0x214C, 0x831E},
	{0x214E, 0x40B2},
	{0x2150, 0xEC78},
	{0x2152, 0x8320},
	{0x2154, 0xB3D2},
	{0x2156, 0x008C},
	{0x2158, 0x2405},
	{0x215A, 0x4E0F},
	{0x215C, 0x503F},
	{0x215E, 0xFFD8},
	{0x2160, 0x4F82},
	{0x2162, 0x831C},
	{0x2164, 0x90F2},
	{0x2166, 0x0003},
	{0x2168, 0x008C},
	{0x216A, 0x2401},
	{0x216C, 0x4130},
	{0x216E, 0x421F},
	{0x2170, 0x831C},
	{0x2172, 0x5E0F},
	{0x2174, 0x4F82},
	{0x2176, 0x831E},
	{0x2178, 0x5E0F},
	{0x217A, 0x4F82},
	{0x217C, 0x8320},
	{0x217E, 0x3FF6},
	{0x2180, 0x432E},
	{0x2182, 0x3FDF},
	{0x2184, 0x421F},
	{0x2186, 0x7100},
	{0x2188, 0x4F0E},
	{0x218A, 0x503E},
	{0x218C, 0xFFD8},
	{0x218E, 0x4E82},
	{0x2190, 0x7A04},
	{0x2192, 0x421E},
	{0x2194, 0x831C},
	{0x2196, 0x5F0E},
	{0x2198, 0x4E82},
	{0x219A, 0x7A06},
	{0x219C, 0x0B00},
	{0x219E, 0x7304},
	{0x21A0, 0x0050},
	{0x21A2, 0x40B2},
	{0x21A4, 0xD081},
	{0x21A6, 0x0B88},
	{0x21A8, 0x421E},
	{0x21AA, 0x831E},
	{0x21AC, 0x5F0E},
	{0x21AE, 0x4E82},
	{0x21B0, 0x7A0E},
	{0x21B2, 0x521F},
	{0x21B4, 0x8320},
	{0x21B6, 0x4F82},
	{0x21B8, 0x7A10},
	{0x21BA, 0x0B00},
	{0x21BC, 0x7304},
	{0x21BE, 0x007A},
	{0x21C0, 0x40B2},
	{0x21C2, 0x0081},
	{0x21C4, 0x0B88},
	{0x21C6, 0x4392},
	{0x21C8, 0x7A0A},
	{0x21CA, 0x0800},
	{0x21CC, 0x7A0C},
	{0x21CE, 0x0B00},
	{0x21D0, 0x7304},
	{0x21D2, 0x022B},
	{0x21D4, 0x40B2},
	{0x21D6, 0xD081},
	{0x21D8, 0x0B88},
	{0x21DA, 0x0B00},
	{0x21DC, 0x7304},
	{0x21DE, 0x0255},
	{0x21E0, 0x40B2},
	{0x21E2, 0x0081},
	{0x21E4, 0x0B88},
	{0x21E6, 0x4130},
	{0x23FE, 0xC056},
	{0x3232, 0xFC0C},
	{0x3236, 0xFC22},
	{0x3238, 0xFCFC},
	{0x323A, 0xFD84},
	{0x323C, 0xFD08},
	{0x3246, 0xFCD8},
	{0x3248, 0xFCA8},
	{0x324E, 0xFCB4},
	{0x326A, 0x8302},
	{0x326C, 0x830A},
	{0x326E, 0x0000},
	{0x32CA, 0xFC28},
	{0x32CC, 0xC3BC},
	{0x32CE, 0xC34C},
	{0x32D0, 0xC35A},
	{0x32D2, 0xC368},
	{0x32D4, 0xC376},
	{0x32D6, 0xC3C2},
	{0x32D8, 0xC3E6},
	{0x32DA, 0x0003},
	{0x32DC, 0x0003},
	{0x32DE, 0x00C7},
	{0x32E0, 0x0031},
	{0x32E2, 0x0031},
	{0x32E4, 0x0031},
	{0x32E6, 0xFC28},
	{0x32E8, 0xC3BC},
	{0x32EA, 0xC384},
	{0x32EC, 0xC392},
	{0x32EE, 0xC3A0},
	{0x32F0, 0xC3AE},
	{0x32F2, 0xC3C4},
	{0x32F4, 0xC3E6},
	{0x32F6, 0x0003},
	{0x32F8, 0x0003},
	{0x32FA, 0x00C7},
	{0x32FC, 0x0031},
	{0x32FE, 0x0031},
	{0x3300, 0x0031},
	{0x3302, 0x82CA},
	{0x3304, 0xC164},
	{0x3306, 0x82E6},
	{0x3308, 0xC19C},
	{0x330A, 0x001F},
	{0x330C, 0x001A},
	{0x330E, 0x0034},
	{0x3310, 0x0000},
	{0x3312, 0x0000},
	{0x3314, 0xFC94},
	{0x3316, 0xC3D8},

	{0x0A00, 0x0000},
	{0x0E04, 0x0012},
	{0x002E, 0x1111},
	{0x0032, 0x1111},
	{0x0022, 0x0008},
	{0x0026, 0x0040},
	{0x0028, 0x0017},
	{0x002C, 0x09CF},
	{0x005C, 0x2101},
	{0x0006, 0x09DE},
	{0x0008, 0x0ED8},
	{0x000E, 0x0100},
	{0x000C, 0x0022},
	{0x0A22, 0x0000},
	{0x0A24, 0x0000},
	{0x0804, 0x0000},
	{0x0A12, 0x0CC0},
	{0x0A14, 0x0990},
	{0x0074, 0x09D8},
	{0x0076, 0x0000},
	{0x051E, 0x0000},
	{0x0200, 0x0400},
	{0x0A1A, 0x0C00},
	{0x0A0C, 0x0010},
	{0x0A1E, 0x0CCF},
	{0x0402, 0x0110},
	{0x0404, 0x00F4},
	{0x0408, 0x0000},
	{0x0410, 0x008D},
	{0x0412, 0x011A},
	{0x0414, 0x864C},
	/* For OTP */
	{0x021C, 0x0003},
	{0x021E, 0x0235},
	/* For OTP */
	{0x0C00, 0x9950},
	{0x0C06, 0x0021},
	{0x0C10, 0x0040},
	{0x0C12, 0x0040},
	{0x0C14, 0x0040},
	{0x0C16, 0x0040},
	{0x0A02, 0x0100},
	{0x0A04, 0x015A},
	{0x0418, 0x0000},
	{0x0128, 0x0028},
	{0x012A, 0xFFFF},
	{0x0120, 0x0046},
	{0x0122, 0x0376},
	{0x012C, 0x0020},
	{0x012E, 0xFFFF},
	{0x0124, 0x0040},
	{0x0126, 0x0378},
	{0x0746, 0x0050},
	{0x0748, 0x01D5},
	{0x074A, 0x022B},
	{0x074C, 0x03B0},
	{0x0756, 0x043F},
	{0x0758, 0x3F1D},
	{0x0B02, 0xE04D},
	{0x0B10, 0x6821},
	{0x0B12, 0x0120},
	{0x0B14, 0x0001},
	{0x2008, 0x38FD},
	{0x326E, 0x0000},
	{0x0900, 0x0300},
	{0x0902, 0xC319},
	{0x0914, 0xC109},
	{0x0916, 0x061A},
	{0x0918, 0x0407},
	{0x091A, 0x0A0B},
	{0x091C, 0x0E08},
	{0x091E, 0x0A00},
	{0x090C, 0x0427},
	{0x090E, 0x0059},
	{0x0954, 0x0089},
	{0x0956, 0x0000},
	{0x0958, 0xCA80},
	{0x095A, 0x9240},
	{0x0F08, 0x2F04},
	{0x0F30, 0x001F},
	{0x0F36, 0x001F},
	{0x0F04, 0x3A00},
	{0x0F32, 0x025A},
	{0x0F38, 0x025A},
	{0x0F2A, 0x4124},
	{0x006A, 0x0100},
	{0x004C, 0x0100},
	{0x0044, 0x0001},
};

static const struct hi846_reg mode_640x480_config[] = {
	{HI846_REG_MODE_SELECT,			0x0000},
	{HI846_REG_Y_ODD_INC_FOBP,		0x7711},
	{HI846_REG_Y_ODD_INC_VACT,		0x7711},
	{HI846_REG_Y_ADDR_START_VACT_H,		0x0148},
	{HI846_REG_Y_ADDR_END_VACT_H,		0x08C7},
	{HI846_REG_UNKNOWN_005C,		0x4404},
	{HI846_REG_FLL,				0x0277},
	{HI846_REG_LLP,				0x0ED8},
	{HI846_REG_BINNING_MODE,		0x0322},
	{HI846_REG_HBIN_MODE,			0x0200},
	{HI846_REG_UNKNOWN_0A24,		0x0000},
	{HI846_REG_X_START_H,			0x0058},
	{HI846_REG_X_OUTPUT_SIZE_H,		0x0280},
	{HI846_REG_Y_OUTPUT_SIZE_H,		0x01E0},

	/* For OTP */
	{HI846_REG_UNKNOWN_021C,		0x0003},
	{HI846_REG_UNKNOWN_021E,		0x0235},

	{HI846_REG_ISP_EN_H,			0x017A},
	{HI846_REG_UNKNOWN_0418,		0x0210},
	{HI846_REG_UNKNOWN_0B02,		0xE04D},
	{HI846_REG_UNKNOWN_0B10,		0x7021},
	{HI846_REG_UNKNOWN_0B12,		0x0120},
	{HI846_REG_UNKNOWN_0B14,		0x0001},
	{HI846_REG_UNKNOWN_2008,		0x38FD},
	{HI846_REG_UNKNOWN_326E,		0x0000},
};

static const struct hi846_reg mode_640x480_mipi_2lane[] = {
	{HI846_REG_UNKNOWN_0900,		0x0300},
	{HI846_REG_MIPI_TX_OP_MODE,		0x4319},
	{HI846_REG_UNKNOWN_0914,		0xC105},
	{HI846_REG_TCLK_PREPARE,		0x030C},
	{HI846_REG_UNKNOWN_0918,		0x0304},
	{HI846_REG_THS_ZERO,			0x0708},
	{HI846_REG_TCLK_POST,			0x0B04},
	{HI846_REG_UNKNOWN_091E,		0x0500},
	{HI846_REG_UNKNOWN_090C,		0x0208},
	{HI846_REG_UNKNOWN_090E,		0x009A},
	{HI846_REG_UNKNOWN_0954,		0x0089},
	{HI846_REG_UNKNOWN_0956,		0x0000},
	{HI846_REG_UNKNOWN_0958,		0xCA80},
	{HI846_REG_UNKNOWN_095A,		0x9240},
	{HI846_REG_PLL_CFG_MIPI2_H,		0x4924},
	{HI846_REG_TG_ENABLE,			0x0100},
};

static const struct hi846_reg mode_1280x720_config[] = {
	{HI846_REG_MODE_SELECT,			0x0000},
	{HI846_REG_Y_ODD_INC_FOBP,		0x3311},
	{HI846_REG_Y_ODD_INC_VACT,		0x3311},
	{HI846_REG_Y_ADDR_START_VACT_H,		0x0238},
	{HI846_REG_Y_ADDR_END_VACT_H,		0x07D7},
	{HI846_REG_UNKNOWN_005C,		0x4202},
	{HI846_REG_FLL,				0x034A},
	{HI846_REG_LLP,				0x0ED8},
	{HI846_REG_BINNING_MODE,		0x0122},
	{HI846_REG_HBIN_MODE,			0x0100},
	{HI846_REG_UNKNOWN_0A24,		0x0000},
	{HI846_REG_X_START_H,			0x00B0},
	{HI846_REG_X_OUTPUT_SIZE_H,		0x0500},
	{HI846_REG_Y_OUTPUT_SIZE_H,		0x02D0},
	{HI846_REG_EXPOSURE,			0x0344},

	/* For OTP */
	{HI846_REG_UNKNOWN_021C,		0x0003},
	{HI846_REG_UNKNOWN_021E,		0x0235},

	{HI846_REG_ISP_EN_H,			0x017A},
	{HI846_REG_UNKNOWN_0418,		0x0410},
	{HI846_REG_UNKNOWN_0B02,		0xE04D},
	{HI846_REG_UNKNOWN_0B10,		0x6C21},
	{HI846_REG_UNKNOWN_0B12,		0x0120},
	{HI846_REG_UNKNOWN_0B14,		0x0005},
	{HI846_REG_UNKNOWN_2008,		0x38FD},
	{HI846_REG_UNKNOWN_326E,		0x0000},
};

static const struct hi846_reg mode_1280x720_mipi_2lane[] = {
	{HI846_REG_UNKNOWN_0900,		0x0300},
	{HI846_REG_MIPI_TX_OP_MODE,		0x4319},
	{HI846_REG_UNKNOWN_0914,		0xC109},
	{HI846_REG_TCLK_PREPARE,		0x061A},
	{HI846_REG_UNKNOWN_0918,		0x0407},
	{HI846_REG_THS_ZERO,			0x0A0B},
	{HI846_REG_TCLK_POST,			0x0E08},
	{HI846_REG_UNKNOWN_091E,		0x0A00},
	{HI846_REG_UNKNOWN_090C,		0x0427},
	{HI846_REG_UNKNOWN_090E,		0x0145},
	{HI846_REG_UNKNOWN_0954,		0x0089},
	{HI846_REG_UNKNOWN_0956,		0x0000},
	{HI846_REG_UNKNOWN_0958,		0xCA80},
	{HI846_REG_UNKNOWN_095A,		0x9240},
	{HI846_REG_PLL_CFG_MIPI2_H,		0x4124},
	{HI846_REG_TG_ENABLE,			0x0100},
};

static const struct hi846_reg mode_1280x720_mipi_4lane[] = {
	/* 360Mbps */
	{HI846_REG_UNKNOWN_0900,		0x0300},
	{HI846_REG_MIPI_TX_OP_MODE,		0xC319},
	{HI846_REG_UNKNOWN_0914,		0xC105},
	{HI846_REG_TCLK_PREPARE,		0x030C},
	{HI846_REG_UNKNOWN_0918,		0x0304},
	{HI846_REG_THS_ZERO,			0x0708},
	{HI846_REG_TCLK_POST,			0x0B04},
	{HI846_REG_UNKNOWN_091E,		0x0500},
	{HI846_REG_UNKNOWN_090C,		0x0208},
	{HI846_REG_UNKNOWN_090E,		0x008A},
	{HI846_REG_UNKNOWN_0954,		0x0089},
	{HI846_REG_UNKNOWN_0956,		0x0000},
	{HI846_REG_UNKNOWN_0958,		0xCA80},
	{HI846_REG_UNKNOWN_095A,		0x9240},
	{HI846_REG_PLL_CFG_MIPI2_H,		0x4924},
	{HI846_REG_TG_ENABLE,			0x0100},
};

static const struct hi846_reg mode_1632x1224_config[] = {
	{HI846_REG_MODE_SELECT,			0x0000},
	{HI846_REG_Y_ODD_INC_FOBP,		0x3311},
	{HI846_REG_Y_ODD_INC_VACT,		0x3311},
	{HI846_REG_Y_ADDR_START_VACT_H,		0x0040},
	{HI846_REG_Y_ADDR_END_VACT_H,		0x09CF},
	{HI846_REG_UNKNOWN_005C,		0x4202},
	{HI846_REG_FLL,				0x09DE},
	{HI846_REG_LLP,				0x0ED8},
	{HI846_REG_BINNING_MODE,		0x0122},
	{HI846_REG_HBIN_MODE,			0x0100},
	{HI846_REG_UNKNOWN_0A24,		0x0000},
	{HI846_REG_X_START_H,			0x0000},
	{HI846_REG_X_OUTPUT_SIZE_H,		0x0660},
	{HI846_REG_Y_OUTPUT_SIZE_H,		0x04C8},
	{HI846_REG_EXPOSURE,			0x09D8},
	{HI846_REG_ISP_EN_H,			0x017A},

	/* For OTP */
	{HI846_REG_UNKNOWN_021C,		0x0003},
	{HI846_REG_UNKNOWN_021E,		0x0235},

	{HI846_REG_UNKNOWN_0418,		0x0000},
	{HI846_REG_UNKNOWN_0B02,		0xE04D},
	{HI846_REG_UNKNOWN_0B10,		0x6C21},
	{HI846_REG_UNKNOWN_0B12,		0x0120},
	{HI846_REG_UNKNOWN_0B14,		0x0005},
	{HI846_REG_UNKNOWN_2008,		0x38FD},
	{HI846_REG_UNKNOWN_326E,		0x0000},
};

static const struct hi846_reg mode_1632x1224_mipi_2lane[] = {
	{HI846_REG_UNKNOWN_0900,		0x0300},
	{HI846_REG_MIPI_TX_OP_MODE,		0x4319},
	{HI846_REG_UNKNOWN_0914,		0xC109},
	{HI846_REG_TCLK_PREPARE,		0x061A},
	{HI846_REG_UNKNOWN_0918,		0x0407},
	{HI846_REG_THS_ZERO,			0x0A0B},
	{HI846_REG_TCLK_POST,			0x0E08},
	{HI846_REG_UNKNOWN_091E,		0x0A00},
	{HI846_REG_UNKNOWN_090C,		0x0427},
	{HI846_REG_UNKNOWN_090E,		0x0069},
	{HI846_REG_UNKNOWN_0954,		0x0089},
	{HI846_REG_UNKNOWN_0956,		0x0000},
	{HI846_REG_UNKNOWN_0958,		0xCA80},
	{HI846_REG_UNKNOWN_095A,		0x9240},
	{HI846_REG_PLL_CFG_MIPI2_H,		0x4124},
	{HI846_REG_TG_ENABLE,			0x0100},
};

static const struct hi846_reg mode_1632x1224_mipi_4lane[] = {
	{HI846_REG_UNKNOWN_0900,		0x0300},
	{HI846_REG_MIPI_TX_OP_MODE,		0xC319},
	{HI846_REG_UNKNOWN_0914,		0xC105},
	{HI846_REG_TCLK_PREPARE,		0x030C},
	{HI846_REG_UNKNOWN_0918,		0x0304},
	{HI846_REG_THS_ZERO,			0x0708},
	{HI846_REG_TCLK_POST,			0x0B04},
	{HI846_REG_UNKNOWN_091E,		0x0500},
	{HI846_REG_UNKNOWN_090C,		0x0208},
	{HI846_REG_UNKNOWN_090E,		0x001C},
	{HI846_REG_UNKNOWN_0954,		0x0089},
	{HI846_REG_UNKNOWN_0956,		0x0000},
	{HI846_REG_UNKNOWN_0958,		0xCA80},
	{HI846_REG_UNKNOWN_095A,		0x9240},
	{HI846_REG_PLL_CFG_MIPI2_H,		0x4924},
	{HI846_REG_TG_ENABLE,			0x0100},
};

static const struct hi846_reg mode_3264x2448_config[] = {
	{HI846_REG_MODE_SELECT,			0x0000},
	{HI846_REG_Y_ODD_INC_FOBP,		0x1111},
	{HI846_REG_Y_ODD_INC_VACT,		0x1111},
	{HI846_REG_Y_ADDR_START_VACT_H,		0x0040},
	{HI846_REG_Y_ADDR_END_VACT_H,		0x09CF},
	{HI846_REG_UNKNOWN_005C,		0x2101},
	{HI846_REG_FLL,				0x09DE},
	{HI846_REG_LLP,				0x0ED8},
	{HI846_REG_BINNING_MODE,		0x0022},
	{HI846_REG_HBIN_MODE,			0x0000},
	{HI846_REG_UNKNOWN_0A24,		0x0000},
	{HI846_REG_X_START_H,			0x0000},
	{HI846_REG_X_OUTPUT_SIZE_H,		0x0CC0},
	{HI846_REG_Y_OUTPUT_SIZE_H,		0x0990},
	{HI846_REG_EXPOSURE,			0x09D8},

	/* For OTP */
	{HI846_REG_UNKNOWN_021C,		0x0003},
	{HI846_REG_UNKNOWN_021E,		0x0235},

	{HI846_REG_ISP_EN_H,			0x015A},
	{HI846_REG_UNKNOWN_0418,		0x0000},
	{HI846_REG_UNKNOWN_0B02,		0xE04D},
	{HI846_REG_UNKNOWN_0B10,		0x6821},
	{HI846_REG_UNKNOWN_0B12,		0x0120},
	{HI846_REG_UNKNOWN_0B14,		0x0001},
	{HI846_REG_UNKNOWN_2008,		0x38FD},
	{HI846_REG_UNKNOWN_326E,		0x0000},
};

static const struct hi846_reg mode_3264x2448_mipi_2lane[] = {
	{HI846_REG_UNKNOWN_0900,		0x0320},
	{HI846_REG_MIPI_TX_OP_MODE,		0x431A},
	{HI846_REG_UNKNOWN_0914,		0xC109},
	{HI846_REG_TCLK_PREPARE,		0x061A},
	{HI846_REG_UNKNOWN_0918,		0x0306},
	{HI846_REG_THS_ZERO,			0x0B09},
	{HI846_REG_TCLK_POST,			0x0C07},
	{HI846_REG_UNKNOWN_091E,		0x0A00},
	{HI846_REG_UNKNOWN_090C,		0x042A},
	{HI846_REG_UNKNOWN_090E,		0x006B},
	{HI846_REG_UNKNOWN_0954,		0x0089},
	{HI846_REG_UNKNOWN_0956,		0x0000},
	{HI846_REG_UNKNOWN_0958,		0xCA00},
	{HI846_REG_UNKNOWN_095A,		0x9240},
	{HI846_REG_PLL_CFG_MIPI2_H,		0x0024},
	{HI846_REG_TG_ENABLE,			0x0100},
};

static const struct hi846_reg mode_3264x2448_mipi_4lane[] = {
	/* 720Mbps*/
	{HI846_REG_UNKNOWN_0900,		0x0300},
	{HI846_REG_MIPI_TX_OP_MODE,		0xC319},
	{HI846_REG_UNKNOWN_0914,		0xC109},
	{HI846_REG_TCLK_PREPARE,		0x061A},
	{HI846_REG_UNKNOWN_0918,		0x0407},
	{HI846_REG_THS_ZERO,			0x0A0B},
	{HI846_REG_TCLK_POST,			0x0E08},
	{HI846_REG_UNKNOWN_091E,		0x0A00},
	{HI846_REG_UNKNOWN_090C,		0x0427},
	{HI846_REG_UNKNOWN_090E,		0x0059},
	{HI846_REG_UNKNOWN_0954,		0x0089},
	{HI846_REG_UNKNOWN_0956,		0x0000},
	{HI846_REG_UNKNOWN_0958,		0xCA80},
	{HI846_REG_UNKNOWN_095A,		0x9240},
	{HI846_REG_PLL_CFG_MIPI2_H,		0x4124},
	{HI846_REG_TG_ENABLE,			0x0100},
};

static const char * const hi846_test_pattern_menu[] = {
	"Disabled",
	"Solid Colour",
	"100% Colour Bars",
	"Fade To Grey Colour Bars",
	"PN9",
	"Gradient Horizontal",
	"Gradient Vertical",
	"Check Board",
	"Slant Pattern",
	"Resolution Pattern",
};

#define FREQ_INDEX_640	0
#define FREQ_INDEX_1280	1
#define FREQ_INDEX_3264	2
static const s64 hi846_link_freqs[] = {
	[FREQ_INDEX_640] = 80000000,
	[FREQ_INDEX_1280] = 200000000,
	[FREQ_INDEX_3264] = 300000000,
};

static const struct hi846_reg_list hi846_init_regs_list_2lane = {
	.num_of_regs = ARRAY_SIZE(hi846_init_2lane),
	.regs = hi846_init_2lane,
};

static const struct hi846_reg_list hi846_init_regs_list_4lane = {
	.num_of_regs = ARRAY_SIZE(hi846_init_4lane),
	.regs = hi846_init_4lane,
};

static const struct hi846_mode supported_modes[] = {
	{
		.width = 640,
		.height = 480,
		.link_freq_index = FREQ_INDEX_640,
		.fps = 120,
		.frame_len = 631,
		.llp = HI846_LINE_LENGTH,
		.reg_list_config = {
			.num_of_regs = ARRAY_SIZE(mode_640x480_config),
			.regs = mode_640x480_config,
		},
		.reg_list_2lane = {
			.num_of_regs = ARRAY_SIZE(mode_640x480_mipi_2lane),
			.regs = mode_640x480_mipi_2lane,
		},
	},
	{
		.width = 1280,
		.height = 720,
		.link_freq_index = FREQ_INDEX_1280,
		.fps = 90,
		.frame_len = 842,
		.llp = HI846_LINE_LENGTH,
		.reg_list_config = {
			.num_of_regs = ARRAY_SIZE(mode_1280x720_config),
			.regs = mode_1280x720_config,
		},
		.reg_list_2lane = {
			.num_of_regs = ARRAY_SIZE(mode_1280x720_mipi_2lane),
			.regs = mode_1280x720_mipi_2lane,
		},
		.reg_list_4lane = {
			.num_of_regs = ARRAY_SIZE(mode_1280x720_mipi_4lane),
			.regs = mode_1280x720_mipi_4lane,
		},
	},
	{
		.width = 1632,
		.height = 1224,
		.link_freq_index = FREQ_INDEX_1280,
		.fps = 30,
		.frame_len = 2526,
		.llp = HI846_LINE_LENGTH,
		.reg_list_config = {
			.num_of_regs = ARRAY_SIZE(mode_1632x1224_config),
			.regs = mode_1632x1224_config,
		},
		.reg_list_2lane = {
			.num_of_regs = ARRAY_SIZE(mode_1632x1224_mipi_2lane),
			.regs = mode_1632x1224_mipi_2lane,
		},
		.reg_list_4lane = {
			.num_of_regs = ARRAY_SIZE(mode_1632x1224_mipi_4lane),
			.regs = mode_1632x1224_mipi_4lane,
		},
	}
};

struct hi846_gpio {
	int gpio;
	int level;
};

struct hi846_datafmt {
	u32 code;
	enum v4l2_colorspace colorspace;
};

struct hi846 {
	struct hi846_gpio rst_gpio;
	struct regulator *vdd1_regulator;
	struct regulator *vdd_regulator;
	struct clk *clock;
	struct hi846_datafmt *fmt;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;
	u8 nr_lanes;

	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *exposure;

	const struct hi846_mode *cur_mode;
	struct mutex mutex;
	bool streaming;
};

static struct hi846_datafmt hi846_colour_fmts[] = {
	{HI846_MEDIA_BUS_FORMAT, V4L2_COLORSPACE_RAW},
};

static struct hi846_datafmt *hi846_find_datafmt(u32 code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hi846_colour_fmts); i++)
		if (hi846_colour_fmts[i].code == code)
			return hi846_colour_fmts + i;

	return NULL;
}

static inline u8 hi846_get_link_freq_index(struct hi846 *hi846)
{
	return hi846->cur_mode->link_freq_index;
}

static s64 hi846_get_link_freq(struct hi846 *hi846)
{
	u8 index = hi846_get_link_freq_index(hi846);

	return *(hi846_link_freqs + index);
}

static u64 hi846_calc_pixel_rate(struct hi846 *hi846)
{
	s64 link_freq = hi846_get_link_freq(hi846);
	u64 pixel_rate = link_freq * 2 * hi846->nr_lanes;

	do_div(pixel_rate, HI846_RGB_DEPTH);

	return pixel_rate;
}

static int hi846_read_reg(struct hi846 *hi846, u16 reg, u8 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&hi846->sd);
	struct i2c_msg msgs[2];
	u8 addr_buf[2];
	u8 data_buf[1] = {0};
	int ret;

	put_unaligned_be16(reg, addr_buf);
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = sizeof(addr_buf);
	msgs[0].buf = addr_buf;
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].buf = &data_buf[0];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs)) {
		dev_err(&client->dev, "i2c read error: %d\n", ret);
		return -EIO;
	}

	*val = data_buf[0];

	return 0;
}

static int hi846_write_reg_16(struct hi846 *hi846, u16 reg, u16 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&hi846->sd);
	u8 buf[6];

	put_unaligned_be16(reg, buf);
	put_unaligned_be32(val << 8 * 2, buf + 2);
	if (i2c_master_send(client, buf, 4) != 4)
		return -EIO;

	return 0;
}

static int hi846_write_reg_list(struct hi846 *hi846,
				const struct hi846_reg_list *r_list)
{
	struct i2c_client *client = v4l2_get_subdevdata(&hi846->sd);
	unsigned int i;
	int ret;

	for (i = 0; i < r_list->num_of_regs; i++) {
		ret = hi846_write_reg_16(hi846, r_list->regs[i].address,
					 r_list->regs[i].val);
		if (ret) {
			dev_err_ratelimited(&client->dev,
					    "failed to write reg 0x%4.4x. error = %d",
					    r_list->regs[i].address, ret);
			return ret;
		}
	}

	return 0;
}

static int hi846_write_reg(struct hi846 *hi846, u16 reg, u8 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&hi846->sd);
	u8 buf[3] = { reg >> 8, reg & 0xff, val };
	int ret;

	struct i2c_msg msg[] = {
		{ .addr = client->addr, .flags = 0,
		  .len = ARRAY_SIZE(buf), .buf = buf },
	};

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret != ARRAY_SIZE(msg)) {
		dev_err(&client->dev, "i2c write error\n");
		return -EIO;
	}

	return 0;
}

static int hi846_update_digital_gain(struct hi846 *hi846, u16 d_gain)
{
	int ret;

	ret = hi846_write_reg_16(hi846, HI846_REG_MWB_GR_GAIN_H, d_gain);
	if (ret)
		return ret;

	ret = hi846_write_reg_16(hi846, HI846_REG_MWB_GB_GAIN_H, d_gain);
	if (ret)
		return ret;

	ret = hi846_write_reg_16(hi846, HI846_REG_MWB_R_GAIN_H, d_gain);
	if (ret)
		return ret;

	return hi846_write_reg_16(hi846, HI846_REG_MWB_B_GAIN_H, d_gain);
}

static int hi846_test_pattern(struct hi846 *hi846, u32 pattern)
{
	int ret;
	u8 val;

	if (pattern) {
		ret = hi846_read_reg(hi846, HI846_REG_ISP, &val);
		if (ret)
			return ret;

		ret = hi846_write_reg(hi846, HI846_REG_ISP,
				      val | HI846_REG_ISP_TPG_EN);
		if (ret)
			return ret;
	}

	return hi846_write_reg(hi846, HI846_REG_TEST_PATTERN, pattern);
}

static int hi846_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct hi846 *hi846 = container_of(ctrl->handler,
					     struct hi846, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&hi846->sd);
	s64 exposure_max;
	int ret = 0;
	u32 shutter, frame_len;

	if (!pm_runtime_get_if_in_use(&client->dev))
		goto out;

	/* Propagate change of current control to all related controls */
	if (ctrl->id == V4L2_CID_VBLANK) {
		/* Update max exposure while meeting expected vblanking */
		exposure_max = hi846->cur_mode->height + ctrl->val -
			       HI846_EXPOSURE_MAX_MARGIN;
		__v4l2_ctrl_modify_range(hi846->exposure,
					 hi846->exposure->minimum,
					 exposure_max, hi846->exposure->step,
					 exposure_max);
	}

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = hi846_write_reg(hi846, HI846_REG_ANALOG_GAIN, ctrl->val);
		break;

	case V4L2_CID_DIGITAL_GAIN:
		ret = hi846_update_digital_gain(hi846, ctrl->val);
		break;

	case V4L2_CID_EXPOSURE:
		shutter = ctrl->val;
		frame_len = hi846->cur_mode->frame_len;

		if (shutter > frame_len - 6) { /* margin */
			frame_len = shutter + 6;
			if (frame_len > 0xffff) { /* max frame len */
				frame_len = 0xffff;
			}
		}

		if (shutter < 6)
			shutter = 6;
		if (shutter > (0xffff - 6))
			shutter = 0xffff - 6;

		ret = hi846_write_reg_16(hi846, HI846_REG_FLL,
					 frame_len);
		ret = hi846_write_reg_16(hi846, HI846_REG_EXPOSURE,
					 shutter);
		break;

	case V4L2_CID_VBLANK:
		/* Update FLL that meets expected vertical blanking */
		ret = hi846_write_reg_16(hi846, HI846_REG_FLL,
					 hi846->cur_mode->height + ctrl->val);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = hi846_test_pattern(hi846, ctrl->val);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(&client->dev);
out:
	return ret;
}

static const struct v4l2_ctrl_ops hi846_ctrl_ops = {
	.s_ctrl = hi846_set_ctrl,
};

static int hi846_init_controls(struct hi846 *hi846)
{
	struct v4l2_ctrl_handler *ctrl_hdlr;
	s64 exposure_max, h_blank;
	int ret;
	struct i2c_client *client = v4l2_get_subdevdata(&hi846->sd);

	ctrl_hdlr = &hi846->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 8);
	if (ret)
		return ret;

	ctrl_hdlr->lock = &hi846->mutex;

	hi846->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr, &hi846_ctrl_ops,
						  V4L2_CID_LINK_FREQ,
					ARRAY_SIZE(hi846_link_freqs) - 1,
					0, hi846_link_freqs);
	if (hi846->link_freq)
		hi846->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	hi846->pixel_rate = v4l2_ctrl_new_std
			    (ctrl_hdlr, &hi846_ctrl_ops,
			     V4L2_CID_PIXEL_RATE, 0,
			     hi846_calc_pixel_rate(hi846),
			     1,
			     hi846_calc_pixel_rate(hi846));
	hi846->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &hi846_ctrl_ops,
					  V4L2_CID_VBLANK,
					  hi846->cur_mode->frame_len -
					  hi846->cur_mode->height,
					  HI846_FLL_MAX -
					  hi846->cur_mode->height, 1,
					  hi846->cur_mode->frame_len -
					  hi846->cur_mode->height);

	h_blank = hi846->cur_mode->llp - hi846->cur_mode->width;

	hi846->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &hi846_ctrl_ops,
					  V4L2_CID_HBLANK, h_blank, h_blank, 1,
					  h_blank);
	if (hi846->hblank)
		hi846->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(ctrl_hdlr, &hi846_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  HI846_ANAL_GAIN_MIN, HI846_ANAL_GAIN_MAX,
			  HI846_ANAL_GAIN_STEP, HI846_ANAL_GAIN_MIN);
	v4l2_ctrl_new_std(ctrl_hdlr, &hi846_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  HI846_DGTL_GAIN_MIN, HI846_DGTL_GAIN_MAX,
			  HI846_DGTL_GAIN_STEP, HI846_DGTL_GAIN_DEFAULT);
	exposure_max = hi846->cur_mode->frame_len - HI846_EXPOSURE_MAX_MARGIN;
	hi846->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &hi846_ctrl_ops,
					    V4L2_CID_EXPOSURE,
					    HI846_EXPOSURE_MIN, exposure_max,
					    HI846_EXPOSURE_STEP,
					    exposure_max);
	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &hi846_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(hi846_test_pattern_menu) - 1,
				     0, 0, hi846_test_pattern_menu);
	if (ctrl_hdlr->error) {
		dev_err(&client->dev, "v4l ctrl handler error: %d\n",
			ctrl_hdlr->error);
		return ctrl_hdlr->error;
	}

	hi846->sd.ctrl_handler = ctrl_hdlr;

	return 0;
}

static int hi846_set_video_mode(struct hi846 *hi846, int fps)
{
	struct i2c_client *client = v4l2_get_subdevdata(&hi846->sd);
	int frame_length;
	int ret;
	int dummy_lines;

	dev_dbg(&client->dev, "%s: link freq: %lld\n", __func__,
		hi846_get_link_freq(hi846));

	frame_length = hi846_get_link_freq(hi846) / fps / HI846_LINE_LENGTH;
	dummy_lines = (frame_length > hi846->cur_mode->frame_len) ?
			(frame_length - hi846->cur_mode->frame_len) : 0;

	frame_length = hi846->cur_mode->frame_len + dummy_lines;

	dev_dbg(&client->dev, "%s: frame length calculated: %d\n", __func__,
		frame_length);

	ret = hi846_write_reg_16(hi846, HI846_REG_FLL, frame_length & 0xFFFF);
	ret = hi846_write_reg_16(hi846, HI846_REG_LLP, HI846_LINE_LENGTH & 0xFFFF);

	return ret;
}

static int hi846_start_streaming(struct hi846 *hi846)
{
	struct i2c_client *client = v4l2_get_subdevdata(&hi846->sd);
	int ret = 0;
	u8 val;

	if (hi846->nr_lanes == 2)
		ret = hi846_write_reg_list(hi846, &hi846_init_regs_list_2lane);
	else
		ret = hi846_write_reg_list(hi846, &hi846_init_regs_list_4lane);
	if (ret) {
		dev_err(&client->dev, "failed to set plls: %d\n", ret);
		return ret;
	}

	ret = hi846_write_reg_list(hi846, &hi846->cur_mode->reg_list_config);
	if (ret) {
		dev_err(&client->dev, "failed to set mode: %d\n", ret);
		return ret;
	}

	if (hi846->nr_lanes == 2) {
		if (!&hi846->cur_mode->reg_list_2lane) {
			dev_err(&client->dev, "2 lanes unsupported for this mode\n");
			return -EINVAL;
		}
		ret = hi846_write_reg_list(hi846, &hi846->cur_mode->reg_list_2lane);
	} else {
		if (!&hi846->cur_mode->reg_list_4lane) {
			dev_err(&client->dev, "4 lanes unsupported for this mode\n");
			return -EINVAL;
		}
		ret = hi846_write_reg_list(hi846, &hi846->cur_mode->reg_list_4lane);
	}
	if (ret) {
		dev_err(&client->dev, "failed to set mipi mode: %d\n", ret);
		return ret;
	}

	hi846_set_video_mode(hi846, hi846->cur_mode->fps);

	ret = __v4l2_ctrl_handler_setup(hi846->sd.ctrl_handler);
	if (ret)
		return ret;

	/*
	 * Reading 0x0034 is purely done for debugging reasons: It is not
	 * documented in the DS but only mentioned once:
	 * "If 0x0034[2] bit is disabled , Visible pixel width and height is 0."
	 * So even though that sounds like we won't see anything, we don't
	 * know more about this, so in that case only inform the user but do
	 * nothing more.
	 */
	ret = hi846_read_reg(hi846, 0x0034, &val);
	if (ret)
		return ret;
	if (!(val & BIT(2)))
		dev_info(&client->dev, "visible pixel width and height is 0\n");

	ret = hi846_write_reg(hi846, HI846_REG_MODE_SELECT,
			      HI846_MODE_STREAMING);
	if (ret) {
		dev_err(&client->dev, "failed to start stream");
		return ret;
	}

	hi846->streaming = 1;

	dev_dbg(&client->dev, "%s: started streaming successfully\n", __func__);

	return ret;
}

static void hi846_stop_streaming(struct hi846 *hi846)
{
	struct i2c_client *client = v4l2_get_subdevdata(&hi846->sd);

	if (hi846_write_reg(hi846, HI846_REG_MODE_SELECT, HI846_MODE_STANDBY))
		dev_err(&client->dev, "failed to stop stream");

	hi846->streaming = 0;
}

static int hi846_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct hi846 *hi846 = to_hi846(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	if (hi846->streaming == enable)
		return 0;

	mutex_lock(&hi846->mutex);

	if (enable) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto out;
		}

		ret = hi846_start_streaming(hi846);
		if (ret) {
			enable = 0;
			hi846_stop_streaming(hi846);
			pm_runtime_put(&client->dev);
		}
	} else {
		hi846_stop_streaming(hi846);
		pm_runtime_put(&client->dev);
	}

out:
	mutex_unlock(&hi846->mutex);

	return ret;
}

static int hi846_regulator_enable(struct hi846 *hi846)
{
	int ret = 0;

	ret = regulator_enable(hi846->vdd_regulator);
	if (ret)
		return ret;

	return regulator_enable(hi846->vdd1_regulator);
}

static void hi846_regulator_disable(struct hi846 *hi846)
{
	if (regulator_is_enabled(hi846->vdd1_regulator))
		regulator_disable(hi846->vdd1_regulator);

	if (regulator_is_enabled(hi846->vdd_regulator))
		regulator_disable(hi846->vdd_regulator);
}

static int __maybe_unused hi846_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct hi846 *hi846 = to_hi846(sd);

	if (hi846->streaming)
		hi846_stop_streaming(hi846);

	if (!IS_ERR(hi846->clock))
		clk_disable_unprepare(hi846->clock);

	hi846_regulator_disable(hi846);

	return 0;
}

static int __maybe_unused hi846_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct hi846 *hi846 = to_hi846(sd);
	int ret;

	ret = hi846_regulator_enable(hi846);
	if (ret) {
		dev_err(dev, "enable regulator failed: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(hi846->clock);
	if (ret < 0) {
		dev_err(dev, "enable clk failed: %d\n", ret);
		goto error_regulator;
	}

	msleep(100);

	if (hi846->streaming) {
		ret = hi846_start_streaming(hi846);
		if (ret) {
			dev_err(dev, "%s: start streaming failed: %d\n",
				__func__, ret);
			goto error;
		}
	}

	return 0;

error:
	clk_disable_unprepare(hi846->clock);

error_regulator:
	hi846_regulator_disable(hi846);

	return ret;
}

static int hi846_set_format(struct v4l2_subdev *sd,
			    struct v4l2_subdev_pad_config *cfg,
			    struct v4l2_subdev_format *format)
{
	struct hi846 *hi846 = to_hi846(sd);
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct hi846_datafmt *fmt = hi846_find_datafmt(mf->code);
	u32 tgt_fps;
	s32 vblank_def, h_blank;

	if (!fmt) {
		mf->code = hi846_colour_fmts[0].code;
		mf->colorspace = hi846_colour_fmts[0].colorspace;
		fmt = &hi846_colour_fmts[0];
	}

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_get_try_format(sd, cfg, format->pad) = *mf;
		return 0;
	}

	mutex_lock(&hi846->mutex);

	if (hi846->streaming) {
		mutex_unlock(&hi846->mutex);
		return -EBUSY;
	}

	hi846->fmt = fmt;

	hi846->cur_mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes), width,
				      height, mf->width,
				      mf->height);
	dev_dbg(&client->dev, "%s: found mode: %dx%d\n", __func__,
		hi846->cur_mode->width, hi846->cur_mode->height);

	tgt_fps = hi846->cur_mode->fps;
	dev_dbg(&client->dev, "%s: target fps: %d\n", __func__, tgt_fps);

	mf->width = hi846->cur_mode->width;
	mf->height = hi846->cur_mode->height;
	mf->code = HI846_MEDIA_BUS_FORMAT;
	mf->field = V4L2_FIELD_NONE;

	__v4l2_ctrl_s_ctrl(hi846->link_freq, hi846_get_link_freq_index(hi846));
	__v4l2_ctrl_s_ctrl_int64(hi846->pixel_rate,
				 hi846_calc_pixel_rate(hi846));

	/* Update limits and set FPS to default */
	vblank_def = hi846->cur_mode->frame_len - hi846->cur_mode->height;
	__v4l2_ctrl_modify_range(hi846->vblank,
				 hi846->cur_mode->frame_len - hi846->cur_mode->height,
				 HI846_FLL_MAX - hi846->cur_mode->height, 1,
				 vblank_def);
	__v4l2_ctrl_s_ctrl(hi846->vblank, vblank_def);

	h_blank = hi846->cur_mode->llp - hi846->cur_mode->width;

	__v4l2_ctrl_modify_range(hi846->hblank, h_blank, h_blank, 1,
				 h_blank);

	dev_dbg(&client->dev, "Set fmt w=%d h=%d code=0x%x colorspace=0x%x\n",
		mf->width, mf->height,
		fmt->code, fmt->colorspace);

	mutex_unlock(&hi846->mutex);

	return 0;
}

static int hi846_get_format(struct v4l2_subdev *sd,
			    struct v4l2_subdev_pad_config *cfg,
			    struct v4l2_subdev_format *format)
{
	struct hi846 *hi846 = to_hi846(sd);
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (format->pad)
		return -EINVAL;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY) {
		format->format = *v4l2_subdev_get_try_format(&hi846->sd,
							cfg,
							format->pad);
		return 0;
	}

	mutex_lock(&hi846->mutex);
	mf->code        = HI846_MEDIA_BUS_FORMAT;
	mf->colorspace  = V4L2_COLORSPACE_RAW;
	mf->field       = V4L2_FIELD_NONE;
	mf->width       = hi846->cur_mode->width;
	mf->height      = hi846->cur_mode->height;
	mutex_unlock(&hi846->mutex);
	dev_dbg(&client->dev,
		"Get format w=%d h=%d code=0x%x colorspace=0x%x\n",
		mf->width, mf->height, mf->code, mf->colorspace);

	return 0;
}

static int hi846_enum_mbus_code(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index > 0)
		return -EINVAL;

	code->code = HI846_MEDIA_BUS_FORMAT;

	return 0;
}

static int hi846_enum_frame_size(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_frame_size_enum *fse)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (fse->pad || fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != HI846_MEDIA_BUS_FORMAT) {
		dev_err(&client->dev, "frame size enum not matching\n");
		return -EINVAL;
	}

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = supported_modes[fse->index].width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = supported_modes[fse->index].height;

	dev_dbg(&client->dev, "%s: max width: %d max height: %d\n", __func__,
		fse->max_width, fse->max_height);

	return 0;
}

static int hi846_s_power(struct v4l2_subdev *sd, int on)
{
	struct hi846 *hi846 = to_hi846(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct device *dev = &client->dev;
	int ret = 0;

	mutex_lock(&hi846->mutex);
	if (on) {
		ret = hi846_resume(dev);
		if (ret)
			goto out;
	} else {
		ret = hi846_suspend(dev);
		if (ret)
			goto out;
	}
out:
	mutex_unlock(&hi846->mutex);
	return ret;
}

static int hi846_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct hi846 *hi846 = to_hi846(sd);
	struct v4l2_mbus_framefmt *mf;

	mf = v4l2_subdev_get_try_format(sd, fh->pad, 0);

	mutex_lock(&hi846->mutex);
	mf->code        = HI846_MEDIA_BUS_FORMAT;
	mf->colorspace  = V4L2_COLORSPACE_RAW;
	mf->field       = V4L2_FIELD_NONE;
	mf->width       = hi846->cur_mode->width;
	mf->height      = hi846->cur_mode->height;
	mutex_unlock(&hi846->mutex);

	return 0;
}

static const struct v4l2_subdev_video_ops hi846_video_ops = {
	.s_stream = hi846_set_stream,
};

static const struct v4l2_subdev_pad_ops hi846_pad_ops = {
	.enum_frame_size = hi846_enum_frame_size,
	.enum_mbus_code = hi846_enum_mbus_code,
	.set_fmt = hi846_set_format,
	.get_fmt = hi846_get_format,
};

static struct v4l2_subdev_core_ops hi846_core_ops = {
	.s_power = hi846_s_power,
	.log_status = v4l2_ctrl_subdev_log_status,
};

static const struct v4l2_subdev_ops hi846_subdev_ops = {
	.core = &hi846_core_ops,
	.video = &hi846_video_ops,
	.pad = &hi846_pad_ops,
};

static const struct media_entity_operations hi846_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops hi846_internal_ops = {
	.open = hi846_open,
};

static void hi846_rst_gpio_assert(struct hi846 *hi846, bool on)
{
	struct hi846_gpio *gpio = &hi846->rst_gpio;

	if (gpio == 0)
		return;

	if (on)
		gpio_set_value(gpio->gpio, gpio->level);
	else
		gpio_set_value(gpio->gpio, !gpio->level);
}

static int hi846_parse_gpio(struct hi846_gpio *gpio, struct device *dev)
{
	struct device_node *node = dev->of_node;
	enum of_gpio_flags flags;
	int ret;

	ret = of_get_named_gpio_flags(node, "rst-gpios", 0, &flags);
	if (ret < 0) {
		dev_err(dev, "no rst-gpios provided: %d\n", ret);
		return ret;
	}
	gpio->gpio = ret;
	gpio->level = !(flags & OF_GPIO_ACTIVE_LOW);

	return 0;
}

static int hi846_identify_module(struct hi846 *hi846)
{
	struct i2c_client *client = v4l2_get_subdevdata(&hi846->sd);
	int ret;
	u8 hi, lo;

	ret = hi846_read_reg(hi846, HI846_REG_CHIP_ID_L, &lo);
	if (ret)
		return ret;

	if (lo != HI846_CHIP_ID_L) {
		dev_err(&client->dev, "wrong chip id low byte: %x", lo);
		return -ENXIO;
	}

	ret = hi846_read_reg(hi846, HI846_REG_CHIP_ID_H, &hi);
	if (ret)
		return ret;

	if (hi != HI846_CHIP_ID_H) {
		dev_err(&client->dev, "wrong chip id high byte: %x", hi);
		return -ENXIO;
	}

	dev_info(&client->dev, "chip id %02X %02X / %d mipi lanes\n", hi, lo,
		 hi846->nr_lanes);

	return 0;
}

static int hi846_check_hwcfg(struct device *dev, u8 *lanes)
{
	struct fwnode_handle *ep;
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	int ret = 0;

	if (!fwnode)
		return -ENXIO;

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return -ENXIO;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	if (bus_cfg.bus.mipi_csi2.num_data_lanes != 2 &&
	    bus_cfg.bus.mipi_csi2.num_data_lanes != 4) {
		dev_err(dev, "number of CSI2 data lanes %d is not supported",
			bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto check_hwcfg_error;
	}

	*lanes = bus_cfg.bus.mipi_csi2.num_data_lanes;

check_hwcfg_error:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

static int hi846_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct hi846 *hi846 = to_hi846(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		hi846_suspend(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	mutex_destroy(&hi846->mutex);

	return 0;
}

static int hi846_probe(struct i2c_client *client)
{
	struct hi846 *hi846;
	int ret;
	u8 lanes;
	struct dentry *d;

	ret = hi846_check_hwcfg(&client->dev, &lanes);
	if (ret) {
		dev_err(&client->dev, "failed to check HW configuration: %d",
			ret);
		return ret;
	}

	hi846 = devm_kzalloc(&client->dev, sizeof(*hi846), GFP_KERNEL);
	if (!hi846)
		return -ENOMEM;

	hi846->nr_lanes = lanes;

	v4l2_i2c_subdev_init(&hi846->sd, client, &hi846_subdev_ops);

	ret = hi846_parse_gpio(&hi846->rst_gpio, &client->dev);
	if (ret < 0) {
		dev_err(&client->dev, "parse gpio failed: %d\n", ret);
		return ret;
	}

	hi846->vdd_regulator = devm_regulator_get(&client->dev, "vdd");
	if (IS_ERR(hi846->vdd_regulator))
		dev_warn(&client->dev, "cannot get voltage regulator\n");

	hi846->vdd1_regulator = devm_regulator_get(&client->dev, "vdd1");
	if (IS_ERR(hi846->vdd1_regulator))
		dev_warn(&client->dev, "cannot get voltage regulator\n");

	ret = hi846_regulator_enable(hi846);
	if (ret) {
		dev_err(&client->dev, "regulator enable failed: %d\n", ret);
		return ret;
	}

	hi846->clock = devm_clk_get(hi846->sd.dev, "mclk");
	if (IS_ERR(hi846->clock)) {
		dev_err(&client->dev, "get clk failed\n");
		ret = -EPROBE_DEFER;
		goto probe_error_regulator;
	}

	ret = clk_prepare_enable(hi846->clock);
	if (ret < 0)
		goto probe_error_regulator;

	msleep(100);

	hi846_rst_gpio_assert(hi846, true);

	ret = hi846_identify_module(hi846);
	if (ret) {
		dev_err(&client->dev, "failed to find sensor: %d", ret);
		goto probe_error_regulator;
	}

	mutex_init(&hi846->mutex);

	hi846->cur_mode = &supported_modes[0];
	ret = hi846_init_controls(hi846);
	if (ret) {
		dev_err(&client->dev, "failed to init controls: %d", ret);
		goto probe_error_v4l2_ctrl_handler_free;
	}

	hi846->sd.internal_ops = &hi846_internal_ops;
	hi846->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	hi846->sd.entity.ops = &hi846_subdev_entity_ops;
	hi846->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	hi846->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&hi846->sd.entity, 1, &hi846->pad);
	if (ret) {
		dev_err(&client->dev, "failed to init entity pads: %d", ret);
		goto probe_error_v4l2_ctrl_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor_common(&hi846->sd);
	if (ret < 0) {
		dev_err(&client->dev, "failed to register V4L2 subdev: %d",
			ret);
		goto probe_error_media_entity_cleanup;
	}

	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	return 0;

probe_error_media_entity_cleanup:
	media_entity_cleanup(&hi846->sd.entity);

probe_error_v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(hi846->sd.ctrl_handler);
	mutex_destroy(&hi846->mutex);

probe_error_regulator:
	hi846_regulator_disable(hi846);

	return ret;
}

UNIVERSAL_DEV_PM_OPS(hi846_pm_ops, hi846_suspend, hi846_resume, NULL);

static const struct of_device_id hi846_of_match[] = {
	{ .compatible = "hynix,hi846", },
	{},
};
MODULE_DEVICE_TABLE(of, hi846_of_match);

static struct i2c_driver hi846_i2c_driver = {
	.driver = {
		.name = "hi846",
		.pm = &hi846_pm_ops,
		.acpi_match_table = ACPI_PTR(hi846_acpi_ids),
		.of_match_table = of_match_ptr(hi846_of_match),
	},
	.probe_new = hi846_probe,
	.remove = hi846_remove,
};

module_i2c_driver(hi846_i2c_driver);

MODULE_AUTHOR("Angus Ainslie <angus@akkea.ca>");
MODULE_AUTHOR("Martin Kepplinger <martin.kepplinger@puri.sm>");
MODULE_DESCRIPTION("Hynix HI846 sensor driver");
MODULE_LICENSE("GPL v2");
