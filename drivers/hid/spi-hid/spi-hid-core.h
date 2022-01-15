/* SPDX-License-Identifier: GPL-2.0 */
/*
 * spi-hid-core.h
 *
 * Copyright (c) 2021 Microsoft Corporation
 */

#ifndef SPI_HID_CORE_H
#define SPI_HID_CORE_H

#include <linux/completion.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "spi-hid-of.h"

/* Protocol constants */
#define SPI_HID_READ_APPROVAL_CONSTANT		0xff
#define SPI_HID_INPUT_HEADER_SYNC_BYTE		0x5a
#define SPI_HID_INPUT_HEADER_VERSION		0x03
#define SPI_HID_SUPPORTED_VERSION		0x0300

/* Protocol message size constants */
#define SPI_HID_READ_APPROVAL_LEN		5
#define SPI_HID_INPUT_HEADER_LEN		4
#define SPI_HID_INPUT_BODY_LEN			4
#define SPI_HID_OUTPUT_HEADER_LEN		8
#define SPI_HID_DEVICE_DESCRIPTOR_LENGTH	24

/* Protocol message type constants */
#define SPI_HID_INPUT_REPORT_TYPE_DATA				0x01
#define SPI_HID_INPUT_REPORT_TYPE_RESET_RESP			0x03
#define SPI_HID_INPUT_REPORT_TYPE_COMMAND_RESP			0x04
#define SPI_HID_INPUT_REPORT_TYPE_GET_FEATURE_RESP		0x05
#define SPI_HID_INPUT_REPORT_TYPE_DEVICE_DESC			0x07
#define SPI_HID_INPUT_REPORT_TYPE_REPORT_DESC			0x08
#define SPI_HID_INPUT_REPORT_TYPE_SET_FEATURE_RESP		0x09
#define SPI_HID_INPUT_REPORT_TYPE_SET_OUTPUT_REPORT_RESP	0x0a
#define SPI_HID_INPUT_REPORT_TYPE_GET_INPUT_REPORT_RESP		0x0b

#define SPI_HID_OUTPUT_REPORT_TYPE_DEVICE_DESC_REQUEST	0x01
#define SPI_HID_OUTPUT_REPORT_TYPE_REPORT_DESC_REQUEST	0x02
#define SPI_HID_OUTPUT_REPORT_TYPE_HID_SET_FEATURE	0x03
#define SPI_HID_OUTPUT_REPORT_TYPE_HID_GET_FEATURE	0x04
#define SPI_HID_OUTPUT_REPORT_TYPE_HID_OUTPUT_REPORT	0x05
#define SPI_HID_OUTPUT_REPORT_TYPE_INPUT_REPORT_REQUEST	0x06
#define SPI_HID_OUTPUT_REPORT_TYPE_COMMAND		0x07

#define SPI_HID_OUTPUT_REPORT_CONTENT_ID_DESC_REQUEST	0x00

/* Power mode constants */
#define SPI_HID_POWER_MODE_ON			0x01
#define SPI_HID_POWER_MODE_SLEEP		0x02
#define SPI_HID_POWER_MODE_OFF			0x03
#define SPI_HID_POWER_MODE_WAKING_SLEEP		0x04

/* Raw input buffer with data from the bus */
struct spi_hid_input_buf {
	__u8 header[SPI_HID_INPUT_HEADER_LEN];
	__u8 body[SPI_HID_INPUT_BODY_LEN];
	u8 content[SZ_8K];
};

/* Processed data from  input report header */
struct spi_hid_input_header {
	u8 version;
	u16 report_length;
	u8 last_fragment_flag;
	u8 sync_const;
};

/* Processed data from input report body, excluding the content */
struct spi_hid_input_body {
	u8 report_type;
	u16 content_length;
	u8 content_id;
};

/* Processed data from an input report */
struct spi_hid_input_report {
	u8 report_type;
	u16 content_length;
	u8 content_id;
	u8 *content;
};

/* Raw output report buffer to be put on the bus */
struct spi_hid_output_buf {
	__u8 header[SPI_HID_OUTPUT_HEADER_LEN];
	u8 content[SZ_8K];
};

/* Data necessary to send an output report */
struct spi_hid_output_report {
	u8 report_type;
	u16 content_length;
	u8 content_id;
	u8 *content;
};

/* Raw content in device descriptor */
struct spi_hid_device_desc_raw {
	__le16 wDeviceDescLength;
	__le16 bcdVersion;
	__le16 wReportDescLength;
	__le16 wMaxInputLength;
	__le16 wMaxOutputLength;
	__le16 wMaxFragmentLength;
	__le16 wVendorID;
	__le16 wProductID;
	__le16 wVersionID;
	__le16 wFlags;
	__u8 reserved[4];
} __packed;

/* Processed data from a device descriptor */
struct spi_hid_device_descriptor {
	u16 hid_version;
	u16 report_descriptor_length;
	u16 max_input_length;
	u16 max_output_length;
	u16 max_fragment_length;
	u16 vendor_id;
	u16 product_id;
	u16 version_id;
	u8 no_output_report_ack;
};

/* Driver context */
struct spi_hid {
	struct spi_device	*spi;
	struct hid_device	*hid;

	struct spi_transfer	input_transfer[2];
	struct spi_transfer	output_transfer;
	struct spi_message	input_message;
	struct spi_message	output_message;

	struct spi_hid_of_config conf;
	struct spi_hid_device_descriptor desc;
	struct spi_hid_output_buf output;
	struct spi_hid_input_buf input;
	struct spi_hid_input_buf response;

	spinlock_t		input_lock;

	u32 input_transfer_pending;

	u8 power_state;

	u8 attempts;

	/*
	 * ready flag indicates that the FW is ready to accept commands and
	 * requests. The FW becomes ready after sending the report descriptor.
	 */
	bool ready;
	/*
	 * refresh_in_progress is set to true while the refresh_device worker
	 * thread is destroying and recreating the hidraw device. When this flag
	 * is set to true, the ll_close and ll_open functions will not cause
	 * power state changes.
	 */
	bool refresh_in_progress;

	struct work_struct reset_work;
	struct work_struct create_device_work;
	struct work_struct refresh_device_work;
	struct work_struct error_work;

	struct mutex lock;
	struct completion output_done;

	__u8 read_approval_header[SPI_HID_READ_APPROVAL_LEN];
	__u8 read_approval_body[SPI_HID_READ_APPROVAL_LEN];

	u32 report_descriptor_crc32;

	u32 regulator_error_count;
	int regulator_last_error;
	u32 bus_error_count;
	int bus_last_error;
	u32 dir_count;
};

#endif
