// SPDX-License-Identifier: GPL-2.0
/*
 * Based on the H616 CCU driver, which is:
 *   Copyright (c) 2020 Arm Ltd.
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#include "ccu_common.h"
#include "ccu_reset.h"

#include "ccu_div.h"
#include "ccu_gate.h"
#include "ccu_mp.h"
#include "ccu_mult.h"
#include "ccu_nk.h"
#include "ccu_nkm.h"
#include "ccu_nkmp.h"
#include "ccu_nm.h"

#include "ccu-sun50i-r329.h"

/*
 * An external divider of PLL-CPUX is controlled here. As it's similar to
 * the external divider of PLL-CPUX on previous SoCs (only usable under
 * 288MHz}, ignore it.
 */
static const char * const cpux_parents[] = { "osc24M", "osc32k", "iosc",
					     "pll-cpux", "pll-periph",
					     "pll-periph-2x",
					     "pll=periph-800m" };
static SUNXI_CCU_MUX(cpux_clk, "cpux", cpux_parents,
		     0x500, 24, 3, CLK_SET_RATE_PARENT | CLK_IS_CRITICAL);
static SUNXI_CCU_M(axi_clk, "axi", "cpux", 0x500, 0, 2, 0);
static SUNXI_CCU_M(cpux_apb_clk, "cpux-apb", "cpux", 0x500, 8, 2, 0);

static const char * const ahb_parents[] = { "osc24M", "osc32k",
					    "iosc", "pll-periph" };
static SUNXI_CCU_MP_WITH_MUX(ahb_clk, "ahb",
			     ahb_parents, 0x510,
			     0, 2,	/* M */
			     8, 2,	/* P */
			     24, 3,	/* mux */
			     0);

static const char * const apb_parents[] = { "osc24M", "osc32k",
					    "ahb", "pll-periph" };
static SUNXI_CCU_MP_WITH_MUX(apb1_clk, "apb1", apb_parents, 0x520,
			     0, 2,	/* M */
			     8, 2,	/* P */
			     24, 3,	/* mux */
			     0);

static SUNXI_CCU_MP_WITH_MUX(apb2_clk, "apb2", apb_parents, 0x524,
			     0, 2,	/* M */
			     8, 2,	/* P */
			     24, 3,	/* mux */
			     0);

static const char * const ce_parents[] = { "osc24M", "pll-periph-2x" };
static SUNXI_CCU_MP_WITH_MUX_GATE(ce_clk, "ce", ce_parents, 0x680,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 1,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_GATE(bus_ce_clk, "bus-ce", "ahb",
		      0x68c, BIT(0), 0);

static const char * const aipu_parents[] = { "pll-periph-2x", "pll-periph-800m",
					     "pll-audio0-div2", "pll-audio0-div5",
					     "pll-cpux" };
static SUNXI_CCU_MP_WITH_MUX_GATE(aipu_clk, "aipu", aipu_parents, 0x6f0,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_GATE(bus_aipu_clk, "bus-aipu", "ahb",
		      0x6fc, BIT(0), 0);

static SUNXI_CCU_GATE(bus_dma_clk, "bus-dma", "ahb",
		      0x70c, BIT(0), 0);

static SUNXI_CCU_GATE(bus_msgbox_clk, "bus-msgbox", "ahb",
		      0x71c, BIT(0), 0);

static SUNXI_CCU_GATE(bus_spinlock_clk, "bus-spinlock", "ahb",
		      0x72c, BIT(0), 0);

static SUNXI_CCU_GATE(bus_hstimer_clk, "bus-hstimer", "ahb",
		      0x73c, BIT(0), 0);

static SUNXI_CCU_GATE(avs_clk, "avs", "osc24M", 0x740, BIT(31), 0);

static SUNXI_CCU_GATE(bus_dbg_clk, "bus-dbg", "ahb",
		      0x78c, BIT(0), 0);

static SUNXI_CCU_GATE(bus_pwm_clk, "bus-pwm", "apb1", 0x7ac, BIT(0), 0);

static const char * const dram_parents[] = { "pll-periph-2x",
					     "pll-periph-800m",
					     "pll-audio0-div2",
					     "pll-audio0-div5" };
static SUNXI_CCU_MP_WITH_MUX_GATE(dram_clk, "dram", dram_parents, 0x800,
				  0, 2,		/* M */
				  8, 2,		/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  CLK_IS_CRITICAL);


static SUNXI_CCU_GATE(mbus_dma_clk, "mbus-dma", "dram",
		      0x804, BIT(0), 0);
static SUNXI_CCU_GATE(mbus_ce_clk, "mbus-ce", "dram",
		      0x804, BIT(2), 0);
static SUNXI_CCU_GATE(mbus_r_dma_clk, "mbus-r-dma", "dram",
		      0x804, BIT(3), 0);
static SUNXI_CCU_GATE(mbus_nand_clk, "mbus-nand", "dram",
		      0x804, BIT(5), 0);
static SUNXI_CCU_GATE(mbus_aipu_clk, "mbus-aipu", "dram",
		      0x804, BIT(16), 0);

static SUNXI_CCU_GATE(bus_dram_clk, "bus-dram", "ahb",
		      0x80c, BIT(0), CLK_IS_CRITICAL);

static const char * const nand_parents[] = { "osc24M", "pll-periph",
					     "pll-audio-div2",
					     "pll-periph-2x" };
static SUNXI_CCU_MP_WITH_MUX_GATE(nand0_clk, "nand0", nand_parents, 0x810,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_MP_WITH_MUX_GATE(nand1_clk, "nand1", nand_parents, 0x814,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_GATE(bus_nand_clk, "bus-nand", "ahb", 0x82c, BIT(0), 0);

static const char * const mmc_parents[] = { "osc24M", "pll-periph",
					    "pll-periph-2x",
					    "pll-audio0-div2" };
static SUNXI_CCU_MP_WITH_MUX_GATE_POSTDIV(mmc0_clk, "mmc0", mmc_parents, 0x830,
					  0, 4,		/* M */
					  8, 2,		/* P */
					  24, 2,	/* mux */
					  BIT(31),	/* gate */
					  2,		/* post-div */
					  0);

static SUNXI_CCU_MP_WITH_MUX_GATE_POSTDIV(mmc1_clk, "mmc1", mmc_parents, 0x834,
					  0, 4,		/* M */
					  8, 2,		/* P */
					  24, 2,	/* mux */
					  BIT(31),	/* gate */
					  2,		/* post-div */
					  0);

static SUNXI_CCU_GATE(bus_mmc0_clk, "bus-mmc0", "ahb", 0x84c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_mmc1_clk, "bus-mmc1", "ahb", 0x84c, BIT(1), 0);

static SUNXI_CCU_GATE(bus_uart0_clk, "bus-uart0", "apb2", 0x90c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_uart1_clk, "bus-uart1", "apb2", 0x90c, BIT(1), 0);
static SUNXI_CCU_GATE(bus_uart2_clk, "bus-uart2", "apb2", 0x90c, BIT(2), 0);
static SUNXI_CCU_GATE(bus_uart3_clk, "bus-uart3", "apb2", 0x90c, BIT(3), 0);

static SUNXI_CCU_GATE(bus_i2c0_clk, "bus-i2c0", "apb2", 0x91c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_i2c1_clk, "bus-i2c1", "apb2", 0x91c, BIT(1), 0);

static SUNXI_CCU_GATE(bus_scr_clk, "bus-scr", "apb2", 0x93c, BIT(0), 0);

static const char * const spi_parents[] = { "osc24M", "pll-periph",
					    "pll-periph-2x",
					    "pll-audio0-div2",
					    "pll-audio0-div5" };
static SUNXI_CCU_MP_WITH_MUX_GATE(spi0_clk, "spi0", spi_parents, 0x940,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_MP_WITH_MUX_GATE(spi1_clk, "spi1", spi_parents, 0x944,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_GATE(bus_spi0_clk, "bus-spi0", "ahb", 0x96c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_spi1_clk, "bus-spi1", "ahb", 0x96c, BIT(1), 0);

static CLK_FIXED_FACTOR(emac_25m_div_clk, "emac-25m-div", "pll-periph",
			2, 1, 0);
static SUNXI_CCU_GATE(emac_25m_clk, "emac-25m", "emac-25m-div", 0x970,
		      BIT(31) | BIT(30), 0);

static SUNXI_CCU_GATE(bus_emac_clk, "bus-emac", "ahb", 0x97c, BIT(0), 0);

static const char * const ir_parents[] = { "osc32k", "iosc",
					   "pll-periph", "pll-audio0-div2" };
static SUNXI_CCU_MP_WITH_MUX_GATE(ir_rx_clk, "ir-rx", ir_parents, 0x990,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_GATE(bus_ir_rx_clk, "bus-ir-rx", "apb1", 0x99c, BIT(0), 0);

static SUNXI_CCU_MP_WITH_MUX_GATE(ir_tx_clk, "ir-tx", ir_parents, 0x9c0,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_GATE(bus_ir_tx_clk, "bus-ir-tx", "apb1", 0x9cc, BIT(0), 0);

static const char * const audio_parents[] = { "pll-audio1",
					      "pll-audio1-4x",
					      "pll-audio0-div2",
					      "pll-audio0-div5" };
static SUNXI_CCU_MP_WITH_MUX_GATE(i2s0_clk, "i2s0", audio_parents, 0xa10,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_MP_WITH_MUX_GATE(i2s1_clk, "i2s1", audio_parents, 0xa14,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_GATE(bus_i2s0_clk, "bus-i2s0", "apb1", 0xa20, BIT(0), 0);
static SUNXI_CCU_GATE(bus_i2s1_clk, "bus-i2s1", "apb1", 0xa20, BIT(1), 0);

static SUNXI_CCU_MP_WITH_MUX_GATE(spdif_clk, "spdif", audio_parents, 0xa20,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_GATE(bus_spdif_clk, "bus-spdif", "apb1", 0xa2c, BIT(0), 0);

/*
 * There are OHCI 12M clock source selection bits for 2 USB 2.0 ports.
 * We will force them to 0 (12M divided from 48M).
 */
#define SUN50I_R329_USB0_CLK_REG		0xa70
#define SUN50I_R329_USB1_CLK_REG		0xa74

static SUNXI_CCU_GATE(usb_ohci0_clk, "usb-ohci0", "osc12M", 0xa70, BIT(31), 0);
static SUNXI_CCU_GATE(usb_phy0_clk, "usb-phy0", "osc24M", 0xa70, BIT(29), 0);

static SUNXI_CCU_GATE(usb_ohci1_clk, "usb-ohci1", "osc12M", 0xa74, BIT(31), 0);
static SUNXI_CCU_GATE(usb_phy1_clk, "usb-phy1", "osc24M", 0xa74, BIT(29), 0);

static SUNXI_CCU_GATE(bus_ohci0_clk, "bus-ohci0", "ahb", 0xa8c, BIT(0), 0);
static SUNXI_CCU_GATE(bus_ohci1_clk, "bus-ohci1", "ahb", 0xa8c, BIT(1), 0);
static SUNXI_CCU_GATE(bus_ehci0_clk, "bus-ehci0", "ahb", 0xa8c, BIT(4), 0);
static SUNXI_CCU_GATE(bus_otg_clk, "bus-otg", "ahb", 0xa8c, BIT(8), 0);

static const char * const ledc_parents[] = { "osc24M", "pll-periph" };
static SUNXI_CCU_MP_WITH_MUX_GATE(ledc_clk, "ledc", ledc_parents, 0xbf0,
				  0, 4,		/* M */
				  8, 2,		/* P */
				  24, 1,	/* mux */
				  BIT(31),	/* gate */
				  0);

static SUNXI_CCU_GATE(bus_ledc_clk, "bus-ledc", "apb1", 0xbfc, BIT(0), 0);

/* Fixed factor clocks */
static CLK_FIXED_FACTOR_FW_NAME(osc12M_clk, "osc12M", "hosc", 2, 1, 0);

static struct ccu_common *sun50i_r329_ccu_clks[] = {
	&cpux_clk.common,
	&axi_clk.common,
	&cpux_apb_clk.common,
	&ahb_clk.common,
	&apb1_clk.common,
	&apb2_clk.common,
	&ce_clk.common,
	&bus_ce_clk.common,
	&aipu_clk.common,
	&bus_aipu_clk.common,
	&bus_dma_clk.common,
	&bus_msgbox_clk.common,
	&bus_spinlock_clk.common,
	&bus_hstimer_clk.common,
	&avs_clk.common,
	&bus_dbg_clk.common,
	&bus_pwm_clk.common,
	&dram_clk.common,
	&mbus_dma_clk.common,
	&mbus_ce_clk.common,
	&mbus_r_dma_clk.common,
	&mbus_nand_clk.common,
	&mbus_aipu_clk.common,
	&bus_dram_clk.common,
	&nand0_clk.common,
	&nand1_clk.common,
	&bus_nand_clk.common,
	&mmc0_clk.common,
	&mmc1_clk.common,
	&bus_mmc0_clk.common,
	&bus_mmc1_clk.common,
	&bus_uart0_clk.common,
	&bus_uart1_clk.common,
	&bus_uart2_clk.common,
	&bus_uart3_clk.common,
	&bus_i2c0_clk.common,
	&bus_i2c1_clk.common,
	&bus_scr_clk.common,
	&spi0_clk.common,
	&spi1_clk.common,
	&bus_spi0_clk.common,
	&bus_spi1_clk.common,
	&emac_25m_clk.common,
	&bus_emac_clk.common,
	&ir_rx_clk.common,
	&bus_ir_rx_clk.common,
	&ir_tx_clk.common,
	&bus_ir_tx_clk.common,
	&i2s0_clk.common,
	&i2s1_clk.common,
	&bus_i2s0_clk.common,
	&bus_i2s1_clk.common,
	&spdif_clk.common,
	&bus_spdif_clk.common,
	&usb_ohci0_clk.common,
	&usb_phy0_clk.common,
	&usb_ohci1_clk.common,
	&usb_phy1_clk.common,
	&bus_ohci0_clk.common,
	&bus_ohci1_clk.common,
	&bus_ehci0_clk.common,
	&bus_otg_clk.common,
	&ledc_clk.common,
	&bus_ledc_clk.common,
};

static struct clk_hw_onecell_data sun50i_r329_hw_clks = {
	.hws	= {
		[CLK_OSC12M]		= &osc12M_clk.hw,
		[CLK_CPUX]		= &cpux_clk.common.hw,
		[CLK_AXI]		= &axi_clk.common.hw,
		[CLK_CPUX_APB]		= &cpux_apb_clk.common.hw,
		[CLK_AHB]		= &ahb_clk.common.hw,
		[CLK_APB1]		= &apb1_clk.common.hw,
		[CLK_APB2]		= &apb2_clk.common.hw,
		[CLK_CE]		= &ce_clk.common.hw,
		[CLK_BUS_CE]		= &bus_ce_clk.common.hw,
		[CLK_AIPU]		= &aipu_clk.common.hw,
		[CLK_BUS_AIPU]		= &bus_aipu_clk.common.hw,
		[CLK_BUS_DMA]		= &bus_dma_clk.common.hw,
		[CLK_BUS_MSGBOX]	= &bus_msgbox_clk.common.hw,
		[CLK_BUS_SPINLOCK]	= &bus_spinlock_clk.common.hw,
		[CLK_BUS_HSTIMER]	= &bus_hstimer_clk.common.hw,
		[CLK_AVS]		= &avs_clk.common.hw,
		[CLK_BUS_DBG]		= &bus_dbg_clk.common.hw,
		[CLK_BUS_PWM]		= &bus_pwm_clk.common.hw,
		[CLK_DRAM]		= &dram_clk.common.hw,
		[CLK_MBUS_DMA]		= &mbus_dma_clk.common.hw,
		[CLK_MBUS_CE]		= &mbus_ce_clk.common.hw,
		[CLK_MBUS_R_DMA]	= &mbus_r_dma_clk.common.hw,
		[CLK_MBUS_NAND]		= &mbus_nand_clk.common.hw,
		[CLK_MBUS_AIPU]		= &mbus_aipu_clk.common.hw,
		[CLK_BUS_DRAM]		= &bus_dram_clk.common.hw,
		[CLK_NAND0]		= &nand0_clk.common.hw,
		[CLK_NAND1]		= &nand1_clk.common.hw,
		[CLK_BUS_NAND]		= &bus_nand_clk.common.hw,
		[CLK_MMC0]		= &mmc0_clk.common.hw,
		[CLK_MMC1]		= &mmc1_clk.common.hw,
		[CLK_BUS_MMC0]		= &bus_mmc0_clk.common.hw,
		[CLK_BUS_MMC1]		= &bus_mmc1_clk.common.hw,
		[CLK_BUS_UART0]		= &bus_uart0_clk.common.hw,
		[CLK_BUS_UART1]		= &bus_uart1_clk.common.hw,
		[CLK_BUS_UART2]		= &bus_uart2_clk.common.hw,
		[CLK_BUS_UART3]		= &bus_uart3_clk.common.hw,
		[CLK_BUS_I2C0]		= &bus_i2c0_clk.common.hw,
		[CLK_BUS_I2C1]		= &bus_i2c1_clk.common.hw,
		[CLK_BUS_SCR]		= &bus_scr_clk.common.hw,
		[CLK_SPI0]		= &spi0_clk.common.hw,
		[CLK_SPI1]		= &spi1_clk.common.hw,
		[CLK_BUS_SPI0]		= &bus_spi0_clk.common.hw,
		[CLK_BUS_SPI1]		= &bus_spi1_clk.common.hw,
		[CLK_EMAC_25M_DIV]	= &emac_25m_div_clk.hw,
		[CLK_EMAC_25M]		= &emac_25m_clk.common.hw,
		[CLK_BUS_EMAC]		= &bus_emac_clk.common.hw,
		[CLK_IR_RX]		= &ir_rx_clk.common.hw,
		[CLK_BUS_IR_RX]		= &bus_ir_rx_clk.common.hw,
		[CLK_IR_TX]		= &ir_tx_clk.common.hw,
		[CLK_BUS_IR_TX]		= &bus_ir_tx_clk.common.hw,
		[CLK_I2S0]		= &i2s0_clk.common.hw,
		[CLK_I2S1]		= &i2s1_clk.common.hw,
		[CLK_BUS_I2S0]		= &bus_i2s0_clk.common.hw,
		[CLK_BUS_I2S1]		= &bus_i2s1_clk.common.hw,
		[CLK_SPDIF]		= &spdif_clk.common.hw,
		[CLK_BUS_SPDIF]		= &bus_spdif_clk.common.hw,
		[CLK_USB_OHCI0]		= &usb_ohci0_clk.common.hw,
		[CLK_USB_PHY0]		= &usb_phy0_clk.common.hw,
		[CLK_USB_OHCI1]		= &usb_ohci1_clk.common.hw,
		[CLK_USB_PHY1]		= &usb_phy1_clk.common.hw,
		[CLK_BUS_OHCI0]		= &bus_ohci0_clk.common.hw,
		[CLK_BUS_OHCI1]		= &bus_ohci1_clk.common.hw,
		[CLK_BUS_EHCI0]		= &bus_ehci0_clk.common.hw,
		[CLK_BUS_OTG]		= &bus_otg_clk.common.hw,
		[CLK_LEDC]		= &ledc_clk.common.hw,
		[CLK_BUS_LEDC]		= &bus_ledc_clk.common.hw,
	},
	.num = CLK_NUMBER,
};

static struct ccu_reset_map sun50i_r329_ccu_resets[] = {
	[RST_MBUS]		= { 0x540, BIT(30) },

	[RST_BUS_CE]		= { 0x68c, BIT(16) },
	[RST_BUS_AIPU]		= { 0x6fc, BIT(16) },
	[RST_BUS_DMA]		= { 0x70c, BIT(16) },
	[RST_BUS_MSGBOX]	= { 0x71c, BIT(16) },
	[RST_BUS_SPINLOCK]	= { 0x72c, BIT(16) },
	[RST_BUS_HSTIMER]	= { 0x73c, BIT(16) },
	[RST_BUS_DBG]		= { 0x78c, BIT(16) },
	[RST_BUS_PWM]		= { 0x7ac, BIT(16) },
	[RST_BUS_DRAM]		= { 0x80c, BIT(16) },
	[RST_BUS_NAND]		= { 0x82c, BIT(16) },
	[RST_BUS_MMC0]		= { 0x84c, BIT(16) },
	[RST_BUS_MMC1]		= { 0x84c, BIT(17) },
	[RST_BUS_UART0]		= { 0x90c, BIT(16) },
	[RST_BUS_UART1]		= { 0x90c, BIT(17) },
	[RST_BUS_UART2]		= { 0x90c, BIT(18) },
	[RST_BUS_UART3]		= { 0x90c, BIT(19) },
	[RST_BUS_I2C0]		= { 0x91c, BIT(16) },
	[RST_BUS_I2C1]		= { 0x91c, BIT(17) },
	[RST_BUS_SCR]		= { 0x93c, BIT(16) },
	[RST_BUS_SPI0]		= { 0x96c, BIT(16) },
	[RST_BUS_SPI1]		= { 0x96c, BIT(17) },
	[RST_BUS_EMAC]		= { 0x97c, BIT(16) },
	[RST_BUS_IR_RX]		= { 0x99c, BIT(16) },
	[RST_BUS_IR_TX]		= { 0x9cc, BIT(16) },
	[RST_BUS_I2S0]		= { 0xa1c, BIT(16) },
	[RST_BUS_I2S1]		= { 0xa1c, BIT(17) },
	[RST_BUS_SPDIF]		= { 0xa2c, BIT(16) },

	[RST_USB_PHY0]		= { 0xa70, BIT(30) },
	[RST_USB_PHY1]		= { 0xa74, BIT(30) },

	[RST_BUS_OHCI0]		= { 0xa8c, BIT(16) },
	[RST_BUS_OHCI1]		= { 0xa8c, BIT(17) },
	[RST_BUS_EHCI0]		= { 0xa8c, BIT(20) },
	[RST_BUS_OTG]		= { 0xa8c, BIT(24) },

	[RST_BUS_LEDC]		= { 0xbfc, BIT(16) },
};

static const struct sunxi_ccu_desc sun50i_r329_ccu_desc = {
	.ccu_clks	= sun50i_r329_ccu_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun50i_r329_ccu_clks),

	.hw_clks	= &sun50i_r329_hw_clks,

	.resets		= sun50i_r329_ccu_resets,
	.num_resets	= ARRAY_SIZE(sun50i_r329_ccu_resets),
};

static const u32 sun50i_r329_usb_clk_regs[] = {
	SUN50I_R329_USB0_CLK_REG,
	SUN50I_R329_USB1_CLK_REG,
};

static int sun50i_r329_ccu_probe(struct platform_device *pdev)
{
	void __iomem *reg;
	u32 val;
	int i;

	reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	/*
	 * Force OHCI 12M clock sources to 00 (12MHz divided from 48MHz)
	 *
	 * This clock mux is still mysterious, and the code just enforces
	 * it to have a valid clock parent.
	 */
	for (i = 0; i < ARRAY_SIZE(sun50i_r329_usb_clk_regs); i++) {
		val = readl(reg + sun50i_r329_usb_clk_regs[i]);
		val &= ~GENMASK(25, 24);
		writel(val, reg + sun50i_r329_usb_clk_regs[i]);
	}

	return sunxi_ccu_probe(pdev->dev.of_node, reg, &sun50i_r329_ccu_desc);
}

static const struct of_device_id sun50i_r329_ccu_ids[] = {
	{ .compatible = "allwinner,sun50i-r329-ccu" },
	{ }
};

static struct platform_driver sun50i_r329_ccu_driver = {
	.probe	= sun50i_r329_ccu_probe,
	.driver	= {
		.name	= "sun50i-r329-ccu",
		.of_match_table	= sun50i_r329_ccu_ids,
	},
};
module_platform_driver(sun50i_r329_ccu_driver);
