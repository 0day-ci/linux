/* SPDX-License-Identifier: GPL-2.0 */
/* SP7021 Pin Controller Driver.
 * Copyright (C) Sunplus Tech/Tibbo Tech.
 */

#ifndef __SPPCTL_H__
#define __SPPCTL_H__

#define SPPCTL_MODULE_NAME		"sppctl_sp7021"
#define SPPCTL_MAX_GROUPS		5

#define SPPCTL_GPIO_OFF_FIRST		0x00
#define SPPCTL_GPIO_OFF_MASTER		0x00
#define SPPCTL_GPIO_OFF_OE		0x20
#define SPPCTL_GPIO_OFF_OUT		0x40
#define SPPCTL_GPIO_OFF_IN		0x60
#define SPPCTL_GPIO_OFF_IINV		0x00
#define SPPCTL_GPIO_OFF_OINV		0x20
#define SPPCTL_GPIO_OFF_OD		0x40

#define SPPCTL_FULLY_PINMUX_MASK_MASK	GENMASK(22, 16)
#define SPPCTL_FULLY_PINMUX_SEL_MASK	GENMASK(6, 0)
#define SPPCTL_FULLY_PINMUX_UPPER_SHIFT	8
#define SPPCTL_FULLY_PINMUX_TBL_START	2

/* Fully pin-mux pin maps to GPIO(8 : 71)
 * Refer to following table:
 *
 * control-field |  GPIO
 * --------------+--------
 *        1      |    8
 *        2      |    9
 *        3      |   10
 *        :      |    :
 *       65      |   71
 */
#define SPPCTL_FULLY_PINMUX_CONV(x)	((x) - 7)

#define SPPCTL_GROUP_PINMUX_MASK_SHIFT	16
#define SPPCTL_MASTER_MASK_SHIFT	16
#define SPPCTL_GPIO_MASK_SHIFT		16

#define FNCE(n, r, o, bo, bl, g) { \
	.name = n, \
	.freg = r, \
	.roff = o, \
	.boff = bo, \
	.blen = bl, \
	.grps = (g), \
	.gnum = ARRAY_SIZE(g), \
}

#define FNCN(n, r, o, bo, bl) { \
	.name = n, \
	.freg = r, \
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

/* FIRST register:
 *   0: MUX
 *   1: GPIO/IOP
 *   2: No change
 */
enum mux_f_mg {
	mux_f_mux = 0,
	mux_f_gpio = 1,
	mux_f_keep = 2,
};

/* MASTER register:
 *   0: IOP
 *   1: GPIO
 *   2: No change
 */
enum mux_m_ig {
	mux_m_iop = 0,
	mux_m_gpio = 1,
	mux_m_keep = 2,
};

enum f_off {
	f_off_0,	/* nowhere          */
	f_off_m,	/* mux registers    */
	f_off_g,	/* group registers  */
	f_off_i,	/* iop registers    */
};

struct grp2fp_map {
	u16 f_idx;      /* function index   */
	u16 g_idx;      /* pins/group index */
};

struct sppctl_sdata {
	u8 i;
	u8 ridx;
	struct sppctl_pdata *pdata;
};

struct sppctl_gpio_chip {
	void __iomem *gpioxt_base;	/* MASTER, OE, OUT, IN */
	void __iomem *gpioxt2_base;	/* I_INV, O_INV, OD    */
	void __iomem *first_base;	/* GPIO_FIRST          */

	struct gpio_chip chip;
};

struct sppctl_pdata {
	/* base addresses */
	void __iomem *moon2_base;	/* MOON2               */
	void __iomem *gpioxt_base;	/* MASTER, OE, OUT, IN */
	void __iomem *gpioxt2_base;	/* I_INV, O_INV, OD    */
	void __iomem *first_base;	/* FIRST               */
	void __iomem *moon1_base;	/* MOON1               */

	/* pinctrl and gpio-chip */
	struct pinctrl_desc pctl_desc;
	struct pinctrl_dev *pctl_dev;
	struct pinctrl_gpio_range pctl_grange;
	struct sppctl_gpio_chip *spp_gchip;

	/* others */
	char const **unq_grps;
	struct grp2fp_map *g2fp_maps;
	size_t unq_grps_sz;
	const char **groups_name;
};

struct sppctl_grp {
	const char * const name;
	const u8 gval;                  /* group number    */
	const unsigned * const pins;    /* list of pins    */
	const unsigned int pnum;        /* number of pins  */
};

struct sppctl_func {
	const char * const name;
	const enum f_off freg;          /* function register type */
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
