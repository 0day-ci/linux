// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/regulator/consumer.h>
#include <linux/interrupt.h>
#include <linux/pm.h>
#include <linux/input.h>
#include <linux/module.h>

#define CYPRESS_SF_DEV_NAME "cypress-sf"

#define CYPRESS_SF_REG_FW_VERSION	0x46
#define CYPRESS_SF_REG_HW_VERSION	0x48
#define CYPRESS_SF_REG_BUTTON_STATUS	0x4a

struct cypress_sf_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct regulator_bulk_data regulators[2];
	u32 *keycodes;
	unsigned long keystates;
	int num_keys;
};

static irqreturn_t cypress_sf_irq_handler(int irq, void *devid)
{
	struct cypress_sf_data *touchkey = devid;
	unsigned long keystates;
	bool curr_state, new_state;
	int key;

	keystates = i2c_smbus_read_byte_data(touchkey->client,
					CYPRESS_SF_REG_BUTTON_STATUS);
	if (keystates < 0) {
		dev_err(&touchkey->client->dev, "Failed to read button status");
		return IRQ_NONE;
	}

	for(key = 0; key < touchkey->num_keys; ++key) {
		curr_state = test_bit(key, &touchkey->keystates);
		new_state = test_bit(key, &keystates);

		if(curr_state ^ new_state) {
			dev_dbg(&touchkey->client->dev,\
				"Key %d changed to %d", key, new_state);
			input_report_key(touchkey->input_dev,
					touchkey->keycodes[key],
					new_state);
		}
	}
	input_sync(touchkey->input_dev);
	touchkey->keystates = keystates;

	return IRQ_HANDLED;
}

static int cypress_sf_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct cypress_sf_data *touchkey;
	int hw_version, fw_version;
	int ret;
	int i;

	touchkey = devm_kzalloc(&client->dev, sizeof(*touchkey), GFP_KERNEL);
	if(!touchkey)
		return -ENOMEM;

	touchkey->client = client;
	i2c_set_clientdata(client, touchkey);

	touchkey->regulators[0].supply = "vdd";
	touchkey->regulators[1].supply = "avdd";

	ret = devm_regulator_bulk_get(&client->dev,
					ARRAY_SIZE(touchkey->regulators),
					touchkey->regulators);
	if(ret) {
		dev_err(&client->dev, "Failed to get regulators: %d\n", ret);
		return ret;
	}

	touchkey->num_keys = of_property_count_elems_of_size(client->dev.of_node,
							"linux,keycodes",
							sizeof(u32));
	if(touchkey->num_keys < 0)
		/* Default key count */
		touchkey->num_keys = 2;

	touchkey->keycodes = devm_kzalloc(&client->dev,
				sizeof(u32) * touchkey->num_keys, GFP_KERNEL);
	if(!touchkey->keycodes)
		return -ENOMEM;

	ret = of_property_read_u32_array(client->dev.of_node, "linux,keycodes",
						touchkey->keycodes, touchkey->num_keys);

	if(touchkey->num_keys < 0) {
		/* Default keycodes */
		touchkey->keycodes[0] = KEY_BACK;
		touchkey->keycodes[1] = KEY_MENU;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(touchkey->regulators),
					touchkey->regulators);
	if(ret) {
		dev_err(&client->dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	touchkey->input_dev = devm_input_allocate_device(&client->dev);
	if(!touchkey->input_dev) {
		dev_err(&client->dev, "Failed to allocate input device\n");
		return -ENOMEM;
	}

	touchkey->input_dev->name = CYPRESS_SF_DEV_NAME;
	touchkey->input_dev->id.bustype = BUS_I2C;

	hw_version = i2c_smbus_read_byte_data(touchkey->client,
				CYPRESS_SF_REG_HW_VERSION);
	fw_version = i2c_smbus_read_word_data(touchkey->client,
				CYPRESS_SF_REG_FW_VERSION);
	if(hw_version < 0 || fw_version < 0)
		dev_warn(&client->dev, "Failed to read versions\n");
	else
		dev_info(&client->dev, "HW version %d, FW version %d\n",
				hw_version, fw_version);

	for(i = 0; i < touchkey->num_keys; ++i)
		input_set_capability(touchkey->input_dev, EV_KEY,
					touchkey->keycodes[i]);

	ret = input_register_device(touchkey->input_dev);
	if(ret) {
		dev_err(&client->dev,
			"Failed to register input device: %d\n", ret);
		return ret;
	}

	ret = devm_request_threaded_irq(&client->dev, client->irq,
					NULL, cypress_sf_irq_handler,
					IRQF_ONESHOT,
					CYPRESS_SF_DEV_NAME, touchkey);
	if(ret) {
		dev_err(&client->dev,
			"Failed to register threaded irq: %d", ret);
		return ret;
	}

	return 0;
};

static int __maybe_unused cypress_sf_suspend(struct device *dev) {
	struct i2c_client *client = to_i2c_client(dev);
	struct cypress_sf_data *touchkey = i2c_get_clientdata(client);
	int ret;

	disable_irq(client->irq);
	ret = regulator_bulk_disable(ARRAY_SIZE(touchkey->regulators),
					touchkey->regulators);
	if(ret) {
		dev_err(dev, "Failed to disable regulators: %d", ret);
		enable_irq(client->irq);
		return ret;
	}
	dev_dbg(dev, "Suspended device");

	return 0;
}

static int __maybe_unused cypress_sf_resume(struct device *dev) {
	struct i2c_client *client = to_i2c_client(dev);
	struct cypress_sf_data *touchkey = i2c_get_clientdata(client);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(touchkey->regulators),
					touchkey->regulators);
	if(ret) {
		dev_err(dev, "Failed to enable regulators: %d", ret);
		return ret;
	}
	enable_irq(client->irq);
	dev_dbg(dev, "Resumed device");

	return 0;
}

static SIMPLE_DEV_PM_OPS(cypress_sf_pm_ops,
			 cypress_sf_suspend, cypress_sf_resume);

static struct i2c_device_id cypress_sf_id_table[] = {
	{ CYPRESS_SF_DEV_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cypress_sf_id_table);

static const struct of_device_id cypress_sf_of_match[] = {
	{ .compatible = "cypress,sf3155", },
	{ },
};
MODULE_DEVICE_TABLE(of, cypress_sf_of_match);

static struct i2c_driver cypress_sf_driver = {
	.driver = {
		.name = CYPRESS_SF_DEV_NAME,
		.pm = &cypress_sf_pm_ops,
		.of_match_table = of_match_ptr(cypress_sf_of_match),
	},
	.id_table = cypress_sf_id_table,
	.probe = cypress_sf_probe,
};
module_i2c_driver(cypress_sf_driver);

MODULE_AUTHOR("Yassine Oudjana <y.oudjana@protonmail.com>");
MODULE_DESCRIPTION("Cypress StreetFighter Touchkey Driver");
MODULE_LICENSE("GPL v2");