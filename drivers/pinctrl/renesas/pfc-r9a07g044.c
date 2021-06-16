// SPDX-License-Identifier: GPL-2.0
/*
 * R9A07G044 processor support - pinctrl GPIO hardware block.
 *
 * Copyright (C) 2021 Renesas Electronics Corp.
 */

#include "pinctrl-rzg2l.h"

#define RZG2L_GPIO_PIN_CONF	(0)

static const struct {
	struct pinctrl_pin_desc pin_gpio[392];
} pinmux_pins = {
	.pin_gpio = {
		RZ_G2L_PINCTRL_PIN_GPIO(0, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(1, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(2, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(3, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(4, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(5, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(6, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(7, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(8, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(9, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(10, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(11, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(12, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(13, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(14, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(15, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(16, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(17, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(18, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(19, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(20, 0),
		RZ_G2L_PINCTRL_PIN_GPIO(21, 0),
		RZ_G2L_PINCTRL_PIN_GPIO(22, 0),
		RZ_G2L_PINCTRL_PIN_GPIO(23, 0),
		RZ_G2L_PINCTRL_PIN_GPIO(24, 0),
		RZ_G2L_PINCTRL_PIN_GPIO(25, 0),
		RZ_G2L_PINCTRL_PIN_GPIO(26, 0),
		RZ_G2L_PINCTRL_PIN_GPIO(27, 0),
		RZ_G2L_PINCTRL_PIN_GPIO(28, 0),
		RZ_G2L_PINCTRL_PIN_GPIO(29, 0),
		RZ_G2L_PINCTRL_PIN_GPIO(30, 0),
		RZ_G2L_PINCTRL_PIN_GPIO(31, 0),
		RZ_G2L_PINCTRL_PIN_GPIO(32, 0),
		RZ_G2L_PINCTRL_PIN_GPIO(33, 0),
		RZ_G2L_PINCTRL_PIN_GPIO(34, 0),
		RZ_G2L_PINCTRL_PIN_GPIO(35, 0),
		RZ_G2L_PINCTRL_PIN_GPIO(36, 0),
		RZ_G2L_PINCTRL_PIN_GPIO(37, 0),
		RZ_G2L_PINCTRL_PIN_GPIO(38, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(39, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(40, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(41, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(42, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(43, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(44, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(45, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(46, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(47, RZG2L_GPIO_PIN_CONF),
		RZ_G2L_PINCTRL_PIN_GPIO(48, RZG2L_GPIO_PIN_CONF),
	},
};

/* - RIIC2 ------------------------------------------------------------------ */
static int i2c2_a_pins[] = {
	/* SDA, SCL */
	RZ_G2L_PIN(3, 0), RZ_G2L_PIN(3, 1),
};
static int i2c2_b_pins[] = {
	/* SDA, SCL */
	RZ_G2L_PIN(19, 0), RZ_G2L_PIN(19, 1),
};
static int i2c2_c_pins[] = {
	/* SDA, SCL */
	RZ_G2L_PIN(42, 3), RZ_G2L_PIN(42, 4),
};
static int i2c2_d_pins[] = {
	/* SDA, SCL */
	RZ_G2L_PIN(46, 0), RZ_G2L_PIN(46, 1),
};
static int i2c2_e_pins[] = {
	/* SDA, SCL */
	RZ_G2L_PIN(48, 0), RZ_G2L_PIN(48, 1),
};
/* - RIIC3 ------------------------------------------------------------------ */
static int i2c3_a_pins[] = {
	/* SDA, SCL */
	RZ_G2L_PIN(8, 1), RZ_G2L_PIN(8, 0),
};
static int i2c3_b_pins[] = {
	/* SDA, SCL */
	RZ_G2L_PIN(18, 0), RZ_G2L_PIN(18, 1),
};
static int i2c3_c_pins[] = {
	/* SDA, SCL */
	RZ_G2L_PIN(46, 2), RZ_G2L_PIN(46, 3),
};
static int i2c3_d_pins[] = {
	/* SDA, SCL */
	RZ_G2L_PIN(48, 2), RZ_G2L_PIN(48, 3),
};

/* - SCIF0 ------------------------------------------------------------------ */
static int scif0_clk_pins[] = {
	/* SCK */
	RZ_G2L_PIN(39, 0),
};
static int scif0_ctrl_pins[] = {
	/* CTS, RTS */
	RZ_G2L_PIN(39, 1), RZ_G2L_PIN(39, 2),
};
static int scif0_data_pins[] = {
	/* TX, RX */
	RZ_G2L_PIN(38, 0), RZ_G2L_PIN(38, 1),
};
/* - SCIF1 ------------------------------------------------------------------ */
static int scif1_clk_pins[] = {
	/* SCK */
	RZ_G2L_PIN(40, 2),
};
static int scif1_ctrl_pins[] = {
	/* CTS, RTS */
	RZ_G2L_PIN(41, 0), RZ_G2L_PIN(41, 1),
};
static int scif1_data_pins[] = {
	/* TX, RX */
	RZ_G2L_PIN(40, 0), RZ_G2L_PIN(40, 1),
};
/* - SCIF2 ------------------------------------------------------------------ */
static int scif2_clk_a_pins[] = {
	/* SCK */
	RZ_G2L_PIN(5, 0),
};
static int scif2_clk_b_pins[] = {
	/* SCK */
	RZ_G2L_PIN(17, 0),
};
static int scif2_clk_c_pins[] = {
	/* SCK */
	RZ_G2L_PIN(37, 0),
};
static int scif2_clk_d_pins[] = {
	/* SCK */
	RZ_G2L_PIN(42, 2),
};
static int scif2_clk_e_pins[] = {
	/* SCK */
	RZ_G2L_PIN(48, 2),
};
static int scif2_ctrl_a_pins[] = {
	/* CTS, RTS */
	RZ_G2L_PIN(5, 1), RZ_G2L_PIN(5, 2),
};
static int scif2_ctrl_b_pins[] = {
	/* CTS, RTS */
	RZ_G2L_PIN(17, 1), RZ_G2L_PIN(17, 2),
};
static int scif2_ctrl_c_pins[] = {
	/* CTS, RTS */
	RZ_G2L_PIN(37, 1), RZ_G2L_PIN(37, 2),
};
static int scif2_ctrl_d_pins[] = {
	/* CTS, RTS */
	RZ_G2L_PIN(42, 3), RZ_G2L_PIN(42, 4),
};
static int scif2_ctrl_e_pins[] = {
	/* CTS, RTS */
	RZ_G2L_PIN(48, 3), RZ_G2L_PIN(48, 4),
};
static int scif2_data_a_pins[] = {
	/* TX, RX */
	RZ_G2L_PIN(4, 0), RZ_G2L_PIN(4, 1),
};
static int scif2_data_b_pins[] = {
	/* TX, RX */
	RZ_G2L_PIN(16, 0), RZ_G2L_PIN(16, 1),
};
static int scif2_data_c_pins[] = {
	/* TX, RX */
	RZ_G2L_PIN(33, 0), RZ_G2L_PIN(33, 1),
};
static int scif2_data_d_pins[] = {
	/* TX, RX */
	RZ_G2L_PIN(42, 0), RZ_G2L_PIN(42, 1),
};
static int scif2_data_e_pins[] = {
	/* TX, RX */
	RZ_G2L_PIN(48, 0), RZ_G2L_PIN(48, 1),
};
/* - SCIF3 ------------------------------------------------------------------ */
static int scif3_clk_pins[] = {
	/* SCK */
	RZ_G2L_PIN(1, 0),
};
static int scif3_data_pins[] = {
	/* TX, RX */
	RZ_G2L_PIN(0, 0), RZ_G2L_PIN(0, 1),
};
/* - SCIF4 ------------------------------------------------------------------ */
static int scif4_clk_pins[] = {
	/* SCK */
	RZ_G2L_PIN(3, 0),
};
static int scif4_data_pins[] = {
	/* TX, RX */
	RZ_G2L_PIN(2, 0), RZ_G2L_PIN(2, 1),
};

/* - USB0 ------------------------------------------------------------------- */
static int usb0_a_pins[] = {
	/* VBUS, OVC */
	RZ_G2L_PIN(4, 0), RZ_G2L_PIN(5, 0),
};
static int usb0_a_otg_exicen_pins[] = {
	/* OTG_EXICEN */
	RZ_G2L_PIN(5, 2),
};
static int usb0_a_otg_id_pins[] = {
	/* OTG_ID */
	RZ_G2L_PIN(5, 1),
};
static int usb0_b_pins[] = {
	/* VBUS, OVC */
	RZ_G2L_PIN(6, 0), RZ_G2L_PIN(7, 0),
};
static int usb0_b_otg_exicen_pins[] = {
	/* OTG_EXICEN */
	RZ_G2L_PIN(7, 2),
};
static int usb0_b_otg_id_pins[] = {
	/* OTG_ID */
	RZ_G2L_PIN(7, 1),
};
/* - USB1 ------------------------------------------------------------------- */
static int usb1_a_pins[] = {
	/* VBUS, OVC */
	RZ_G2L_PIN(8, 0), RZ_G2L_PIN(8, 1),
};
static int usb1_b_pins[] = {
	/* VBUS, OVC */
	RZ_G2L_PIN(29, 0), RZ_G2L_PIN(29, 1),
};
static int usb1_c_pins[] = {
	/* VBUS, OVC */
	RZ_G2L_PIN(38, 0), RZ_G2L_PIN(38, 1),
};
static int usb1_d_pins[] = {
	/* VBUS, OVC */
	RZ_G2L_PIN(42, 0), RZ_G2L_PIN(42, 1),
};

static struct group_desc pinmux_groups[] = {
	RZ_G2L_PINCTRL_PIN_GROUP(i2c2_a, 2),
	RZ_G2L_PINCTRL_PIN_GROUP(i2c2_b, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(i2c2_c, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(i2c2_d, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(i2c2_e, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(i2c3_a, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(i2c3_b, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(i2c3_c, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(i2c3_d, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(scif0_clk, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(scif0_ctrl, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(scif0_data, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(scif1_clk, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(scif1_ctrl, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(scif1_data, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(scif2_clk_a, 2),
	RZ_G2L_PINCTRL_PIN_GROUP(scif2_clk_b, 2),
	RZ_G2L_PINCTRL_PIN_GROUP(scif2_clk_c, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(scif2_clk_d, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(scif2_clk_e, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(scif2_ctrl_a, 2),
	RZ_G2L_PINCTRL_PIN_GROUP(scif2_ctrl_b, 2),
	RZ_G2L_PINCTRL_PIN_GROUP(scif2_ctrl_c, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(scif2_ctrl_d, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(scif2_ctrl_e, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(scif2_data_a, 2),
	RZ_G2L_PINCTRL_PIN_GROUP(scif2_data_b, 2),
	RZ_G2L_PINCTRL_PIN_GROUP(scif2_data_c, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(scif2_data_d, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(scif2_data_e, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(scif3_clk, 5),
	RZ_G2L_PINCTRL_PIN_GROUP(scif3_data, 5),
	RZ_G2L_PINCTRL_PIN_GROUP(scif4_clk, 5),
	RZ_G2L_PINCTRL_PIN_GROUP(scif4_data, 5),
	RZ_G2L_PINCTRL_PIN_GROUP(usb0_a, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(usb0_a_otg_exicen, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(usb0_a_otg_id, 1),
	RZ_G2L_PINCTRL_PIN_GROUP(usb0_b, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(usb0_b_otg_exicen, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(usb0_b_otg_id, 3),
	RZ_G2L_PINCTRL_PIN_GROUP(usb1_a, 2),
	RZ_G2L_PINCTRL_PIN_GROUP(usb1_b, 4),
	RZ_G2L_PINCTRL_PIN_GROUP(usb1_c, 5),
	RZ_G2L_PINCTRL_PIN_GROUP(usb1_d, 1),
};

static const char *i2c2_groups[] = {
	"i2c2_a", "i2c2_b", "i2c2_c", "i2c2_d", "i2c2_e",
};

static const char *i2c3_groups[] = {
	"i2c3_a", "i2c3_b", "i2c3_c", "i2c3_d",
};

static const char *scif0_groups[] = {
	"scif0_clk", "scif0_ctrl", "scif0_data",
};

static const char *scif1_groups[] = {
	"scif1_clk", "scif1_ctrl", "scif1_data",
};

static const char *scif2_groups[] = {
	"scif2_clk_a", "scif2_clk_b", "scif2_clk_c", "scif2_clk_d", "scif2_clk_e",
	"scif2_ctrl_a", "scif2_ctrl_b", "scif2_ctrl_c", "scif2_ctrl_d", "scif2_ctrl_e",
	"scif2_data_a", "scif2_data_b", "scif2_data_c", "scif2_data_d", "scif2_data_e",
};

static const char *scif3_groups[] = {
	"scif3_clk", "scif3_data",
};

static const char *scif4_groups[] = {
	"scif4_clk", "scif4_data",
};

static const char *usb0_groups[] = {
	"usb0_a", "usb0_a_otg_exicen", "usb0_a_otg_id",
	"usb0_b", "usb0_b_otg_exicen", "usb0_b_otg_id",
};

static const char *usb1_groups[] = {
	"usb1_a", "usb1_b", "usb1_c", "usb1_d",
};

static const struct function_desc pinmux_functions[] = {
	RZ_G2L_FN_DESC(i2c2),
	RZ_G2L_FN_DESC(i2c3),
	RZ_G2L_FN_DESC(scif0),
	RZ_G2L_FN_DESC(scif1),
	RZ_G2L_FN_DESC(scif2),
	RZ_G2L_FN_DESC(scif3),
	RZ_G2L_FN_DESC(scif4),
	RZ_G2L_FN_DESC(usb0),
	RZ_G2L_FN_DESC(usb1),
};

const struct rzg2l_pin_soc r9a07g044_pinctrl_data = {
	.pins = pinmux_pins.pin_gpio,
	.npins = ARRAY_SIZE(pinmux_pins.pin_gpio),
	.groups = pinmux_groups,
	.ngroups = ARRAY_SIZE(pinmux_groups),
	.funcs = pinmux_functions,
	.nfuncs = ARRAY_SIZE(pinmux_functions),
	.nports = ARRAY_SIZE(pinmux_pins.pin_gpio) / RZG2L_MAX_PINS_PER_PORT,
};
