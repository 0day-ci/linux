// SPDX-License-Identifier: GPL-2.0
/*
 * Backlight driver to control the brightness over DisplayPort aux channel.
 */

#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <drm/drm_dp_helper.h>

#define DP_AUX_MAX_BRIGHTNESS		0xffff

/**
 * struct dp_aux_backlight - DisplayPort aux backlight data
 * @dev: pointer to our device.
 * @aux: the DisplayPort aux channel.
 * @enable_gpio: the backlight enable gpio.
 * @enabled: true if backlight is enabled else false.
 */
struct dp_aux_backlight {
	struct device *dev;
	struct drm_dp_aux *aux;
	struct gpio_desc *enable_gpio;
	bool enabled;
};

static struct drm_dp_aux *i2c_to_aux(struct i2c_adapter *i2c)
{
	return container_of(i2c, struct drm_dp_aux, ddc);
}

static int dp_aux_backlight_enable(struct dp_aux_backlight *aux_bl)
{
	u8 val = 0;
	int ret;

	if (aux_bl->enabled)
		return 0;

	/* Set backlight control mode */
	ret = drm_dp_dpcd_readb(aux_bl->aux, DP_EDP_BACKLIGHT_MODE_SET_REGISTER,
				&val);
	if (ret < 0)
		return ret;

	val &= ~DP_EDP_BACKLIGHT_CONTROL_MODE_MASK;
	val |= DP_EDP_BACKLIGHT_CONTROL_MODE_DPCD;
	ret = drm_dp_dpcd_writeb(aux_bl->aux, DP_EDP_BACKLIGHT_MODE_SET_REGISTER,
				 val);
	if (ret < 0)
		return ret;

	/* Enable backlight */
	ret = drm_dp_dpcd_readb(aux_bl->aux, DP_EDP_DISPLAY_CONTROL_REGISTER,
				&val);
	if (ret < 0)
		return ret;

	val |= DP_EDP_BACKLIGHT_ENABLE;
	ret = drm_dp_dpcd_writeb(aux_bl->aux, DP_EDP_DISPLAY_CONTROL_REGISTER,
				 val);
	if (ret < 0)
		return ret;

	if (aux_bl->enable_gpio)
		gpiod_set_value(aux_bl->enable_gpio, 1);

	aux_bl->enabled = true;

	return 0;
}

static int dp_aux_backlight_disable(struct dp_aux_backlight *aux_bl)
{
	u8 val = 0;
	int ret;

	if (!aux_bl->enabled)
		return 0;

	if (aux_bl->enable_gpio)
		gpiod_set_value(aux_bl->enable_gpio, 0);

	ret = drm_dp_dpcd_readb(aux_bl->aux, DP_EDP_DISPLAY_CONTROL_REGISTER,
				&val);
	if (ret < 0)
		return ret;

	val &= ~DP_EDP_BACKLIGHT_ENABLE;
	ret = drm_dp_dpcd_writeb(aux_bl->aux, DP_EDP_DISPLAY_CONTROL_REGISTER,
				 val);
	if (ret < 0)
		return ret;

	aux_bl->enabled = false;

	return 0;
}

static int dp_aux_backlight_update_status(struct backlight_device *bd)
{
	struct dp_aux_backlight *aux_bl = bl_get_data(bd);
	u16 brightness = backlight_get_brightness(bd);
	u8 val[2] = { 0x0 };
	int ret = 0;

	if (brightness > 0) {
		val[0] = brightness >> 8;
		val[1] = brightness & 0xff;
		ret = drm_dp_dpcd_write(aux_bl->aux, DP_EDP_BACKLIGHT_BRIGHTNESS_MSB,
					val, sizeof(val));
		if (ret < 0)
			return ret;

		dp_aux_backlight_enable(aux_bl);
	} else {
		dp_aux_backlight_disable(aux_bl);
	}

	return 0;
}

static int dp_aux_backlight_get_brightness(struct backlight_device *bd)
{
	struct dp_aux_backlight *aux_bl = bl_get_data(bd);
	u8 val[2] = { 0x0 };
	int ret = 0;

	if (backlight_is_blank(bd))
		return 0;

	ret = drm_dp_dpcd_read(aux_bl->aux, DP_EDP_BACKLIGHT_BRIGHTNESS_MSB,
			       &val, sizeof(val));
	if (ret < 0)
		return ret;

	return (val[0] << 8 | val[1]);
}

static const struct backlight_ops aux_bl_ops = {
	.update_status = dp_aux_backlight_update_status,
	.get_brightness = dp_aux_backlight_get_brightness,
};


static int dp_aux_backlight_probe(struct platform_device *pdev)
{
	struct dp_aux_backlight *aux_bl;
	struct backlight_device *bd;
	struct backlight_properties bl_props = { 0 };
	struct device_node *np;
	struct i2c_adapter *ddc;
	int ret = 0;
	u32 val;

	aux_bl = devm_kzalloc(&pdev->dev, sizeof(*aux_bl), GFP_KERNEL);
	if (!aux_bl)
		return -ENOMEM;

	aux_bl->dev = &pdev->dev;

	np = of_parse_phandle(pdev->dev.of_node, "ddc-i2c-bus", 0);
	if (!np) {
		dev_err(&pdev->dev, "failed to get aux ddc I2C bus\n");
		return -ENODEV;
	}

	ddc = of_find_i2c_adapter_by_node(np);
	of_node_put(np);
	if (!ddc)
		return -EPROBE_DEFER;

	aux_bl->aux = i2c_to_aux(ddc);
	dev_dbg(&pdev->dev, "using dp aux %s\n", aux_bl->aux->name);

	aux_bl->enable_gpio = devm_gpiod_get_optional(&pdev->dev, "enable",
					     GPIOD_OUT_LOW);
	if (IS_ERR(aux_bl->enable_gpio)) {
		ret = PTR_ERR(aux_bl->enable_gpio);
		goto free_ddc;
	}

	val = DP_AUX_MAX_BRIGHTNESS;
	of_property_read_u32(pdev->dev.of_node, "max-brightness", &val);
	if (val > DP_AUX_MAX_BRIGHTNESS)
		val = DP_AUX_MAX_BRIGHTNESS;

	bl_props.max_brightness = val;
	bl_props.brightness = val;
	bl_props.type = BACKLIGHT_RAW;
	bd = devm_backlight_device_register(&pdev->dev, dev_name(&pdev->dev),
					    &pdev->dev, aux_bl,
					    &aux_bl_ops, &bl_props);
	if (IS_ERR(bd)) {
		ret = PTR_ERR(bd);
		dev_err(&pdev->dev,
			      "failed to register backlight (%d)\n", ret);
		goto free_ddc;
	}

	platform_set_drvdata(pdev, bd);

	return 0;

free_ddc:
	if (ddc)
		put_device(&ddc->dev);

	return ret;
}

static int dp_aux_backlight_remove(struct platform_device *pdev)
{
	struct backlight_device *bd = platform_get_drvdata(pdev);
	struct dp_aux_backlight *aux_bl = bl_get_data(bd);
	struct i2c_adapter *ddc = &aux_bl->aux->ddc;

	if (ddc)
		put_device(&ddc->dev);

	return 0;
}

static const struct of_device_id dp_aux_bl_of_match_table[] = {
	{ .compatible = "dp-aux-backlight"},
	{},
}
MODULE_DEVICE_TABLE(of, dp_aux_bl_of_match_table);

static struct platform_driver dp_aux_backlight_driver = {
	.driver = {
		.name = "dp-aux-backlight",
		.of_match_table = dp_aux_bl_of_match_table,
	},
	.probe = dp_aux_backlight_probe,
	.remove = dp_aux_backlight_remove,

};
module_platform_driver(dp_aux_backlight_driver);

MODULE_DESCRIPTION("DisplayPort aux backlight driver");
MODULE_LICENSE("GPL v2");
