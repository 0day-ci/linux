// SPDX-License-Identifier: GPL-2.0+
/*
 * Ampere Computing SoC's SMpro Error Monitoring Driver
 *
 * Copyright (c) 2021, Ampere Computing LLC
 *
 */

#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/* GPI RAS Error Registers */
#define GPI_RAS_ERR		0x7E

/* Core and L2C Error Registers */
#define CORE_CE_ERR_CNT		0x80
#define CORE_CE_ERR_LEN		0x81
#define CORE_CE_ERR_DATA	0x82
#define CORE_UE_ERR_CNT		0x83
#define CORE_UE_ERR_LEN		0x84
#define CORE_UE_ERR_DATA	0x85

/* Memory Error Registers */
#define MEM_CE_ERR_CNT		0x90
#define MEM_CE_ERR_LEN		0x91
#define MEM_CE_ERR_DATA		0x92
#define MEM_UE_ERR_CNT		0x93
#define MEM_UE_ERR_LEN		0x94
#define MEM_UE_ERR_DATA		0x95

/* RAS Error/Warning Registers */
#define ERR_SMPRO_TYPE		0xA0
#define ERR_PMPRO_TYPE		0xA1
#define ERR_SMPRO_INFO_LO	0xA2
#define ERR_SMPRO_INFO_HI	0xA3
#define ERR_SMPRO_DATA_LO	0xA4
#define ERR_SMPRO_DATA_HI	0xA5
#define WARN_SMPRO_INFO_LO	0xAA
#define WARN_SMPRO_INFO_HI	0xAB
#define ERR_PMPRO_INFO_LO	0xA6
#define ERR_PMPRO_INFO_HI	0xA7
#define ERR_PMPRO_DATA_LO	0xA8
#define ERR_PMPRO_DATA_HI	0xA9
#define WARN_PMPRO_INFO_LO	0xAC
#define WARN_PMPRO_INFO_HI	0xAD

/* PCIE Error Registers */
#define PCIE_CE_ERR_CNT		0xC0
#define PCIE_CE_ERR_LEN		0xC1
#define PCIE_CE_ERR_DATA	0xC2
#define PCIE_UE_ERR_CNT		0xC3
#define PCIE_UE_ERR_LEN		0xC4
#define PCIE_UE_ERR_DATA	0xC5

/* Other Error Registers */
#define OTHER_CE_ERR_CNT	0xD0
#define OTHER_CE_ERR_LEN	0xD1
#define OTHER_CE_ERR_DATA	0xD2
#define OTHER_UE_ERR_CNT	0xD8
#define OTHER_UE_ERR_LEN	0xD9
#define OTHER_UE_ERR_DATA	0xDA

/* Event Source Registers */
#define EVENT_SRC1		0x62
#define EVENT_SRC2		0x63

/* Event Data Registers */
#define VRD_WARN_FAULT_EVENT_DATA	0x78
#define VRD_HOT_EVENT_DATA		0x79
#define DIMM_HOT_EVENT_DATA		0x7A
#define DIMM_2X_REFRESH_EVENT_DATA	0x96

#define MAX_READ_BLOCK_LENGTH	48
#define NUM_I2C_MESSAGES	2
#define MAX_READ_ERROR		35
#define MAX_MSG_LEN		128

#define RAS_SMPRO_ERRS		0
#define RAS_PMPRO_ERRS		1

enum RAS_48BYTES_ERR_TYPES {
	CORE_CE_ERRS,
	CORE_UE_ERRS,
	MEM_CE_ERRS,
	MEM_UE_ERRS,
	PCIE_CE_ERRS,
	PCIE_UE_ERRS,
	OTHER_CE_ERRS,
	OTHER_UE_ERRS,
	NUM_48BYTES_ERR_TYPE,
};

struct smpro_error_hdr {
	u8 err_count;	/* Number of the RAS errors */
	u8 err_len;	/* Number of data bytes */
	u8 err_data;	/* Start of 48-byte data */
};

/*
 * Included Address of registers to get Count, Length of data and Data
 * of the 48 bytes error data
 */
struct smpro_error_hdr smpro_error_table[NUM_48BYTES_ERR_TYPE] = {
	{CORE_CE_ERR_CNT, CORE_CE_ERR_LEN, CORE_CE_ERR_DATA},
	{CORE_UE_ERR_CNT, CORE_UE_ERR_LEN, CORE_UE_ERR_DATA},
	{MEM_CE_ERR_CNT, MEM_CE_ERR_LEN, MEM_CE_ERR_DATA},
	{MEM_UE_ERR_CNT, MEM_UE_ERR_LEN, MEM_UE_ERR_DATA},
	{PCIE_CE_ERR_CNT, PCIE_CE_ERR_LEN, PCIE_CE_ERR_DATA},
	{PCIE_UE_ERR_CNT, PCIE_UE_ERR_LEN, PCIE_UE_ERR_DATA},
	{OTHER_CE_ERR_CNT, OTHER_CE_ERR_LEN, OTHER_CE_ERR_DATA},
	{OTHER_UE_ERR_CNT, OTHER_UE_ERR_LEN, OTHER_UE_ERR_DATA},
};

/*
 * List of SCP registers which are used to get
 * one type of RAS Internal errors.
 */
struct smpro_int_error_hdr {
	u8 err_type;
	u8 err_info_low;
	u8 err_info_high;
	u8 err_data_high;
	u8 err_data_low;
	u8 warn_info_low;
	u8 warn_info_high;
};

struct smpro_int_error_hdr list_smpro_int_error_hdr[2] = {
	{
	 ERR_SMPRO_TYPE,
	 ERR_SMPRO_INFO_LO, ERR_SMPRO_INFO_HI,
	 ERR_SMPRO_DATA_LO, ERR_SMPRO_DATA_HI,
	 WARN_SMPRO_INFO_LO, WARN_SMPRO_INFO_HI
	},
	{
	 ERR_PMPRO_TYPE,
	 ERR_PMPRO_INFO_LO, ERR_PMPRO_INFO_HI,
	 ERR_PMPRO_DATA_LO, ERR_PMPRO_DATA_HI,
	 WARN_PMPRO_INFO_LO, WARN_PMPRO_INFO_HI
	},
};

struct smpro_errmon {
	struct regmap *regmap;
};

enum EVENT_TYPES {
	VRD_WARN_FAULT_EVENTS,
	VRD_HOT_EVENTS,
	DIMM_HOT_EVENTS,
	NUM_EVENTS_TYPE,
};

struct smpro_event_hdr {
	u8 event_src;	/* Source register of event type */
	u8 event_data;	/* Data register of event type */
};

/* Included Address of event source and data registers */
struct smpro_event_hdr smpro_event_table[NUM_EVENTS_TYPE] = {
	{EVENT_SRC1, VRD_WARN_FAULT_EVENT_DATA},
	{EVENT_SRC1, VRD_HOT_EVENT_DATA},
	{EVENT_SRC2, DIMM_HOT_EVENT_DATA}
};

static int read_i2c_block_data(struct i2c_client *client, u16 address, u16 length, u8 *data)
{
	unsigned char outbuf[MAX_READ_BLOCK_LENGTH];
	unsigned char inbuf[2];
	struct i2c_msg msgs[2];
	ssize_t ret;

	inbuf[0] = (address & 0xff);
	inbuf[1] = length;

	msgs[0].addr = client->addr;
	msgs[0].flags = client->flags & I2C_M_TEN;
	msgs[0].len = 2;
	msgs[0].buf = inbuf;

	msgs[1].addr = client->addr;
	msgs[1].flags = (client->flags  & I2C_M_TEN) | I2C_M_RD;
	msgs[1].len = length;
	msgs[1].buf = outbuf;

	ret = i2c_transfer(client->adapter, msgs, NUM_I2C_MESSAGES);
	if (ret < 0)
		return ret;

	if (ret != NUM_I2C_MESSAGES)
		return -EIO;

	memcpy(data, outbuf, length);

	return length;
}

static int errmon_read_block(struct regmap *map, u16 address, u16 length, u8 *data)
{
	struct i2c_client *client = to_i2c_client(regmap_get_device(map));
	int ret;

	regmap_acquire_lock(map);

	ret = read_i2c_block_data(client, address, length, data);

	regmap_release_lock(map);

	return ret;
}

static ssize_t smpro_event_data_read(struct device *dev,
				     struct device_attribute *da, char *buf,
				     int channel)
{
	struct smpro_errmon *errmon = dev_get_drvdata(dev);
	unsigned char msg[MAX_MSG_LEN] = {'\0'};
	struct smpro_event_hdr event_info;
	s32 event_data = 0;
	int ret;

	*buf = 0;
	if (channel >= NUM_EVENTS_TYPE)
		goto done;

	event_info = smpro_event_table[channel];
	ret = regmap_read(errmon->regmap, event_info.event_data, &event_data);
	if (ret)
		goto done;

	ret = scnprintf(msg, MAX_MSG_LEN, "%02x %04x\n", channel, event_data);
	strncat(buf, msg, ret);
done:
	return strlen(buf);
}

static ssize_t smpro_error_data_read(struct device *dev, struct device_attribute *da,
				     char *buf, int channel)
{
	struct smpro_errmon *errmon = dev_get_drvdata(dev);
	unsigned char err_data[MAX_READ_BLOCK_LENGTH];
	unsigned char msg[MAX_MSG_LEN] = {'\0'};
	struct smpro_error_hdr err_info;
	s32 err_count = 1, err_length = 0;
	u8 i = 0;
	int len;
	int ret;

	*buf = 0;
	if (channel >= NUM_48BYTES_ERR_TYPE)
		goto done;

	err_info = smpro_error_table[channel];

	ret = regmap_read(errmon->regmap, err_info.err_count, &err_count);
	if (ret || err_count <= 0)
		goto done;

	/* Bit 8 indentifies the overflow status of one error type */
	if (err_count & BIT(8)) {
		len = scnprintf(msg, MAX_MSG_LEN,
				"%02x %02x %04x %08x %016llx %016llx %016llx %016llx %016llx\n",
				0xFF, 0xFF, 0, 0, 0LL, 0LL, 0LL, 0LL, 0LL);
		strncat(buf, msg, len);
	}

	/* Error count is the low byte */
	err_count = err_count & 0xff;

	if (err_count > MAX_READ_ERROR)
		err_count = MAX_READ_ERROR;

	for (i = 0; i < err_count; i++) {
		ret = regmap_read(errmon->regmap, err_info.err_len, &err_length);
		if (ret || err_length <= 0)
			break;

		if (err_length > MAX_READ_BLOCK_LENGTH)
			err_length = MAX_READ_BLOCK_LENGTH;

		ret = errmon_read_block(errmon->regmap, err_info.err_data, err_length, err_data);
		if (ret < 0)
			break;
		/*
		 * The output of Core/Memory/PCIe/Others UE/CE errors follows below format:
		 * <Error Type>  <Error SubType>  <Instance>  <Error Status> \
		 * <Error Address>  <Error Misc 0> <Error Misc 1> <Error Misc2> <Error Misc 3>
		 * Where:
		 *  + Error Type: The hardwares cause the errors. (1 byte)
		 *  + SubType: Sub type of error in the specified hardware error. (1 byte)
		 *  + Instance: Combination of the socket, channel,
		 *    slot cause the error. (2 bytes)
		 *  + Error Status: Encode of error status. (4 bytes)
		 *  + Error Address: The address in device causes the errors. (8 bytes)
		 *  + Error Misc 0/1/2/3: Addition info about the errors. (8 bytes for each)
		 * Reference Altra SOC BMC Interface specification.
		 */
		len = scnprintf(msg, MAX_MSG_LEN,
				"%02x %02x %04x %08x %016llx %016llx %016llx %016llx %016llx\n",
				err_data[0], err_data[1], *(u16 *)&err_data[2],
				*(u32 *)&err_data[4], *(u64 *)&err_data[8],
				*(u64 *)&err_data[16], *(u64 *)&err_data[24],
				*(u64 *)&err_data[32], *(u64 *)&err_data[40]);

		/* go to next error */
		ret = regmap_write(errmon->regmap, err_info.err_count, 0x100);
		if (ret)
			break;

		/* add error message to buffer */
		strncat(buf, msg, len);
	}
done:
	return strlen(buf);
}

static s32 smpro_internal_err_get_info(struct regmap *regmap, u8 addr, u8 addr1, u8 addr2, u8 addr3,
				       u8 subtype, char *buf)
{
	unsigned int ret_hi = 0, ret_lo = 0, data_lo = 0, data_hi = 0;
	int ret;

	ret = regmap_read(regmap, addr, &ret_lo);
	if (ret)
		return ret;

	ret = regmap_read(regmap, addr1, &ret_hi);
	if (ret)
		return ret;

	if (addr2 != 0xff) {
		ret = regmap_read(regmap, addr2, &data_lo);
		if (ret)
			return ret;
		ret = regmap_read(regmap, addr3, &data_hi);
		if (ret)
			return ret;
	}
	/*
	 * Output format:
	 * <errType> <image> <dir> <Location> <errorCode> <data>
	 * Where:
	 *   + errType: SCP Error Type (3 bits)
	 *      1: Warning
	 *      2: Error
	 *      4: Error with data
	 *   + image: SCP Image Code (8 bits)
	 *   + dir: Direction (1 bit)
	 *      0: Enter
	 *      1: Exit
	 *   + location: SCP Module Location Code (8 bits)
	 *   + errorCode: SCP Error Code (16 bits)
	 *   + data : Extensive data (32 bits)
	 *      All bits are 0 when errType is warning or error.
	 */
	return scnprintf(buf, MAX_MSG_LEN, "%01x %02x %01x %02x %04x %04x%04x\n",
			 subtype, (ret_hi & 0xf000) >> 12, (ret_hi & 0x0800) >> 11,
			 ret_hi & 0xff, ret_lo, data_hi, data_lo);
}

static ssize_t smpro_internal_err_read(struct device *dev, struct device_attribute *da,
				       char *buf, int channel)
{
	struct smpro_errmon *errmon = dev_get_drvdata(dev);
	struct smpro_int_error_hdr err_info;
	unsigned char msg[MAX_MSG_LEN] = {'\0'};
	unsigned int err_type;
	unsigned int value;
	int ret = 0;

	*buf = 0;
	/* read error status */
	ret = regmap_read(errmon->regmap, GPI_RAS_ERR, &value);
	if (ret)
		goto done;

	if (!((channel == RAS_SMPRO_ERRS && (value & BIT(0))) ||
	      (channel == RAS_PMPRO_ERRS && (value & BIT(1)))))
		goto done;

	err_info = list_smpro_int_error_hdr[channel];
	ret = regmap_read(errmon->regmap, err_info.err_type, &err_type);
	if (ret)
		goto done;

	/* Warning type */
	if (err_type & BIT(0)) {
		ret = smpro_internal_err_get_info(errmon->regmap, err_info.warn_info_low,
						  err_info.warn_info_high, 0xff, 0xff, 1, msg);
		if (ret < 0)
			goto done;

		strncat(buf, msg, ret);
	}

	/* Error with data type */
	if (err_type & BIT(2)) {
		ret = smpro_internal_err_get_info(errmon->regmap, err_info.err_info_low,
						  err_info.err_info_high, err_info.err_data_low,
						  err_info.err_data_high, 4, msg);
		if (ret < 0)
			goto done;

		strncat(buf, msg, ret);
	}
	/* Error type */
	else if (err_type & BIT(1)) {
		ret = smpro_internal_err_get_info(errmon->regmap, err_info.err_info_low,
						  err_info.err_info_high, 0xff, 0xff, 2, msg);
		if (ret < 0)
			goto done;

		strncat(buf, msg, ret);
	}

	/* clear the read errors */
	regmap_write(errmon->regmap, err_info.err_type, err_type);

done:
	return strlen(buf);
}

static int errors_core_ce_show(struct device *dev, struct device_attribute *da, char *buf)
{
	return smpro_error_data_read(dev, da, buf, CORE_CE_ERRS);
}
static DEVICE_ATTR_RO(errors_core_ce);

static int errors_core_ue_show(struct device *dev, struct device_attribute *da, char *buf)
{
	return smpro_error_data_read(dev, da, buf, CORE_UE_ERRS);
}
static DEVICE_ATTR_RO(errors_core_ue);

static int errors_mem_ce_show(struct device *dev, struct device_attribute *da, char *buf)
{
	return smpro_error_data_read(dev, da, buf, MEM_CE_ERRS);
}
static DEVICE_ATTR_RO(errors_mem_ce);

static int errors_mem_ue_show(struct device *dev, struct device_attribute *da, char *buf)
{
	return smpro_error_data_read(dev, da, buf, MEM_UE_ERRS);
}
static DEVICE_ATTR_RO(errors_mem_ue);

static int errors_pcie_ce_show(struct device *dev, struct device_attribute *da, char *buf)
{
	return smpro_error_data_read(dev, da, buf, PCIE_CE_ERRS);
}
static DEVICE_ATTR_RO(errors_pcie_ce);

static int errors_pcie_ue_show(struct device *dev, struct device_attribute *da, char *buf)
{
	return smpro_error_data_read(dev, da, buf, PCIE_UE_ERRS);
}
static DEVICE_ATTR_RO(errors_pcie_ue);

static int errors_other_ce_show(struct device *dev, struct device_attribute *da, char *buf)
{
	return smpro_error_data_read(dev, da, buf, OTHER_CE_ERRS);
}
static DEVICE_ATTR_RO(errors_other_ce);

static int errors_other_ue_show(struct device *dev, struct device_attribute *da, char *buf)
{
	return smpro_error_data_read(dev, da, buf, OTHER_UE_ERRS);
}
static DEVICE_ATTR_RO(errors_other_ue);

static int errors_smpro_show(struct device *dev, struct device_attribute *da, char *buf)
{
	return smpro_internal_err_read(dev, da, buf, RAS_SMPRO_ERRS);
}
static DEVICE_ATTR_RO(errors_smpro);

static int errors_pmpro_show(struct device *dev, struct device_attribute *da, char *buf)
{
	return smpro_internal_err_read(dev, da, buf, RAS_PMPRO_ERRS);
}
static DEVICE_ATTR_RO(errors_pmpro);

static int event_vrd_warn_fault_show(struct device *dev, struct device_attribute *da, char *buf)
{
	return smpro_event_data_read(dev, da, buf, VRD_WARN_FAULT_EVENTS);
}
static DEVICE_ATTR_RO(event_vrd_warn_fault);

static int event_vrd_hot_show(struct device *dev, struct device_attribute *da, char *buf)
{
	return smpro_event_data_read(dev, da, buf, VRD_HOT_EVENTS);
}
static DEVICE_ATTR_RO(event_vrd_hot);

static int event_dimm_hot_show(struct device *dev, struct device_attribute *da, char *buf)
{
	return smpro_event_data_read(dev, da, buf, DIMM_HOT_EVENTS);
}
static DEVICE_ATTR_RO(event_dimm_hot);

static struct attribute *smpro_errmon_attrs[] = {
	&dev_attr_errors_core_ce.attr,
	&dev_attr_errors_core_ue.attr,
	&dev_attr_errors_mem_ce.attr,
	&dev_attr_errors_mem_ue.attr,
	&dev_attr_errors_pcie_ce.attr,
	&dev_attr_errors_pcie_ue.attr,
	&dev_attr_errors_other_ce.attr,
	&dev_attr_errors_other_ue.attr,
	&dev_attr_errors_smpro.attr,
	&dev_attr_errors_pmpro.attr,
	&dev_attr_event_vrd_warn_fault.attr,
	&dev_attr_event_vrd_hot.attr,
	&dev_attr_event_dimm_hot.attr,
	NULL
};

static const struct attribute_group smpro_errmon_attr_group = {
	.attrs = smpro_errmon_attrs
};

static int smpro_errmon_probe(struct platform_device *pdev)
{
	struct smpro_errmon *errmon;
	int ret;

	errmon = devm_kzalloc(&pdev->dev, sizeof(struct smpro_errmon), GFP_KERNEL);
	if (!errmon)
		return -ENOMEM;

	platform_set_drvdata(pdev, errmon);

	errmon->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!errmon->regmap)
		return -ENODEV;

	ret = sysfs_create_group(&pdev->dev.kobj, &smpro_errmon_attr_group);
	if (ret)
		dev_err(&pdev->dev, "SMPro errmon sysfs registration failed\n");

	return 0;
}

static int smpro_errmon_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &smpro_errmon_attr_group);
	pr_info("SMPro errmon sysfs entries removed");

	return 0;
}

static struct platform_driver smpro_errmon_driver = {
	.probe          = smpro_errmon_probe,
	.remove         = smpro_errmon_remove,
	.driver = {
		.name   = "smpro-errmon",
	},
};

module_platform_driver(smpro_errmon_driver);

MODULE_AUTHOR("Tung Nguyen <tung.nguyen@amperecomputing.com>");
MODULE_AUTHOR("Thinh Pham <thinh.pham@amperecomputing.com>");
MODULE_AUTHOR("Hoang Nguyen <hnguyen@amperecomputing.com>");
MODULE_AUTHOR("Thu Nguyen <thu@os.amperecomputing.com>");
MODULE_AUTHOR("Quan Nguyen <quan@os.amperecomputing.com>");
MODULE_DESCRIPTION("Ampere Altra SMpro driver");
MODULE_LICENSE("GPL");
