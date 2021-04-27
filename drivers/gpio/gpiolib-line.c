/*
 * GPIOlib - userspace I/O line interface
 *
 *
 * Copyright (C) 2020-2021   Rodolfo Giometti <giometti@enneenne.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/idr.h>
#include <linux/kdev_t.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>

#define GPIO_LINE_MAX_SOURCES       128      /* should be enough... */

/*
 * Local variables
 */

static dev_t gpio_line_devt;
static struct class *gpio_line_class;

static DEFINE_MUTEX(gpio_line_idr_lock);
static DEFINE_IDR(gpio_line_idr);

struct gpio_line_device {
	struct gpio_desc *gpiod;
	const char *name;
	unsigned int id;
	struct device *dev;
};

/*
 * sysfs methods
 */

static ssize_t state_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct gpio_line_device *gpio_line = dev_get_drvdata(dev);
	int status, ret;

	ret = sscanf(buf, "%d", &status);
	if (ret != 1 && status != 0 && status != 1)
		return -EINVAL;

	gpiod_set_value_cansleep(gpio_line->gpiod, status);

	return count;
}

static ssize_t state_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct gpio_line_device *gpio_line = dev_get_drvdata(dev);
	int status = gpiod_get_value_cansleep(gpio_line->gpiod);

	return sprintf(buf, "%d\n", status);
}
static DEVICE_ATTR_RW(state);

/*
 * Class attributes
 */

static struct attribute *gpio_line_attrs[] = {
	&dev_attr_state.attr,
	NULL,
};

static const struct attribute_group gpio_line_group = {
	.attrs = gpio_line_attrs,
};

static const struct attribute_group *gpio_line_groups[] = {
	&gpio_line_group,
	NULL,
};

/*
 * Driver stuff
 */

static struct gpio_line_device *gpio_line_create_entry(const char *name,
				struct gpio_desc *gpiod,
				struct device *parent)
{
	struct gpio_line_device *gpio_line;
	dev_t devt;
	int ret;

	/* First allocate a new gpio_line device */
	gpio_line = kmalloc(sizeof(struct gpio_line_device), GFP_KERNEL);
	if (!gpio_line)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&gpio_line_idr_lock);
	/*
	 * Get new ID for the new gpio_line source.  After idr_alloc() calling
	 * the new source will be freely available into the kernel.
	 */
	ret = idr_alloc(&gpio_line_idr, gpio_line, 0,
			GPIO_LINE_MAX_SOURCES, GFP_KERNEL);
	if (ret < 0) {
		if (ret == -ENOSPC) {
			pr_err("%s: too many GPIO lines in the system\n",
			       name);
			ret = -EBUSY;
		}
		goto error_device_create;
	}
	gpio_line->id = ret;
	mutex_unlock(&gpio_line_idr_lock);

	/* Create the device and init the device's data */
	devt = MKDEV(MAJOR(gpio_line_devt), gpio_line->id);
	gpio_line->dev = device_create(gpio_line_class, parent, devt, gpio_line,
				   "%s", name);
	if (IS_ERR(gpio_line->dev)) {
		dev_err(gpio_line->dev, "unable to create device %s\n", name);
		ret = PTR_ERR(gpio_line->dev);
		goto error_idr_remove;
	}
	dev_set_drvdata(gpio_line->dev, gpio_line);

	/* Init the gpio_line data */
	gpio_line->gpiod = gpiod;
	gpio_line->name = name;

	return gpio_line;

error_idr_remove:
	mutex_lock(&gpio_line_idr_lock);
	idr_remove(&gpio_line_idr, gpio_line->id);

error_device_create:
	mutex_unlock(&gpio_line_idr_lock);
	kfree(gpio_line);

	return ERR_PTR(ret);
}

static int gpio_line_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fwnode_handle *child;
	struct gpio_line_device *gpio_line;
	int ret;

	device_for_each_child_node(dev, child) {
		struct device_node *np = to_of_node(child);
		const char *name;
		enum gpiod_flags flags;
		struct gpio_desc *gpiod;

		ret = fwnode_property_read_string(child, "line-name", &name);
		if (ret && IS_ENABLED(CONFIG_OF) && np)
			name = np->name;
		if (!name) {
			dev_err(dev,
				"name property not defined or invalid!\n");
			goto skip;
		}

		flags = GPIOD_ASIS;
		if (of_property_read_bool(np, "input"))
			flags = GPIOD_IN;
		else if (of_property_read_bool(np, "output-low"))
			flags = GPIOD_OUT_LOW;
		else if (of_property_read_bool(np, "output-high"))
			flags = GPIOD_OUT_HIGH;
		gpiod = devm_fwnode_get_gpiod_from_child(dev, NULL, child,
							 flags, name);
		if (IS_ERR(gpiod)) {
			dev_err(dev, "gpios property not defined!\n");
			goto skip;
		}

		gpio_line = gpio_line_create_entry(name, gpiod, dev);
		if (IS_ERR(gpio_line))
			goto skip;

		/* Success, go to the next child */
		dev_info(gpio_line->dev, "GPIO%d added as %s\n",
			desc_to_gpio(gpiod),
			flags == GPIOD_ASIS ? "as-is" :
			  flags == GPIOD_OUT_HIGH ? "output-high" :
			    flags == GPIOD_OUT_LOW ? "output-low" :
			      flags == GPIOD_IN ? "input" : "unknow!");
		continue;

skip:		/* Error, skip the child */
		fwnode_handle_put(child);
		dev_err(dev, "failed to register GPIO lines interface\n");
	}

	return 0;
}

static const struct of_device_id of_gpio_gpio_line_match[] = {
	{ .compatible = "gpio-line", },
	{ /* sentinel */ }
};

static struct platform_driver gpio_line_gpio_driver = {
	.driver	 = {
		.name   = "gpio-line",
		.of_match_table = of_gpio_gpio_line_match,
	},
};

builtin_platform_driver_probe(gpio_line_gpio_driver, gpio_line_gpio_probe);

/*
 * Module stuff
 */

static int __init gpiolib_line_init(void)
{
	/* Create the new class */
	gpio_line_class = class_create(THIS_MODULE, "line");
	if (!gpio_line_class) {
		printk(KERN_ERR "gpio_line: failed to create class\n");
		return -ENOMEM;
	}
	gpio_line_class->dev_groups = gpio_line_groups;

	return 0;
}

postcore_initcall(gpiolib_line_init);
