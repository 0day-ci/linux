// SPDX-License-Identifier: GPL-2.0-or-later

#include "loongson_i2c.h"
#include "loongson_drv.h"
#include "linux/gpio.h"
#include <linux/gpio/consumer.h>

static struct gpio i2c_gpios[4] = {
	{ .gpio = DC_GPIO_0, .flags = GPIOF_OPEN_DRAIN, .label = "i2c-6-sda" },
	{ .gpio = DC_GPIO_1, .flags = GPIOF_OPEN_DRAIN, .label = "i2c-6-scl" },
	{ .gpio = DC_GPIO_2, .flags = GPIOF_OPEN_DRAIN, .label = "i2c-7-sda" },
	{ .gpio = DC_GPIO_3, .flags = GPIOF_OPEN_DRAIN, .label = "i2c-7-scl" },
};

static inline void __dc_gpio_set_dir(struct loongson_device *ldev,
				     unsigned int pin, int input)
{
	u32 temp;

	temp = ls7a_mm_rreg(ldev, LS7A_DC_GPIO_CFG_OFFSET);

	if (input)
		temp |= 1UL << pin;
	else
		temp &= ~(1UL << pin);

	ls7a_mm_wreg(ldev, LS7A_DC_GPIO_CFG_OFFSET, temp);
}

static void __dc_gpio_set_val(struct loongson_device *ldev,
			      unsigned int pin, int high)
{
	u32 temp;

	temp = ls7a_mm_rreg(ldev, LS7A_DC_GPIO_OUT_OFFSET);

	if (high)
		temp |= 1UL << pin;
	else
		temp &= ~(1UL << pin);

	ls7a_mm_wreg(ldev, LS7A_DC_GPIO_OUT_OFFSET, temp);
}

static int ls_dc_gpio_request(struct gpio_chip *chip, unsigned int pin)
{
	if (pin >= (chip->ngpio + chip->base))
		return -EINVAL;
	return 0;

}

static int ls_dc_gpio_dir_input(struct gpio_chip *chip, unsigned int pin)
{
	struct loongson_device *ldev =
			container_of(chip, struct loongson_device, chip);

	__dc_gpio_set_dir(ldev, pin, 1);

	return 0;
}

static int ls_dc_gpio_dir_output(struct gpio_chip *chip,
				 unsigned int pin, int value)
{
	struct loongson_device *ldev =
			container_of(chip, struct loongson_device, chip);

	__dc_gpio_set_val(ldev, pin, value);
	__dc_gpio_set_dir(ldev, pin, 0);

	return 0;
}

static void ls_dc_gpio_set(struct gpio_chip *chip, unsigned int pin, int value)
{
	struct loongson_device *ldev =
			container_of(chip, struct loongson_device, chip);

	__dc_gpio_set_val(ldev, pin, value);
}

static int ls_dc_gpio_get(struct gpio_chip *chip, unsigned int pin)
{
	struct loongson_device *ldev =
			container_of(chip, struct loongson_device, chip);
	u32 val = ls7a_mm_rreg(ldev, LS7A_DC_GPIO_IN_OFFSET);

	return (val >> pin) & 1;
}

static void loongson_i2c_set_data(void *i2c, int value)
{
	struct loongson_i2c *li2c = i2c;
	struct gpio_desc *gpiod = gpio_to_desc(i2c_gpios[li2c->data].gpio);

	gpiod_set_value_cansleep(gpiod, value);
}

static void loongson_i2c_set_clock(void *i2c, int value)
{
	struct loongson_i2c *li2c = i2c;
	struct gpio_desc *gpiod = gpio_to_desc(i2c_gpios[li2c->clock].gpio);

	gpiod_set_value_cansleep(gpiod, value);
}

static int loongson_i2c_get_data(void *i2c)
{
	struct loongson_i2c *li2c = i2c;
	struct gpio_desc *gpiod = gpio_to_desc(i2c_gpios[li2c->data].gpio);

	return gpiod_get_value_cansleep(gpiod);
}

static int loongson_i2c_get_clock(void *i2c)
{
	struct loongson_i2c *li2c = i2c;
	struct gpio_desc *gpiod = gpio_to_desc(i2c_gpios[li2c->clock].gpio);

	return gpiod_get_value_cansleep(gpiod);
}

static int loongson_i2c_create(struct loongson_device *ldev,
			       struct loongson_i2c *li2c, const char *name)
{
	int ret;
	unsigned int i2c_num;
	struct i2c_client *i2c_cli;
	struct i2c_adapter *i2c_adapter;
	struct i2c_algo_bit_data *i2c_algo_data;
	const struct i2c_board_info i2c_info = {
		.type = "ddc-dev",
		.addr = DDC_ADDR,
		.flags = I2C_CLASS_DDC,
	};

	i2c_num = li2c->i2c_id;
	i2c_adapter = kzalloc(sizeof(struct i2c_adapter), GFP_KERNEL);
	if (!i2c_adapter)
		return -ENOMEM;

	i2c_algo_data = kzalloc(sizeof(struct i2c_algo_bit_data), GFP_KERNEL);
	if (!i2c_algo_data) {
		ret = -ENOMEM;
		goto free_adapter;
	}

	i2c_adapter->owner = THIS_MODULE;
	i2c_adapter->class = I2C_CLASS_DDC;
	i2c_adapter->algo_data = i2c_algo_data;
	i2c_adapter->dev.parent = ldev->dev->dev;
	i2c_adapter->nr = -1;
	snprintf(i2c_adapter->name, sizeof(i2c_adapter->name), "%s%d",
		 name, i2c_num);

	li2c->data = i2c_num * 2;
	li2c->clock = i2c_num * 2 + 1;
	DRM_INFO("Created i2c-%d, sda=%d, scl=%d\n",
		 i2c_num, li2c->data, li2c->clock);

	i2c_algo_data->setsda = loongson_i2c_set_data;
	i2c_algo_data->setscl = loongson_i2c_set_clock;
	i2c_algo_data->getsda = loongson_i2c_get_data;
	i2c_algo_data->getscl = loongson_i2c_get_clock;
	i2c_algo_data->udelay = DC_I2C_TON;
	i2c_algo_data->timeout = usecs_to_jiffies(2200);

	ret = i2c_bit_add_numbered_bus(i2c_adapter);
	if (ret)
		goto free_algo_data;

	li2c->adapter = i2c_adapter;
	i2c_algo_data->data = li2c;
	i2c_set_adapdata(li2c->adapter, li2c);
	DRM_INFO("Register i2c algo-bit adapter [%s]\n", i2c_adapter->name);

	i2c_cli = i2c_new_client_device(i2c_adapter, &i2c_info);
	if (IS_ERR(i2c_cli)) {
		ret = PTR_ERR(i2c_cli);
		goto remove_i2c_adapter;
	}

	return 0;

remove_i2c_adapter:
	DRM_ERROR("Failed to create i2c client\n");
	i2c_del_adapter(i2c_adapter);
free_algo_data:
	DRM_ERROR("Failed to register i2c adapter %s\n", i2c_adapter->name);
	kfree(i2c_algo_data);
free_adapter:
	kfree(i2c_adapter);

	return ret;
}

int loongson_dc_gpio_init(struct loongson_device *ldev)
{
	struct gpio_chip *chip = &ldev->chip;
	int ret;

	chip->label = "ls7a-dc-gpio";
	chip->base = LS7A_DC_GPIO_BASE;
	chip->ngpio = 4;
	chip->parent = ldev->dev->dev;
	chip->request = ls_dc_gpio_request;
	chip->direction_input = ls_dc_gpio_dir_input;
	chip->direction_output = ls_dc_gpio_dir_output;
	chip->set = ls_dc_gpio_set;
	chip->get = ls_dc_gpio_get;
	chip->can_sleep = false;

	ret = devm_gpiochip_add_data(ldev->dev->dev, chip, ldev);
	if (ret) {
		DRM_ERROR("Failed to register ls7a dc gpio driver\n");
		return ret;
	}
	DRM_INFO("Registered ls7a dc gpio driver\n");

	return 0;
}

int loongson_i2c_init(struct loongson_device *ldev)
{
	int ret;
	int i;

	ret = gpio_request_array(i2c_gpios, ARRAY_SIZE(i2c_gpios));
	if (ret) {
		DRM_ERROR("Failed to request gpio array i2c_gpios\n");
		return -ENODEV;
	}

	ldev->i2c_bus[0].i2c_id = 0;
	ldev->i2c_bus[1].i2c_id = 1;

	for (i = 0; i < 2; i++) {
		ret = loongson_i2c_create(ldev, &ldev->i2c_bus[i], DC_I2C_NAME);
		if (ret)
			return ret;
	}

	return 0;
}

