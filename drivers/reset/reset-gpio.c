// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2021 Sean Anderson <sean.anderson@seco.com>
 *
 * This driver controls GPIOs used to reset device(s). It may be used for when
 * there is a need for more complex behavior than a simple reset-gpios
 * property. It may also be used to unify code paths between device-based and
 * gpio-based resets.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/sched.h>
#include <linux/wait.h>

/**
 * struct reset_gpio_priv - Private data for GPIO reset driver
 * @rc: Reset controller for this driver
 * @done_queue: Queue to wait for changes on done GPIOs. Events occur whenever
 *              the value of any done GPIO changes. Valid only when @done is
 *              non-%NULL.
 * @reset: Array of gpios to use when (de)asserting resets
 * @done: Array of gpios to determine whether a reset has finished; may be
 *        %NULL
 * @done_timeout_jiffies: Timeout when waiting for a done GPIO to be asserted, in jiffies
 * @post_assert_delay: Time to wait after asserting a reset, in us
 * @post_deassert_delay: Time to wait after deasserting a reset, in us
 */
struct reset_gpio_priv {
	struct reset_controller_dev rc;
	struct wait_queue_head done_queue;
	struct gpio_descs *reset;
	struct gpio_descs *done;
	unsigned long done_timeout_jiffies;
	u32 pre_assert_delay;
	u32 post_assert_delay;
	u32 pre_deassert_delay;
	u32 post_deassert_delay;
};

static inline struct reset_gpio_priv
*rc_to_reset_gpio(struct reset_controller_dev *rc)
{
	return container_of(rc, struct reset_gpio_priv, rc);
}

static int reset_gpio_assert(struct reset_controller_dev *rc, unsigned long id)
{
	struct reset_gpio_priv *priv = rc_to_reset_gpio(rc);

	if (priv->pre_assert_delay)
		fsleep(priv->pre_assert_delay);
	gpiod_set_value_cansleep(priv->reset->desc[id], 1);
	if (priv->post_assert_delay)
		fsleep(priv->post_assert_delay);
	return 0;
}

static int reset_gpio_deassert(struct reset_controller_dev *rc,
			       unsigned long id)
{
	int ret = 0;
	unsigned int remaining;
	struct reset_gpio_priv *priv = rc_to_reset_gpio(rc);

	if (priv->pre_deassert_delay)
		fsleep(priv->pre_deassert_delay);
	gpiod_set_value_cansleep(priv->reset->desc[id], 0);
	if (priv->post_deassert_delay)
		fsleep(priv->post_deassert_delay);

	if (!priv->done)
		return 0;

	remaining = wait_event_idle_timeout(
		priv->done_queue,
		(ret = gpiod_get_value_cansleep(priv->done->desc[id])),
		priv->done_timeout_jiffies);
	dev_dbg(rc->dev, "%s: remaining=%u\n", __func__, remaining);
	if (ret < 0)
		return ret;
	if (ret)
		return 0;
	return -ETIMEDOUT;
}

static int reset_gpio_reset(struct reset_controller_dev *rc, unsigned long id)
{
	int ret = reset_gpio_assert(rc, id);

	if (!ret)
		return ret;

	return reset_gpio_deassert(rc, id);
}

static int reset_gpio_status(struct reset_controller_dev *rc, unsigned long id)
{
	struct reset_gpio_priv *priv = rc_to_reset_gpio(rc);

	return gpiod_get_value_cansleep(priv->reset->desc[id]);
}

static const struct reset_control_ops reset_gpio_ops = {
	.reset = reset_gpio_reset,
	.assert = reset_gpio_assert,
	.deassert = reset_gpio_deassert,
	.status = reset_gpio_status,
};

static irqreturn_t reset_gpio_irq(int irq, void *data)
{
	struct reset_gpio_priv *priv = data;

	wake_up(&priv->done_queue);
	return IRQ_HANDLED;
}

static int reset_gpio_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct reset_gpio_priv *priv;
	u32 done_timeout_us;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	platform_set_drvdata(pdev, priv);

	/* A short macro to reduce repetitive error handling */
#define read_delay(propname, val) do { \
	ret = of_property_read_u32(dev->of_node, (propname), &(val)); \
	if (ret == -EINVAL) \
		(val) = 0; \
	else if (ret) \
		return dev_err_probe(dev, ret, \
				     "Could not read %s\n", propname); \
} while (0)

	read_delay("pre-assert-us", priv->pre_assert_delay);
	read_delay("post-assert-us", priv->post_assert_delay);
	read_delay("pre-deassert-us", priv->pre_deassert_delay);
	read_delay("post-deassert-us", priv->post_deassert_delay);

	ret = of_property_read_u32(np, "done-timeout-us", &done_timeout_us);
	if (ret == -EINVAL) {
		if (priv->post_deassert_delay)
			done_timeout_us = 10 * priv->post_deassert_delay;
		else
			done_timeout_us = 1000;
	} else if (ret)
		return dev_err_probe(dev, ret,
				     "Could not read done timeout\n");
	priv->done_timeout_jiffies = usecs_to_jiffies(done_timeout_us);

	priv->reset = devm_gpiod_get_array(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->reset))
		return dev_err_probe(dev, PTR_ERR(priv->reset),
				     "Could not get reset gpios\n");

	priv->done = devm_gpiod_get_array_optional(dev, "done",
						   GPIOD_IN);
	if (IS_ERR(priv->done))
		return dev_err_probe(dev, PTR_ERR(priv->done),
				     "Could not get done gpios\n");
	if (priv->done) {
		int i;

		if (priv->reset->ndescs != priv->done->ndescs)
			return dev_err_probe(dev, -EINVAL,
					     "Number of reset and done gpios does not match\n");
		init_waitqueue_head(&priv->done_queue);
		for (i = 0; i < priv->done->ndescs; i++) {
			ret = gpiod_to_irq(priv->done->desc[i]);
			if (ret < 0)
				return dev_err_probe(dev, ret,
						     "Could not convert GPIO to IRQ\n");

			ret = devm_request_irq(dev, ret, reset_gpio_irq,
					       IRQF_SHARED, dev_name(dev),
					       priv);
			if (ret)
				return dev_err_probe(dev, ret,
						     "Could not request IRQ\n");
		}
	}

	priv->rc.ops = &reset_gpio_ops;
	priv->rc.owner = THIS_MODULE;
	priv->rc.dev = dev;
	priv->rc.of_node = np;
	priv->rc.nr_resets = priv->reset->ndescs;
	ret = devm_reset_controller_register(dev, &priv->rc);
	if (!ret)
		dev_info(dev, "probed with %u resets\n", priv->reset->ndescs);
	return ret;
}

static const struct of_device_id reset_gpio_of_match[] = {
	{ .compatible = "gpio-reset", },
	{},
};
MODULE_DEVICE_TABLE(of, reset_gpio_of_match);

static struct platform_driver reset_gpio_driver = {
	.probe = reset_gpio_probe,
	.driver = {
		.name = "gpio-reset",
		.of_match_table = of_match_ptr(reset_gpio_of_match),
	},
};
module_platform_driver(reset_gpio_driver);

MODULE_ALIAS("platform:gpio-reset");
MODULE_DESCRIPTION("Generic GPIO reset driver");
MODULE_LICENSE("GPL v2");
