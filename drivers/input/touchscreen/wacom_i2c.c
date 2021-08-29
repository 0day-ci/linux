// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Wacom Penabled Driver for I2C
 *
 * Copyright (c) 2011 - 2013 Tatsunosuke Tobita, Wacom.
 * <tobita.tatsunosuke@wacom.co.jp>
 */

#include <linux/module.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/of.h>
#include <asm/unaligned.h>

// Bitmasks (for data[3])
#define WACOM_TIP_SWITCH_bm         (1 << 0)
#define WACOM_BARREL_SWITCH_bm      (1 << 1)
#define WACOM_ERASER_bm             (1 << 2)
#define WACOM_INVERT_bm             (1 << 3)
#define WACOM_BARREL_SWITCH_2_bm    (1 << 4)
#define WACOM_IN_RANGE_bm           (1 << 5)

// Registers
#define WACOM_DESC_REG	0x01
#define WACOM_COMMAND_LSB   0x04
#define WACOM_COMMAND_MSB   0x00

#define WACOM_DATA_LSB      0x05
#define WACOM_DATA_MSB      0x00

// Report types
#define REPORT_FEATURE      0x30

// Requests / operations
#define OPCODE_GET_REPORT   0x02

// Power settings
#define POWER_ON            0x00
#define POWER_SLEEP         0x01

// Input report ids
#define WACOM_PEN_DATA_REPORT           2
#define WACOM_SHINONOME_REPORT          26

#define WACOM_QUERY_REPORT	3
#define WACOM_QUERY_SIZE	22

#define WACOM_MAX_DATA_SIZE_BG9     10
#define WACOM_MAX_DATA_SIZE_G12     15
#define WACOM_MAX_DATA_SIZE_AG14    17
#define WACOM_MAX_DATA_SIZE         22

/* Generation selction */
/* Before and at G9 generation */
#define WACOM_BG9	0
/* G12 generation the IC supports "height"*/
#define WACOM_G12	1
/* After and at G14 generation the IC supports "height" and
 * it is defined as "Z" axis
 */
#define WACOM_AG14	2

struct wacom_desc {
	u16 descLen;
	u16 version;
	u16 reportLen;
	u16 reportReg;
	u16 inputReg;
	u16 maxInputLen;
	u16 outputReg;
	u16 maxOutputLen;
	u16 commReg;
	u16 dataReg;
	u16 vendorID;
	u16 productID;
	u16 fwVersion;
	u16 misc_high;
	u16 misc_low;
};

struct wacom_features {
	struct wacom_desc desc;
	int x_max;
	int y_max;
	int pressure_max;
	int distance_max;
	int tilt_x_max;
	int tilt_y_max;
	char fw_version;
	unsigned char generation;
};

struct wacom_i2c {
	struct i2c_client *client;
	struct input_dev *input;
	struct touchscreen_properties props;
	struct wacom_features features;
	struct regulator *vdd;
	u8 data[WACOM_QUERY_SIZE];
	bool prox;
	int tool;
};

static int wacom_query_device(struct i2c_client *client,
			      struct wacom_features *features)
{
	int ret;
	u8 cmd_wac_desc[] = {WACOM_DESC_REG, 0x00};
	u8 data[WACOM_QUERY_SIZE];

	u8 get_query_data_cmd[] = {
		WACOM_COMMAND_LSB,
		WACOM_COMMAND_MSB,
		REPORT_FEATURE | WACOM_QUERY_REPORT,
		OPCODE_GET_REPORT,
		WACOM_DATA_LSB,
		WACOM_DATA_MSB,
	};

	struct i2c_msg msgs[] = {
		// Request reading of feature ReportID: 3 (Pen Query Data)
		{
			.addr = client->addr,
			.flags = 0,
			.len = sizeof(get_query_data_cmd),
			.buf = get_query_data_cmd,
		},
		// Read 21 bytes
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = WACOM_QUERY_SIZE - 1,
			.buf = data,
		},
	};

	/* Read the description register */
	ret = i2c_master_send(client, cmd_wac_desc, sizeof(cmd_wac_desc));
	if (ret < 0)
		return ret;
	ret = i2c_master_recv(client, (char *)&features->desc, sizeof(features->desc));
	if (ret < 0)
		return ret;

	switch (features->desc.maxInputLen) {
	case WACOM_MAX_DATA_SIZE_BG9:
		features->generation = WACOM_BG9;
		break;

	case WACOM_MAX_DATA_SIZE_G12:
		features->generation = WACOM_G12;
		break;

	case WACOM_MAX_DATA_SIZE_AG14:
		features->generation = WACOM_AG14;
		break;

	default:
		/* Cover all generations possible */
		features->generation = WACOM_AG14;
		break;
	}

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret < 0)
		return ret;
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	features->x_max = get_unaligned_le16(&data[3]);
	features->y_max = get_unaligned_le16(&data[5]);
	features->pressure_max = get_unaligned_le16(&data[11]);
	features->fw_version = get_unaligned_le16(&data[13]);
	if (features->generation) {
		features->distance_max = data[16];
		features->tilt_x_max = get_unaligned_le16(&data[17]);
		features->tilt_y_max = get_unaligned_le16(&data[19]);
	} else {
		features->distance_max = -1;
		features->tilt_x_max = -1;
		features->tilt_y_max = -1;
	}

	dev_dbg(&client->dev,
		"x_max:%d, y_max:%d, pressure:%d, fw:%d, "
		"distance: %d, tilt_x_max: %d, tilt_y_max: %d\n",
		features->x_max, features->y_max,
		features->pressure_max, features->fw_version,
		features->distance_max,
		features->tilt_x_max, features->tilt_y_max);

	return 0;
}

static irqreturn_t wacom_i2c_irq(int irq, void *dev_id)
{
	struct wacom_i2c *wac_i2c = dev_id;
	struct input_dev *input = wac_i2c->input;
	struct wacom_features *features = &wac_i2c->features;
	u8 *data = wac_i2c->data;
	unsigned int x, y, pressure;
	unsigned char tsw, f1, f2, ers;
	short tilt_x, tilt_y, distance;
	int error;

	error = i2c_master_recv(wac_i2c->client,
				wac_i2c->data, sizeof(wac_i2c->data));
	if (error < 0)
		goto out;

	tsw = data[3] & WACOM_TIP_SWITCH_bm;
	ers = data[3] & WACOM_ERASER_bm;
	f1 = data[3] & WACOM_BARREL_SWITCH_bm;
	f2 = data[3] & WACOM_BARREL_SWITCH_2_bm;
	x = le16_to_cpup((__le16 *)&data[4]);
	y = le16_to_cpup((__le16 *)&data[6]);
	pressure = le16_to_cpup((__le16 *)&data[8]);

	/* Signed */
	tilt_x = get_unaligned_le16(&data[11]);
	tilt_y = get_unaligned_le16(&data[13]);

	distance = get_unaligned_le16(&data[15]);

	if (!wac_i2c->prox)
		wac_i2c->tool = (data[3] & 0x0c) ?
			BTN_TOOL_RUBBER : BTN_TOOL_PEN;

	wac_i2c->prox = data[3] & 0x20;

	touchscreen_report_pos(input, &wac_i2c->props, features->x_max,
			       features->y_max, true);
	input_report_key(input, BTN_TOUCH, tsw || ers);
	input_report_key(input, wac_i2c->tool, wac_i2c->prox);
	input_report_key(input, BTN_STYLUS, f1);
	input_report_key(input, BTN_STYLUS2, f2);
	input_report_abs(input, ABS_X, x);
	input_report_abs(input, ABS_Y, y);
	input_report_abs(input, ABS_PRESSURE, pressure);
	input_report_abs(input, ABS_DISTANCE, distance);
	input_report_abs(input, ABS_TILT_X, tilt_x);
	input_report_abs(input, ABS_TILT_Y, tilt_y);
	input_sync(input);

out:
	return IRQ_HANDLED;
}

static int wacom_i2c_open(struct input_dev *dev)
{
	struct wacom_i2c *wac_i2c = input_get_drvdata(dev);
	struct i2c_client *client = wac_i2c->client;

	enable_irq(client->irq);

	return 0;
}

static void wacom_i2c_close(struct input_dev *dev)
{
	struct wacom_i2c *wac_i2c = input_get_drvdata(dev);
	struct i2c_client *client = wac_i2c->client;

	disable_irq(client->irq);
}

static int wacom_i2c_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct wacom_i2c *wac_i2c;
	struct input_dev *input;
	struct wacom_features *features;
	int error;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(dev, "i2c_check_functionality error\n");
		return -EIO;
	}

	wac_i2c = devm_kzalloc(dev, sizeof(*wac_i2c), GFP_KERNEL);
	if (!wac_i2c)
		return -ENOMEM;

	wac_i2c->vdd = regulator_get(&client->dev, "vdd");
	if (IS_ERR(wac_i2c->vdd))
		return PTR_ERR(wac_i2c->vdd);

	error = regulator_enable(wac_i2c->vdd);
	if (error) {
		regulator_put(wac_i2c->vdd);
		return error;
	}

	features = &wac_i2c->features;
	error = wacom_query_device(client, features);
	if (error)
		return error;

	wac_i2c->client = client;

	input = devm_input_allocate_device(dev);
	if (!input) {
		regulator_disable(wac_i2c->vdd);
		regulator_put(wac_i2c->vdd);
		return -ENOMEM;
	}

	wac_i2c->input = input;

	input->name = "Wacom I2C Digitizer";
	input->id.bustype = BUS_I2C;
	input->id.vendor = 0x56a;
	input->id.version = features->fw_version;
	input->open = wacom_i2c_open;
	input->close = wacom_i2c_close;

	input->evbit[0] |= BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);

	__set_bit(BTN_TOOL_PEN, input->keybit);
	__set_bit(BTN_TOOL_RUBBER, input->keybit);
	__set_bit(BTN_STYLUS, input->keybit);
	__set_bit(BTN_STYLUS2, input->keybit);
	__set_bit(BTN_TOUCH, input->keybit);

	touchscreen_parse_properties(input, true, &wac_i2c->props);
	input_set_abs_params(input, ABS_X, 0, features->x_max, 0, 0);
	input_set_abs_params(input, ABS_Y, 0, features->y_max, 0, 0);
	input_set_abs_params(input, ABS_PRESSURE,
			     0, features->pressure_max, 0, 0);
	input_set_abs_params(input, ABS_DISTANCE, 0, features->distance_max, 0, 0);
	input_set_abs_params(input, ABS_TILT_X, -features->tilt_x_max,
			     features->tilt_x_max, 0, 0);
	input_set_abs_params(input, ABS_TILT_Y, -features->tilt_y_max,
			     features->tilt_y_max, 0, 0);
	input_set_drvdata(input, wac_i2c);

	error = devm_request_threaded_irq(dev, client->irq, NULL, wacom_i2c_irq,
					  IRQF_ONESHOT, "wacom_i2c", wac_i2c);
	if (error) {
		dev_err(dev, "Failed to request IRQ: %d\n", error);
		regulator_disable(wac_i2c->vdd);
		regulator_put(wac_i2c->vdd);
		return error;
	}

	/* Disable the IRQ, we'll enable it in wac_i2c_open() */
	disable_irq(client->irq);

	error = input_register_device(wac_i2c->input);
	if (error) {
		dev_err(dev, "Failed to register input device: %d\n", error);
		regulator_disable(wac_i2c->vdd);
		regulator_put(wac_i2c->vdd);
		return error;
	}

	return 0;
}

static int __maybe_unused wacom_i2c_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	disable_irq(client->irq);

	return 0;
}

static int __maybe_unused wacom_i2c_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	enable_irq(client->irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(wacom_i2c_pm, wacom_i2c_suspend, wacom_i2c_resume);

static const struct i2c_device_id wacom_i2c_id[] = {
	{ "WAC_I2C_EMR", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, wacom_i2c_id);

static const struct of_device_id wacom_i2c_of_match_table[] = {
	{ .compatible = "wacom,i2c-30" },
	{}
};
MODULE_DEVICE_TABLE(of, wacom_i2c_of_match_table);

static struct i2c_driver wacom_i2c_driver = {
	.driver	= {
		.name	= "wacom_i2c",
		.pm	= &wacom_i2c_pm,
		.of_match_table = wacom_i2c_of_match_table,
	},

	.probe		= wacom_i2c_probe,
	.id_table	= wacom_i2c_id,
};
module_i2c_driver(wacom_i2c_driver);

MODULE_AUTHOR("Tatsunosuke Tobita <tobita.tatsunosuke@wacom.co.jp>");
MODULE_DESCRIPTION("WACOM EMR I2C Driver");
MODULE_LICENSE("GPL");
