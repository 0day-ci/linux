// SPDX-License-Identifier: GPL-2.0-only
/*
 * regmap based generic GPIO driver
 *
 * Copyright 2020 Michael Walle <michael@walle.cc>
 */

#include <linux/gpio/driver.h>
#include <linux/gpio/regmap.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>

struct gpio_regmap {
	struct gpio_regmap_config config;
	struct gpio_regmap_ops *ops;
	struct gpio_chip gpio_chip;

	int (*reg_mask_xlate)(const struct gpio_regmap_config *config,
			      unsigned int base, unsigned int offset,
			      unsigned int *reg, unsigned int *mask);
};

static unsigned int gpio_regmap_addr(unsigned int addr)
{
	if (addr == GPIO_REGMAP_ADDR_ZERO)
		return 0;

	return addr;
}

static int regmap_gpio_init_valid_mask(struct gpio_chip *gc,
					unsigned long *valid_mask,
					unsigned int ngpios)
{
	struct gpio_regmap *gpio;

	gpio = gpiochip_get_data(gc);

	return gpio->ops->init_valid_mask(&gpio->config, valid_mask, ngpios);
}

static int gpio_regmap_set_config(struct gpio_chip *gc, unsigned int offset,
				  unsigned long config)
{
	struct gpio_regmap *gpio;

	gpio = gpiochip_get_data(gc);

	return gpio->ops->set_config(&gpio->config, offset, config);
}

static int gpio_regmap_simple_xlate(const struct gpio_regmap_config *config,
				    unsigned int base, unsigned int offset,
				    unsigned int *reg, unsigned int *mask)
{
	unsigned int line = offset % config->ngpio_per_reg;
	unsigned int stride = offset / config->ngpio_per_reg;

	*reg = base + stride * config->reg_stride;
	*mask = BIT(line);

	return 0;
}

static int gpio_regmap_get(struct gpio_chip *chip, unsigned int offset)
{
	struct gpio_regmap *gpio = gpiochip_get_data(chip);
	struct gpio_regmap_config *config = &gpio->config;
	unsigned int base, val, reg, mask;
	int ret;

	/* we might not have an output register if we are input only */
	if (config->reg_dat_base)
		base = gpio_regmap_addr(config->reg_dat_base);
	else
		base = gpio_regmap_addr(config->reg_set_base);

	ret = gpio->reg_mask_xlate(config, base, offset, &reg, &mask);
	if (ret)
		return ret;

	ret = regmap_read(config->regmap, reg, &val);
	if (ret)
		return ret;

	return !!(val & mask);
}

static void gpio_regmap_set(struct gpio_chip *chip, unsigned int offset,
			    int val)
{
	struct gpio_regmap *gpio = gpiochip_get_data(chip);
	struct gpio_regmap_config *config = &gpio->config;
	unsigned int base = gpio_regmap_addr(config->reg_set_base);
	unsigned int reg, mask;

	gpio->reg_mask_xlate(config, base, offset, &reg, &mask);
	if (val)
		regmap_update_bits(config->regmap, reg, mask, mask);
	else
		regmap_update_bits(config->regmap, reg, mask, 0);
}

static void gpio_regmap_set_with_clear(struct gpio_chip *chip,
				       unsigned int offset, int val)
{
	struct gpio_regmap *gpio = gpiochip_get_data(chip);
	struct gpio_regmap_config *config = &gpio->config;
	unsigned int base, reg, mask;

	if (val)
		base = gpio_regmap_addr(config->reg_set_base);
	else
		base = gpio_regmap_addr(config->reg_clr_base);

	gpio->reg_mask_xlate(config, base, offset, &reg, &mask);
	regmap_write(config->regmap, reg, mask);
}

static int gpio_regmap_get_direction(struct gpio_chip *chip,
				     unsigned int offset)
{
	struct gpio_regmap *gpio = gpiochip_get_data(chip);
	struct gpio_regmap_config *config = &gpio->config;
	unsigned int base, val, reg, mask;
	int invert, ret;

	if (config->reg_dir_out_base) {
		base = gpio_regmap_addr(config->reg_dir_out_base);
		invert = 0;
	} else if (config->reg_dir_in_base) {
		base = gpio_regmap_addr(config->reg_dir_in_base);
		invert = 1;
	} else {
		return -EOPNOTSUPP;
	}

	ret = gpio->reg_mask_xlate(config, base, offset, &reg, &mask);
	if (ret)
		return ret;

	ret = regmap_read(config->regmap, reg, &val);
	if (ret)
		return ret;

	if (!!(val & mask) ^ invert)
		return GPIO_LINE_DIRECTION_OUT;
	else
		return GPIO_LINE_DIRECTION_IN;
}

static int gpio_regmap_set_direction(struct gpio_chip *chip,
				     unsigned int offset, bool output)
{
	struct gpio_regmap *gpio = gpiochip_get_data(chip);
	struct gpio_regmap_config *config = &gpio->config;
	unsigned int base, val, reg, mask;
	int invert, ret;

	if (config->reg_dir_out_base) {
		base = gpio_regmap_addr(config->reg_dir_out_base);
		invert = 0;
	} else if (config->reg_dir_in_base) {
		base = gpio_regmap_addr(config->reg_dir_in_base);
		invert = 1;
	} else {
		return -EOPNOTSUPP;
	}

	ret = gpio->reg_mask_xlate(config, base, offset, &reg, &mask);
	if (ret)
		return ret;

	if (invert)
		val = output ? 0 : mask;
	else
		val = output ? mask : 0;

	return regmap_update_bits(config->regmap, reg, mask, val);
}

static int gpio_regmap_direction_input(struct gpio_chip *chip,
				       unsigned int offset)
{
	return gpio_regmap_set_direction(chip, offset, false);
}

static int gpio_regmap_direction_output(struct gpio_chip *chip,
					unsigned int offset, int value)
{
	gpio_regmap_set(chip, offset, value);

	return gpio_regmap_set_direction(chip, offset, true);
}

/**
 * gpio_regmap_register() - Register a generic regmap GPIO controller
 * @config:	configuration for gpio_regmap
 * @ops:	Provide pointer IC specific functions to handle needs where
 *		the standard gpio_regmap does not provide generic functions
 *		or provided functions do not fit the IC. Can be set NULL if
 *		no IC specific operations are required.
 *
 * Return: A pointer to the registered gpio_regmap or ERR_PTR error value.
 */
struct gpio_regmap *gpio_regmap_register(const struct gpio_regmap_config *config,
					 const struct gpio_regmap_ops *ops)
{
	struct gpio_regmap *gpio;
	struct gpio_chip *chip;
	int ret;

	if (!config->parent)
		return ERR_PTR(-EINVAL);

	if (!config->ngpio)
		return ERR_PTR(-EINVAL);

	/* we need at least one */
	if (!config->reg_dat_base && !config->reg_set_base)
		return ERR_PTR(-EINVAL);

	/* if we have a direction register we need both input and output */
	if ((config->reg_dir_out_base || config->reg_dir_in_base) &&
	    (!config->reg_dat_base || !config->reg_set_base))
		return ERR_PTR(-EINVAL);

	/* we don't support having both registers simultaneously for now */
	if (config->reg_dir_out_base && config->reg_dir_in_base)
		return ERR_PTR(-EINVAL);

	gpio = kzalloc(sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return ERR_PTR(-ENOMEM);

	gpio->config = *config;
	if (ops) {
		/*
		 * We could avoid ops struct allocation if just the
		 * xlate is given as it is used directly from gpio_regmap.
		 * I don't think that optimization is worth the hassle as
		 * there may not be many cases with custom xlate and no other
		 * ops. We can change this if I am wrong.
		 */
		gpio->ops = kzalloc(sizeof(*ops), GFP_KERNEL);
		if (!gpio->ops) {
			ret = -ENOMEM;
			goto err_free_gpio;
		}
		*gpio->ops = *ops;
	}

	/* if not set, assume there is only one register */
	if (!gpio->config.ngpio_per_reg)
		gpio->config.ngpio_per_reg = config->ngpio;

	/* if not set, assume they are consecutive */
	if (!gpio->config.reg_stride)
		gpio->config.reg_stride = 1;

	/*
	 * Dublicate the reg_mask_xlate to gpio_regmap so we don't need to
	 * always check if ops is populated and reg_mask_xlate is given
	 * - or allocate whole ops struct just for unconditional
	 * reg_mask_xlate if no ops are required.
	 */
	if (ops && ops->reg_mask_xlate)
		gpio->reg_mask_xlate = ops->reg_mask_xlate;
	else
		gpio->reg_mask_xlate = gpio_regmap_simple_xlate;

	chip = &gpio->gpio_chip;
	chip->parent = config->parent;
	chip->base = -1;
	chip->ngpio = config->ngpio;
	chip->names = config->names;
	chip->label = config->label ?: dev_name(config->parent);
	if (gpio->ops) {
		if (gpio->ops->set_config)
			chip->set_config = gpio_regmap_set_config;
		if (gpio->ops->init_valid_mask)
			chip->init_valid_mask = regmap_gpio_init_valid_mask;
	}
#if defined(CONFIG_OF_GPIO)
	/* gpiolib will use of_node of the parent if chip->of_node is NULL */
	chip->of_node = to_of_node(config->fwnode);
#endif /* CONFIG_OF_GPIO */

	/*
	 * If our regmap is fast_io we should probably set can_sleep to false.
	 * Right now, the regmap doesn't save this property, nor is there any
	 * access function for it.
	 * The only regmap type which uses fast_io is regmap-mmio. For now,
	 * assume a safe default of true here.
	 */
	chip->can_sleep = true;

	chip->get = gpio_regmap_get;
	if (gpio->config.reg_set_base && gpio->config.reg_clr_base)
		chip->set = gpio_regmap_set_with_clear;
	else if (gpio->config.reg_set_base)
		chip->set = gpio_regmap_set;

	if (gpio->config.reg_dir_in_base || gpio->config.reg_dir_out_base) {
		chip->get_direction = gpio_regmap_get_direction;
		chip->direction_input = gpio_regmap_direction_input;
		chip->direction_output = gpio_regmap_direction_output;
	}

	ret = gpiochip_add_data(chip, gpio);
	if (ret < 0)
		goto err_free_gpio;

	if (config->irq_domain) {
		ret = gpiochip_irqchip_add_domain(chip, config->irq_domain);
		if (ret)
			goto err_remove_gpiochip;
	}

	return gpio;

err_remove_gpiochip:
	gpiochip_remove(chip);
err_free_gpio:
	kfree(gpio->ops);
	kfree(gpio);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(gpio_regmap_register);

/**
 * gpio_regmap_unregister() - Unregister a generic regmap GPIO controller
 * @gpio: gpio_regmap device to unregister
 */
void gpio_regmap_unregister(struct gpio_regmap *gpio)
{
	gpiochip_remove(&gpio->gpio_chip);
	kfree(gpio->ops);
	kfree(gpio);
}
EXPORT_SYMBOL_GPL(gpio_regmap_unregister);

static void devm_gpio_regmap_unregister(void *res)
{
	gpio_regmap_unregister(res);
}

/**
 * devm_gpio_regmap_register() - resource managed gpio_regmap_register()
 * @dev: device that is registering this GPIO device
 * @config: configuration for gpio_regmap
 *
 * Managed gpio_regmap_register(). For generic regmap GPIO device registered by
 * this function, gpio_regmap_unregister() is automatically called on driver
 * detach. See gpio_regmap_register() for more information.
 *
 * Return: A pointer to the registered gpio_regmap or ERR_PTR error value.
 */
struct gpio_regmap *devm_gpio_regmap_register(struct device *dev,
					      const struct gpio_regmap_config *config,
					      const struct gpio_regmap_ops *ops)
{
	struct gpio_regmap *gpio;
	int ret;

	gpio = gpio_regmap_register(config, ops);

	if (IS_ERR(gpio))
		return gpio;

	ret = devm_add_action_or_reset(dev, devm_gpio_regmap_unregister, gpio);
	if (ret)
		return ERR_PTR(ret);

	return gpio;
}
EXPORT_SYMBOL_GPL(devm_gpio_regmap_register);

MODULE_AUTHOR("Michael Walle <michael@walle.cc>");
MODULE_DESCRIPTION("GPIO generic regmap driver core");
MODULE_LICENSE("GPL");
