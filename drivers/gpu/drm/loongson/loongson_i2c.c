// SPDX-License-Identifier: GPL-2.0-or-later

#include "loongson_drv.h"
#include "loongson_i2c.h"

static inline void dc_gpio_set_dir(struct loongson_device *ldev,
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

static void dc_gpio_set_val(struct loongson_device *ldev, unsigned int pin,
			    int high)
{
	u32 temp;

	temp = ls7a_mm_rreg(ldev, LS7A_DC_GPIO_OUT_OFFSET);
	if (high)
		temp |= 1UL << pin;
	else
		temp &= ~(1UL << pin);

	ls7a_mm_wreg(ldev, LS7A_DC_GPIO_OUT_OFFSET, temp);
}

static void loongson_i2c_set_data(void *i2c, int value)
{
	struct loongson_i2c *li2c = i2c;
	struct loongson_device *ldev = li2c->ldev;
	unsigned int pin = li2c->data;

	if (value)
		dc_gpio_set_dir(ldev, pin, 1);
	else {
		dc_gpio_set_val(ldev, pin, 0);
		dc_gpio_set_dir(ldev, pin, 0);
	}
}

static void loongson_i2c_set_clock(void *i2c, int value)
{
	struct loongson_i2c *li2c = i2c;
	struct loongson_device *ldev = li2c->ldev;
	unsigned int pin = li2c->clock;

	if (value)
		dc_gpio_set_dir(ldev, pin, 1);
	else {
		dc_gpio_set_val(ldev, pin, 0);
		dc_gpio_set_dir(ldev, pin, 0);
	}
}

static int loongson_i2c_get_data(void *i2c)
{
	int val;
	struct loongson_i2c *li2c = i2c;
	struct loongson_device *ldev = li2c->ldev;
	unsigned int pin = li2c->data;

	val = ls7a_mm_rreg(ldev, LS7A_DC_GPIO_IN_OFFSET);

	return (val >> pin) & 1;
}

static int loongson_i2c_get_clock(void *i2c)
{
	int val;
	struct loongson_i2c *li2c = i2c;
	struct loongson_device *ldev = li2c->ldev;
	unsigned int pin = li2c->clock;

	val = ls7a_mm_rreg(ldev, LS7A_DC_GPIO_IN_OFFSET);

	return (val >> pin) & 1;
}

static int loongson_i2c_create(struct loongson_device *ldev,
			       struct loongson_i2c *li2c, const char *name)
{
	int ret;
	unsigned int i2c_num;
	struct drm_device *dev = &ldev->dev;
	struct i2c_client *i2c_cli;
	struct i2c_adapter *i2c_adapter;
	struct i2c_algo_bit_data *i2c_algo_data;
	const struct i2c_board_info i2c_info = {
		.type = "ddc-dev",
		.addr = DDC_ADDR,
		.flags = I2C_CLASS_DDC,
	};

	i2c_num = li2c->i2c_id;
	i2c_adapter = devm_kzalloc(dev->dev, sizeof(*i2c_adapter), GFP_KERNEL);
	if (!i2c_adapter)
		return -ENOMEM;

	i2c_algo_data = devm_kzalloc(dev->dev, sizeof(*i2c_algo_data), GFP_KERNEL);
	if (!i2c_algo_data) {
		ret = -ENOMEM;
		goto free_adapter;
	}

	i2c_adapter->owner = THIS_MODULE;
	i2c_adapter->class = I2C_CLASS_DDC;
	i2c_adapter->algo_data = i2c_algo_data;
	i2c_adapter->dev.parent = dev->dev;
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
	li2c->ldev = ldev;
	DRM_INFO("Register i2c algo-bit adapter [%s]\n", i2c_adapter->name);

	i2c_cli = i2c_new_client_device(i2c_adapter, &i2c_info);
	if (IS_ERR(i2c_cli)) {
		ret = PTR_ERR(i2c_cli);
		goto remove_i2c_adapter;
	}

	return 0;

remove_i2c_adapter:
	drm_err(dev, "Failed to create i2c client\n");
	i2c_del_adapter(i2c_adapter);
free_algo_data:
	drm_err(dev, "Failed to register i2c adapter %s\n", i2c_adapter->name);
	kfree(i2c_algo_data);
free_adapter:
	kfree(i2c_adapter);

	return ret;
}

int loongson_dc_gpio_init(struct loongson_device *ldev)
{
	int pin;

	/* set gpio dir output 0-3 */
	for (pin = 0; pin < 4; pin++) {
		dc_gpio_set_val(ldev, pin, 0);
		dc_gpio_set_dir(ldev, pin, 0);
	}

	return 0;
}

int loongson_i2c_init(struct loongson_device *ldev)
{
	int ret;
	int i;

	for (i = 0; i < 2; i++) {
		ldev->i2c_bus[1].i2c_id = i;
		ret = loongson_i2c_create(ldev, &ldev->i2c_bus[i], DC_I2C_NAME);
		if (ret)
			return ret;
	}

	return 0;
}

