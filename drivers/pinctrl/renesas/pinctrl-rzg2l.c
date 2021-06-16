// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas RZ/G2L Pin Control and GPIO driver core
 *
 * Copyright (C) 2021 Renesas Electronics Corporation.
 */

#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include "pinctrl-rzg2l.h"

#define DRV_NAME	"pinctrl-rzg2l"

#define P(n)			(0x0000 + 0x10 + (n))
#define PM(n)			(0x0100 + 0x20 + (n) * 2)
#define PMC(n)			(0x0200 + 0x10 + (n))
#define PFC(n)			(0x0400 + 0x40 + (n) * 4)
#define PIN(n)			(0x0800 + 0x10 + (n))
#define PWPR			(0x3014)

#define PWPR_B0WI		BIT(7)	/* Bit Write Disable */
#define PWPR_PFCWE		BIT(6)	/* PFC Register Write Enable */

#define PM_MASK			0x03
#define PFC_MASK		0x07

#define PM_INPUT		0x1
#define PM_OUTPUT		0x2
#define PM_OUTPUT_INPUT		0x3

#define GPIOF_OUTPUT			0
#define GPIOF_INPUT			1
#define GPIOF_BIDIRECTION		2
#define GPIOF_HI_Z			3

#define RZG2L_PIN_ID_TO_PORT(id)	((id) / RZG2L_MAX_PINS_PER_PORT)
#define RZG2L_PIN_ID_TO_PIN(id)		((id) % RZG2L_MAX_PINS_PER_PORT)

struct rzg2l_pinctrl {
	struct pinctrl_dev		*pctrl_dev;
	struct pinctrl_desc		pctrl_desc;

	void __iomem			*base;
	struct device			*dev;
	struct clk			*clk;

	struct gpio_chip		gpio_chip;
	struct pinctrl_gpio_range	gpio_range;

	const struct rzg2l_pin_soc	*psoc;

	spinlock_t			lock;
	unsigned int			nports;
};

static void rzg2l_pinctrl_set_pfc_mode(struct rzg2l_pinctrl *pctrl,
				       int pins, unsigned long pfc_mode)
{
	u32 port = RZG2L_PIN_ID_TO_PORT(pins);
	u8 bit = RZG2L_PIN_ID_TO_PIN(pins);
	unsigned long flags;
	u32 reg32, mask32;
	u16 reg16, mask16;
	u8 reg8;

	spin_lock_irqsave(&pctrl->lock, flags);

	/* Set pin to 'Non-use (Hi-Z input protection)'  */
	reg16 = readw(pctrl->base + PM(port));
	mask16 = PM_MASK << (bit * 2);
	reg16 = reg16 & ~mask16;
	writew(reg16, pctrl->base + PM(port));

	/* Temporarily switch to GPIO mode with PMC register */
	reg8 = readb(pctrl->base + PMC(port));
	writeb(reg8 & ~BIT(bit), pctrl->base + PMC(port));

	/* Set the PWPR register to allow PFC register to write */
	writel(0x00, pctrl->base + PWPR);        /* B0WI=0, PFCWE=0 */
	writel(PWPR_PFCWE, pctrl->base + PWPR);  /* B0WI=0, PFCWE=1 */

	/* Select Pin function mode with PFC register */
	reg32 = readl(pctrl->base + PFC(port));
	mask32 = PFC_MASK << (bit * 4);
	reg32 = reg32 & ~mask32;
	pfc_mode = pfc_mode << (bit * 4);
	writel(reg32 | pfc_mode, pctrl->base + PFC(port));

	/* Set the PWPR register to be write-protected */
	writel(0x00, pctrl->base + PWPR);        /* B0WI=0, PFCWE=0 */
	writel(PWPR_B0WI, pctrl->base + PWPR);  /* B0WI=1, PFCWE=0 */

	/* Switch to Peripheral pin function with PMC register */
	reg8 = readb(pctrl->base + PMC(port));
	writeb(reg8 | BIT(bit), pctrl->base + PMC(port));

	spin_unlock_irqrestore(&pctrl->lock, flags);
};

static int rzg2l_pinctrl_set_mux(struct pinctrl_dev *pctldev,
				 unsigned int func_selector,
				 unsigned int group_selector)
{
	struct rzg2l_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct function_desc *func;
	struct group_desc *group;
	unsigned long data;
	int *pins;
	int i;

	func = pinmux_generic_get_function(pctldev, func_selector);
	if (!func)
		return -EINVAL;
	group = pinctrl_generic_get_group(pctldev, group_selector);
	if (!group)
		return -EINVAL;

	pins = group->pins;
	data = (unsigned long)group->data;

	dev_dbg(pctldev->dev, "enable function %s group %s\n",
		func->name, group->name);

	for (i = 0; i < group->num_pins; i++)
		rzg2l_pinctrl_set_pfc_mode(pctrl, *(pins + i), data);

	return 0;
};

static const struct pinctrl_ops rzg2l_pinctrl_pctlops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinconf_generic_dt_free_map,
};

static const struct pinmux_ops rzg2l_pinctrl_pmxops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = rzg2l_pinctrl_set_mux,
	.strict = true,
};

static int rzg2l_pinctrl_add_groups(struct rzg2l_pinctrl *pctrl)
{
	int ret, i;

	for (i = 0; i < pctrl->psoc->ngroups; i++) {
		const struct group_desc *group = pctrl->psoc->groups + i;

		ret = pinctrl_generic_add_group(pctrl->pctrl_dev, group->name,
						group->pins, group->num_pins,
						group->data);
		if (ret < 0) {
			dev_err(pctrl->dev, "Failed to register group %s\n",
				group->name);
			return ret;
		}
	}

	return 0;
}

static int rzg2l_pinctrl_add_functions(struct rzg2l_pinctrl *pctrl)
{
	int ret, i;

	for (i = 0; i < pctrl->psoc->nfuncs; i++) {
		const struct function_desc *func = pctrl->psoc->funcs + i;

		ret = pinmux_generic_add_function(pctrl->pctrl_dev, func->name,
						  func->group_names,
						  func->num_group_names,
						  func->data);
		if (ret < 0) {
			dev_err(pctrl->dev, "Failed to register function %s\n",
				func->name);
			return ret;
		}
	}

	return 0;
}

static int rzg2l_gpio_request(struct gpio_chip *chip, unsigned int offset)
{
	struct rzg2l_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 port = RZG2L_PIN_ID_TO_PORT(offset);
	u8 bit = RZG2L_PIN_ID_TO_PIN(offset);
	unsigned long flags;
	u8 reg8;
	int ret;

	ret = pinctrl_gpio_request(chip->base + offset);
	if (ret)
		return ret;

	spin_lock_irqsave(&pctrl->lock, flags);

	/* Select GPIO mode in PMC Register */
	reg8 = readb(pctrl->base + PMC(port));
	reg8 &= ~BIT(bit);
	writeb(reg8, pctrl->base + PMC(port));

	spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static void rzg2l_gpio_set_direction(struct rzg2l_pinctrl *pctrl, u32 port,
				     u8 bit, bool output)
{
	unsigned long flags;
	u16 reg16;

	spin_lock_irqsave(&pctrl->lock, flags);

	reg16 = readw(pctrl->base + PM(port));
	reg16 = reg16 & ~(PM_MASK << (bit * 2));

	if (output)
		writew(reg16 | (PM_OUTPUT << (bit * 2)),
		       pctrl->base + PM(port));
	else
		writew(reg16 | (PM_INPUT << (bit * 2)),
		       pctrl->base + PM(port));

	spin_unlock_irqrestore(&pctrl->lock, flags);
}

static int rzg2l_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct rzg2l_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 port = RZG2L_PIN_ID_TO_PORT(offset);
	u8 bit = RZG2L_PIN_ID_TO_PIN(offset);

	if (!(readb(pctrl->base + PMC(port)) & BIT(bit))) {
		u16 reg16;

		reg16 = readw(pctrl->base + PM(port));
		reg16 = (reg16 >> (bit * 2)) & PM_MASK;
		if (reg16 == PM_OUTPUT)
			return GPIOF_OUTPUT;
		else if (reg16 == PM_INPUT)
			return GPIOF_INPUT;
		else if (reg16 == PM_OUTPUT_INPUT)
			return GPIOF_BIDIRECTION;
		else
			return GPIOF_HI_Z;
	} else {
		return -EINVAL;
	}
}

static int rzg2l_gpio_direction_input(struct gpio_chip *chip,
				      unsigned int offset)
{
	struct rzg2l_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 port = RZG2L_PIN_ID_TO_PORT(offset);
	u8 bit = RZG2L_PIN_ID_TO_PIN(offset);

	rzg2l_gpio_set_direction(pctrl, port, bit, false);

	return 0;
}

static void rzg2l_gpio_set(struct gpio_chip *chip, unsigned int offset,
			   int value)
{
	struct rzg2l_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 port = RZG2L_PIN_ID_TO_PORT(offset);
	u8 bit = RZG2L_PIN_ID_TO_PIN(offset);
	unsigned long flags;
	u8 reg8;

	spin_lock_irqsave(&pctrl->lock, flags);

	reg8 = readb(pctrl->base + P(port));

	if (value)
		writeb(reg8 | BIT(bit), pctrl->base + P(port));
	else
		writeb(reg8 & ~BIT(bit), pctrl->base + P(port));

	spin_unlock_irqrestore(&pctrl->lock, flags);
}

static int rzg2l_gpio_direction_output(struct gpio_chip *chip,
				       unsigned int offset, int value)
{
	struct rzg2l_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 port = RZG2L_PIN_ID_TO_PORT(offset);
	u8 bit = RZG2L_PIN_ID_TO_PIN(offset);

	rzg2l_gpio_set_direction(pctrl, port, bit, true);
	rzg2l_gpio_set(chip, offset, value);

	return 0;
}

static int rzg2l_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct rzg2l_pinctrl *pctrl = gpiochip_get_data(chip);
	u32 port = RZG2L_PIN_ID_TO_PORT(offset);
	u8 bit = RZG2L_PIN_ID_TO_PIN(offset);
	u16 reg16;

	reg16 = readw(pctrl->base + PM(port));
	reg16 = (reg16 >> (bit * 2)) & PM_MASK;

	if (reg16 == PM_INPUT || reg16 == PM_OUTPUT_INPUT)
		return !!(readb(pctrl->base + PIN(port)) & BIT(bit));
	else if (reg16 == PM_OUTPUT)
		return !!(readb(pctrl->base + P(port)) & BIT(bit));
	else
		return -EINVAL;
}

static void rzg2l_gpio_free(struct gpio_chip *chip, unsigned int offset)
{
	pinctrl_gpio_free(chip->base + offset);

	/*
	 * Set the GPIO as an input to ensure that the next GPIO request won't
	 * drive the GPIO pin as an output.
	 */
	rzg2l_gpio_direction_input(chip, offset);
}

static const char * const rzg2l_gpio_names[] = {
	"P0_0", "P0_1", "P0_2", "P0_3", "P0_4", "P0_5", "P0_6", "P0_7",
	"P1_0", "P1_1", "P1_2", "P1_3", "P1_4", "P1_5", "P1_6", "P1_7",
	"P2_0", "P2_1", "P2_2", "P2_3", "P2_4", "P2_5", "P2_6", "P2_7",
	"P3_0", "P3_1", "P3_2", "P3_3", "P3_4", "P3_5", "P3_6", "P3_7",
	"P4_0", "P4_1", "P4_2", "P4_3", "P4_4", "P4_5", "P4_6", "P4_7",
	"P5_0", "P5_1", "P5_2", "P5_3", "P5_4", "P5_5", "P5_6", "P5_7",
	"P6_0", "P6_1", "P6_2", "P6_3", "P6_4", "P6_5", "P6_6", "P6_7",
	"P7_0", "P7_1", "P7_2", "P7_3", "P7_4", "P7_5", "P7_6", "P7_7",
	"P8_0", "P8_1", "P8_2", "P8_3", "P8_4", "P8_5", "P8_6", "P8_7",
	"P9_0", "P9_1", "P9_2", "P9_3", "P9_4", "P9_5", "P9_6", "P9_7",
	"P10_0", "P10_1", "P10_2", "P10_3", "P10_4", "P10_5", "P10_6", "P10_7",
	"P11_0", "P11_1", "P11_2", "P11_3", "P11_4", "P11_5", "P11_6", "P11_7",
	"P12_0", "P12_1", "P12_2", "P12_3", "P12_4", "P12_5", "P12_6", "P12_7",
	"P13_0", "P13_1", "P13_2", "P13_3", "P13_4", "P13_5", "P13_6", "P13_7",
	"P14_0", "P14_1", "P14_2", "P14_3", "P14_4", "P14_5", "P14_6", "P14_7",
	"P15_0", "P15_1", "P15_2", "P15_3", "P15_4", "P15_5", "P15_6", "P15_7",
	"P16_0", "P16_1", "P16_2", "P16_3", "P16_4", "P16_5", "P16_6", "P16_7",
	"P17_0", "P17_1", "P17_2", "P17_3", "P17_4", "P17_5", "P17_6", "P17_7",
	"P18_0", "P18_1", "P18_2", "P18_3", "P18_4", "P18_5", "P18_6", "P18_7",
	"P19_0", "P19_1", "P19_2", "P19_3", "P19_4", "P19_5", "P19_6", "P19_7",
	"P20_0", "P20_1", "P20_2", "P20_3", "P20_4", "P20_5", "P20_6", "P20_7",
	"P21_0", "P21_1", "P21_2", "P21_3", "P21_4", "P21_5", "P21_6", "P21_7",
	"P22_0", "P22_1", "P22_2", "P22_3", "P22_4", "P22_5", "P22_6", "P22_7",
	"P23_0", "P23_1", "P23_2", "P23_3", "P23_4", "P23_5", "P23_6", "P23_7",
	"P24_0", "P24_1", "P24_2", "P24_3", "P24_4", "P24_5", "P24_6", "P24_7",
	"P25_0", "P25_1", "P25_2", "P25_3", "P25_4", "P25_5", "P25_6", "P25_7",
	"P26_0", "P26_1", "P26_2", "P26_3", "P26_4", "P26_5", "P26_6", "P26_7",
	"P27_0", "P27_1", "P27_2", "P27_3", "P27_4", "P27_5", "P27_6", "P27_7",
	"P28_0", "P28_1", "P28_2", "P28_3", "P28_4", "P28_5", "P28_6", "P28_7",
	"P29_0", "P29_1", "P29_2", "P29_3", "P29_4", "P29_5", "P29_6", "P29_7",
	"P30_0", "P30_1", "P30_2", "P30_3", "P30_4", "P30_5", "P30_6", "P30_7",
	"P31_0", "P31_1", "P31_2", "P31_3", "P31_4", "P31_5", "P31_6", "P31_7",
	"P32_0", "P32_1", "P32_2", "P32_3", "P32_4", "P32_5", "P32_6", "P32_7",
	"P33_0", "P33_1", "P33_2", "P33_3", "P33_4", "P33_5", "P33_6", "P33_7",
	"P34_0", "P34_1", "P34_2", "P34_3", "P34_4", "P34_5", "P34_6", "P34_7",
	"P35_0", "P35_1", "P35_2", "P35_3", "P35_4", "P35_5", "P35_6", "P35_7",
	"P36_0", "P36_1", "P36_2", "P36_3", "P36_4", "P36_5", "P36_6", "P36_7",
	"P37_0", "P37_1", "P37_2", "P37_3", "P37_4", "P37_5", "P37_6", "P37_7",
	"P38_0", "P38_1", "P38_2", "P38_3", "P38_4", "P38_5", "P38_6", "P38_7",
	"P39_0", "P39_1", "P39_2", "P39_3", "P39_4", "P39_5", "P39_6", "P39_7",
	"P40_0", "P40_1", "P40_2", "P40_3", "P40_4", "P40_5", "P40_6", "P40_7",
	"P41_0", "P41_1", "P41_2", "P41_3", "P41_4", "P41_5", "P41_6", "P41_7",
	"P42_0", "P42_1", "P42_2", "P42_3", "P42_4", "P42_5", "P42_6", "P42_7",
	"P43_0", "P43_1", "P43_2", "P43_3", "P43_4", "P43_5", "P43_6", "P43_7",
	"P44_0", "P44_1", "P44_2", "P44_3", "P44_4", "P44_5", "P44_6", "P44_7",
	"P45_0", "P45_1", "P45_2", "P45_3", "P45_4", "P45_5", "P45_6", "P45_7",
	"P46_0", "P46_1", "P46_2", "P46_3", "P46_4", "P46_5", "P46_6", "P46_7",
	"P47_0", "P47_1", "P47_2", "P47_3", "P47_4", "P47_5", "P47_6", "P47_7",
	"P48_0", "P48_1", "P48_2", "P48_3", "P48_4", "P48_5", "P48_6", "P48_7",
};

static int rzg2l_gpio_register(struct rzg2l_pinctrl *pctrl)
{
	struct device_node *np = pctrl->dev->of_node;
	struct gpio_chip *chip = &pctrl->gpio_chip;
	const char *name = dev_name(pctrl->dev);
	struct of_phandle_args of_args;
	int ret;

	ret = of_parse_phandle_with_fixed_args(np, "gpio-ranges", 3, 0, &of_args);
	if (ret) {
		dev_err(pctrl->dev, "Unable to parse gpio-ranges\n");
		return ret;
	}

	if (of_args.args[0] != 0 || of_args.args[1] != 0 ||
	    of_args.args[2] != ARRAY_SIZE(rzg2l_gpio_names)) {
		dev_err(pctrl->dev, "gpio-ranges does not match selected SOC\n");
		return -EINVAL;
	}

	chip->names = rzg2l_gpio_names;
	chip->request = rzg2l_gpio_request;
	chip->free = rzg2l_gpio_free;
	chip->get_direction = rzg2l_gpio_get_direction;
	chip->direction_input = rzg2l_gpio_direction_input;
	chip->direction_output = rzg2l_gpio_direction_output;
	chip->get = rzg2l_gpio_get;
	chip->set = rzg2l_gpio_set;
	chip->label = name;
	chip->parent = pctrl->dev;
	chip->owner = THIS_MODULE;
	chip->base = -1;
	chip->ngpio = of_args.args[2];

	pctrl->gpio_range.id = 0;
	pctrl->gpio_range.pin_base = 0;
	pctrl->gpio_range.base = 0;
	pctrl->gpio_range.npins = chip->ngpio;
	pctrl->gpio_range.name = chip->label;
	pctrl->gpio_range.gc = chip;
	ret = devm_gpiochip_add_data(pctrl->dev, chip, pctrl);
	if (ret) {
		dev_err(pctrl->dev, "failed to add GPIO controller\n");
		return ret;
	}

	dev_dbg(pctrl->dev, "Registered gpio controller\n");

	return 0;
}

static int rzg2l_pinctrl_probe(struct platform_device *pdev)
{
	const struct rzg2l_pin_soc *psoc;
	struct rzg2l_pinctrl *pctrl;
	int ret;

	psoc = of_device_get_match_data(&pdev->dev);
	if (!psoc)
		return -EINVAL;

	pctrl = devm_kzalloc(&pdev->dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->psoc = psoc;
	pctrl->nports = psoc->nports;
	pctrl->dev = &pdev->dev;

	pctrl->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pctrl->base))
		return PTR_ERR(pctrl->base);

	pctrl->clk = devm_clk_get(pctrl->dev, NULL);
	if (IS_ERR(pctrl->clk)) {
		ret = PTR_ERR(pctrl->clk);
		dev_err(pctrl->dev, "failed to get GPIO clk : %i\n", ret);
		return ret;
	};

	spin_lock_init(&pctrl->lock);

	pctrl->pctrl_desc.name = DRV_NAME;
	pctrl->pctrl_desc.pins = pctrl->psoc->pins;
	pctrl->pctrl_desc.npins = pctrl->psoc->npins;
	pctrl->pctrl_desc.pctlops = &rzg2l_pinctrl_pctlops;
	pctrl->pctrl_desc.pmxops = &rzg2l_pinctrl_pmxops;
	pctrl->pctrl_desc.owner = THIS_MODULE;

	ret = devm_pinctrl_register_and_init(pctrl->dev, &pctrl->pctrl_desc,
					     pctrl, &pctrl->pctrl_dev);
	if (ret) {
		dev_err(pctrl->dev, "could not register: %i\n", ret);
		return ret;
	};

	ret = rzg2l_pinctrl_add_groups(pctrl);
	if (ret)
		return ret;

	ret = rzg2l_pinctrl_add_functions(pctrl);
	if (ret)
		return ret;

	ret = pinctrl_enable(pctrl->pctrl_dev);
	if (ret)
		return ret;

	ret = rzg2l_gpio_register(pctrl);
	if (ret) {
		dev_err(pctrl->dev, "failed to add GPIO chip: %i\n", ret);
		return ret;
	};

	ret = clk_prepare_enable(pctrl->clk);
	if (ret) {
		dev_err(pctrl->dev, "failed to enable GPIO clk: %i\n", ret);
		return ret;
	};

	platform_set_drvdata(pdev, pctrl);

	dev_info(pctrl->dev, "%s support registered\n", DRV_NAME);
	return 0;
}

static const struct of_device_id rzg2l_pinctrl_of_table[] = {
	{ /* sentinel */ }
};

static struct platform_driver rzg2l_pinctrl_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(rzg2l_pinctrl_of_table),
	},
	.probe = rzg2l_pinctrl_probe,
};

static int __init rzg2l_pinctrl_init(void)
{
	return platform_driver_register(&rzg2l_pinctrl_driver);
}
subsys_initcall_sync(rzg2l_pinctrl_init);
