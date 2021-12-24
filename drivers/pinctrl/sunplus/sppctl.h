/* SPDX-License-Identifier: GPL-2.0 */
/*
 * SP7021 Pin Controller Driver.
 * Copyright (C) Sunplus Tech / Tibbo Tech.
 */

#ifndef __SPPCTL_H__
#define __SPPCTL_H__

#include <linux/bits.h>
#include <linux/gpio/driver.h>
#include <linux/kernel.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/spinlock.h>

#define SPPCTL_MODULE_NAME		"sppctl_sp7021"
#define SPPCTL_MAX_GROUPS		5

#define SPPCTL_GPIO_OFF_FIRST		0x00
#define SPPCTL_GPIO_OFF_MASTER		0x00
#define SPPCTL_GPIO_OFF_OE		0x20
#define SPPCTL_GPIO_OFF_OUT		0x40
#define SPPCTL_GPIO_OFF_IN		0x60
#define SPPCTL_GPIO_OFF_IINV		0x80
#define SPPCTL_GPIO_OFF_OINV		0xa0
#define SPPCTL_GPIO_OFF_OD		0xc0

#define SPPCTL_FULLY_PINMUX_MASK_MASK	GENMASK(22, 16)
#define SPPCTL_FULLY_PINMUX_SEL_MASK	GENMASK(6, 0)
#define SPPCTL_FULLY_PINMUX_UPPER_SHIFT	8
#define SPPCTL_GROUP_PINMUX_MASK_SHIFT	16
#define SPPCTL_MASTER_MASK_SHIFT	16
#define SPPCTL_GPIO_MASK_SHIFT		16

#define SPPCTL_IOP_CONFIGS		0xff

#define FNCE(n, r, o, bo, bl, g) { \
	.name = n, \
	.type = r, \
	.roff = o, \
	.boff = bo, \
	.blen = bl, \
	.grps = (g), \
	.gnum = ARRAY_SIZE(g), \
}

#define FNCN(n, r, o, bo, bl) { \
	.name = n, \
	.type = r, \
	.roff = o, \
	.boff = bo, \
	.blen = bl, \
	.grps = NULL, \
	.gnum = 0, \
}

#define EGRP(n, v, p) { \
	.name = n, \
	.gval = (v), \
	.pins = (p), \
	.pnum = ARRAY_SIZE(p), \
}

/** enum mux_first_reg - define modes of FIRST register accesses
 *    - mux_f_mux:  Select the pin to a fully-pinmux pin
 *    - mux_f_gpio: Select the pin to a GPIO or IOP pin
 *    - mux_f_keep: Don't change (keep intact)
 */
enum mux_first_reg {
	mux_f_mux = 0,		/* select fully-pinmux       */
	mux_f_gpio = 1,		/* select GPIO or IOP pinmux */
	mux_f_keep = 2,		/* keep no change            */
};

/** enum mux_master_reg - define modes of MASTER register accesses
 *    - mux_m_iop:  Select the pin to a IO processor (IOP) pin
 *    - mux_m_gpio: Select the pin to a digital GPIO pin
 *    - mux_m_keep: Don't change (keep intact)
 */
enum mux_master_reg {
	mux_m_iop = 0,		/* select IOP pin   */
	mux_m_gpio = 1,		/* select GPIO pin  */
	mux_m_keep = 2,		/* select no change */
};

/** enum pinmux_type - define types of pinmux pins
 *    - pinmux_type_fpmx: It is a fully-pinmux pin
 *    - pinmux_type_grp:  It is a group-pinmux pin
 */
enum pinmux_type {
	pinmux_type_fpmx,	/* fully-pinmux */
	pinmux_type_grp,	/* group-pinmux */
};

struct grp2fp_map {
	u16 f_idx;		/* function index */
	u16 g_idx;		/* group index    */
};

struct sppctl_gpio_chip {
	void __iomem *gpioxt_base;	/* MASTER, OE, OUT, IN, I_INV, O_INV, OD */
	void __iomem *first_base;	/* GPIO_FIRST                            */

	struct gpio_chip chip;
	spinlock_t lock;		/* lock for accessing OE register        */
};

struct sppctl_pdata {
	/* base addresses */
	void __iomem *moon2_base;	/* MOON2                                 */
	void __iomem *gpioxt_base;	/* MASTER, OE, OUT, IN, I_INV, O_INV, OD */
	void __iomem *first_base;	/* FIRST                                 */
	void __iomem *moon1_base;	/* MOON1               */

	/* pinctrl and gpio-chip */
	struct pinctrl_desc pctl_desc;
	struct pinctrl_dev *pctl_dev;
	struct pinctrl_gpio_range pctl_grange;
	struct sppctl_gpio_chip *spp_gchip;

	/* groups */
	char const **unq_grps;
	size_t unq_grps_sz;
	struct grp2fp_map *g2fp_maps;
};

struct sppctl_grp {
	const char * const name;
	const u8 gval;                  /* group number   */
	const unsigned * const pins;    /* list of pins   */
	const unsigned int pnum;        /* number of pins */
};

struct sppctl_func {
	const char * const name;
	const enum pinmux_type type;    /* function type          */
	const u8 roff;                  /* register offset        */
	const u8 boff;                  /* bit offset             */
	const u8 blen;                  /* bit length             */
	const struct sppctl_grp * const grps; /* list of groups   */
	const unsigned int gnum;        /* number of groups       */
};

extern const struct sppctl_func sppctl_list_funcs[];
extern const char * const sppctl_pmux_list_s[];
extern const char * const sppctl_gpio_list_s[];
extern const struct pinctrl_pin_desc sppctl_pins_all[];
extern const unsigned int sppctl_pins_gpio[];

extern const size_t sppctl_list_funcs_sz;
extern const size_t sppctl_pmux_list_sz;
extern const size_t sppctl_gpio_list_sz;
extern const size_t sppctl_pins_all_sz;

#endif
