// SPDX-License-Identifier: GPL-2.0-only
/*
 * PWM-based multi-color LED control
 *
 * Copyright 2022 Sven Schwermer <sven.schwermer@disruptive-technologies.com>
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/led-class-multicolor.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

struct pwm_led {
	struct pwm_device *pwm;
	struct pwm_state state;
};

struct pwm_mc_led {
	struct led_classdev_mc mc_cdev;
	struct mutex lock;
	struct pwm_led leds[];
};

static int led_pwm_mc_set(struct led_classdev *cdev,
			  enum led_brightness brightness)
{
	int i;
	unsigned long long duty;
	int ret = 0;
	struct led_classdev_mc *mc_cdev = lcdev_to_mccdev(cdev);
	struct pwm_mc_led *priv = container_of(mc_cdev, struct pwm_mc_led, mc_cdev);

	led_mc_calc_color_components(mc_cdev, brightness);

	mutex_lock(&priv->lock);

	for (i = 0; i < mc_cdev->num_colors; ++i) {
		duty = priv->leds[i].state.period;
		duty *= mc_cdev->subled_info[i].brightness;
		do_div(duty, cdev->max_brightness);

		priv->leds[i].state.duty_cycle = duty;
		priv->leds[i].state.enabled = duty > 0;
		ret = pwm_apply_state(priv->leds[i].pwm,
				      &priv->leds[i].state);
		if (ret)
			break;
	}

	mutex_unlock(&priv->lock);

	return ret;
}

static int led_pwm_mc_probe(struct platform_device *pdev)
{
	struct fwnode_handle *mcnode, *fwnode;
	int count = 0;
	struct pwm_mc_led *priv;
	struct mc_subled *subled;
	struct led_classdev *cdev;
	struct pwm_led *pwmled;
	u32 color;
	int ret = 0;
	struct led_init_data init_data = {};

	mcnode = device_get_named_child_node(&pdev->dev, "multi-led");
	if (!mcnode) {
		dev_err(&pdev->dev, "expected multi-led node\n");
		ret = -ENODEV;
		goto out;
	}

	/* count the nodes inside the multi-led node */
	fwnode_for_each_child_node(mcnode, fwnode)
		++count;

	priv = devm_kzalloc(&pdev->dev, struct_size(priv, leds, count),
			    GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto release_mcnode;
	}
	mutex_init(&priv->lock);

	subled = devm_kcalloc(&pdev->dev, count, sizeof(*subled), GFP_KERNEL);
	if (!subled) {
		ret = -ENOMEM;
		goto destroy_mutex;
	}
	priv->mc_cdev.subled_info = subled;

	/* init the multicolor's LED class device */
	cdev = &priv->mc_cdev.led_cdev;
	fwnode_property_read_u32(mcnode, "max-brightness",
				 &cdev->max_brightness);
	cdev->flags = LED_CORE_SUSPENDRESUME;
	cdev->brightness_set_blocking = led_pwm_mc_set;

	/* iterate over the nodes inside the multi-led node */
	fwnode_for_each_child_node(mcnode, fwnode) {
		pwmled = &priv->leds[priv->mc_cdev.num_colors];
		pwmled->pwm = devm_fwnode_pwm_get(&pdev->dev, fwnode, NULL);
		if (IS_ERR(pwmled->pwm)) {
			ret = PTR_ERR(pwmled->pwm);
			dev_err(&pdev->dev, "unable to request PWM: %d\n", ret);
			fwnode_handle_put(fwnode);
			goto destroy_mutex;
		}
		pwm_init_state(pwmled->pwm, &pwmled->state);

		ret = fwnode_property_read_u32(fwnode, "color", &color);
		if (ret) {
			dev_err(&pdev->dev, "cannot read color: %d\n", ret);
			fwnode_handle_put(fwnode);
			goto destroy_mutex;
		}

		subled[priv->mc_cdev.num_colors].color_index = color;
		++priv->mc_cdev.num_colors;
	}

	init_data.fwnode = mcnode;
	ret = devm_led_classdev_multicolor_register_ext(&pdev->dev,
							&priv->mc_cdev,
							&init_data);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to register multicolor PWM led for %s: %d\n",
			cdev->name, ret);
		goto destroy_mutex;
	}

	ret = led_pwm_mc_set(cdev, cdev->brightness);
	if (ret) {
		dev_err(&pdev->dev, "failed to set led PWM value for %s: %d",
			cdev->name, ret);
		goto destroy_mutex;
	}

	platform_set_drvdata(pdev, priv);
	return 0;

destroy_mutex:
	mutex_destroy(&priv->lock);
release_mcnode:
	fwnode_handle_put(mcnode);
out:
	return ret;
}

static int led_pwm_mc_remove(struct platform_device *pdev)
{
	struct pwm_mc_led *priv = platform_get_drvdata(pdev);

	mutex_destroy(&priv->lock);
	return 0;
}

static const struct of_device_id of_pwm_leds_mc_match[] = {
	{ .compatible = "pwm-leds-multicolor", },
	{},
};
MODULE_DEVICE_TABLE(of, of_pwm_leds_mc_match);

static struct platform_driver led_pwm_mc_driver = {
	.probe		= led_pwm_mc_probe,
	.remove		= led_pwm_mc_remove,
	.driver		= {
		.name	= "leds_pwm_multicolor",
		.of_match_table = of_pwm_leds_mc_match,
	},
};

module_platform_driver(led_pwm_mc_driver);

MODULE_AUTHOR("Sven Schwermer <sven.schwermer@disruptive-technologies.com>");
MODULE_DESCRIPTION("multi-color PWM LED driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:leds-pwm-multicolor");
