// SPDX-License-Identifier: GPL-2.0
/* SP7021 Pin Controller Driver.
 * Copyright (C) Sunplus Tech/Tibbo Tech.
 */

#include <linux/platform_device.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/bitfield.h>

#include <dt-bindings/pinctrl/sppctl-sp7021.h>
#include "../pinctrl-utils.h"
#include "../core.h"
#include "sppctl.h"

/* sppctl_func_set - Set pin of fully pin-mux function.
 *
 * Mask-fields and control-fields of fully pin-mux function of SP7021 are
 * arranged as shown below:
 *
 *  func# | register |  mask-field  | control-field
 * -------+----------+--------------+---------------
 *    0   | base[0]  |  (22 : 16)   |   ( 6 : 0)
 *    1   | base[0]  |  (30 : 24)   |   (14 : 8)
 *    2   | base[1]  |  (22 : 16)   |   ( 6 : 0)
 *    3   | baeg[1]  |  (30 : 24)   |   (14 : 8)
 *    :   |    :     |      :       |       :
 *
 * where mask-fields are used to protect control-fields from write-in
 * accidentally. Set the corresponding bits in the mask-field before
 * you write a value into a control-field.
 *
 * Control-fields are used to set where the function pin is going to
 * be routed to.
 *
 * Note that mask-fields and control-fields of even number of 'func'
 * are located at bits (22:16) and (6:0), while odd number of 'func's
 * are located at bits (30:24) and (14:8).
 */
static void sppctl_func_set(struct sppctl_pdata *pctl, u8 func, u8 val)
{
	u32 reg, offset;

	/* Note that upper 16-bit word are mask-fields and lower 16-bit
	 * word are the control-fields. Set corresponding bits in mask-
	 * field before write to a control-field.
	 */
	reg = SPPCTL_FULLY_PINMUX_MASK_MASK | val;

	/* Check if 'func' is an odd number or not. Mask and control-
	 * fields of odd number 'func' is located at upper portion of
	 * a register. Extra shift is needed.
	 */
	if (func & BIT(0))
		reg <<= SPPCTL_FULLY_PINMUX_UPPER_SHIFT;

	/* Convert func# to register offset w.r.t. base register. */
	offset = func * 2;
	offset &= GENMASK(31, 2);

	dev_dbg(pctl->pctl_dev->dev, "%s(0x%x, 0x%x): offset: 0x%x, reg: 0x%08x\n",
		__func__, func, val, offset, reg);

	writel(reg, pctl->moon2_base + offset);
}

static u8 sppctl_func_get(struct sppctl_pdata *pctl, u8 func)
{
	u32 reg, offset;
	u8 val;

	/* Refer to descriptions of sppctl_func_set().
	 * Convert func# to register offset w.r.t. base register.
	 */
	offset = func * 2;
	offset &= GENMASK(31, 2);

	reg = readl(pctl->moon2_base + offset);

	/* Check if 'func' is an odd number or not. Mask and control-
	 * fields of odd number 'func' is located at upper portion of
	 * a register. Extra shift is needed.
	 */
	if (func & BIT(0))
		val = reg >> SPPCTL_FULLY_PINMUX_UPPER_SHIFT;
	else
		val = reg;
	val = FIELD_GET(SPPCTL_FULLY_PINMUX_SEL_MASK, val);

	dev_dbg(pctl->pctl_dev->dev, "%s(0x%x): offset: 0x%x, reg: 0x%08X, val: 0x%x\n",
		__func__, func, offset, reg, val);

	return val;
}

/* sppctl_gmx_set - Set pin of group pin-mux.
 *
 * Mask-fields and control-fields of group pin-mux function of SP7021 are
 * arranged as shown below:
 *
 *  register |  mask-fields | control-fields
 * ----------+--------------+----------------
 *  base[0]  |  (31 : 16)   |   (15 : 0)
 *  base[1]  |  (31 : 24)   |   (15 : 0)
 *  base[2]  |  (31 : 24)   |   (15 : 0)
 *     :     |      :       |       :
 *
 * where mask-fields are used to protect control-fields from write-in
 * accidentally. Set the corresponding bits in the mask-field before
 * you write a value into a control-field.
 *
 * Control-fields are used to set where the function pin is going to
 * be routed to. A control-field consists of one or more bits.
 */
static void sppctl_gmx_set(struct sppctl_pdata *pctl, u8 gmx, u8 bit_off, u8 bit_sz,
			   u8 val)
{
	u32 mask, reg;

	/* Note that upper 16-bit word are mask-fields and lower 16-bit
	 * word are the control-fields. Set corresponding bits in mask-
	 * field before write to a control-field.
	 */
	mask = GENMASK(bit_off + SPPCTL_GROUP_PINMUX_MASK_SHIFT + bit_sz - 1,
		       bit_off + SPPCTL_GROUP_PINMUX_MASK_SHIFT);
	reg = mask | (val << bit_off);

	writel(reg, pctl->moon1_base + gmx * 4);

	dev_dbg(pctl->pctl_dev->dev, "%s(0x%x, 0x%x, 0x%x, 0x%x): reg: 0x%08X\n",
		__func__, gmx, bit_off, bit_sz, val, reg);
}

/* sppctl_first_get - get bit of first register.
 *
 * There are 4 FIRST registers. Each has 32 control-bits.
 * Totally, there are 4 * 32 = 128 control-bits.
 * Control-bits are arranged as shown below:
 *
 *  registers | control-bits
 * -----------+--------------
 *  first[0]  |  (31 :  0)
 *  first[1]  |  (63 : 32)
 *  first[2]  |  (95 : 64)
 *  first[3]  | (127 : 96)
 *
 * Each control-bit sets type of a GPIO pin.
 *   0: a fully pin-mux pin
 *   1: a GPIO or IOP pin
 */
static int sppctl_first_get(struct gpio_chip *chip, unsigned int offset)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off = (offset / 32) * 4;
	u32 bit_off = offset % 32;
	u32 reg;
	int val;

	reg = readl(spp_gchip->first_base + SPPCTL_GPIO_OFF_FIRST + reg_off);
	val = (reg & BIT(bit_off)) ? 1 : 0;

	dev_dbg(chip->parent, "%s(%u): addr = %p, reg = %08x, val = %d\n", __func__, offset,
		spp_gchip->first_base + SPPCTL_GPIO_OFF_FIRST +	reg_off, reg, val);

	return val;
}

/* sppctl_master_get - get bit of master register.
 *
 * There are 8 MASTER registers. Each has 16 mask-bits and 16 control-bits.
 * Upper 16-bit of MASTER registers are mask-bits while lower 16-bit are
 * control-bits. Totally, there are 128 mask-bits and 128 control-bits.
 * They are arranged as shown below:
 *
 *  register  |  mask-bits  | control-bits
 * -----------+-------------+--------------
 *  master[0] |  (15 :   0) |  (15 :   0)
 *  master[1] |  (31 :  16) |  (31 :  16)
 *  master[2] |  (47 :  32) |  (47 :  32)
 *     :      |      :      |      :
 *  master[7] | (127 : 112) | (127 : 112)
 *
 * where mask-bits are used to protect control-bits from write-in
 * accidentally. Set the corresponding mask-bit before you write
 * a value into a control-bit.
 *
 * Each control-bit sets type of a GPIO pin when FIRST bit is 1.
 *   0: a IOP pin
 *   1: a GPIO pin
 */
static int sppctl_master_get(struct gpio_chip *chip, unsigned int offset)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off = (offset / 16) * 4;
	u32 bit_off = offset % 16;
	u32 reg;
	int val;

	reg = readl(spp_gchip->gpioxt_base + SPPCTL_GPIO_OFF_MASTER + reg_off);
	val = (reg & BIT(bit_off)) ? 1 : 0;

	dev_dbg(chip->parent, "%s(%u): addr = %p, reg = %08x, val = %d\n", __func__, offset,
		spp_gchip->gpioxt_base + SPPCTL_GPIO_OFF_MASTER + reg_off, reg, val);

	return val;
}

static void sppctl_first_master_set(struct gpio_chip *chip, unsigned int offset,
				    enum mux_f_mg first, enum mux_m_ig master)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off, bit_off, reg;
	int val;

	/* FIRST register */
	if (first != mux_f_keep) {
		/* Refer to descriptions of function sppctl_first_get()
		 * for usage of FIRST registers.
		 */
		reg_off = (offset / 32) * 4;
		bit_off = offset % 32;

		reg = readl(spp_gchip->first_base + SPPCTL_GPIO_OFF_FIRST + reg_off);
		val = (reg & BIT(bit_off)) ? 1 : 0;

		dev_dbg(chip->parent, "First: %08x (%p)\n", reg, spp_gchip->first_base +
			SPPCTL_GPIO_OFF_FIRST + reg_off);

		if (first != val) {
			if (first == mux_f_gpio)
				reg |= BIT(bit_off);
			else
				reg &= ~BIT(bit_off);
			writel(reg, spp_gchip->first_base + SPPCTL_GPIO_OFF_FIRST + reg_off);

			dev_dbg(chip->parent, "First: %08x\n", reg);
		}
	}

	/* MASTER register */
	if (master != mux_m_keep) {
		/* Refer to descriptions of function sppctl_master_get()
		 * for usage of MASTER registers.
		 */
		reg_off = (offset / 16) * 4;
		bit_off = offset % 16;

		reg = BIT(bit_off) << SPPCTL_MASTER_MASK_SHIFT;
		if (master == mux_m_gpio)
			reg |= BIT(bit_off);
		writel(reg, spp_gchip->gpioxt_base + SPPCTL_GPIO_OFF_MASTER + reg_off);

		dev_dbg(chip->parent, "Master: %08x (%p)\n", reg, spp_gchip->gpioxt_base +
			SPPCTL_GPIO_OFF_MASTER + reg_off);
	}
}

static void sppctl_gpio_input_inv_set(struct gpio_chip *chip, unsigned int offset)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off, bit_off, reg;

	/* Upper 16-bit word is mask. Lower 16-bit word is value.
	 * Refer to descriptions of function sppctl_master_get().
	 */
	reg_off = (offset / 16) * 4;
	bit_off = offset % 16;
	reg = BIT(bit_off + SPPCTL_GPIO_MASK_SHIFT) | BIT(bit_off);

	writel(reg, spp_gchip->gpioxt2_base + SPPCTL_GPIO_OFF_IINV + reg_off);
}

static void sppctl_gpio_output_inv_set(struct gpio_chip *chip, unsigned int offset)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off, bit_off, reg;

	/* Upper 16-bit word is mask. Lower 16-bit word is value.
	 * Refer to descriptions of function sppctl_master_get().
	 */
	reg_off = (offset / 16) * 4;
	bit_off = offset % 16;
	reg = BIT(bit_off + SPPCTL_GPIO_MASK_SHIFT) | BIT(bit_off);

	writel(reg, spp_gchip->gpioxt2_base + SPPCTL_GPIO_OFF_OINV + reg_off);
}

static int sppctl_gpio_output_od_get(struct gpio_chip *chip, unsigned int offset)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off, bit_off, reg;

	/* Upper 16-bit word is mask. Lower 16-bit word is value.
	 * Refer to descriptions of function sppctl_master_get().
	 */
	reg_off = (offset / 16) * 4;
	bit_off = offset % 16;
	reg = readl(spp_gchip->gpioxt2_base + SPPCTL_GPIO_OFF_OD + reg_off);

	return (reg & BIT(bit_off)) ? 1 : 0;
}

static void sppctl_gpio_output_od_set(struct gpio_chip *chip, unsigned int offset,
				      unsigned int val)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off, bit_off, reg;

	/* Upper 16-bit word is mask. Lower 16-bit word is value.
	 * Refer to descriptions of function sppctl_master_get().
	 */
	reg_off = (offset / 16) * 4;
	bit_off = offset % 16;
	reg = BIT(bit_off + SPPCTL_GPIO_MASK_SHIFT) | BIT(bit_off);

	writel(reg, spp_gchip->gpioxt2_base + SPPCTL_GPIO_OFF_OD + reg_off);
}

static int sppctl_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off, bit_off, reg;

	/* Upper 16-bit word is mask. Lower 16-bit word is value.
	 * Refer to descriptions of function sppctl_master_get().
	 */
	reg_off = (offset / 16) * 4;
	bit_off = offset % 16;
	reg = readl(spp_gchip->gpioxt_base + SPPCTL_GPIO_OFF_OE + reg_off);

	return (reg & BIT(bit_off)) ? 0 : 1;
}

static int sppctl_gpio_inv_get(struct gpio_chip *chip, unsigned int offset)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off, bit_off, reg;
	u16 inv_off;

	/* Upper 16-bit word is mask. Lower 16-bit word is value.
	 * Refer to descriptions of function sppctl_master_get().
	 */
	reg_off = (offset / 16) * 4;
	bit_off = offset % 16;

	inv_off = SPPCTL_GPIO_OFF_IINV;
	if (sppctl_gpio_get_direction(chip, offset) == 0)
		inv_off = SPPCTL_GPIO_OFF_OINV;
	reg = readl(spp_gchip->gpioxt2_base + inv_off + reg_off);

	return (reg & BIT(bit_off)) ? 1 : 0;
}

static int sppctl_gpio_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off, bit_off, reg;

	/* Upper 16-bit word is mask. Lower 16-bit word is value.
	 * Refer to descriptions of function sppctl_master_get().
	 */
	reg_off = (offset / 16) * 4;
	bit_off = offset % 16;
	reg = BIT(bit_off + SPPCTL_GPIO_MASK_SHIFT);

	writel(reg, spp_gchip->gpioxt_base + SPPCTL_GPIO_OFF_OE + reg_off);
	return 0;
}

static int sppctl_gpio_direction_output(struct gpio_chip *chip, unsigned int offset, int val)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off, bit_off, reg;

	/* Upper 16-bit word is mask. Lower 16-bit word is value.
	 * Refer to descriptions of function sppctl_master_get().
	 */
	reg_off = (offset / 16) * 4;
	bit_off = offset % 16;
	reg = BIT(bit_off + SPPCTL_GPIO_MASK_SHIFT) | BIT(bit_off);
	writel(reg, spp_gchip->gpioxt_base + SPPCTL_GPIO_OFF_OE + reg_off);

	if (val < 0)
		return 0;

	reg = BIT(bit_off + SPPCTL_GPIO_MASK_SHIFT);
	if (val)
		reg |= BIT(bit_off);

	writel(reg, spp_gchip->gpioxt_base + SPPCTL_GPIO_OFF_OUT + reg_off);
	return 0;
}

static int sppctl_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off, bit_off, reg;

	reg_off = (offset / 32) * 4;
	bit_off = offset % 32;
	reg = readl(spp_gchip->gpioxt_base + SPPCTL_GPIO_OFF_IN + reg_off);

	return (reg & BIT(bit_off)) ? 1 : 0;
}

static void sppctl_gpio_set(struct gpio_chip *chip, unsigned int offset, int val)
{
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u32 reg_off, bit_off, reg;

	/* Upper 16-bit word is mask. Lower 16-bit word is value.
	 * Refer to descriptions of function sppctl_master_get().
	 */
	reg_off = (offset / 16) * 4;
	bit_off = offset % 16;
	reg = BIT(bit_off + SPPCTL_GPIO_MASK_SHIFT);
	if (val)
		reg |= BIT(bit_off);

	writel(reg, spp_gchip->gpioxt_base + SPPCTL_GPIO_OFF_OUT + reg_off);
}

static int sppctl_gpio_set_config(struct gpio_chip *chip, unsigned int offset,
				  unsigned long config)
{
	enum pin_config_param param = pinconf_to_config_param(config);
	struct sppctl_gpio_chip *spp_gchip = gpiochip_get_data(chip);
	u16 arg = pinconf_to_config_argument(config);
	u32 reg_off, bit_off, reg;
	int ret = 0;

	dev_dbg(chip->parent, "%s(%03d, %lX) param: %d, arg: %d\n", __func__,
		offset, config, param, arg);

	switch (param) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		/* Upper 16-bit word is mask. Lower 16-bit word is value.
		 * Refer to descriptions of function sppctl_master_get().
		 */
		reg_off = (offset / 16) * 4;
		bit_off = offset % 16;
		reg = BIT(bit_off + SPPCTL_GPIO_MASK_SHIFT) | BIT(bit_off);

		writel(reg, spp_gchip->gpioxt2_base + SPPCTL_GPIO_OFF_OD + reg_off);
		break;

	case PIN_CONFIG_INPUT_ENABLE:
		dev_dbg(chip->parent, "%s(%03d, %lX) arg: %d\n", __func__,
			offset, config, arg);
		break;

	case PIN_CONFIG_OUTPUT:
		ret = sppctl_gpio_direction_output(chip, offset, 0);
		break;

	case PIN_CONFIG_PERSIST_STATE:
		dev_dbg(chip->parent, "%s(%03d, %lX) not support, param: %d\n", __func__,
			offset, config, param);
		ret = -ENOTSUPP;
		break;

	default:
		dev_dbg(chip->parent, "%s(%03d, %lX) unknown, param: %d\n", __func__,
			offset, config, param);
		ret = -EINVAL;
		break;
	}

	return ret;
}

#ifdef CONFIG_DEBUG_FS
static void sppctl_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	const char *label;
	int i;

	for (i = 0; i < chip->ngpio; i++) {
		label = gpiochip_is_requested(chip, i);
		if (!label)
			label = "";

		seq_printf(s, " gpio-%03d (%-16.16s | %-16.16s)", i + chip->base,
			   chip->names[i], label);
		seq_printf(s, " %c", sppctl_gpio_get_direction(chip, i) == 0 ? 'O' : 'I');
		seq_printf(s, ":%d", sppctl_gpio_get(chip, i));
		seq_printf(s, " %s", (sppctl_first_get(chip, i) ? "gpi" : "mux"));
		seq_printf(s, " %s", (sppctl_master_get(chip, i) ? "gpi" : "iop"));
		seq_printf(s, " %s", (sppctl_gpio_inv_get(chip, i) ? "inv" : "   "));
		seq_printf(s, " %s", (sppctl_gpio_output_od_get(chip, i) ? "oDr" : ""));
		seq_puts(s, "\n");
	}
}
#endif

static int sppctl_gpio_new(struct platform_device *pdev, struct sppctl_pdata *pctl)
{
	struct sppctl_gpio_chip *spp_gchip;
	struct gpio_chip *gchip;
	int err;

	if (!of_find_property(pdev->dev.of_node, "gpio-controller", NULL)) {
		dev_err_probe(&pdev->dev, -EINVAL, "Not a gpio-controller!\n");
		return -EINVAL;
	}

	spp_gchip = devm_kzalloc(&pdev->dev, sizeof(*spp_gchip), GFP_KERNEL);
	if (!spp_gchip)
		return -ENOMEM;
	pctl->spp_gchip = spp_gchip;

	spp_gchip->gpioxt_base  = pctl->gpioxt_base;
	spp_gchip->gpioxt2_base = pctl->gpioxt2_base;
	spp_gchip->first_base   = pctl->first_base;

	gchip =                    &spp_gchip->chip;
	gchip->label =             SPPCTL_MODULE_NAME;
	gchip->parent =            &pdev->dev;
	gchip->owner =             THIS_MODULE;
	gchip->request =           gpiochip_generic_request;
	gchip->free =              gpiochip_generic_free;
	gchip->get_direction =     sppctl_gpio_get_direction;
	gchip->direction_input =   sppctl_gpio_direction_input;
	gchip->direction_output =  sppctl_gpio_direction_output;
	gchip->get =               sppctl_gpio_get;
	gchip->set =               sppctl_gpio_set;
	gchip->set_config =        sppctl_gpio_set_config;
#ifdef CONFIG_DEBUG_FS
	gchip->dbg_show =          sppctl_gpio_dbg_show;
#endif
	gchip->base =              0; /* it is main platform GPIO controller */
	gchip->ngpio =             sppctl_gpio_list_sz;
	gchip->names =             sppctl_gpio_list_s;
	gchip->can_sleep =         0;
	gchip->of_node =           pdev->dev.of_node;
	gchip->of_gpio_n_cells =   2;

	pctl->pctl_grange.npins = gchip->ngpio;
	pctl->pctl_grange.base =  gchip->base;
	pctl->pctl_grange.name =  gchip->label;
	pctl->pctl_grange.gc =    gchip;

	err = devm_gpiochip_add_data(&pdev->dev, gchip, spp_gchip);
	if (err) {
		dev_err_probe(&pdev->dev, err, "Failed to add gpiochip!\n");
		return err;
	}

	return 0;
}

/* pinconf operations */
static int sppctl_pin_config_get(struct pinctrl_dev *pctldev, unsigned int pin,
				 unsigned long *config)
{
	struct sppctl_pdata *pctl = pinctrl_dev_get_drvdata(pctldev);
	unsigned int param = pinconf_to_config_param(*config);
	unsigned int arg = 0;

	dev_dbg(pctldev->dev, "%s(%d)\n", __func__, pin);

	switch (param) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		if (!sppctl_gpio_output_od_get(&pctl->spp_gchip->chip, pin))
			return -EINVAL;
		break;

	case PIN_CONFIG_OUTPUT:
		if (!sppctl_first_get(&pctl->spp_gchip->chip, pin))
			return -EINVAL;
		if (!sppctl_master_get(&pctl->spp_gchip->chip, pin))
			return -EINVAL;
		if (sppctl_gpio_get_direction(&pctl->spp_gchip->chip, pin) != 0)
			return -EINVAL;
		arg = sppctl_gpio_get(&pctl->spp_gchip->chip, pin);
		break;

	default:
		dev_dbg(pctldev->dev, "%s(%d) skipping, param: 0x%x\n",
			__func__, pin, param);
		return -EOPNOTSUPP;
	}
	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int sppctl_pin_config_set(struct pinctrl_dev *pctldev, unsigned int pin,
				 unsigned long *configs, unsigned int num_configs)
{
	struct sppctl_pdata *pctl = pinctrl_dev_get_drvdata(pctldev);
	int i = 0;

	dev_dbg(pctldev->dev, "%s(%d, %ld, %d)\n", __func__, pin, *configs, num_configs);

	/* Special handling for IOP */
	if (configs[i] == 0xFF) {
		sppctl_first_master_set(&pctl->spp_gchip->chip, pin, mux_f_gpio, mux_m_iop);
		return 0;
	}

	for (i = 0; i < num_configs; i++) {
		if (configs[i] & SPPCTL_PCTL_L_OUT) {
			dev_dbg(pctldev->dev, "%d: OUT\n", i);
			sppctl_gpio_direction_output(&pctl->spp_gchip->chip, pin, 0);
		}
		if (configs[i] & SPPCTL_PCTL_L_OU1) {
			dev_dbg(pctldev->dev, "%d: OU1\n", i);
			sppctl_gpio_direction_output(&pctl->spp_gchip->chip, pin, 1);
		}
		if (configs[i] & SPPCTL_PCTL_L_INV) {
			dev_dbg(pctldev->dev, "%d: INV\n", i);
			sppctl_gpio_input_inv_set(&pctl->spp_gchip->chip, pin);
		}
		if (configs[i] & SPPCTL_PCTL_L_ONV) {
			dev_dbg(pctldev->dev, "%d: ONV\n", i);
			sppctl_gpio_output_inv_set(&pctl->spp_gchip->chip, pin);
		}
		if (configs[i] & SPPCTL_PCTL_L_ODR) {
			dev_dbg(pctldev->dev, "%d: ODR\n", i);
			sppctl_gpio_output_od_set(&pctl->spp_gchip->chip, pin, 1);
		}
	}

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void sppctl_config_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
				   unsigned int offset)
{
	dev_dbg(pctldev->dev, "%s(%d)\n", __func__, offset);
	seq_printf(s, " %s", dev_name(pctldev->dev));
}
#endif

static const struct pinconf_ops sppctl_pconf_ops = {
	.is_generic                 = true,
	.pin_config_get             = sppctl_pin_config_get,
	.pin_config_set             = sppctl_pin_config_set,
#ifdef CONFIG_DEBUG_FS
	.pin_config_dbg_show        = sppctl_config_dbg_show,
#endif
};

/* pinmux operations */
static int sppctl_get_functions_count(struct pinctrl_dev *pctldev)
{
	return sppctl_list_funcs_sz;
}

static const char *sppctl_get_function_name(struct pinctrl_dev *pctldev,
					    unsigned int selector)
{
	return sppctl_list_funcs[selector].name;
}

static int sppctl_get_function_groups(struct pinctrl_dev *pctldev, unsigned int selector,
				      const char * const **groups, unsigned int *num_groups)
{
	struct sppctl_pdata *pctl = pinctrl_dev_get_drvdata(pctldev);
	const struct sppctl_func *f = &sppctl_list_funcs[selector];

	*num_groups = 0;
	switch (f->freg) {
	case f_off_i:
	case f_off_0:   /* gen GPIO/IOP: all groups = all pins */
		*num_groups = sppctl_gpio_list_sz;
		*groups = sppctl_gpio_list_s;
		break;

	case f_off_m:   /* pin-mux */
		*num_groups = sppctl_pmux_list_sz;
		*groups = sppctl_pmux_list_s;
		break;

	case f_off_g:   /* pin-group */
		if (!f->grps)
			break;
		*num_groups = f->gnum;
		*groups = &pctl->groups_name[selector * SPPCTL_MAX_GROUPS];
		break;

	default:
		dev_err(pctldev->dev, "%s(selector: %d) unknown fOFF %d\n", __func__,
			selector, f->freg);
		break;
	}

	dev_dbg(pctldev->dev, "%s(selector: %d) %d\n", __func__, selector, *num_groups);
	return 0;
}

static int sppctl_set_mux(struct pinctrl_dev *pctldev, unsigned int func_selector,
			  unsigned int group_selector)
{
	const struct sppctl_func *f = &sppctl_list_funcs[func_selector];
	struct sppctl_pdata *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct grp2fp_map g2fpm = pctl->g2fp_maps[group_selector];
	int i, j;

	dev_dbg(pctldev->dev, "%s(func: %d, grp: %d)\n", __func__, func_selector,
		group_selector);

	switch (f->freg) {
	case f_off_0:   /* Detach from full pin-mux pin */
		j = -1;
		for (i = 0; i < sppctl_list_funcs_sz; i++) {
			if (sppctl_list_funcs[i].freg != f_off_m)
				continue;
			j++; /* j starts at 0 because its initial value is -1. */
			if (sppctl_func_get(pctl, j) != group_selector)
				continue;
			sppctl_func_set(pctl, j, 0);
		}
		break;

	case f_off_m:   /* fully pin-mux */
		sppctl_first_master_set(&pctl->spp_gchip->chip, group_selector,
					mux_f_mux, mux_m_keep);
		sppctl_func_set(pctl, func_selector - SPPCTL_FULLY_PINMUX_TBL_START,
				(group_selector == 0) ?	group_selector :
				SPPCTL_FULLY_PINMUX_CONV(group_selector));
		break;

	case f_off_g:   /* group pin-mux*/
		for (i = 0; i < f->grps[g2fpm.g_idx].pnum; i++)
			sppctl_first_master_set(&pctl->spp_gchip->chip,
						f->grps[g2fpm.g_idx].pins[i],
						mux_f_mux, mux_m_keep);
		sppctl_gmx_set(pctl, f->roff, f->boff, f->blen, f->grps[g2fpm.g_idx].gval);
		break;

	case f_off_i:   /* IOP */
		sppctl_first_master_set(&pctl->spp_gchip->chip, group_selector,
					mux_f_gpio, mux_m_iop);
		break;

	default:
		dev_err(pctldev->dev, "%s(func_selector: %d) unknown f_off: %d\n",
			__func__, func_selector, f->freg);
		break;
	}

	return 0;
}

static int sppctl_gpio_request_enable(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range, unsigned int offset)
{
	struct sppctl_pdata *pctl = pinctrl_dev_get_drvdata(pctldev);
	int g_f, g_m;

	dev_dbg(pctldev->dev, "%s(%d)\n", __func__, offset);

	g_f = sppctl_first_get(&pctl->spp_gchip->chip, offset);
	g_m = sppctl_master_get(&pctl->spp_gchip->chip, offset);
	if (g_f == mux_f_gpio && g_m == mux_m_gpio)
		return 0;

	pin_desc_get(pctldev, offset);

	sppctl_first_master_set(&pctl->spp_gchip->chip, offset, mux_f_gpio, mux_m_gpio);
	return 0;
}

static const struct pinmux_ops sppctl_pinmux_ops = {
	.get_functions_count = sppctl_get_functions_count,
	.get_function_name   = sppctl_get_function_name,
	.get_function_groups = sppctl_get_function_groups,
	.set_mux             = sppctl_set_mux,
	.gpio_request_enable = sppctl_gpio_request_enable,
	.strict              = true
};

/* pinctrl operations */
static int sppctl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct sppctl_pdata *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->unq_grps_sz;
}

static const char *sppctl_get_group_name(struct pinctrl_dev *pctldev, unsigned int selector)
{
	struct sppctl_pdata *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->unq_grps[selector];
}

static int sppctl_get_group_pins(struct pinctrl_dev *pctldev, unsigned int selector,
				 const unsigned int **pins, unsigned int *num_pins)
{
	struct sppctl_pdata *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct grp2fp_map g2fpm = pctl->g2fp_maps[selector];
	const struct sppctl_func *f;

	f = &sppctl_list_funcs[g2fpm.f_idx];
	dev_dbg(pctldev->dev, "%s(%d), f_idx: %d, g_idx: %d, freg: %d\n",
		__func__, selector, g2fpm.f_idx, g2fpm.g_idx, f->freg);

	*num_pins = 0;

	/* MUX | GPIO | IOP: 1 pin -> 1 group */
	if (f->freg != f_off_g) {
		*num_pins = 1;
		*pins = &sppctl_pins_gpio[selector];
		return 0;
	}

	/* IOP (several pins at once in a group) */
	if (!f->grps)
		return 0;
	if (f->gnum < 1)
		return 0;

	*num_pins = f->grps[g2fpm.g_idx].pnum;
	*pins = f->grps[g2fpm.g_idx].pins;

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void sppctl_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
				unsigned int offset)
{
	struct sppctl_pdata *pctl = pinctrl_dev_get_drvdata(pctldev);
	const char *tmpp;
	u8 g_f, g_m;

	seq_printf(s, "%s", dev_name(pctldev->dev));
	g_f = sppctl_first_get(&pctl->spp_gchip->chip, offset);
	g_m = sppctl_master_get(&pctl->spp_gchip->chip, offset);

	tmpp = "?";
	if (g_f &&  g_m)
		tmpp = "GPIO";
	if (g_f && !g_m)
		tmpp = " IOP";
	if (!g_f)
		tmpp = " MUX";
	seq_printf(s, " %s", tmpp);
}
#endif

static int sppctl_dt_node_to_map(struct pinctrl_dev *pctldev, struct device_node *np_config,
				 struct pinctrl_map **map, unsigned int *num_maps)
{
	struct sppctl_pdata *pctl = pinctrl_dev_get_drvdata(pctldev);
	int nmG = of_property_count_strings(np_config, "groups");
	const struct sppctl_func *f = NULL;
	struct device_node *parent;
	unsigned long *configs;
	struct property *prop;
	const char *s_f, *s_g;
	u8 p_p, p_g, p_f, p_l;
	const __be32 *list;
	u32 dt_pin, dt_fun;
	int i, size = 0;

	list = of_get_property(np_config, "sunplus,pins", &size);

	if (nmG <= 0)
		nmG = 0;

	parent = of_get_parent(np_config);
	*num_maps = size / sizeof(*list);

	/* Check if out of range or invalid? */
	for (i = 0; i < (*num_maps); i++) {
		dt_pin = be32_to_cpu(list[i]);
		p_p = SPPCTL_PCTLD_P(dt_pin);
		p_g = SPPCTL_PCTLD_G(dt_pin);

		if (p_p >= sppctl_pins_all_sz) {
			dev_dbg(pctldev->dev, "Invalid pin property at index %d (0x%08x)\n",
				i, dt_pin);
			return -EINVAL;
		}
	}

	*map = kcalloc(*num_maps + nmG, sizeof(**map), GFP_KERNEL);
	for (i = 0; i < (*num_maps); i++) {
		dt_pin = be32_to_cpu(list[i]);
		p_p = SPPCTL_PCTLD_P(dt_pin);
		p_g = SPPCTL_PCTLD_G(dt_pin);
		p_f = SPPCTL_PCTLD_F(dt_pin);
		p_l = SPPCTL_PCTLD_L(dt_pin);
		(*map)[i].name = parent->name;
		dev_dbg(pctldev->dev, "map [%d]=%08x, p=%d, g=%d, f=%d, l=%d\n",
			i, dt_pin, p_p, p_g, p_f, p_l);

		if (p_g == SPPCTL_PCTL_G_GPIO) {
			(*map)[i].type = PIN_MAP_TYPE_CONFIGS_PIN;
			(*map)[i].data.configs.num_configs = 1;
			(*map)[i].data.configs.group_or_pin = pin_get_name(pctldev, p_p);
			configs = kcalloc(1, sizeof(*configs), GFP_KERNEL);
			*configs = p_l;
			(*map)[i].data.configs.configs = configs;

			dev_dbg(pctldev->dev, "%s(%d) = 0x%x\n",
				(*map)[i].data.configs.group_or_pin, p_p, p_l);
		} else if (p_g == SPPCTL_PCTL_G_IOPP) {
			(*map)[i].type = PIN_MAP_TYPE_CONFIGS_PIN;
			(*map)[i].data.configs.num_configs = 1;
			(*map)[i].data.configs.group_or_pin = pin_get_name(pctldev, p_p);
			configs = kcalloc(1, sizeof(*configs), GFP_KERNEL);
			*configs = 0xFF;
			(*map)[i].data.configs.configs = configs;

			dev_dbg(pctldev->dev, "%s(%d) = 0x%x\n",
				(*map)[i].data.configs.group_or_pin, p_p, p_l);
		} else {
			(*map)[i].type = PIN_MAP_TYPE_MUX_GROUP;
			(*map)[i].data.mux.function = sppctl_list_funcs[p_f].name;
			(*map)[i].data.mux.group = pin_get_name(pctldev, p_p);

			dev_dbg(pctldev->dev, "f->p: %s(%d)->%s(%d)\n",
				(*map)[i].data.mux.function, p_f,
				(*map)[i].data.mux.group, p_p);
		}
	}

	/* Handle pin-group function. */
	if (nmG > 0 && of_property_read_string(np_config, "function", &s_f) == 0) {
		dev_dbg(pctldev->dev, "found func: %s\n", s_f);
		of_property_for_each_string(np_config, "groups", prop, s_g) {
			dev_dbg(pctldev->dev, " %s: %s\n", s_f, s_g);
			(*map)[*num_maps].type = PIN_MAP_TYPE_MUX_GROUP;
			(*map)[*num_maps].data.mux.function = s_f;
			(*map)[*num_maps].data.mux.group = s_g;
			dev_dbg(pctldev->dev, "f->g: %s->%s\n",
				(*map)[*num_maps].data.mux.function,
				(*map)[*num_maps].data.mux.group);
			(*num_maps)++;
		}
	}

	/* Handle zero function. */
	list = of_get_property(np_config, "sunplus,zero_func", &size);
	if (list) {
		for (i = 0; i < (size / sizeof(*list)); i++) {
			dt_fun = be32_to_cpu(list[i]);
			if (dt_fun >= sppctl_list_funcs_sz) {
				dev_err(pctldev->dev, "Zero-func %d out of range!\n",
					dt_fun);
				continue;
			}

			f = &sppctl_list_funcs[dt_fun];
			switch (f->freg) {
			case f_off_m:
				dev_dbg(pctldev->dev, "Zero-func: %d (%s)\n",
					dt_fun, f->name);
				sppctl_func_set(pctl, dt_fun - 2, 0);
				break;

			case f_off_g:
				dev_dbg(pctldev->dev, "zero-group: %d (%s)\n",
					dt_fun, f->name);
				sppctl_gmx_set(pctl, f->roff, f->boff, f->blen, 0);
				break;

			default:
				dev_err(pctldev->dev, "Wrong zero-group: %d (%s)\n",
					dt_fun, f->name);
				break;
			}
		}
	}

	of_node_put(parent);
	dev_dbg(pctldev->dev, "%d pins mapped\n", *num_maps);
	return 0;
}

static void sppctl_dt_free_map(struct pinctrl_dev *pctldev, struct pinctrl_map *map,
			       unsigned int num_maps)
{
	dev_dbg(pctldev->dev, "%s(%d)\n", __func__, num_maps);
	pinctrl_utils_free_map(pctldev, map, num_maps);
}

static const struct pinctrl_ops sppctl_pctl_ops = {
	.get_groups_count = sppctl_get_groups_count,
	.get_group_name   = sppctl_get_group_name,
	.get_group_pins   = sppctl_get_group_pins,
#ifdef CONFIG_DEBUG_FS
	.pin_dbg_show     = sppctl_pin_dbg_show,
#endif
	.dt_node_to_map   = sppctl_dt_node_to_map,
	.dt_free_map      = sppctl_dt_free_map,
};

/* platform driver functions */
static int sppctl_group_groups(struct platform_device *pdev)
{
	struct sppctl_pdata *sppctl = pdev->dev.platform_data;
	const char *name;
	int i, k, j;

	/* Fill array of all groups. */
	sppctl->unq_grps = NULL;
	sppctl->unq_grps_sz = sppctl_gpio_list_sz;

	/* Calculate unique group names array size. */
	for (i = 0; i < sppctl_list_funcs_sz; i++)
		if (sppctl_list_funcs[i].freg == f_off_g)
			sppctl->unq_grps_sz += sppctl_list_funcs[i].gnum;

	/* Fill up unique group names array. */
	sppctl->unq_grps = devm_kzalloc(&pdev->dev, (sppctl->unq_grps_sz + 1) *
					sizeof(char *), GFP_KERNEL);
	if (!sppctl->unq_grps)
		return -ENOMEM;

	sppctl->g2fp_maps = devm_kzalloc(&pdev->dev, (sppctl->unq_grps_sz + 1) *
					 sizeof(struct grp2fp_map), GFP_KERNEL);
	if (!sppctl->g2fp_maps)
		return -ENOMEM;

	sppctl->groups_name = devm_kzalloc(&pdev->dev, sppctl_list_funcs_sz *
					   SPPCTL_MAX_GROUPS * sizeof(char *), GFP_KERNEL);
	if (!sppctl->groups_name)
		return -ENOMEM;

	/* gpio */
	for (i = 0; i < sppctl_gpio_list_sz; i++) {
		sppctl->unq_grps[i] = sppctl_gpio_list_s[i];
		sppctl->g2fp_maps[i].f_idx = 0;
		sppctl->g2fp_maps[i].g_idx = i;
	}

	/* groups */
	j = sppctl_gpio_list_sz;
	for (i = 0; i < sppctl_list_funcs_sz; i++) {
		if (sppctl_list_funcs[i].freg != f_off_g)
			continue;

		for (k = 0; k < sppctl_list_funcs[i].gnum; k++) {
			name = sppctl_list_funcs[i].grps[k].name;
			sppctl->groups_name[i * SPPCTL_MAX_GROUPS + k] = name;
			sppctl->unq_grps[j] = name;
			sppctl->g2fp_maps[j].f_idx = i;
			sppctl->g2fp_maps[j].g_idx = k;
			j++;
		}
	}

	dev_dbg(&pdev->dev, "funcs: %zd unq_grps: %zd\n", sppctl_list_funcs_sz,
		sppctl->unq_grps_sz);
	return 0;
}

static int sppctl_pinctrl_init(struct platform_device *pdev)
{
	struct device_node *np = of_node_get(pdev->dev.of_node);
	struct sppctl_pdata *sppctl = pdev->dev.platform_data;
	int err;

	/* Initialize pctl_desc */
	sppctl->pctl_desc.owner   = THIS_MODULE;
	sppctl->pctl_desc.name    = dev_name(&pdev->dev);
	sppctl->pctl_desc.pins    = &sppctl_pins_all[0];
	sppctl->pctl_desc.npins   = sppctl_pins_all_sz;
	sppctl->pctl_desc.pctlops = &sppctl_pctl_ops;
	sppctl->pctl_desc.confops = &sppctl_pconf_ops;
	sppctl->pctl_desc.pmxops  = &sppctl_pinmux_ops;

	err = sppctl_group_groups(pdev);
	if (err) {
		of_node_put(np);
		return err;
	}

	err = devm_pinctrl_register_and_init(&pdev->dev, &sppctl->pctl_desc,
					     sppctl, &sppctl->pctl_dev);
	if (err) {
		dev_err_probe(&pdev->dev, err, "Failed to register pinctrl!\n");
		of_node_put(np);
		return err;
	}

	pinctrl_enable(sppctl->pctl_dev);
	return 0;
}

static int sppctl_resource_map(struct platform_device *pdev, struct sppctl_pdata *sppctl)
{
	struct resource *rp;
	int ret;

	/* MOON2 registers */
	rp = platform_get_resource_byname(pdev, IORESOURCE_MEM, "moon2");
	sppctl->moon2_base = devm_ioremap_resource(&pdev->dev, rp);
	if (IS_ERR(sppctl->moon2_base)) {
		ret = PTR_ERR(sppctl->moon2_base);
		goto ioremap_failed;
	}
	dev_dbg(&pdev->dev, "MOON2:   %pr\n", rp);

	/* GPIOXT registers */
	rp = platform_get_resource_byname(pdev, IORESOURCE_MEM, "gpioxt");
	sppctl->gpioxt_base = devm_ioremap_resource(&pdev->dev, rp);
	if (IS_ERR(sppctl->gpioxt_base)) {
		ret = PTR_ERR(sppctl->gpioxt_base);
		goto ioremap_failed;
	}
	dev_dbg(&pdev->dev, "GPIOXT:  %pr\n", rp);

	/* GPIOXT 2 registers */
	rp = platform_get_resource_byname(pdev, IORESOURCE_MEM, "gpioxt2");
	sppctl->gpioxt2_base = devm_ioremap_resource(&pdev->dev, rp);
	if (IS_ERR(sppctl->gpioxt2_base)) {
		ret = PTR_ERR(sppctl->gpioxt2_base);
		goto ioremap_failed;
	}
	dev_dbg(&pdev->dev, "GPIOXT2: %pr\n", rp);

	/* FIRST registers */
	rp = platform_get_resource_byname(pdev, IORESOURCE_MEM, "first");
	sppctl->first_base = devm_ioremap_resource(&pdev->dev, rp);
	if (IS_ERR(sppctl->first_base)) {
		ret = PTR_ERR(sppctl->first_base);
		goto ioremap_failed;
	}
	dev_dbg(&pdev->dev, "FIRST:   %pr\n", rp);

	/* MOON1 registers */
	rp = platform_get_resource_byname(pdev, IORESOURCE_MEM, "moon1");
	sppctl->moon1_base = devm_ioremap_resource(&pdev->dev, rp);
	if (IS_ERR(sppctl->moon1_base)) {
		ret = PTR_ERR(sppctl->moon1_base);
		goto ioremap_failed;
	}
	dev_dbg(&pdev->dev, "MOON1:   %pr\n", rp);

	return 0;

ioremap_failed:
	dev_err_probe(&pdev->dev, ret, "ioremap failed!\n");
	return ret;
}

static int sppctl_probe(struct platform_device *pdev)
{
	struct sppctl_pdata *sppctl;
	int ret;

	sppctl = devm_kzalloc(&pdev->dev, sizeof(*sppctl), GFP_KERNEL);
	if (!sppctl)
		return -ENOMEM;
	pdev->dev.platform_data = sppctl;

	ret = sppctl_resource_map(pdev, sppctl);
	if (ret)
		return ret;

	ret = sppctl_gpio_new(pdev, sppctl);
	if (ret)
		return ret;

	ret = sppctl_pinctrl_init(pdev);
	if (ret)
		return ret;

	pinctrl_add_gpio_range(sppctl->pctl_dev, &sppctl->pctl_grange);
	dev_info(&pdev->dev, "SP7021 PinCtrl by Sunplus/Tibbo Tech. (c)");

	return 0;
}

static const struct of_device_id sppctl_match_table[] = {
	{ .compatible = "sunplus,sp7021-pctl" },
	{ /* zero */ }
};

static struct platform_driver sppctl_pinctrl_driver = {
	.driver = {
		.name           = SPPCTL_MODULE_NAME,
		.owner          = THIS_MODULE,
		.of_match_table = sppctl_match_table,
	},
	.probe  = sppctl_probe,
};
builtin_platform_driver(sppctl_pinctrl_driver)

MODULE_AUTHOR("Dvorkin Dmitry <dvorkin@tibbo.com>");
MODULE_AUTHOR("Wells Lu <wellslutw@gmail.com>");
MODULE_DESCRIPTION("Sunplus SP7021 Pin Control and GPIO driver");
MODULE_LICENSE("GPL v2");
