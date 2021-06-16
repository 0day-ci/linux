/* SPDX-License-Identifier: GPL-2.0
 *
 * Renesas RZ/G2L Pin Function Controller and GPIO support
 *
 * Copyright (C) 2021 Renesas Electronics Corp.
 */

#ifndef __PINCTRL_RZG2L_H__
#define __PINCTRL_RZG2L_H__

#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/phy.h>

#include "../core.h"
#include "../pinconf.h"
#include "../pinmux.h"

#define RZG2L_MAX_PINS_PER_PORT		8

struct rzg2l_pin_soc {
	const struct pinctrl_pin_desc	*pins;
	const unsigned int		npins;
	const struct group_desc		*groups;
	const unsigned int		ngroups;
	const struct function_desc	*funcs;
	const unsigned int		nfuncs;
	const unsigned int		nports;
};

#define RZ_G2L_PINCTRL_PIN_GPIO(port, configs)			\
	{							\
		(RZG2L_MAX_PINS_PER_PORT) * (port),		\
		__stringify(P##port##_0),			\
		(void *)(configs),				\
	},							\
	{							\
		(RZG2L_MAX_PINS_PER_PORT) * (port) + 1,		\
		__stringify(P##port##_1),			\
		(void *)(configs),				\
	},							\
	{							\
		(RZG2L_MAX_PINS_PER_PORT) * (port) + 2,		\
		__stringify(P##port##_2),			\
		(void *)(configs),				\
	},							\
	{							\
		(RZG2L_MAX_PINS_PER_PORT) * (port) + 3,		\
		__stringify(P##port##_3),			\
		(void *)(configs),				\
	},							\
	{							\
		(RZG2L_MAX_PINS_PER_PORT) * (port) + 4,		\
		__stringify(P##port##_4),			\
		(void *)(configs),				\
	},							\
	{							\
		(RZG2L_MAX_PINS_PER_PORT) * (port) + 5,		\
		__stringify(P##port##_5),			\
		(void *)(configs),				\
	},							\
	{							\
		(RZG2L_MAX_PINS_PER_PORT) * (port) + 6,		\
		__stringify(P##port##_6),			\
		(void *)(configs),				\
	},							\
	{							\
		(RZG2L_MAX_PINS_PER_PORT) * (port) + 7,		\
		__stringify(P##port##_7),			\
		(void *)(configs),				\
	}

#define RZ_G2L_PIN(port, bit)		((port) * RZG2L_MAX_PINS_PER_PORT + (bit))

#define RZ_G2L_PINCTRL_PIN_GROUP(name, mode)			\
	{							\
		__stringify(name),				\
		name##_pins,					\
		ARRAY_SIZE(name##_pins),			\
		(void *)mode,					\
	}

#define RZ_G2L_FN_DESC(id)	{ #id, id##_groups, ARRAY_SIZE(id##_groups) }

#endif
