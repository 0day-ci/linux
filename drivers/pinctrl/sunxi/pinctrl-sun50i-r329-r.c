// SPDX-License-Identifier: GPL-2.0
/*
 * Allwinner H616 R_PIO pin controller driver
 *
 * Copyright (C) 2020 Arm Ltd.
 * Based on former work, which is:
 *   Copyright (C) 2017 Icenowy Zheng <icenowy@aosc.io>
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/reset.h>

#include "pinctrl-sunxi.h"

static const struct sunxi_desc_pin sun50i_r329_r_pins[] = {
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_i2s"),		/* LRCK */
		  SUNXI_FUNCTION(0x4, "s_dmic"),	/* DATA3 */
		  SUNXI_FUNCTION(0x5, "s_pwm"),		/* PWM0 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 0)),	/* PL_EINT0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_i2s"),		/* BCLK */
		  SUNXI_FUNCTION(0x4, "s_dmic"),	/* DATA2 */
		  SUNXI_FUNCTION(0x5, "s_pwm"),		/* PWM1 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 1)),	/* PL_EINT1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_i2s_dout0"),
		  SUNXI_FUNCTION(0x3, "s_i2s_din1"),
		  SUNXI_FUNCTION(0x4, "s_dmic"),	/* DATA1 */
		  SUNXI_FUNCTION(0x5, "s_pwm"),		/* PWM2 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 2)),	/* PL_EINT2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_i2s_dout1"),
		  SUNXI_FUNCTION(0x3, "s_i2s_din0"),
		  SUNXI_FUNCTION(0x4, "s_dmic"),	/* DATA0 */
		  SUNXI_FUNCTION(0x5, "s_i2c"),		/* SDA */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 3)),	/* PL_EINT3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_i2s"),		/* MCLK */
		  SUNXI_FUNCTION(0x3, "s_ir"),		/* RX */
		  SUNXI_FUNCTION(0x4, "s_dmic"),	/* CLK */
		  SUNXI_FUNCTION(0x5, "s_i2c"),		/* SCK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 4)),	/* PL_EINT4 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_i2c"),		/* SDA */
		  SUNXI_FUNCTION(0x5, "s_pwm"),		/* PWM3 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 5)),	/* PL_EINT5 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_i2c"),		/* SCK */
		  SUNXI_FUNCTION(0x5, "s_pwm"),		/* PWM4 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 6)),	/* PL_EINT6 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_ir"),		/* RX */
		  SUNXI_FUNCTION(0x4, "clock"),		/* X32KFOUT */
		  SUNXI_FUNCTION(0x5, "s_pwm"),		/* PWM5 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 7)),	/* PL_EINT7 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_uart"),	/* TX */
		  SUNXI_FUNCTION(0x3, "s_i2c"),		/* SDA */
		  SUNXI_FUNCTION(0x4, "s_ir"),		/* RX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 8)),	/* PL_EINT8 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 9),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_uart"),	/* RX */
		  SUNXI_FUNCTION(0x3, "s_i2c"),		/* SCK */
		  SUNXI_FUNCTION(0x4, "clock"),		/* X32KFOUT */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 9)),	/* PL_EINT9 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(L, 10),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 0, 10)),	/* PL_EINT10 */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_uart"),	/* TX */
		  SUNXI_FUNCTION(0x3, "s_jtag"),	/* MS */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 0)),	/* PM_EINT0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_uart"),	/* RX */
		  SUNXI_FUNCTION(0x3, "s_jtag"),	/* CK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 1)),	/* PM_EINT1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "s_jtag"),	/* DO */
		  SUNXI_FUNCTION(0x4, "s_i2c"),		/* SDA */
		  SUNXI_FUNCTION(0x5, "s_ir"),		/* RX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 2)),	/* PM_EINT2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_i2c"),		/* SDA */
		  SUNXI_FUNCTION(0x3, "s_ir"),		/* RX */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 3)),	/* PM_EINT3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_i2c"),		/* SCK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 4)),	/* PM_EINT4 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "clock"),		/* X32KFOUT */
		  SUNXI_FUNCTION(0x3, "s_jtag"),	/* DI */
		  SUNXI_FUNCTION(0x4, "s_i2c"),		/* SCK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 5)),	/* PM_EINT5 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "nmi"),
		  SUNXI_FUNCTION(0x3, "s_ir"),		/* RX */
		  SUNXI_FUNCTION(0x4, "clock"),		/* X32KFOUT */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 6)),	/* PM_EINT6 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x2, "s_ir"),		/* RX */
		  SUNXI_FUNCTION(0x3, "clock"),		/* X32KFOUT */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 7)),	/* PM_EINT7 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(M, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 1, 8)),	/* PM_EINT8 */
	/* Hole */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 0),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 0)),	/* PN_EINT0 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 1),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "emac"),		/* MDC */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 1)),	/* PN_EINT1 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 2),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "emac"),		/* MDIO */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 2)),	/* PN_EINT2 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 3),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "emac"),		/* TXD3 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 3)),	/* PN_EINT3 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 4),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "emac"),		/* TXCK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 4)),	/* PN_EINT4 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 5),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "emac"),		/* RXD2 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 5)),	/* PN_EINT5 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 6),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "emac"),		/* RXD0 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 6)),	/* PN_EINT6 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 7),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "emac"),		/* RXCK */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 7)),	/* PN_EINT7 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 8),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "emac"),		/* RXERR */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 8)),	/* PN_EINT8 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 9),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "emac"),		/* TXCTL/TXEN */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 9)),	/* PN_EINT9 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 10),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "emac"),		/* RXD3 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 10)),	/* PN_EINT10 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 11),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "emac"),		/* RXD1 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 11)),	/* PN_EINT11 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 12),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "emac"),		/* RXCTL/CRS_DV */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 12)),	/* PN_EINT12 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 13),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "emac"),		/* TXD2 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 13)),	/* PN_EINT13 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 14),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "emac"),		/* TXD1 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 14)),	/* PN_EINT14 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 15),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "emac"),		/* TXD0 */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 15)),	/* PN_EINT15 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 16),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "emac"),		/* EPHY-25M */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 16)),	/* PN_EINT16 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 17),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION(0x3, "emac"),		/* CLKIN */
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 17)),	/* PN_EINT17 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 18),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 18)),	/* PN_EINT18 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 19),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 19)),	/* PN_EINT19 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 20),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 20)),	/* PN_EINT20 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 21),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 21)),	/* PN_EINT21 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 22),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 22)),	/* PN_EINT22 */
	SUNXI_PIN(SUNXI_PINCTRL_PIN(N, 23),
		  SUNXI_FUNCTION(0x0, "gpio_in"),
		  SUNXI_FUNCTION(0x1, "gpio_out"),
		  SUNXI_FUNCTION_IRQ_BANK(0x6, 2, 23)),	/* PN_EINT23 */
};

static const struct sunxi_pinctrl_desc sun50i_r329_r_pinctrl_data = {
	.pins = sun50i_r329_r_pins,
	.npins = ARRAY_SIZE(sun50i_r329_r_pins),
	.pin_base = PL_BASE,
	.irq_banks = 3,
	.io_bias_cfg_variant = BIAS_VOLTAGE_PIO_POW_MODE_SEL,
};

static int sun50i_r329_r_pinctrl_probe(struct platform_device *pdev)
{
	return sunxi_pinctrl_init(pdev,
				  &sun50i_r329_r_pinctrl_data);
}

static const struct of_device_id sun50i_r329_r_pinctrl_match[] = {
	{ .compatible = "allwinner,sun50i-r329-r-pinctrl", },
	{}
};

static struct platform_driver sun50i_r329_r_pinctrl_driver = {
	.probe	= sun50i_r329_r_pinctrl_probe,
	.driver	= {
		.name		= "sun50i-r329-r-pinctrl",
		.of_match_table	= sun50i_r329_r_pinctrl_match,
	},
};
builtin_platform_driver(sun50i_r329_r_pinctrl_driver);
