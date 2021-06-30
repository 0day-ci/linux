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
	else
		return 0;
}

static int ls_dc_gpio_dir_input(struct gpio_chip *chip, unsigned int pin)
{
	struct loongson_device *ldev;

	ldev = container_of(chip, struct loongson_device, chip);
	__dc_gpio_set_dir(ldev, pin, 1);

	return 0;
}

static int ls_dc_gpio_dir_output(struct gpio_chip *chip,
				 unsigned int pin, int value)
{
	struct loongson_device *ldev;

	ldev = container_of(chip, struct loongson_device, chip);
	__dc_gpio_set_val(ldev, pin, value);
	__dc_gpio_set_dir(ldev, pin, 0);

	return 0;
}

static void ls_dc_gpio_set(struct gpio_chip *chip, unsigned int pin, int value)
{
	struct loongson_device *ldev;

	ldev = container_of(chip, struct loongson_device, chip);
	__dc_gpio_set_val(ldev, pin, value);
}

static int ls_dc_gpio_get(struct gpio_chip *chip, unsigned int pin)
{
	u32 val;
	struct loongson_device *ldev;

	ldev = container_of(chip, struct loongson_device, chip);
	val = ls7a_mm_rreg(ldev, LS7A_DC_GPIO_IN_OFFSET);
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

static int loongson_i2c_create(struct loongson_i2c *li2c, const char *name)
{
	int ret;
	unsigned int i2c_num;
	struct i2c_client *i2c_cli;
	struct i2c_adapter *i2c_adapter;
	struct i2c_board_info i2c_info;
	struct i2c_algo_bit_data *i2c_algo_data;
	struct device *dev;

	dev = &li2c->adapter->dev;
	i2c_num = li2c->i2c_id;
	i2c_adapter = kzalloc(sizeof(struct i2c_adapter), GFP_KERNEL);
	if (IS_ERR(i2c_adapter)) {
		ret = PTR_ERR(i2c_adapter);
		goto error_mem;
	}
	i2c_algo_data = kzalloc(sizeof(struct i2c_algo_bit_data), GFP_KERNEL);
	if (IS_ERR(i2c_algo_data)) {
		ret = PTR_ERR(i2c_algo_data);
		goto error_mem;
	}

	i2c_adapter->owner = THIS_MODULE;
	i2c_adapter->class = I2C_CLASS_DDC;
	i2c_adapter->algo_data = i2c_algo_data;
	i2c_adapter->nr = i2c_num;
	snprintf(i2c_adapter->name, sizeof(i2c_adapter->name), "%s%d",
		 name, i2c_num - DC_I2C_BASE);

	li2c->data = i2c_num % DC_I2C_BASE * 2;
	li2c->clock = i2c_num % DC_I2C_BASE * 2 + 1;
	DRM_INFO("Created i2c-%d, sda=%d, scl=%d\n",
		 i2c_num, li2c->data, li2c->clock);

	if (gpio_cansleep(i2c_gpios[li2c->data].gpio) ||
	    gpio_cansleep(i2c_gpios[li2c->clock].gpio))
		dev_warn(dev, "Slow GPIO pins might wreak havoc I2C timing\n");

	i2c_algo_data->setsda = loongson_i2c_set_data;
	i2c_algo_data->setscl = loongson_i2c_set_clock;
	i2c_algo_data->getsda = loongson_i2c_get_data;
	i2c_algo_data->getscl = loongson_i2c_get_clock;
	i2c_algo_data->udelay = DC_I2C_TON;
	i2c_algo_data->timeout = usecs_to_jiffies(2200);

	ret = i2c_bit_add_numbered_bus(i2c_adapter);
	if (ret) {
		DRM_ERROR("Failed to register i2c algo-bit adapter %s\n",
			  i2c_adapter->name);
		kfree(i2c_adapter);
		i2c_adapter = NULL;
	}

	li2c->adapter = i2c_adapter;
	i2c_algo_data->data = li2c;
	i2c_set_adapdata(li2c->adapter, li2c);
	DRM_INFO("Register i2c algo-bit adapter [%s]\n", i2c_adapter->name);

	memset(&i2c_info, 0, sizeof(struct i2c_board_info));
	strncpy(i2c_info.type, name, I2C_NAME_SIZE);
	i2c_info.addr = DDC_ADDR;
	i2c_cli = i2c_new_client_device(i2c_adapter, &i2c_info);
	if (i2c_cli == NULL) {
		DRM_ERROR("Failed to create i2c adapter\n");
		return -EBUSY;
	}
	li2c->init = true;
	return 0;

error_mem:
	DRM_ERROR("Failed to malloc memory for loongson i2c\n");
	return ret;
}

static int loongson_i2c_add(struct loongson_device *ldev, const char *name)
{
	int i;

	for (i = 0; i < LS_MAX_I2C_BUS; i++) {
		if (ldev->i2c_bus[i].use)
			loongson_i2c_create(&ldev->i2c_bus[i], name);
		else {
			DRM_DEBUG_DRIVER("i2c_bus[%d] not use\n", i);
			return -ENODEV;
		}
	}
	return 0;
}

int loongson_dc_gpio_init(struct loongson_device *ldev)
{
	int ret;
	struct gpio_chip *chip;

	chip = &ldev->chip;
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
		return -ENODEV;
	}
	DRM_INFO("Registered ls7a dc gpio driver\n");

	return 0;
}

int loongson_i2c_init(struct loongson_device *ldev)
{
	int ret;

	ret = gpio_request_array(i2c_gpios, ARRAY_SIZE(i2c_gpios));
	if (ret) {
		DRM_ERROR("Failed to request gpio array i2c_gpios\n");
		return -ENODEV;
	}

	ldev->i2c_bus[0].i2c_id = 6;
	ldev->i2c_bus[0].use = true;
	ldev->i2c_bus[1].i2c_id = 7;
	ldev->i2c_bus[1].use = true;

	loongson_i2c_add(ldev, DC_I2C_NAME);

	return 0;
}

struct loongson_i2c *loongson_i2c_bus_match(struct loongson_device *ldev, u32 i2c_id)
{
	u32 i;
	struct loongson_i2c *match = NULL, *tables;

	tables = ldev->i2c_bus;

	for (i = 0; i < LS_MAX_I2C_BUS; i++) {
		if (tables->i2c_id == i2c_id && tables->init == true) {
			match = tables;
			break;
		}

		tables++;
	}

	return match;
}

