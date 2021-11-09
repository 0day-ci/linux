// SPDX-License-Identifier: GPL-2.0
#include <net/dsa.h>

#include "qca8k.h"

static int
qca8k_get_enable_led_reg(int port_num, int led_num, struct qca8k_led_pattern_en *reg_info)
{
	int shift;

	switch (port_num) {
	case 0:
		reg_info->reg = QCA8K_LED_CTRL_REG(led_num);
		reg_info->shift = 14;
		break;
	case 1:
	case 2:
	case 3:
		reg_info->reg = QCA8K_LED_CTRL_REG(3);
		shift = 2 * led_num + (6 * (port_num - 1));

		reg_info->shift = 8 + shift;

		break;
	case 4:
		reg_info->reg = QCA8K_LED_CTRL_REG(led_num);
		reg_info->shift = 30;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
qca8k_get_control_led_reg(int port_num, int led_num, struct qca8k_led_pattern_en *reg_info)
{
	reg_info->reg = QCA8K_LED_CTRL_REG(led_num);

	/* 6 total control rule:
	 * 3 control rules for phy0-3 that applies to all their leds
	 * 3 control rules for phy4
	 */
	if (port_num == 4)
		reg_info->shift = 16;
	else
		reg_info->shift = 0;

	return 0;
}

static int
qca8k_parse_hardware_phy_activity(unsigned long rules, u32 *offload_trigger,
				  u32 *mask)
{
	/* Parsing specific to hardware-phy-activity trigger */
	if (test_bit(BLINK_TX, &rules))
		*offload_trigger = QCA8K_LED_TX_BLINK_MASK;
	if (test_bit(BLINK_RX, &rules))
		*offload_trigger = QCA8K_LED_RX_BLINK_MASK;
	if (test_bit(KEEP_LINK_10M, &rules))
		*offload_trigger = QCA8K_LED_LINK_10M_EN_MASK;
	if (test_bit(KEEP_LINK_100M, &rules))
		*offload_trigger = QCA8K_LED_LINK_100M_EN_MASK;
	if (test_bit(KEEP_LINK_1000M, &rules))
		*offload_trigger = QCA8K_LED_LINK_1000M_EN_MASK;
	if (test_bit(KEEP_HALF_DUPLEX, &rules))
		*offload_trigger = QCA8K_LED_HALF_DUPLEX_MASK;
	if (test_bit(KEEP_FULL_DUPLEX, &rules))
		*offload_trigger = QCA8K_LED_FULL_DUPLEX_MASK;
	if (test_bit(OPTION_LINKUP_OVER, &rules))
		*offload_trigger = QCA8K_LED_LINKUP_OVER_MASK;
	if (test_bit(OPTION_BLINK_2HZ, &rules))
		*offload_trigger = QCA8K_LED_BLINK_2HZ;
	if (test_bit(OPTION_BLINK_4HZ, &rules))
		*offload_trigger = QCA8K_LED_BLINK_4HZ;
	if (test_bit(OPTION_BLINK_8HZ, &rules))
		*offload_trigger = QCA8K_LED_BLINK_8HZ;

	if (!*offload_trigger)
		return -EOPNOTSUPP;

	if (test_bit(OPTION_BLINK_2HZ, &rules) ||
	    test_bit(OPTION_BLINK_4HZ, &rules) ||
	    test_bit(OPTION_BLINK_8HZ, &rules)) {
		*mask = QCA8K_LED_BLINK_FREQ_MASK;
	} else {
		*mask = *offload_trigger;
	}

	return 0;
}

static int
qca8k_parse_netdev(unsigned long rules, u32 *offload_trigger, u32 *mask)
{
	/* Parsing specific to netdev trigger */
	if (test_bit(TRIGGER_NETDEV_LINK, &rules))
		*offload_trigger = QCA8K_LED_LINK_10M_EN_MASK |
				   QCA8K_LED_LINK_100M_EN_MASK |
				   QCA8K_LED_LINK_1000M_EN_MASK;
	if (test_bit(TRIGGER_NETDEV_TX, &rules))
		*offload_trigger = QCA8K_LED_TX_BLINK_MASK;
	if (test_bit(TRIGGER_NETDEV_RX, &rules))
		*offload_trigger = QCA8K_LED_RX_BLINK_MASK;

	if (!*offload_trigger)
		return -EOPNOTSUPP;

	*mask = *offload_trigger;

	return 0;
}

static int
qca8k_cled_hw_control_configure(struct led_classdev *ldev, unsigned long rules,
				enum blink_mode_cmd cmd)
{
	struct qca8k_led *led = container_of(ldev, struct qca8k_led, cdev);
	struct led_trigger *trigger = ldev->trigger;
	struct qca8k_led_pattern_en reg_info;
	struct qca8k_priv *priv = led->priv;
	u32 offload_trigger = 0, mask, val;
	int ret;

	/* Check trigger compatibility */
	if (strcmp(trigger->name, "hardware-phy-activity") &&
	    strcmp(trigger->name, "netdev"))
		return -EOPNOTSUPP;

	if (!strcmp(trigger->name, "hardware-phy-activity"))
		ret = qca8k_parse_hardware_phy_activity(rules, &offload_trigger, &mask);

	if (!strcmp(trigger->name, "netdev"))
		ret = qca8k_parse_netdev(rules, &offload_trigger, &mask);

	if (ret)
		return ret;

	qca8k_get_control_led_reg(led->port_num, led->led_num, &reg_info);

	switch (cmd) {
	case BLINK_MODE_SUPPORTED:
		/* We reache this point, we are sure the trigger is supported */
		return 1;
	case BLINK_MODE_ZERO:
		/* We set 4hz by default */
		u32 default_reg = QCA8K_LED_BLINK_4HZ;

		ret = qca8k_rmw(priv, reg_info.reg,
				QCA8K_LED_RULE_MASK << reg_info.shift,
				default_reg << reg_info.shift);
		break;
	case BLINK_MODE_ENABLE:
		ret = qca8k_rmw(priv, reg_info.reg,
				mask << reg_info.shift,
				offload_trigger << reg_info.shift);
		break;
	case BLINK_MODE_DISABLE:
		ret = qca8k_rmw(priv, reg_info.reg,
				mask << reg_info.shift,
				0);
		break;
	case BLINK_MODE_READ:
		ret = qca8k_read(priv, reg_info.reg, &val);
		if (ret)
			return ret;

		val >>= reg_info.shift;
		val &= offload_trigger;

		/* Special handling for LED_BLINK_2HZ */
		if (!val && offload_trigger == QCA8K_LED_BLINK_2HZ)
			val = 1;

		return val;
	default:
		return -EOPNOTSUPP;
	}

	return ret;
}

static void
qca8k_led_brightness_set(struct qca8k_led *led,
			 enum led_brightness b)
{
	struct qca8k_led_pattern_en reg_info;
	struct qca8k_priv *priv = led->priv;
	u32 val = QCA8K_LED_ALWAYS_OFF;

	qca8k_get_enable_led_reg(led->port_num, led->led_num, &reg_info);

	if (b)
		val = QCA8K_LED_ALWAYS_ON;

	qca8k_rmw(priv, reg_info.reg,
		  GENMASK(1, 0) << reg_info.shift,
		  val << reg_info.shift);
}

static void
qca8k_cled_brightness_set(struct led_classdev *ldev,
			  enum led_brightness b)
{
	struct qca8k_led *led = container_of(ldev, struct qca8k_led, cdev);

	return qca8k_led_brightness_set(led, b);
}

static enum led_brightness
qca8k_led_brightness_get(struct qca8k_led *led)
{
	struct qca8k_led_pattern_en reg_info;
	struct qca8k_priv *priv = led->priv;
	u32 val;
	int ret;

	qca8k_get_enable_led_reg(led->port_num, led->led_num, &reg_info);

	ret = qca8k_read(priv, reg_info.reg, &val);
	if (ret)
		return 0;

	val >>= reg_info.shift;
	val &= GENMASK(1, 0);

	return val > 0 ? 1 : 0;
}

static enum led_brightness
qca8k_cled_brightness_get(struct led_classdev *ldev)
{
	struct qca8k_led *led = container_of(ldev, struct qca8k_led, cdev);

	return qca8k_led_brightness_get(led);
}

static int
qca8k_cled_blink_set(struct led_classdev *ldev,
		     unsigned long *delay_on,
		     unsigned long *delay_off)
{
	struct qca8k_led *led = container_of(ldev, struct qca8k_led, cdev);
	struct qca8k_led_pattern_en reg_info;
	struct qca8k_priv *priv = led->priv;

	if (*delay_on == 0 && *delay_off == 0) {
		*delay_on = 125;
		*delay_off = 125;
	}

	if (*delay_on != 125 || *delay_off != 125) {
		/* The hardware only supports blinking at 4Hz. Fall back
		 * to software implementation in other cases.
		 */
		return -EINVAL;
	}

	qca8k_get_enable_led_reg(led->port_num, led->led_num, &reg_info);

	qca8k_rmw(priv, reg_info.reg,
		  GENMASK(1, 0) << reg_info.shift,
		  QCA8K_LED_ALWAYS_BLINK_4HZ << reg_info.shift);

	return 0;
}

static int
qca8k_cled_trigger_offload(struct led_classdev *ldev, bool enable)
{
	struct qca8k_led *led = container_of(ldev, struct qca8k_led, cdev);

	struct qca8k_led_pattern_en reg_info;
	struct qca8k_priv *priv = led->priv;
	u32 val = QCA8K_LED_ALWAYS_OFF;

	qca8k_get_enable_led_reg(led->port_num, led->led_num, &reg_info);

	if (enable)
		val = QCA8K_LED_RULE_CONTROLLED;

	return qca8k_rmw(priv, reg_info.reg,
			 GENMASK(1, 0) << reg_info.shift,
			 val << reg_info.shift);
}

static int
qca8k_cled_hw_control_start(struct led_classdev *led_cdev)
{
	return qca8k_cled_trigger_offload(led_cdev, true);
}

static int
qca8k_cled_hw_control_stop(struct led_classdev *led_cdev)
{
	return qca8k_cled_trigger_offload(led_cdev, false);
}

static bool
qca8k_cled_hw_control_status(struct led_classdev *ldev)
{
	struct qca8k_led *led = container_of(ldev, struct qca8k_led, cdev);

	struct qca8k_led_pattern_en reg_info;
	struct qca8k_priv *priv = led->priv;
	u32 val;

	qca8k_get_enable_led_reg(led->port_num, led->led_num, &reg_info);

	qca8k_read(priv, reg_info.reg, &val);

	val >>= reg_info.shift;
	val &= GENMASK(1, 0);

	return val == QCA8K_LED_RULE_CONTROLLED;
}

static int
qca8k_parse_port_leds(struct qca8k_priv *priv, struct fwnode_handle *port, int port_num)
{
	struct fwnode_handle *led = NULL, *leds = NULL;
	struct led_init_data init_data = { };
	struct qca8k_led *port_led;
	int led_num, port_index;
	const char *state;
	int ret;

	leds = fwnode_get_named_child_node(port, "leds");
	if (!leds) {
		dev_dbg(priv->dev, "No Leds node specified in device tree for port %d!\n",
			port_num);
		return 0;
	}

	fwnode_for_each_child_node(leds, led) {
		/* Reg rapresent the led number of the port.
		 * Each port can have at least 3 leds attached
		 * Commonly:
		 * 1. is gigabit led
		 * 2. is mbit led
		 * 3. additional status led
		 */
		if (fwnode_property_read_u32(led, "reg", &led_num))
			continue;

		if (led_num >= QCA8K_LED_PORT_COUNT) {
			dev_warn(priv->dev, "Invalid LED reg defined %d", port_num);
			continue;
		}

		port_index = 3 * port_num + led_num;

		port_led = &priv->ports_led[port_index];
		port_led->port_num = port_num;
		port_led->led_num = led_num;
		port_led->priv = priv;

		ret = fwnode_property_read_string(led, "default-state", &state);
		if (!ret) {
			if (!strcmp(state, "on")) {
				port_led->cdev.brightness = 1;
				qca8k_led_brightness_set(port_led, 1);
			} else if (!strcmp(state, "off")) {
				port_led->cdev.brightness = 0;
				qca8k_led_brightness_set(port_led, 0);
			} else if (!strcmp(state, "keep")) {
				port_led->cdev.brightness =
					qca8k_led_brightness_get(port_led);
			}
		}

		/* 3 brightness settings can be applied from Documentation:
		 * 0 always off
		 * 1 blink at 4Hz
		 * 2 always on
		 * 3 rule controlled
		 * Suppots only 2 mode: (pcb limitation, with always on and blink
		 * only the last led is set to this mode)
		 * 0 always off (sets all leds off)
		 * 3 rule controlled
		 */
		port_led->cdev.blink_mode = SOFTWARE_HARDWARE_CONTROLLED;
		port_led->cdev.max_brightness = 1;
		port_led->cdev.brightness_set = qca8k_cled_brightness_set;
		port_led->cdev.brightness_get = qca8k_cled_brightness_get;
		port_led->cdev.blink_set = qca8k_cled_blink_set;
		port_led->cdev.hw_control_start = qca8k_cled_hw_control_start;
		port_led->cdev.hw_control_stop = qca8k_cled_hw_control_stop;
		port_led->cdev.hw_control_status = qca8k_cled_hw_control_status;
		port_led->cdev.hw_control_configure = qca8k_cled_hw_control_configure;
		init_data.default_label = ":port";
		init_data.devicename = "qca8k";
		init_data.fwnode = led;

		ret = devm_led_classdev_register_ext(priv->dev, &port_led->cdev, &init_data);
		if (ret)
			dev_warn(priv->dev, "Failed to int led");
	}

	return 0;
}

int
qca8k_setup_led_ctrl(struct qca8k_priv *priv)
{
	struct fwnode_handle *mdio, *port;
	int port_num;
	int ret;

	mdio = device_get_named_child_node(priv->dev, "mdio");
	if (!mdio) {
		dev_info(priv->dev, "No MDIO node specified in device tree!\n");
		return 0;
	}

	fwnode_for_each_child_node(mdio, port) {
		if (fwnode_property_read_u32(port, "reg", &port_num))
			continue;

		/* Each port can have at least 3 different leds attached */
		ret = qca8k_parse_port_leds(priv, port, port_num);
		if (ret)
			return ret;
	}

	return 0;
}
