// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Hardware monitoring driver for FSP 3Y-Power PSUs
 *
 * Copyright (c) 2021 Václav Kubernát, CESNET
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include "pmbus.h"

#define YM2151_PAGE_12V		0x00
#define YM2151_PAGE_5V		0x20
#define YH5151E_PAGE_12V	0x00
#define YH5151E_PAGE_5V		0x10
#define YH5151E_PAGE_3V3	0x11

enum chips {
	ym2151e,
	yh5151e
};

static int set_page(struct i2c_client *client, int page)
{
	int rv;

	rv = i2c_smbus_read_byte_data(client, PMBUS_PAGE);

	if (rv < 0)
		return rv;

	if (rv != page) {
		rv = pmbus_set_page(client, page, 0xff);
		if (rv < 0)
			return rv;

		msleep(20);
	}

	return 0;
}

static int fsp3y_read_byte_data(struct i2c_client *client, int page, int reg)
{
	int rv;

	rv = set_page(client, page);
	if (rv < 0)
		return rv;

	return i2c_smbus_read_byte_data(client, reg);
}

static int fsp3y_read_word_data(struct i2c_client *client, int page, int phase, int reg)
{
	int rv;

	if (reg >= PMBUS_VIRT_BASE)
		return -ENXIO;

	switch (reg) {
	case PMBUS_OT_WARN_LIMIT:
	case PMBUS_OT_FAULT_LIMIT:
	case PMBUS_UT_WARN_LIMIT:
	case PMBUS_UT_FAULT_LIMIT:
	case PMBUS_VIN_UV_WARN_LIMIT:
	case PMBUS_VIN_UV_FAULT_LIMIT:
	case PMBUS_VIN_OV_FAULT_LIMIT:
	case PMBUS_VIN_OV_WARN_LIMIT:
	case PMBUS_IOUT_OC_WARN_LIMIT:
	case PMBUS_IOUT_UC_FAULT_LIMIT:
	case PMBUS_IOUT_OC_FAULT_LIMIT:
	case PMBUS_IIN_OC_WARN_LIMIT:
	case PMBUS_IIN_OC_FAULT_LIMIT:
	case PMBUS_VOUT_UV_WARN_LIMIT:
	case PMBUS_VOUT_UV_FAULT_LIMIT:
	case PMBUS_VOUT_OV_WARN_LIMIT:
	case PMBUS_VOUT_OV_FAULT_LIMIT:
	case PMBUS_MFR_VIN_MIN:
	case PMBUS_MFR_VIN_MAX:
	case PMBUS_MFR_IIN_MAX:
	case PMBUS_MFR_VOUT_MIN:
	case PMBUS_MFR_VOUT_MAX:
	case PMBUS_MFR_IOUT_MAX:
	case PMBUS_MFR_PIN_MAX:
	case PMBUS_POUT_MAX:
	case PMBUS_POUT_OP_WARN_LIMIT:
	case PMBUS_POUT_OP_FAULT_LIMIT:
	case PMBUS_MFR_MAX_TEMP_1:
	case PMBUS_MFR_MAX_TEMP_2:
	case PMBUS_MFR_MAX_TEMP_3:
	case PMBUS_MFR_POUT_MAX:
		return -ENXIO;
	}

	rv = set_page(client, page);
	if (rv < 0)
		return rv;

	return i2c_smbus_read_word_data(client, reg);
}

struct pmbus_driver_info fsp3y_info[] = {
	[ym2151e] = {
		.pages = 0x21,
		.func[YM2151_PAGE_12V] =
			PMBUS_HAVE_VOUT | PMBUS_HAVE_IOUT |
			PMBUS_HAVE_PIN | PMBUS_HAVE_POUT  |
			PMBUS_HAVE_TEMP | PMBUS_HAVE_TEMP2 |
			PMBUS_HAVE_VIN | PMBUS_HAVE_IIN |
			PMBUS_HAVE_FAN12,
		.func[YM2151_PAGE_5V] =
			PMBUS_HAVE_VOUT | PMBUS_HAVE_IOUT,
			PMBUS_HAVE_IIN,
		.read_word_data = fsp3y_read_word_data,
		.read_byte_data = fsp3y_read_byte_data,
	},
	[yh5151e] = {
		.pages = 0x12,
		.func[YH5151E_PAGE_12V] =
			PMBUS_HAVE_VOUT | PMBUS_HAVE_IOUT |
			PMBUS_HAVE_POUT  |
			PMBUS_HAVE_TEMP | PMBUS_HAVE_TEMP2 | PMBUS_HAVE_TEMP3,
		.func[YH5151E_PAGE_5V] =
			PMBUS_HAVE_VOUT | PMBUS_HAVE_IOUT |
			PMBUS_HAVE_POUT,
		.func[YH5151E_PAGE_3V3] =
			PMBUS_HAVE_VOUT | PMBUS_HAVE_IOUT |
			PMBUS_HAVE_POUT,
		.read_word_data = fsp3y_read_word_data,
		.read_byte_data = fsp3y_read_byte_data,
	}
};

static int fsp3y_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	return pmbus_do_probe(client, &fsp3y_info[id->driver_data]);
}

static const struct i2c_device_id pmbus_id[] = {
	{"fsp3y_ym2151e", ym2151e},
	{"fsp3y_yh5151e", yh5151e},
	{}
};

MODULE_DEVICE_TABLE(i2c, pmbus_id);

/* This is the driver that will be inserted */
static struct i2c_driver fsp3y_driver = {
	.driver = {
		   .name = "fsp3y",
		   },
	.probe = fsp3y_probe,
	.id_table = pmbus_id
};

module_i2c_driver(fsp3y_driver);

MODULE_AUTHOR("Václav Kubernát");
MODULE_DESCRIPTION("PMBus driver for FSP/3Y-Power power supplies");
MODULE_LICENSE("GPL");
