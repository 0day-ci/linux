// SPDX-License-Identifier: GPL-2.0
/*
 * CZ.NIC's Turris Omnia LEDs driver
 *
 * 2020 by Marek Behún <kabel@kernel.org>
 */

#include <linux/i2c.h>
#include <linux/led-class-multicolor.h>
#include <linux/ledtrig-netdev.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/phy.h>
#include "leds.h"

#define OMNIA_BOARD_LEDS	12
#define OMNIA_LED_NUM_CHANNELS	3

#define CMD_LED_MODE		3
#define CMD_LED_MODE_LED(l)	((l) & 0x0f)
#define CMD_LED_MODE_USER	0x10

#define CMD_LED_STATE		4
#define CMD_LED_STATE_LED(l)	((l) & 0x0f)
#define CMD_LED_STATE_ON	0x10

#define CMD_LED_COLOR		5
#define CMD_LED_SET_BRIGHTNESS	7
#define CMD_LED_GET_BRIGHTNESS	8

#define MII_MARVELL_LED_PAGE		0x03
#define MII_PHY_LED_CTRL		0x10
#define MII_PHY_LED_TCR			0x12
#define MII_PHY_LED_TCR_PULSESTR_MASK	0x7000
#define MII_PHY_LED_TCR_PULSESTR_SHIFT	12
#define MII_PHY_LED_TCR_BLINKRATE_MASK	0x0700
#define MII_PHY_LED_TCR_BLINKRATE_SHIFT	8

struct omnia_led {
	struct led_classdev_mc mc_cdev;
	struct mc_subled subled_info[OMNIA_LED_NUM_CHANNELS];
	int reg;
	struct device_node *trig_src_np;
	struct phy_device *phydev;
};

#define to_omnia_led(l)		container_of(l, struct omnia_led, mc_cdev)

struct omnia_leds {
	struct i2c_client *client;
	struct mutex lock;
	int count;
	struct omnia_led leds[];
};

static int omnia_led_brightness_set(struct i2c_client *client,
				    struct omnia_led *led,
				    enum led_brightness brightness)
{
	u8 buf[5], state;
	int ret;

	led_mc_calc_color_components(&led->mc_cdev, brightness);

	buf[0] = CMD_LED_COLOR;
	buf[1] = led->reg;
	buf[2] = led->mc_cdev.subled_info[0].brightness;
	buf[3] = led->mc_cdev.subled_info[1].brightness;
	buf[4] = led->mc_cdev.subled_info[2].brightness;

	state = CMD_LED_STATE_LED(led->reg);
	if (buf[2] || buf[3] || buf[4])
		state |= CMD_LED_STATE_ON;

	ret = i2c_smbus_write_byte_data(client, CMD_LED_STATE, state);
	if (ret >= 0 && (state & CMD_LED_STATE_ON))
		ret = i2c_master_send(client, buf, 5);

	return ret < 0 ? ret : 0;
}

static int omnia_led_brightness_set_blocking(struct led_classdev *cdev,
					     enum led_brightness brightness)
{
	struct led_classdev_mc *mc_cdev = lcdev_to_mccdev(cdev);
	struct omnia_leds *leds = dev_get_drvdata(cdev->dev->parent);
	struct omnia_led *led = to_omnia_led(mc_cdev);
	int ret;

	mutex_lock(&leds->lock);

	ret = omnia_led_brightness_set(leds->client, led, brightness);

	mutex_unlock(&leds->lock);

	return ret;
}

static int omnia_led_set_sw_mode(struct i2c_client *client, int led, bool sw)
{
	return i2c_smbus_write_byte_data(client, CMD_LED_MODE,
					 CMD_LED_MODE_LED(led) |
					 (sw ? CMD_LED_MODE_USER : 0));
}

static int wan_led_round_blink_rate(unsigned long *period)
{
	/* Each interval is (0.7 * p, 1.3 * p), where p is the period supported
	 * by the chip. Should we change this so that there are no holes between
	 * these intervals?
	 */
	switch (*period) {
	case 29 ... 55:
		*period = 42;
		return 0;
	case 58 ... 108:
		*period = 84;
		return 1;
	case 119 ... 221:
		*period = 170;
		return 2;
	case 238 ... 442:
		*period = 340;
		return 3;
	case 469 ... 871:
		*period = 670;
		return 4;
	default:
		return -EOPNOTSUPP;
	}
}

static int omnia_led_trig_offload_wan(struct omnia_leds *leds,
				      struct omnia_led *led,
				      struct led_netdev_data *trig)
{
	unsigned long period;
	int ret, blink_rate;
	bool link, rx, tx;
	u8 fun;

	/* HW offload on WAN port is supported only via internal PHY */
	if (trig->net_dev->sfp_bus || !trig->net_dev->phydev)
		return -EOPNOTSUPP;

	link = test_bit(NETDEV_LED_LINK, &trig->mode);
	rx = test_bit(NETDEV_LED_RX, &trig->mode);
	tx = test_bit(NETDEV_LED_TX, &trig->mode);

	if (link && rx && tx)
		fun = 0x1;
	else if (!link && rx && tx)
		fun = 0x4;
	else
		return -EOPNOTSUPP;

	period = jiffies_to_msecs(atomic_read(&trig->interval)) * 2;
	blink_rate = wan_led_round_blink_rate(&period);
	if (blink_rate < 0)
		return blink_rate;

	mutex_lock(&leds->lock);

	if (!led->phydev) {
		led->phydev = trig->net_dev->phydev;
		get_device(&led->phydev->mdio.dev);
	}

	/* set PHY's LED[0] pin to blink according to trigger setting */
	ret = phy_modify_paged(led->phydev, MII_MARVELL_LED_PAGE,
			       MII_PHY_LED_TCR,
			       MII_PHY_LED_TCR_PULSESTR_MASK |
			       MII_PHY_LED_TCR_BLINKRATE_MASK,
			       (0 << MII_PHY_LED_TCR_PULSESTR_SHIFT) |
			       (blink_rate << MII_PHY_LED_TCR_BLINKRATE_SHIFT));
	if (ret)
		goto unlock;

	ret = phy_modify_paged(led->phydev, MII_MARVELL_LED_PAGE,
			       MII_PHY_LED_CTRL, 0xf, fun);
	if (ret)
		goto unlock;

	/* put the LED into HW mode */
	ret = omnia_led_set_sw_mode(leds->client, led->reg, false);
	if (ret)
		goto unlock;

	/* set blinking brightness according to led_cdev->blink_brighness */
	ret = omnia_led_brightness_set(leds->client, led,
				       led->mc_cdev.led_cdev.blink_brightness);
	if (ret)
		goto unlock;

	atomic_set(&trig->interval, msecs_to_jiffies(period / 2));

unlock:
	mutex_unlock(&leds->lock);

	if (ret)
		dev_err(led->mc_cdev.led_cdev.dev,
			"Error offloading trigger: %d\n", ret);

	return ret;
}

static int omnia_led_trig_offload_off(struct omnia_leds *leds,
				      struct omnia_led *led)
{
	int ret;

	if (!led->phydev)
		return 0;

	mutex_lock(&leds->lock);

	/* set PHY's LED[0] pin to default values */
	ret = phy_modify_paged(led->phydev, MII_MARVELL_LED_PAGE,
			       MII_PHY_LED_TCR,
			       MII_PHY_LED_TCR_PULSESTR_MASK |
			       MII_PHY_LED_TCR_BLINKRATE_MASK,
			       (4 << MII_PHY_LED_TCR_PULSESTR_SHIFT) |
			       (1 << MII_PHY_LED_TCR_BLINKRATE_SHIFT));

	ret = phy_modify_paged(led->phydev, MII_MARVELL_LED_PAGE,
			       MII_PHY_LED_CTRL, 0xf, 0xe);

	/*
	 * Return to software controlled mode, but only if we aren't being
	 * called from led_classdev_unregister.
	 */
	if (!(led->mc_cdev.led_cdev.flags & LED_UNREGISTERING))
		ret = omnia_led_set_sw_mode(leds->client, led->reg, true);

	put_device(&led->phydev->mdio.dev);
	led->phydev = NULL;

	mutex_unlock(&leds->lock);

	return 0;
}

static int omnia_led_trig_offload(struct led_classdev *cdev, bool enable)
{
	struct omnia_leds *leds = dev_get_drvdata(cdev->dev->parent);
	struct led_classdev_mc *mc_cdev = lcdev_to_mccdev(cdev);
	struct omnia_led *led = to_omnia_led(mc_cdev);
	struct led_netdev_data *trig;
	int ret = -EOPNOTSUPP;

	if (!enable)
		return omnia_led_trig_offload_off(leds, led);

	if (!led->trig_src_np)
		goto end;

	/* only netdev trigger offloading is supported currently */
	if (strcmp(cdev->trigger->name, "netdev"))
		goto end;

	trig = led_get_trigger_data(cdev);

	if (!trig->net_dev)
		goto end;

	if (dev_of_node(trig->net_dev->dev.parent) != led->trig_src_np)
		goto end;

	ret = omnia_led_trig_offload_wan(leds, led, trig);

end:
	/*
	 * if offloading failed (parameters not supported by HW), ensure any
	 * previous offloading is disabled
	 */
	if (ret)
		omnia_led_trig_offload_off(leds, led);

	return ret;
}

static int read_trigger_sources(struct omnia_led *led, struct device_node *np)
{
	struct of_phandle_args args;
	int ret;

	ret = of_count_phandle_with_args(np, "trigger-sources",
					 "#trigger-source-cells");
	if (ret < 0)
		return ret == -ENOENT ? 0 : ret;

	if (!ret)
		return 0;

	ret = of_parse_phandle_with_args(np, "trigger-sources",
					 "#trigger-source-cells", 0, &args);
	if (ret)
		return ret;

	if (of_device_is_compatible(args.np, "marvell,armada-370-neta"))
		led->trig_src_np = args.np;
	else
		of_node_put(args.np);

	return 0;
}

static int omnia_led_register(struct i2c_client *client, struct omnia_led *led,
			      struct device_node *np)
{
	struct led_init_data init_data = {};
	struct device *dev = &client->dev;
	struct led_classdev *cdev;
	int ret, color;

	ret = of_property_read_u32(np, "reg", &led->reg);
	if (ret || led->reg >= OMNIA_BOARD_LEDS) {
		dev_warn(dev,
			 "Node %pOF: must contain 'reg' property with values between 0 and %i\n",
			 np, OMNIA_BOARD_LEDS - 1);
		return 0;
	}

	ret = of_property_read_u32(np, "color", &color);
	if (ret || color != LED_COLOR_ID_RGB) {
		dev_warn(dev,
			 "Node %pOF: must contain 'color' property with value LED_COLOR_ID_RGB\n",
			 np);
		return 0;
	}

	ret = read_trigger_sources(led, np);
	if (ret) {
		dev_warn(dev, "Node %pOF: failed reading trigger sources: %d\n",
			 np, ret);
		return 0;
	}

	led->subled_info[0].color_index = LED_COLOR_ID_RED;
	led->subled_info[0].channel = 0;
	led->subled_info[0].intensity = 255;
	led->subled_info[1].color_index = LED_COLOR_ID_GREEN;
	led->subled_info[1].channel = 1;
	led->subled_info[1].intensity = 255;
	led->subled_info[2].color_index = LED_COLOR_ID_BLUE;
	led->subled_info[2].channel = 2;
	led->subled_info[2].intensity = 255;

	led->mc_cdev.subled_info = led->subled_info;
	led->mc_cdev.num_colors = OMNIA_LED_NUM_CHANNELS;

	init_data.fwnode = &np->fwnode;

	cdev = &led->mc_cdev.led_cdev;
	cdev->max_brightness = 255;
	cdev->brightness_set_blocking = omnia_led_brightness_set_blocking;
	if (led->trig_src_np)
		cdev->trigger_offload = omnia_led_trig_offload;

	/* put the LED into software mode */
	ret = omnia_led_set_sw_mode(client, led->reg, true);
	if (ret < 0) {
		dev_err(dev, "Cannot set LED %pOF to software mode: %i\n", np,
			ret);
		return ret;
	}

	/* disable the LED */
	ret = i2c_smbus_write_byte_data(client, CMD_LED_STATE,
					CMD_LED_STATE_LED(led->reg));
	if (ret < 0) {
		dev_err(dev, "Cannot set LED %pOF brightness: %i\n", np, ret);
		return ret;
	}

	ret = devm_led_classdev_multicolor_register_ext(dev, &led->mc_cdev,
							&init_data);
	if (ret < 0) {
		dev_err(dev, "Cannot register LED %pOF: %i\n", np, ret);
		return ret;
	}

	return 1;
}

/*
 * On the front panel of the Turris Omnia router there is also a button which
 * can be used to control the intensity of all the LEDs at once, so that if they
 * are too bright, user can dim them.
 * The microcontroller cycles between 8 levels of this global brightness (from
 * 100% to 0%), but this setting can have any integer value between 0 and 100.
 * It is therefore convenient to be able to change this setting from software.
 * We expose this setting via a sysfs attribute file called "brightness". This
 * file lives in the device directory of the LED controller, not an individual
 * LED, so it should not confuse users.
 */
static ssize_t brightness_show(struct device *dev, struct device_attribute *a,
			       char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct omnia_leds *leds = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&leds->lock);
	ret = i2c_smbus_read_byte_data(client, CMD_LED_GET_BRIGHTNESS);
	mutex_unlock(&leds->lock);

	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", ret);
}

static ssize_t brightness_store(struct device *dev, struct device_attribute *a,
				const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct omnia_leds *leds = i2c_get_clientdata(client);
	unsigned long brightness;
	int ret;

	if (kstrtoul(buf, 10, &brightness))
		return -EINVAL;

	if (brightness > 100)
		return -EINVAL;

	mutex_lock(&leds->lock);
	ret = i2c_smbus_write_byte_data(client, CMD_LED_SET_BRIGHTNESS,
					(u8)brightness);
	mutex_unlock(&leds->lock);

	if (ret < 0)
		return ret;

	return count;
}
static DEVICE_ATTR_RW(brightness);

static struct attribute *omnia_led_controller_attrs[] = {
	&dev_attr_brightness.attr,
	NULL,
};
ATTRIBUTE_GROUPS(omnia_led_controller);

static int omnia_leds_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *np = dev_of_node(dev), *child;
	struct omnia_leds *leds;
	struct omnia_led *led;
	int ret, count;

	count = of_get_available_child_count(np);
	if (!count) {
		dev_err(dev, "LEDs are not defined in device tree!\n");
		return -ENODEV;
	} else if (count > OMNIA_BOARD_LEDS) {
		dev_err(dev, "Too many LEDs defined in device tree!\n");
		return -EINVAL;
	}

	leds = devm_kzalloc(dev, struct_size(leds, leds, count), GFP_KERNEL);
	if (!leds)
		return -ENOMEM;

	leds->client = client;
	i2c_set_clientdata(client, leds);

	mutex_init(&leds->lock);

	led = &leds->leds[0];
	for_each_available_child_of_node(np, child) {
		ret = omnia_led_register(client, led, child);
		if (ret < 0) {
			of_node_put(child);
			return ret;
		}

		led += ret;
		++leds->count;
	}

	if (devm_device_add_groups(dev, omnia_led_controller_groups))
		dev_warn(dev, "Could not add attribute group!\n");

	return 0;
}

static int omnia_leds_remove(struct i2c_client *client)
{
	struct omnia_leds *leds = i2c_get_clientdata(client);
	struct omnia_led *led;
	u8 buf[5];

	/* put away trigger source OF nodes */
	for (led = &leds->leds[0]; led < &leds->leds[leds->count]; ++led)
		if (led->trig_src_np)
			of_node_put(led->trig_src_np);

	/* put all LEDs into default (HW triggered) mode */
	omnia_led_set_sw_mode(client, OMNIA_BOARD_LEDS, false);

	/* set all LEDs color to [255, 255, 255] */
	buf[0] = CMD_LED_COLOR;
	buf[1] = OMNIA_BOARD_LEDS;
	buf[2] = 255;
	buf[3] = 255;
	buf[4] = 255;

	i2c_master_send(client, buf, 5);

	return 0;
}

static const struct of_device_id of_omnia_leds_match[] = {
	{ .compatible = "cznic,turris-omnia-leds", },
	{},
};

static const struct i2c_device_id omnia_id[] = {
	{ "omnia", 0 },
	{ }
};

static struct i2c_driver omnia_leds_driver = {
	.probe		= omnia_leds_probe,
	.remove		= omnia_leds_remove,
	.id_table	= omnia_id,
	.driver		= {
		.name	= "leds-turris-omnia",
		.of_match_table = of_omnia_leds_match,
	},
};

module_i2c_driver(omnia_leds_driver);

MODULE_AUTHOR("Marek Behun <kabel@kernel.org>");
MODULE_DESCRIPTION("CZ.NIC's Turris Omnia LEDs");
MODULE_LICENSE("GPL v2");
