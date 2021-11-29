/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2020 Linaro Ltd.
 */
#ifndef __PINCTRL_LPASS_LPI_H__
#define __PINCTRL_LPASS_LPI_H__

#define LPI_SLEW_RATE_CTL_REG	0xa000
#define LPI_TLMM_REG_OFFSET		0x1000
#define LPI_SLEW_RATE_MAX		0x03
#define LPI_SLEW_BITS_SIZE		0x02
#define LPI_SLEW_RATE_MASK		GENMASK(1, 0)
#define LPI_GPIO_CFG_REG		0x00
#define LPI_GPIO_PULL_MASK		GENMASK(1, 0)
#define LPI_GPIO_FUNCTION_MASK		GENMASK(5, 2)
#define LPI_GPIO_OUT_STRENGTH_MASK	GENMASK(8, 6)
#define LPI_GPIO_OE_MASK		BIT(9)
#define LPI_GPIO_VALUE_REG		0x04
#define LPI_GPIO_VALUE_IN_MASK		BIT(0)
#define LPI_GPIO_VALUE_OUT_MASK		BIT(1)

#define LPI_GPIO_BIAS_DISABLE		0x0
#define LPI_GPIO_PULL_DOWN		0x1
#define LPI_GPIO_KEEPER			0x2
#define LPI_GPIO_PULL_UP		0x3
#define LPI_GPIO_DS_TO_VAL(v)		(v / 2 - 1)
#define NO_SLEW				-1

#define LPI_FUNCTION(fname)			                \
	[LPI_MUX_##fname] = {		                \
		.name = #fname,				\
		.groups = fname##_groups,               \
		.ngroups = ARRAY_SIZE(fname##_groups),	\
	}

#define LPI_PINGROUP(id, soff, f1, f2, f3, f4)		\
	{						\
		.name = "gpio" #id,			\
		.pins = gpio##id##_pins,		\
		.pin = id,				\
		.slew_offset = soff,			\
		.npins = ARRAY_SIZE(gpio##id##_pins),	\
		.funcs = (int[]){			\
			LPI_MUX_gpio,			\
			LPI_MUX_##f1,			\
			LPI_MUX_##f2,			\
			LPI_MUX_##f3,			\
			LPI_MUX_##f4,			\
		},					\
		.nfuncs = 5,				\
	}

struct lpi_pingroup {
	const char *name;
	const unsigned int *pins;
	unsigned int npins;
	unsigned int pin;
	/* Bit offset in slew register for SoundWire pins only */
	int slew_offset;
	unsigned int *funcs;
	unsigned int nfuncs;
};

struct lpi_function {
	const char *name;
	const char * const *groups;
	unsigned int ngroups;
};

struct lpi_pinctrl_variant_data {
	const struct pinctrl_pin_desc *pins;
	int npins;
	const struct lpi_pingroup *groups;
	int ngroups;
	const struct lpi_function *functions;
	int nfunctions;
};

#define MAX_LPI_NUM_CLKS	2

struct lpi_pinctrl {
	struct device *dev;
	struct pinctrl_dev *ctrl;
	struct gpio_chip chip;
	struct pinctrl_desc desc;
	char __iomem *tlmm_base;
	char __iomem *slew_base;
	struct clk_bulk_data clks[MAX_LPI_NUM_CLKS];
	struct mutex slew_access_lock;
	const struct lpi_pinctrl_variant_data *data;
};

enum lpass_lpi_functions {
	LPI_MUX_dmic1_clk,
	LPI_MUX_dmic1_data,
	LPI_MUX_dmic2_clk,
	LPI_MUX_dmic2_data,
	LPI_MUX_dmic3_clk,
	LPI_MUX_dmic3_data,
	LPI_MUX_i2s1_clk,
	LPI_MUX_i2s1_data,
	LPI_MUX_i2s1_ws,
	LPI_MUX_i2s2_clk,
	LPI_MUX_i2s2_data,
	LPI_MUX_i2s2_ws,
	LPI_MUX_qua_mi2s_data,
	LPI_MUX_qua_mi2s_sclk,
	LPI_MUX_qua_mi2s_ws,
	LPI_MUX_swr_rx_clk,
	LPI_MUX_swr_rx_data,
	LPI_MUX_swr_tx_clk,
	LPI_MUX_swr_tx_data,
	LPI_MUX_wsa_swr_clk,
	LPI_MUX_wsa_swr_data,
	LPI_MUX_gpio,
	LPI_MUX__,
};

static const unsigned int gpio0_pins[] = { 0 };
static const unsigned int gpio1_pins[] = { 1 };
static const unsigned int gpio2_pins[] = { 2 };
static const unsigned int gpio3_pins[] = { 3 };
static const unsigned int gpio4_pins[] = { 4 };
static const unsigned int gpio5_pins[] = { 5 };
static const unsigned int gpio6_pins[] = { 6 };
static const unsigned int gpio7_pins[] = { 7 };
static const unsigned int gpio8_pins[] = { 8 };
static const unsigned int gpio9_pins[] = { 9 };
static const unsigned int gpio10_pins[] = { 10 };
static const unsigned int gpio11_pins[] = { 11 };
static const unsigned int gpio12_pins[] = { 12 };
static const unsigned int gpio13_pins[] = { 13 };
static const unsigned int gpio14_pins[] = { 14 };

int lpi_pinctrl_probe(struct platform_device *pdev);
int lpi_pinctrl_remove(struct platform_device *pdev);

#endif /*__PINCTRL_LPASS_LPI_H__*/

