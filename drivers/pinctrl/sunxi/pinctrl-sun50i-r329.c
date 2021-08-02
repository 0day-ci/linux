// SPDX-License-Identifier: GPL-2.0
/*
 * Allwinner R329 SoC pinctrl driver.
 *
 * Copyright (C) 2021 Sipeed
 * based on the H616 pinctrl driver
 *   Copyright (C) 2020 Arm Ltd.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>

#include "pinctrl-sunxi.h"

static const struct sunxi_desc_pin r329_pins[] = {
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart2"),		/* TX */
		  SUNXI_FUNCTION(0x3, "pwm"),		/* PWM0 */
		  SUNXI_FUNCTION(0x4, "jtag"),		/* MS */
		  SUNXI_FUNCTION(0x5, "ledc"),		/* DO */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 0)),	/* PB_EINT0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart2"),		/* RX */
		  SUNXI_FUNCTION(0x3, "pwm"),		/* PWM1 */
		  SUNXI_FUNCTION(0x4, "jtag"),		/* CK */
		  SUNXI_FUNCTION(0x5, "i2s0"),		/* MCLK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 1)),	/* PB_EINT1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart2"),		/* RTS */
		  SUNXI_FUNCTION(0x3, "pwm"),		/* PWM2 */
		  SUNXI_FUNCTION(0x4, "jtag"),		/* DO */
		  SUNXI_FUNCTION(0x5, "i2s0"),		/* LRCK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 2)),	/* PB_EINT2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart2"),		/* CTS */
		  SUNXI_FUNCTION(0x3, "pwm"),		/* PWM3 */
		  SUNXI_FUNCTION(0x4, "jtag"),		/* DI */
		  SUNXI_FUNCTION(0x5, "i2s0"),		/* BCLK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 3)),	/* PB_EINT3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart0"),		/* TX */
		  SUNXI_FUNCTION(0x3, "pwm"),		/* PWM4 */
		  SUNXI_FUNCTION(0x4, "i2s0_dout0"),
		  SUNXI_FUNCTION(0x5, "i2s0_din1"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 4)),	/* PB_EINT4 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart0"),		/* RX */
		  SUNXI_FUNCTION(0x3, "pwm"),		/* PWM5 */
		  SUNXI_FUNCTION(0x4, "i2s0_dout1"),
		  SUNXI_FUNCTION(0x5, "i2s0_din0"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 5)),	/* PB_EINT5 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ir"),		/* RX */
		  SUNXI_FUNCTION(0x3, "pwm"),		/* PWM6 */
		  SUNXI_FUNCTION(0x4, "i2s0"),		/* DOUT2 */
		  SUNXI_FUNCTION(0x5, "i2c0"),		/* SCK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 6)),	/* PB_EINT6 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ir"),		/* TX */
		  SUNXI_FUNCTION(0x3, "pwm"),		/* PWM7 */
		  SUNXI_FUNCTION(0x4, "i2s0"),		/* DOUT3 */
		  SUNXI_FUNCTION(0x5, "i2c0"),		/* SDA */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 7)),	/* PB_EINT7 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(B, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "ir_tx"),
		  SUNXI_FUNCTION(0x3, "pwm"),		/* PWM8 */
		  SUNXI_FUNCTION(0x4, "ir_rx"),
		  SUNXI_FUNCTION(0x5, "ledc"),		/* DO */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 8)),	/* PB_EINT8 */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* RB0 */
		  SUNXI_FUNCTION(0x3, "mmc0"),		/* CLK */
		  SUNXI_FUNCTION(0x4, "spi0")),		/* CS */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* RE */
		  SUNXI_FUNCTION(0x3, "mmc0"),		/* CMD */
		  SUNXI_FUNCTION(0x4, "spi0")),		/* MISO */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* CE0 */
		  SUNXI_FUNCTION(0x3, "mmc0"),		/* D2 */
		  SUNXI_FUNCTION(0x4, "spi0")),		/* WP */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* CLE */
		  SUNXI_FUNCTION(0x3, "mmc0"),		/* D1 */
		  SUNXI_FUNCTION(0x4, "spi0")),		/* MOSI */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* ALE */
		  SUNXI_FUNCTION(0x3, "mmc0"),		/* D0 */
		  SUNXI_FUNCTION(0x4, "spi0")),		/* CLK */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* WE */
		  SUNXI_FUNCTION(0x3, "mmc0"),		/* D3 */
		  SUNXI_FUNCTION(0x4, "spi0")),		/* HOLD */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* DQ0 */
		  SUNXI_FUNCTION(0x3, "mmc0")),		/* RST */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(C, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand0"),		/* DQ1 */
		  SUNXI_FUNCTION(0x5, "boot_sel")),
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand"),		/* DQ7 */
		  SUNXI_FUNCTION(0x3, "sim0"),		/* VPPEN */
		  SUNXI_FUNCTION(0x4, "jtag"),		/* MS */
		  SUNXI_FUNCTION(0x5, "mmc0"),		/* D1 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 0)),	/* PF_EINT0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand"),		/* DQ6 */
		  SUNXI_FUNCTION(0x3, "sim0"),		/* VPPPP */
		  SUNXI_FUNCTION(0x4, "jtag"),		/* DI */
		  SUNXI_FUNCTION(0x5, "mmc0"),		/* D0 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 1)),	/* PF_EINT1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand"),		/* DQ5 */
		  SUNXI_FUNCTION(0x3, "sim0"),		/* PWREN */
		  SUNXI_FUNCTION(0x4, "uart"),		/* TX */
		  SUNXI_FUNCTION(0x5, "mmc0"),		/* CLK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 2)),	/* PF_EINT2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand"),		/* DQ4 */
		  SUNXI_FUNCTION(0x3, "sim0"),		/* CLK */
		  SUNXI_FUNCTION(0x4, "jtag"),		/* DO */
		  SUNXI_FUNCTION(0x5, "mmc0"),		/* CMD */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 3)),	/* PF_EINT3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand"),		/* DQS */
		  SUNXI_FUNCTION(0x3, "sim0"),		/* DATA */
		  SUNXI_FUNCTION(0x4, "uart"),		/* RX */
		  SUNXI_FUNCTION(0x5, "mmc0"),		/* D3 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 4)),	/* PF_EINT4 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand"),		/* DQ2 */
		  SUNXI_FUNCTION(0x3, "sim0"),		/* RST */
		  SUNXI_FUNCTION(0x4, "jtag"),		/* CK */
		  SUNXI_FUNCTION(0x5, "mmc0"),		/* D2 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 5)),	/* PF_EINT5 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(F, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nand"),		/* DQ1 */
		  SUNXI_FUNCTION(0x3, "sim0"),		/* DET */
		  SUNXI_FUNCTION(0x4, "spdif_in"),
		  SUNXI_FUNCTION(0x5, "spdif_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 6)),	/* PF_EINT6 */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc1_clk"),
		  SUNXI_FUNCTION(0x3, "mmc1_d2"),
		  /* 0x4 is also mmc1_d2 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 0)),	/* PG_EINT0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc1_cmd"),
		  SUNXI_FUNCTION(0x3, "mmc1_d3"),
		  SUNXI_FUNCTION(0x4, "mmc1_clk"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 1)),	/* PG_EINT1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc1_d0"),
		  SUNXI_FUNCTION(0x3, "mmc1_cmd"),
		  SUNXI_FUNCTION(0x4, "mmc1_d3"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 2)),	/* PG_EINT2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc1_d1"),
		  SUNXI_FUNCTION(0x3, "mmc1_clk"),
		  /* 0x4 is also mmc1_d1 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 3)),	/* PG_EINT3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc1_d2"),
		  SUNXI_FUNCTION(0x3, "mmc1_d0"),
		  /* 0x4 is also mmc1_d0 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 4)),	/* PG_EINT4 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "mmc1_d3"),
		  SUNXI_FUNCTION(0x3, "mmc1_d1"),
		  SUNXI_FUNCTION(0x4, "mmc1_cmd"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 5)),	/* PG_EINT5 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart1"),		/* TX */
		  SUNXI_FUNCTION(0x3, "i2c0"),		/* SCK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 6)),	/* PG_EINT6 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart1"),		/* RX */
		  SUNXI_FUNCTION(0x3, "i2c0"),		/* SDA */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 7)),	/* PG_EINT7 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart1"),		/* RTS */
		  SUNXI_FUNCTION(0x3, "i2c1"),		/* SCK */
		  SUNXI_FUNCTION(0x5, "spi1"),		/* HOLD/DBI-DCX/DBI-WRX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 8)),	/* PG_EINT8 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 9),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart1"),		/* CTS */
		  SUNXI_FUNCTION(0x3, "i2c1"),		/* SDA */
		  SUNXI_FUNCTION(0x5, "spi1"),		/* WP/DBI-TE */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 9)),	/* PG_EINT9 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 10),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "i2s1"),		/* MCLK */
		  SUNXI_FUNCTION(0x3, "ledc"),		/* DO */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 10)),	/* PG_EINT10 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 11),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart3"),		/* TX */
		  SUNXI_FUNCTION(0x3, "i2s1"),		/* LRCK */
		  SUNXI_FUNCTION(0x5, "spi1"),		/* CS/DBI-CSX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 11)),	/* PG_EINT11 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 12),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart3"),		/* RX */
		  SUNXI_FUNCTION(0x3, "i2s1"),		/* BCLK */
		  SUNXI_FUNCTION(0x5, "spi1"),		/* CLK/DBI-SCLK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 12)),	/* PG_EINT12 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 13),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart3"),		/* RTS */
		  SUNXI_FUNCTION(0x3, "i2s1_dout0"),
		  SUNXI_FUNCTION(0x4, "i2s1_din1"),
		  SUNXI_FUNCTION(0x5, "spi1"),		/* MOSI/DBI-SDO */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 13)),	/* PG_EINT13 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(G, 14),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart3"),		/* CTS */
		  SUNXI_FUNCTION(0x3, "i2s1_dout1"),
		  SUNXI_FUNCTION(0x4, "i2s1_din0"),
		  SUNXI_FUNCTION(0x5, "spi1"),		/* MISO/DBI-SDI/DBI-TE/DBI-DCX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 14)),	/* PG_EINT14 */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c0"),		/* SCK */
		  SUNXI_FUNCTION(0x3, "uart0"),		/* TX */
		  SUNXI_FUNCTION(0x4, "spi1"),		/* CS/DBI-CSX */
		  SUNXI_FUNCTION(0x5, "pwm"),		/* PWM0 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 3, 0)),	/* PH_EINT0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c0"),		/* SDA */
		  SUNXI_FUNCTION(0x3, "uart0"),		/* RX */
		  SUNXI_FUNCTION(0x4, "spi1"),		/* CLK/DBI-SCLK */
		  SUNXI_FUNCTION(0x5, "pwm"),		/* PWM1 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 3, 1)),	/* PH_EINT1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c1"),		/* SCK */
		  SUNXI_FUNCTION(0x3, "ledc"),		/* DO */
		  SUNXI_FUNCTION(0x4, "spi1"),		/* MOSI/DBI-SDO */
		  SUNXI_FUNCTION(0x5, "ir"),		/* RX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 3, 2)),	/* PH_EINT2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c1"),		/* SDA */
		  SUNXI_FUNCTION(0x3, "spdif"),		/* OUT */
		  SUNXI_FUNCTION(0x4, "spi1"),		/* MISO/DBI-SDI/DBI-TE/DBI-DCX */
		  SUNXI_FUNCTION(0x5, "ir"),		/* TX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 3, 3)),	/* PH_EINT3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart3"),		/* TX */
		  SUNXI_FUNCTION(0x3, "spi1_cs"),	/* CS/DBI-CSX */
		  SUNXI_FUNCTION(0x4, "spi1_hold"),	/* HOLD/DBI-DCX/DBI-WRX */
		  SUNXI_FUNCTION(0x5, "pwm"),		/* PWM2 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 3, 4)),	/* PH_EINT4 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart3"),		/* RX */
		  SUNXI_FUNCTION(0x3, "spi1_clk"),	/* CLK/DBI-SCLK */
		  SUNXI_FUNCTION(0x4, "spi1_wp"),	/* WP/DBI-TE */
		  SUNXI_FUNCTION(0x5, "pwm"),		/* PWM3 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 3, 5)),	/* PH_EINT5 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart3"),		/* RTS */
		  SUNXI_FUNCTION(0x3, "spi1"),		/* MOSI/SPI-DBO */
		  SUNXI_FUNCTION(0x4, "i2c0"),		/* SCK */
		  SUNXI_FUNCTION(0x5, "pwm"),		/* PWM4 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 3, 6)),	/* PH_EINT6 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "uart3"),		/* CTS */
		  SUNXI_FUNCTION(0x3, "spi1"),		/* MISO/DBI-SDI/DBI-TE/DBI-DCX */
		  SUNXI_FUNCTION(0x4, "i2c0"),		/* SDA */
		  SUNXI_FUNCTION(0x5, "pwm"),		/* PWM5 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 3, 7)),	/* PH_EINT7 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c1"),		/* SDA */
		  SUNXI_FUNCTION(0x3, "spi1"),		/* WP/DBI-TE */
		  SUNXI_FUNCTION(0x4, "ledc"),		/* DO */
		  SUNXI_FUNCTION(0x5, "ir"),		/* TX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 3, 8)),	/* PH_EINT8 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(H, 9),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "i2c1"),		/* SCK */
		  SUNXI_FUNCTION(0x3, "spi1"),		/* HOLD/DBI-DCX/DBI-WRX */
		  SUNXI_FUNCTION(0x4, "spdif"),		/* IN */
		  SUNXI_FUNCTION(0x5, "ir"),		/* RX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 3, 9)),	/* PH_EINT9 */
};
static const unsigned int r329_irq_bank_map[] = { 1, 5, 6, 7 };

static const struct sunxi_pinctrl_desc r329_pinctrl_data = {
	.pins = r329_pins,
	.npins = ARRAY_SIZE(r329_pins),
	.irq_banks = ARRAY_SIZE(r329_irq_bank_map),
	.irq_bank_map = r329_irq_bank_map,
	.io_bias_cfg_variant = BIAS_VOLTAGE_PIO_POW_MODE_SEL,
};

static int r329_pinctrl_probe(struct platform_device *pdev)
{
	return sunxi_pinctrl_init(pdev, &r329_pinctrl_data);
}

static const struct of_device_id r329_pinctrl_match[] = {
	{ .compatible = "allwinner,sun50i-r329-pinctrl", },
	{}
};

static struct platform_driver r329_pinctrl_driver = {
	.probe	= r329_pinctrl_probe,
	.driver	= {
		.name		= "sun50i-r329-pinctrl",
		.of_match_table	= r329_pinctrl_match,
	},
};
builtin_platform_driver(r329_pinctrl_driver);
