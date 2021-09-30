// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 NVIDIA Corporation
 *
 * Author: Dipen Patel <dipenp@nvidia.com>
 */

#include <linux/version.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/timer.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

/*
 * Tegra194 On chip HTE (hardware timestamping engine) also known as GTE
 * (generic timestamping engine) can monitor subset of GPIO lines for the event
 * and timestamp accordingly.
 *
 * This sample HTE GPIO test driver demonstrates HTE API usage indirectly
 * through GPIOLIB framework. It enables hardware timestamp on gpio_in line.
 * Also the gpio_in is configured to be a rising edge interrupt.
 *
 * Note: gpio_out and gpio_in need to be shorted externally in order for this
 * test driver to work for the GPIO monitoring. The test driver has been
 * tested on Jetson AGX platform by shorting pin 32 and 16 on 40 pin header.
 */

static unsigned int gpio_in = 322;
module_param(gpio_in, uint, 0660);

static unsigned int gpio_out = 321;
module_param(gpio_out, uint, 0660);

static struct tegra_hte_test {
	bool is_ts_en;
	int gpio_in_irq;
	struct gpio_desc *gpio_in;
	struct gpio_desc *gpio_out;
	struct timer_list timer;
	struct kobject *kobj;
} hte;

static hte_return_t process_hw_ts(struct hte_ts_data *ts, void *p)
{
	char *edge;
	(void)p;

	if (!ts)
		return HTE_CB_ERROR;

	if (ts->dir >= HTE_DIR_NOSUPP)
		edge = "rising";

	pr_info("GPIO HW timestamp(%llu): %llu, edge: %s\n", ts->seq, ts->tsc,
		(ts->dir < HTE_DIR_NOSUPP) ? ((ts->dir == HTE_FALLING_EDGE_TS) ?
					      "falling" : "rising") : edge);

	return HTE_CB_HANDLED;
}

/*
 * Sysfs attribute to request/release HTE gpio line
 */
static ssize_t store_gpio_en_dis(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf, size_t count)
{
	int ret = count;
	unsigned long val = 0;
	(void)kobj;
	(void)attr;

	if (kstrtoul(buf, 10, &val) < 0) {
		ret = -EINVAL;
		goto error;
	}

	if (val == 1) {
		if (hte.is_ts_en) {
			ret = -EEXIST;
			goto error;
		}

		ret = gpiod_req_hw_timestamp_ns(hte.gpio_in, process_hw_ts,
						NULL, NULL);
		if (ret)
			goto error;

		hte.is_ts_en = true;
	} else if (val == 0) {
		if (!hte.is_ts_en) {
			ret = -EINVAL;
			goto error;
		}
		ret = gpiod_rel_hw_timestamp_ns(hte.gpio_in);
		if (ret)
			goto error;

		hte.is_ts_en = false;
	}

	ret = count;

error:
	return ret;
}

struct kobj_attribute gpio_en_dis_attr =
		__ATTR(gpio_en_dis, 0220, NULL, store_gpio_en_dis);

static struct attribute *attrs[] = {
	&gpio_en_dis_attr.attr,
	NULL,
};

static struct attribute_group tegra_hte_test_attr_group = {
	.attrs = attrs,
};

static int tegra_hte_test_sysfs_create(void)
{
	int ret;

	/* Creates /sys/kernel/tegra_hte_gpio_test */
	hte.kobj = kobject_create_and_add("tegra_hte_gpio_test", kernel_kobj);
	if (!hte.kobj)
		return -ENOMEM;

	ret = sysfs_create_group(hte.kobj, &tegra_hte_test_attr_group);
	if (ret)
		kobject_put(hte.kobj);
	return ret;
}

static void gpio_timer_cb(struct timer_list *t)
{
	(void)t;

	gpiod_set_value(hte.gpio_out, !gpiod_get_value(hte.gpio_out));
	mod_timer(&hte.timer, jiffies + msecs_to_jiffies(8000));
}

static irqreturn_t tegra_hte_test_gpio_isr(int irq, void *data)
{
	(void)irq;
	(void)data;

	return IRQ_HANDLED;
}

static int __init tegra_hte_gpio_test_init(void)
{
	int ret = 0;

	ret = gpio_request(gpio_out, "gte_test_gpio_out");
	if (ret) {
		pr_err("failed request gpio out\n");
		return -EINVAL;
	}

	ret = gpio_request(gpio_in, "gte_test_gpio_in");
	if (ret) {
		pr_err("failed request gpio in\n");
		ret = -EINVAL;
		goto free_gpio_out;
	}

	hte.gpio_out = gpio_to_desc(gpio_out);
	if (!hte.gpio_out) {
		pr_err("failed convert gpio out to desc\n");
		ret = -EINVAL;
		goto free_gpio_in;
	}

	hte.gpio_in = gpio_to_desc(gpio_in);
	if (!hte.gpio_in) {
		pr_err("failed convert gpio in to desc\n");
		ret = -EINVAL;
		goto free_gpio_in;
	}

	ret = gpiod_direction_output(hte.gpio_out, 0);
	if (ret) {
		pr_err("failed to set output\n");
		ret = -EINVAL;
		goto free_gpio_in;
	}

	ret = gpiod_direction_input(hte.gpio_in);
	if (ret) {
		pr_err("failed to set input\n");
		ret = -EINVAL;
		goto free_gpio_in;
	}

	ret = gpiod_to_irq(hte.gpio_in);
	if (ret < 0) {
		pr_err("failed to map GPIO to IRQ: %d\n", ret);
		ret = -ENXIO;
		goto free_gpio_in;
	}

	hte.gpio_in_irq = ret;

	ret = request_irq(ret, tegra_hte_test_gpio_isr,
			  IRQF_TRIGGER_RISING,
			  "tegra_hte_gpio_test_isr", &hte);
	if (ret) {
		pr_err("failed to acquire IRQ\n");
		ret = -ENXIO;
		goto free_irq;
	}

	ret = tegra_hte_test_sysfs_create();
	if (ret != 0) {
		pr_err("sysfs creation failed\n");
		ret = -ENXIO;
		goto free_irq;
	}

	timer_setup(&hte.timer, gpio_timer_cb, 0);
	mod_timer(&hte.timer, jiffies + msecs_to_jiffies(5000));

	return 0;

free_irq:
	free_irq(hte.gpio_in_irq, &hte);
free_gpio_in:
	gpio_free(gpio_in);
free_gpio_out:
	gpio_free(gpio_out);

	return ret;
}

static void __exit tegra_hte_gpio_test_exit(void)
{
	free_irq(hte.gpio_in_irq, &hte);
	gpio_free(gpio_in);
	gpio_free(gpio_out);
	kobject_put(hte.kobj);
	del_timer(&hte.timer);
}

module_init(tegra_hte_gpio_test_init);
module_exit(tegra_hte_gpio_test_exit);
MODULE_AUTHOR("Dipen Patel <dipenp@nvidia.com>");
MODULE_LICENSE("GPL v2");
