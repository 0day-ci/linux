/* SPDX-License-Identifier: GPL-2.0 */
/*
 * KMS driver for Loongson display controller
 */

/*
 * Authors:
 *      Sui Jingfeng <suijingfeng@loongson.cn>
 */

#ifndef __LSDC_REGS_H__
#define __LSDC_REGS_H__

#include <linux/bitops.h>
#include <linux/types.h>

/*
 * PLL
 */
#define LSDC_PLL_REF_CLK                100000           /* kHz */

/*
 * Those PLL registers are not located at DC reg bar space,
 * there are relative to LSXXXXX_CFG_REG_BASE.
 * XXXXX = 7A1000, 2K1000, 2K0500
 */

/* LS2K1000 */
#define LS2K1000_PIX_PLL0_REG           0x04B0
#define LS2K1000_PIX_PLL1_REG           0x04C0
#define LS2K1000_CFG_REG_BASE           0x1fe10000

/* LS7A1000 */
#define LS7A1000_PIX_PLL0_REG           0x04B0
#define LS7A1000_PIX_PLL1_REG           0x04C0
#define LS7A1000_CFG_REG_BASE           0x10010000

/* LS2K0500 */
#define LS2K0500_PIX_PLL0_REG           0x0418
#define LS2K0500_PIX_PLL1_REG           0x0420
#define LS2K0500_CFG_REG_BASE           0x1fe10000

/*
 *  CRTC CFG REG
 */
#define CFG_PIX_FMT_MASK                GENMASK(2, 0)

#define CFG_PAGE_FLIP_BIT               BIT(7)     /* triger pageflip */
#define CFG_OUTPUT_EN_BIT               BIT(8)
#define CFG_PANEL_SWITCH                BIT(9)     /* Indicate witch fb addr reg is in using */
#define CFG_FB_IDX_BIT                  BIT(11)
#define CFG_GAMMAR_EN_BIT               BIT(12)

/* CRTC get soft reset if voltage level change from 1 -> 0 */
#define CFG_RESET_BIT                   BIT(20)

#define EN_HSYNC_BIT                    BIT(30)
#define INV_HSYNC_BIT                   BIT(31)
#define EN_VSYNC_BIT                    BIT(30)
#define INV_VSYNC_BIT                   BIT(31)

/******** CRTC0 & DVO0 ********/
#define LSDC_CRTC0_CFG_REG              0x1240
#define LSDC_CRTC0_FB_ADDR0_REG         0x1260
#define LSDC_CRTC0_FB_ADDR1_REG         0x1580
#define LSDC_CRTC0_STRIDE_REG           0x1280
#define LSDC_CRTC0_FB_ORIGIN_REG        0x1300
#define LSDC_CRTC0_HDISPLAY_REG         0x1400
#define LSDC_CRTC0_HSYNC_REG            0x1420
#define LSDC_CRTC0_VDISPLAY_REG         0x1480
#define LSDC_CRTC0_VSYNC_REG            0x14a0

/******** CTRC1 & DVO1 ********/
#define LSDC_CRTC1_CFG_REG              0x1250
#define LSDC_CRTC1_FB_ADDR0_REG         0x1270
#define LSDC_CRTC1_FB_ADDR1_REG         0x1590
#define LSDC_CRTC1_STRIDE_REG           0x1290
#define LSDC_CRTC1_FB_ORIGIN_REG        0x1310
#define LSDC_CRTC1_HDISPLAY_REG         0x1410
#define LSDC_CRTC1_HSYNC_REG            0x1430
#define LSDC_CRTC1_VDISPLAY_REG         0x1490
#define LSDC_CRTC1_VSYNC_REG            0x14b0

/*
 * Hardware cursor
 * There is only one hardware cursor shared by two CRTC in ls7a1000,
 * ls2k1000 and ls2k0500, but ls7a2000 have two Hardware cursor.
 */
#define LSDC_CURSOR_CFG_REG             0x1520

#define CURSOR_FORMAT_MASK              GENMASK(1, 0)
#define CURSOR_FORMAT_DISABLE           0
#define CURSOR_FORMAT_MONOCHROME        BIT(0)
#define CURSOR_FORMAT_ARGB8888          BIT(1)
#define CURSOR_LOCATION_BIT             BIT(4)

#define LSDC_CURSOR_ADDR_REG            0x1530
#define LSDC_CURSOR_POSITION_REG        0x1540
#define LSDC_CURSOR_BG_COLOR_REG        0x1550  /* background color */
#define LSDC_CURSOR_FG_COLOR_REG        0x1560  /* foreground color */

#define CUR_WIDTH_SIZE                  32
#define CUR_HEIGHT_SIZE                 32

#define LSDC_CURS_MIN_SIZE              1
#define LSDC_CURS_MAX_SIZE              64

/*
 * DC Interrupt Control Register, 32bit, Address Offset: 1570
 *
 * Bits  0:10 inidicate the interrupt type, read only
 * Bits 16:26 control if the specific interrupt corresponding to bit 0~10
 * is enabled or not. Write 1 to enable, write 0 to disable
 *
 * RF: Read Finished
 * IDBU : Internal Data Buffer Underflow
 * IDBFU : Internal Data Buffer Fatal Underflow
 *
 *
 * +-------+-------------------------------+-------+--------+--------+-------+
 * | 31:27 |            26:16              | 15:11 |   10   |   9    |   8   |
 * +-------+-------------------------------+-------+--------+--------+-------+
 * |  N/A  | Interrupt Enable Control Bits |  N/A  | IDBFU0 | IDBFU1 | IDBU0 |
 * +-------+-------------------------------+-------+--------+--------+-------+
 *
 * Bit 4 is cursor buffer read finished, no use.
 *
 * +-------+-----+-----+-----+--------+--------+--------+--------+
 * |   7   |  6  |  5  |  4  |   3    |   2    |   1    |   0    |
 * +-------+-----+-----+-----+--------+--------+--------+--------+
 * | IDBU1 | RF0 | RF1 |     | HSYNC0 | VSYNC0 | HSYNC1 | VSYNC1 |
 * +-------+-----+-----+-----+--------+--------+--------+--------+
 *
 */

#define LSDC_INT_REG                           0x1570

#define INT_CRTC0_VS                           BIT(2)
#define INT_CRTC0_HS                           BIT(3)
#define INT_CRTC0_RF                           BIT(6)
#define INT_CRTC0_IDBU                         BIT(8)
#define INT_CRTC0_IDBFU                        BIT(10)

#define INT_CURSOR_RF                          BIT(4)

#define INT_CRTC1_VS                           BIT(0)
#define INT_CRTC1_HS                           BIT(1)
#define INT_CRTC1_RF                           BIT(5)
#define INT_CRTC1_IDBU                         BIT(7)
#define INT_CRTC1_IDBFU                        BIT(9)

#define INT_CRTC0_VS_EN                        BIT(18)
#define INT_CRTC0_HS_EN                        BIT(19)
#define INT_CRTC0_RF_EN                        BIT(22)
#define INT_CRTC0_IDBU_EN                      BIT(24)
#define INT_CRTC0_IDBFU_EN                     BIT(26)

#define INT_CURSOR_RF_EN                       BIT(20)

#define INT_CRTC1_VS_EN                        BIT(16)
#define INT_CRTC1_HS_EN                        BIT(17)
#define INT_CRTC1_RF_EN                        BIT(21)
#define INT_CRTC1_IDBU_EN                      BIT(23)
#define INT_CRTC1_IDBFU_EN                     BIT(25)

#define INT_STATUS_MASK                        GENMASK(10, 0)

/*
 * LS7A1000 have 4 gpio which is under control of the LS7A_DC_GPIO_DAT_REG
 * and LS7A_DC_GPIO_DIR_REG register, it has no relationship whth the general
 * GPIO hardware as those two registers are in the DC register space on
 * LS7A1000.
 *
 * This driver using those GPIO to emulated I2C, for reading edid and
 * monitor detection
 *
 * LS2k1000 and LS2K0500 don't have those registers, they use hardware i2c or
 * generial GPIO emulated i2c from other module.
 *
 * GPIO data register
 *  Address offset: 0x1650
 *   +---------------+-----------+-----------+
 *   | 7 | 6 | 5 | 4 |  3  |  2  |  1  |  0  |
 *   +---------------+-----------+-----------+
 *   |               |    DVO1   |    DVO0   |
 *   +      N/A      +-----------+-----------+
 *   |               | SCL | SDA | SCL | SDA |
 *   +---------------+-----------+-----------+
 */
#define LS7A_DC_GPIO_DAT_REG                   0x1650

/*
 *  GPIO Input/Output direction control register
 *  Address offset: 0x1660
 *  write 1 for Input, 0 for Output.
 */
#define LS7A_DC_GPIO_DIR_REG                   0x1660

#endif
