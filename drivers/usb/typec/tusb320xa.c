// SPDX-License-Identifier: GPL-2.0
/*
 * TI TUSB320LA/TUSB320HA Type-C DRP Port controller driver
 *
 * Based on the drivers/extcon/extcon-tusb320.c driver by Michael Auchter.
 *
 * Copyright (c) 2021 Alvin Šipraga <alsi@bang-olufsen.dk>
 * Copyright (c) 2020 Michael Auchter <michael.auchter@ni.com>
 */

#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/usb/role.h>
#include <linux/usb/typec.h>
#include <linux/usb/typec_altmode.h>

#define TUSB320XA_REG8				0x08
#define TUSB320XA_REG8_ACCESSORY_CONNECTED	GENMASK(3, 1)

#define TUSB320XA_REG9				0x09
#define TUSB320XA_REG9_ATTACHED_STATE		GENMASK(7, 6)
#define TUSB320XA_REG9_CABLE_DIR		BIT(5)
#define TUSB320XA_REG9_INTERRUPT_STATUS		BIT(4)

#define TUSB320XA_REGA				0x0A
#define TUSB320XA_REGA_MODE_SELECT		GENMASK(5, 4)
#define TUSB320XA_REGA_I2C_SOFT_RESET		BIT(3)
#define TUSB320XA_REGA_SOURCE_PREF		GENMASK(2, 1)
#define TUSB320XA_REGA_DISABLE_TERM		BIT(0)

enum tusb320xa_accessory {
	TUSB320XA_ACCESSORY_NONE = 0,
	/* 0b001 ~ 0b011 are reserved */
	TUSB320XA_ACCESSORY_AUDIO = 4,
	TUSB320XA_ACCESSORY_AUDIO_CHARGETHRU = 5,
	TUSB320XA_ACCESSORY_DEBUG_DFP = 6,
	TUSB320XA_ACCESSORY_DEBUG_UFP = 7,
};

static const char *const tusb320xa_accessories[] = {
	[TUSB320XA_ACCESSORY_NONE]             = "none",
	[TUSB320XA_ACCESSORY_AUDIO]            = "audio",
	[TUSB320XA_ACCESSORY_AUDIO_CHARGETHRU] = "audio with charge thru",
	[TUSB320XA_ACCESSORY_DEBUG_DFP]        = "debug while DFP",
	[TUSB320XA_ACCESSORY_DEBUG_UFP]        = "debug while UFP",
};

enum tusb320xa_attached_state {
	TUSB320XA_ATTACHED_STATE_NONE = 0,
	TUSB320XA_ATTACHED_STATE_DFP = 1,
	TUSB320XA_ATTACHED_STATE_UFP = 2,
	TUSB320XA_ATTACHED_STATE_ACC = 3,
};

static const char *const tusb320xa_attached_states[] = {
	[TUSB320XA_ATTACHED_STATE_NONE] = "not attached",
	[TUSB320XA_ATTACHED_STATE_DFP]  = "downstream facing port",
	[TUSB320XA_ATTACHED_STATE_UFP]  = "upstream facing port",
	[TUSB320XA_ATTACHED_STATE_ACC]  = "accessory",
};

enum tusb320xa_cable_dir {
	TUSB320XA_CABLE_DIR_CC1 = 0,
	TUSB320XA_CABLE_DIR_CC2 = 1,
};

static const char *const tusb320xa_cable_directions[] = {
	[TUSB320XA_CABLE_DIR_CC1] = "CC1",
	[TUSB320XA_CABLE_DIR_CC2] = "CC2",
};

enum tusb320xa_mode {
	TUSB320XA_MODE_PORT = 0,
	TUSB320XA_MODE_UFP = 1,
	TUSB320XA_MODE_DFP = 2,
	TUSB320XA_MODE_DRP = 3,
};

enum tusb320xa_source_pref {
	TUSB320XA_SOURCE_PREF_DRP = 0,
	TUSB320XA_SOURCE_PREF_TRY_SNK = 1,
	/* 0b10 is reserved */
	TUSB320XA_SOURCE_PREF_TRY_SRC = 3,
};

struct tusb320xa {
	struct device *dev;
	struct regmap *regmap;
	struct typec_port *port;
	struct usb_role_switch *role_sw;
	struct mutex lock;
	int irq;
};

static int tusb320xa_check_signature(struct tusb320xa *tusb)
{
	static const char sig[] = { '\0', 'T', 'U', 'S', 'B', '3', '2', '0' };
	unsigned int val;
	int i, ret;

	mutex_lock(&tusb->lock);

	for (i = 0; i < sizeof(sig); i++) {
		ret = regmap_read(tusb->regmap, sizeof(sig) - 1 - i, &val);
		if (ret)
			goto done;

		if (val != sig[i]) {
			dev_err(tusb->dev, "signature mismatch!\n");
			ret = -ENODEV;
			goto done;
		}
	}

done:
	mutex_unlock(&tusb->lock);

	return ret;
}

static int tusb320xa_reset(struct tusb320xa *tusb)
{
	int ret;

	mutex_lock(&tusb->lock);

	/* Disable CC state machine */
	ret = regmap_write_bits(tusb->regmap, TUSB320XA_REGA,
				TUSB320XA_REGA_DISABLE_TERM,
				FIELD_PREP(TUSB320XA_REGA_DISABLE_TERM, 1));
	if (ret)
		goto done;

	/* Set to DRP by default, overriding any hardwired PORT setting */
	ret = regmap_write_bits(tusb->regmap, TUSB320XA_REGA,
				TUSB320XA_REGA_MODE_SELECT,
				FIELD_PREP(TUSB320XA_REGA_MODE_SELECT,
					   TUSB320XA_MODE_DRP));
	if (ret)
		goto done;

	/* Wait 5 ms per datasheet specification */
	usleep_range(5000, 10000);

	/* Perform soft reset */
	ret = regmap_write_bits(tusb->regmap, TUSB320XA_REGA,
				TUSB320XA_REGA_I2C_SOFT_RESET,
				FIELD_PREP(TUSB320XA_REGA_I2C_SOFT_RESET, 1));
	if (ret)
		goto done;

	/* Wait 95 ms for chip to reset per datasheet specification */
	msleep(95);

	/* Clear any old interrupt status bit */
	ret = regmap_write_bits(tusb->regmap, TUSB320XA_REG9,
				TUSB320XA_REG9_INTERRUPT_STATUS,
				FIELD_PREP(TUSB320XA_REG9_INTERRUPT_STATUS, 1));
	if (ret)
		goto done;

	/* Re-enable CC state machine */
	ret = regmap_write_bits(tusb->regmap, TUSB320XA_REGA,
				TUSB320XA_REGA_DISABLE_TERM,
				FIELD_PREP(TUSB320XA_REGA_DISABLE_TERM, 0));
	if (ret)
		goto done;

done:
	mutex_unlock(&tusb->lock);

	return ret;
}

static int tusb320xa_sync_state(struct tusb320xa *tusb)
{
	enum tusb320xa_attached_state attached_state;
	enum tusb320xa_cable_dir cable_dir;
	enum tusb320xa_accessory accessory;
	enum typec_orientation typec_orientation;
	enum typec_data_role typec_data_role;
	int typec_mode;
	enum usb_role usb_role;
	unsigned int reg8;
	unsigned int reg9;
	int ret;

	ret = regmap_read(tusb->regmap, TUSB320XA_REG8, &reg8);
	if (ret)
		return ret;

	ret = regmap_read(tusb->regmap, TUSB320XA_REG9, &reg9);
	if (ret)
		return ret;

	attached_state = FIELD_GET(TUSB320XA_REG9_ATTACHED_STATE, reg9);
	cable_dir = FIELD_GET(TUSB320XA_REG9_CABLE_DIR, reg9);
	accessory = FIELD_GET(TUSB320XA_REG8_ACCESSORY_CONNECTED, reg8);

	dev_dbg(tusb->dev,
		"attached state: %s, cable direction: %s, accessory: %s\n",
		tusb320xa_attached_states[attached_state],
		tusb320xa_cable_directions[cable_dir],
		tusb320xa_accessories[accessory]);

	if (cable_dir == TUSB320XA_CABLE_DIR_CC1)
		typec_orientation = TYPEC_ORIENTATION_NORMAL;
	else
		typec_orientation = TYPEC_ORIENTATION_REVERSE;

	switch (attached_state) {
	case TUSB320XA_ATTACHED_STATE_NONE:
		typec_orientation = TYPEC_ORIENTATION_NONE;
		typec_data_role = TYPEC_HOST;
		typec_mode = TYPEC_STATE_USB;
		usb_role = USB_ROLE_NONE;
		break;
	case TUSB320XA_ATTACHED_STATE_DFP:
		typec_data_role = TYPEC_HOST;
		typec_mode = TYPEC_STATE_USB;
		usb_role = USB_ROLE_HOST;
		break;
	case TUSB320XA_ATTACHED_STATE_UFP:
		typec_data_role = TYPEC_DEVICE;
		typec_mode = TYPEC_STATE_USB;
		usb_role = USB_ROLE_DEVICE;
		break;
	case TUSB320XA_ATTACHED_STATE_ACC:
		typec_data_role = TYPEC_HOST;
		usb_role = USB_ROLE_HOST;

		if (accessory == TUSB320XA_ACCESSORY_AUDIO ||
		    accessory == TUSB320XA_ACCESSORY_AUDIO_CHARGETHRU) {
			typec_mode = TYPEC_MODE_AUDIO;
		} else if (accessory == TUSB320XA_ACCESSORY_DEBUG_DFP ||
			   accessory == TUSB320XA_ACCESSORY_DEBUG_UFP) {
			typec_mode = TYPEC_MODE_DEBUG;
		} else {
			dev_warn(tusb->dev, "unknown accessory type: %d\n",
				 accessory);
			typec_mode = TYPEC_STATE_USB;
		}

		break;
	}

	typec_set_orientation(tusb->port, typec_orientation);
	typec_set_data_role(tusb->port, typec_data_role);
	typec_set_mode(tusb->port, typec_mode);
	usb_role_switch_set_role(tusb->role_sw, usb_role);

	return ret;
}

static int tusb320xa_set_source_pref(struct tusb320xa *tusb,
				     enum tusb320xa_source_pref pref)
{
	int ret;

	mutex_lock(&tusb->lock);

	ret = regmap_update_bits(tusb->regmap, TUSB320XA_REGA,
				 TUSB320XA_REGA_SOURCE_PREF,
				 FIELD_PREP(TUSB320XA_REGA_SOURCE_PREF, pref));

	mutex_unlock(&tusb->lock);

	return ret;
}

static int tusb320xa_set_mode(struct tusb320xa *tusb, enum tusb320xa_mode mode)
{
	int ret;

	mutex_lock(&tusb->lock);

	/* Disable CC state machine */
	ret = regmap_write_bits(tusb->regmap, TUSB320XA_REGA,
				TUSB320XA_REGA_DISABLE_TERM,
				FIELD_PREP(TUSB320XA_REGA_DISABLE_TERM, 1));
	if (ret)
		goto done;

	/* Set the desired port mode */
	ret = regmap_write_bits(tusb->regmap, TUSB320XA_REGA,
				TUSB320XA_REGA_MODE_SELECT,
				FIELD_PREP(TUSB320XA_REGA_MODE_SELECT, mode));
	if (ret)
		goto done;

	/* Wait 5 ms per datasheet specification */
	usleep_range(5000, 10000);

	/* Re-enable CC state machine */
	ret = regmap_write_bits(tusb->regmap, TUSB320XA_REGA,
				TUSB320XA_REGA_DISABLE_TERM,
				FIELD_PREP(TUSB320XA_REGA_DISABLE_TERM, 0));
	if (ret)
		goto done;

done:
	mutex_unlock(&tusb->lock);

	return ret;
}

static int tusb320xa_try_role(struct typec_port *port, int role)
{
	struct tusb320xa *tusb = typec_get_drvdata(port);
	enum tusb320xa_source_pref pref;

	switch (role) {
	case TYPEC_NO_PREFERRED_ROLE:
		pref = TUSB320XA_SOURCE_PREF_DRP;
		break;
	case TYPEC_SINK:
		pref = TUSB320XA_SOURCE_PREF_TRY_SNK;
		break;
	case TYPEC_SOURCE:
		pref = TUSB320XA_SOURCE_PREF_TRY_SRC;
		break;
	default:
		dev_warn(tusb->dev, "unknown port role %d\n", role);
		return -EINVAL;
	}

	return tusb320xa_set_source_pref(tusb, pref);
}

static int tusb320xa_port_type_set(struct typec_port *port,
				   enum typec_port_type type)
{
	struct tusb320xa *tusb = typec_get_drvdata(port);
	enum tusb320xa_mode mode;

	switch (type) {
	case TYPEC_PORT_SRC:
		mode = TUSB320XA_MODE_DFP;
		break;
	case TYPEC_PORT_SNK:
		mode = TUSB320XA_MODE_UFP;
		break;
	case TYPEC_PORT_DRP:
		mode = TUSB320XA_MODE_DRP;
		break;
	default:
		dev_warn(tusb->dev, "unknown port type %d\n", type);
		return -EINVAL;
	}

	return tusb320xa_set_mode(tusb, mode);
}

static const struct typec_operations tusb320xa_ops = {
	.try_role = tusb320xa_try_role,
	.port_type_set = tusb320xa_port_type_set,
};

static irqreturn_t tusb320xa_irq_handler_thread(int irq, void *dev_id)
{
	struct tusb320xa *tusb = dev_id;
	unsigned int reg;
	int ret;

	mutex_lock(&tusb->lock);

	/* Check interrupt status bit */
	ret = regmap_read(tusb->regmap, TUSB320XA_REG9, &reg);
	if (ret)
		goto unhandled;

	if (!(reg & TUSB320XA_REG9_INTERRUPT_STATUS))
		goto unhandled;

	/* Clear interrupt status bit */
	ret = regmap_write(tusb->regmap, TUSB320XA_REG9, reg);
	if (ret)
		goto unhandled;

	ret = tusb320xa_sync_state(tusb);
	if (ret)
		dev_err_ratelimited(tusb->dev,
				    "failed to sync state in irq: %d\n", ret);

	mutex_unlock(&tusb->lock);

	return IRQ_HANDLED;

unhandled:
	mutex_unlock(&tusb->lock);

	return IRQ_NONE;
}

static const struct regmap_config tusb320xa_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.disable_locking = true,
};

void tusb320xa_action_role_sw_put(void *data)
{
	struct usb_role_switch *role_sw = data;

	usb_role_switch_put(role_sw);
}

void tusb320xa_action_unregister_port(void *data)
{
	struct typec_port *port = data;

	typec_unregister_port(port);
}

static int tusb320xa_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct tusb320xa *tusb;
	struct typec_capability typec_cap = {};
	int ret;

	tusb = devm_kzalloc(&client->dev, sizeof(*tusb), GFP_KERNEL);
	if (!tusb)
		return -ENOMEM;

	tusb->dev = &client->dev;
	mutex_init(&tusb->lock);

	tusb->irq = client->irq;
	if (!tusb->irq)
		return -EINVAL;

	tusb->regmap = devm_regmap_init_i2c(client, &tusb320xa_regmap_config);
	if (IS_ERR(tusb->regmap))
		return PTR_ERR(tusb->regmap);

	tusb->role_sw = usb_role_switch_get(tusb->dev);
	if (IS_ERR(tusb->role_sw))
		return PTR_ERR(tusb->role_sw);

	ret = devm_add_action_or_reset(tusb->dev, tusb320xa_action_role_sw_put,
				       tusb->role_sw);
	if (ret)
		return ret;

	ret = tusb320xa_check_signature(tusb);
	if (ret)
		return ret;

	ret = tusb320xa_reset(tusb);
	if (ret)
		return ret;

	typec_cap.type = TYPEC_PORT_DRP;
	typec_cap.data = TYPEC_PORT_DRD;
	typec_cap.revision = USB_TYPEC_REV_1_1;
	typec_cap.prefer_role = TYPEC_NO_PREFERRED_ROLE;
	typec_cap.accessory[TYPEC_ACCESSORY_NONE] = 1;
	typec_cap.accessory[TYPEC_ACCESSORY_AUDIO] = 1;
	typec_cap.accessory[TYPEC_ACCESSORY_DEBUG] = 1;
	typec_cap.orientation_aware = 1;
	typec_cap.fwnode = dev_fwnode(tusb->dev);
	typec_cap.driver_data = tusb;
	typec_cap.ops = &tusb320xa_ops;

	tusb->port = typec_register_port(tusb->dev, &typec_cap);
	if (IS_ERR(tusb->port))
		return PTR_ERR(tusb->port);

	ret = devm_add_action_or_reset(tusb->dev,
				       tusb320xa_action_unregister_port,
				       tusb->port);
	if (ret)
		return ret;

	ret = devm_request_threaded_irq(tusb->dev, tusb->irq, NULL,
					tusb320xa_irq_handler_thread,
					IRQF_ONESHOT | IRQF_TRIGGER_LOW,
					"tusb320xa", tusb);
	if (ret)
		return ret;

	i2c_set_clientdata(client, tusb);

	return 0;
}

static const struct of_device_id tusb320xa_dt_match[] = {
	{
		.compatible = "ti,tusb320la",
	},
	{
		.compatible = "ti,tusb320ha",
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, tusb320xa_dt_match);

static struct i2c_driver tusb320xa_driver = {
	.driver	= {
		.name = "tusb320xa",
		.of_match_table = tusb320xa_dt_match,
	},
	.probe  = tusb320xa_probe,
};

module_i2c_driver(tusb320xa_driver);

MODULE_AUTHOR("Alvin Šipraga <alsi@bang-olufsen.dk>");
MODULE_DESCRIPTION("TI TUSB320xA USB Type-C controller driver");
MODULE_LICENSE("GPL v2");
