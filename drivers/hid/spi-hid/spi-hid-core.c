// SPDX-License-Identifier: GPL-2.0
/*
 * HID over SPI protocol implementation
 * spi-hid-core.c
 *
 * Copyright (c) 2021 Microsoft Corporation
 *
 * This code is partly based on "HID over I2C protocol implementation:
 *
 *  Copyright (c) 2012 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 *  Copyright (c) 2012 Ecole Nationale de l'Aviation Civile, France
 *  Copyright (c) 2012 Red Hat, Inc
 *
 *  which in turn is partly based on "USB HID support for Linux":
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2007-2008 Oliver Neukum
 *  Copyright (c) 2006-2010 Jiri Kosina
 */

#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include "spi-hid-core.h"
#include "spi-hid_trace.h"
#include "spi-hid-of.h"
#include "../hid-ids.h"

#define SPI_HID_MAX_RESET_ATTEMPTS 3

static struct hid_ll_driver spi_hid_ll_driver;

static void spi_hid_populate_read_approvals(struct spi_hid_of_config *conf,
	__u8 *header_buf, __u8 *body_buf)
{
	header_buf[0] = conf->read_opcode;
	header_buf[1] = (conf->input_report_header_address >> 16) & 0xff;
	header_buf[2] =	(conf->input_report_header_address >> 8) & 0xff;
	header_buf[3] =	(conf->input_report_header_address >> 0) & 0xff;
	header_buf[4] = SPI_HID_READ_APPROVAL_CONSTANT;

	body_buf[0] = conf->read_opcode;
	body_buf[1] = (conf->input_report_body_address >> 16) & 0xff;
	body_buf[2] = (conf->input_report_body_address >> 8) & 0xff;
	body_buf[3] = (conf->input_report_body_address >> 0) & 0xff;
	body_buf[4] = SPI_HID_READ_APPROVAL_CONSTANT;
}

static void spi_hid_parse_dev_desc(struct spi_hid_device_desc_raw *raw,
					struct spi_hid_device_descriptor *desc)
{
	desc->hid_version = le16_to_cpu(raw->bcdVersion);
	desc->report_descriptor_length = le16_to_cpu(raw->wReportDescLength);
	desc->max_input_length = le16_to_cpu(raw->wMaxInputLength);
	desc->max_output_length = le16_to_cpu(raw->wMaxOutputLength);

	/* FIXME: multi-fragment not supported, field below not used */
	desc->max_fragment_length = le16_to_cpu(raw->wMaxFragmentLength);

	desc->vendor_id = le16_to_cpu(raw->wVendorID);
	desc->product_id = le16_to_cpu(raw->wProductID);
	desc->version_id = le16_to_cpu(raw->wVersionID);
	desc->no_output_report_ack = le16_to_cpu(raw->wFlags) & BIT(0);
}

static void spi_hid_populate_input_header(__u8 *buf,
		struct spi_hid_input_header *header)
{
	header->version            = buf[0] & 0xf;
	header->report_length      = (buf[1] | ((buf[2] & 0x3f) << 8)) * 4;
	header->last_fragment_flag = (buf[2] & 0x40) >> 6;
	header->sync_const         = buf[3];
}

static void spi_hid_populate_input_body(__u8 *buf,
		struct spi_hid_input_body *body)
{
	body->report_type = buf[0];
	body->content_length = buf[1] | (buf[2] << 8);
	body->content_id = buf[3];
}

static void spi_hid_input_report_prepare(struct spi_hid_input_buf *buf,
		struct spi_hid_input_report *report)
{
	struct spi_hid_input_header header;
	struct spi_hid_input_body body;

	spi_hid_populate_input_header(buf->header, &header);
	spi_hid_populate_input_body(buf->body, &body);
	report->report_type = body.report_type;
	report->content_length = body.content_length;
	report->content_id = body.content_id;
	report->content = buf->content;
}

static void spi_hid_populate_output_header(__u8 *buf,
		struct spi_hid_of_config *conf,
		struct spi_hid_output_report *report)
{
	buf[0] = conf->write_opcode;
	buf[1] = (conf->output_report_address >> 16) & 0xff;
	buf[2] = (conf->output_report_address >> 8) & 0xff;
	buf[3] = (conf->output_report_address >> 0) & 0xff;
	buf[4] = report->report_type;
	buf[5] = report->content_length & 0xff;
	buf[6] = (report->content_length >> 8) & 0xff;
	buf[7] = report->content_id;
}

static int spi_hid_input_async(struct spi_hid *shid, void *buf, u16 length,
		void (*complete)(void *), bool is_header)
{
	int ret;
	struct device *dev = &shid->spi->dev;

	shid->input_transfer[0].tx_buf = is_header ? shid->read_approval_header :
						shid->read_approval_body;
	shid->input_transfer[0].len = SPI_HID_READ_APPROVAL_LEN;

	shid->input_transfer[1].rx_buf = buf;
	shid->input_transfer[1].len = length;

	spi_message_init_with_transfers(&shid->input_message,
			shid->input_transfer, 2);

	shid->input_message.complete = complete;
	shid->input_message.context = shid;

	trace_spi_hid_input_async(shid,
			shid->input_transfer[0].tx_buf,
			shid->input_transfer[0].len,
			shid->input_transfer[1].rx_buf,
			shid->input_transfer[1].len, 0);

	ret = spi_async(shid->spi, &shid->input_message);
	if (ret) {
		dev_err(dev, "Error starting async transfer: %d, resetting\n",
									ret);
		shid->bus_error_count++;
		shid->bus_last_error = ret;
		schedule_work(&shid->error_work);
	}

	return ret;
}

static int spi_hid_output(struct spi_hid *shid, void *buf, u16 length)
{
	struct spi_transfer transfer;
	struct spi_message message;
	int ret;

	memset(&transfer, 0, sizeof(transfer));

	transfer.tx_buf = buf;
	transfer.len = length;

	spi_message_init_with_transfers(&message, &transfer, 1);

	/*
	 * REVISIT: Should output be asynchronous?
	 *
	 * According to Documentation/hid/hid-transport.rst, ->output_report()
	 * must be implemented as an asynchronous operation.
	 */
	trace_spi_hid_output_begin(shid, transfer.tx_buf,
			transfer.len, NULL, 0, 0);

	ret = spi_sync(shid->spi, &message);

	trace_spi_hid_output_end(shid, transfer.tx_buf,
			transfer.len, NULL, 0, ret);

	if (ret) {
		shid->bus_error_count++;
		shid->bus_last_error = ret;
	}

	return ret;
}

static const char *const spi_hid_power_mode_string(u8 power_state)
{
	switch (power_state) {
	case SPI_HID_POWER_MODE_ON:
		return "d0";
	case SPI_HID_POWER_MODE_SLEEP:
		return "d2";
	case SPI_HID_POWER_MODE_OFF:
		return "d3";
	case SPI_HID_POWER_MODE_WAKING_SLEEP:
		return "d3*";
	default:
		return "unknown";
	}
}

static void spi_hid_suspend(struct spi_hid *shid)
{
	struct device *dev = &shid->spi->dev;

	if (shid->power_state == SPI_HID_POWER_MODE_OFF)
		return;

	disable_irq(shid->spi->irq);
	shid->ready = false;
	sysfs_notify(&dev->kobj, NULL, "ready");

	spi_hid_of_assert_reset(&shid->conf);

	shid->power_state = SPI_HID_POWER_MODE_OFF;
}

static void spi_hid_resume(struct spi_hid *shid)
{
	if (shid->power_state == SPI_HID_POWER_MODE_ON)
		return;

	shid->power_state = SPI_HID_POWER_MODE_ON;
	enable_irq(shid->spi->irq);
	shid->input_transfer_pending = 0;

	spi_hid_of_deassert_reset(&shid->conf);
}

static struct hid_device *spi_hid_disconnect_hid(struct spi_hid *shid)
{
	struct hid_device *hid = shid->hid;

	shid->hid = NULL;

	return hid;
}

static void spi_hid_stop_hid(struct spi_hid *shid)
{
	struct hid_device *hid;

	hid = spi_hid_disconnect_hid(shid);
	if (hid) {
		cancel_work_sync(&shid->create_device_work);
		cancel_work_sync(&shid->refresh_device_work);
		hid_destroy_device(hid);
	}
}

static void spi_hid_error_handler(struct spi_hid *shid)
{
	struct device *dev = &shid->spi->dev;
	int ret;

	if (shid->power_state == SPI_HID_POWER_MODE_OFF)
		return;

	if (shid->attempts++ >= SPI_HID_MAX_RESET_ATTEMPTS) {
		dev_err(dev, "unresponsive device, aborting.\n");
		spi_hid_stop_hid(shid);
		spi_hid_of_assert_reset(&shid->conf);
		ret = spi_hid_of_power_down(&shid->conf);
		if (ret) {
			dev_err(dev, "failed to disable regulator\n");
			shid->regulator_error_count++;
			shid->regulator_last_error = ret;
		}
		return;
	}

	trace_spi_hid_error_handler(shid);

	shid->ready = false;
	sysfs_notify(&dev->kobj, NULL, "ready");

	spi_hid_of_assert_reset(&shid->conf);

	shid->power_state = SPI_HID_POWER_MODE_OFF;
	shid->input_transfer_pending = 0;
	cancel_work_sync(&shid->reset_work);

	spi_hid_of_sleep_minimal_reset_delay(&shid->conf);

	shid->power_state = SPI_HID_POWER_MODE_ON;

	spi_hid_of_deassert_reset(&shid->conf);
}

static void spi_hid_error_work(struct work_struct *work)
{
	struct spi_hid *shid = container_of(work, struct spi_hid, error_work);

	spi_hid_error_handler(shid);
}

static int spi_hid_send_output_report(struct spi_hid *shid,
		struct spi_hid_output_report *report)
{
	struct spi_hid_output_buf *buf = &shid->output;
	struct device *dev = &shid->spi->dev;
	u16 report_length;
	u16 padded_length;
	u8 padding;
	int ret;

	if (report->content_length > shid->desc.max_output_length) {
		dev_err(dev, "Output report too big, content_length 0x%x\n",
						report->content_length);
		ret = -E2BIG;
		goto out;
	}

	spi_hid_populate_output_header(buf->header, &shid->conf, report);

	if (report->content_length)
		memcpy(&buf->content, report->content, report->content_length);

	report_length = sizeof(buf->header) + report->content_length;
	padded_length = round_up(report_length,	4);
	padding = padded_length - report_length;
	memset(&buf->content[report->content_length], 0, padding);

	ret = spi_hid_output(shid, buf, padded_length);
	if (ret) {
		dev_err(dev, "Failed output transfer\n");
		goto out;
	}

	return 0;

out:
	return ret;
}

static int spi_hid_sync_request(struct spi_hid *shid,
		struct spi_hid_output_report *report)
{
	struct device *dev = &shid->spi->dev;
	int ret = 0;

	ret = spi_hid_send_output_report(shid, report);
	if (ret) {
		dev_err(dev, "Failed to transfer output report\n");
		return ret;
	}

	mutex_unlock(&shid->lock);
	ret = wait_for_completion_interruptible_timeout(&shid->output_done,
			msecs_to_jiffies(1000));
	mutex_lock(&shid->lock);
	if (ret == 0) {
		dev_err(dev, "Response timed out\n");
		return -ETIMEDOUT;
	}

	return 0;
}

/**
 * Handle the reset response from the FW by sending a request for the device
 * descriptor.
 */
static void spi_hid_reset_work(struct work_struct *work)
{
	struct spi_hid *shid =
		container_of(work, struct spi_hid, reset_work);
	struct device *dev = &shid->spi->dev;
	struct spi_hid_output_report report = {
		.report_type = SPI_HID_OUTPUT_REPORT_TYPE_DEVICE_DESC_REQUEST,
		.content_length = 0x0,
		.content_id = SPI_HID_OUTPUT_REPORT_CONTENT_ID_DESC_REQUEST,
		.content = NULL,
	};
	int ret;

	trace_spi_hid_reset_work(shid);

	if (shid->ready) {
		dev_err(dev, "Spontaneous FW reset!");
		shid->ready = false;
		sysfs_notify(&dev->kobj, NULL, "ready");
		shid->dir_count++;
	}

	if (shid->power_state == SPI_HID_POWER_MODE_OFF)
		return;

	if (flush_work(&shid->create_device_work))
		dev_err(dev, "Reset handler waited for create_device_work");

	if (flush_work(&shid->refresh_device_work))
		dev_err(dev, "Reset handler waited for refresh_device_work");

	mutex_lock(&shid->lock);
	ret = spi_hid_sync_request(shid, &report);
	mutex_unlock(&shid->lock);
	if (ret) {
		dev_WARN_ONCE(dev, true,
				"Failed to send device descriptor request\n");
		spi_hid_error_handler(shid);
	}
}

static int spi_hid_input_report_handler(struct spi_hid *shid,
		struct spi_hid_input_buf *buf)
{
	struct device *dev = &shid->spi->dev;
	struct spi_hid_input_report r;
	int ret;

	trace_spi_hid_input_report_handler(shid);

	if (!shid->ready || shid->refresh_in_progress || !shid->hid)
		return 0;

	spi_hid_input_report_prepare(buf, &r);

	ret = hid_input_report(shid->hid, HID_INPUT_REPORT,
			r.content - 1,
			r.content_length + 1, 1);

	if (ret == -ENODEV || ret == -EBUSY) {
		dev_err(dev, "ignoring report --> %d\n", ret);
		return 0;
	} else if (ret) {
		dev_err(dev, "Bad input report, error %d\n", ret);
	}

	return ret;
}

static void spi_hid_response_handler(struct spi_hid *shid,
		struct spi_hid_input_buf *buf)
{
	trace_spi_hid_response_handler(shid);

	/* completion_done returns 0 if there are waiters, otherwise 1 */
	if (completion_done(&shid->output_done)) {
		dev_err(&shid->spi->dev, "Unexpected response report\n");
	} else {
		if (shid->input.body[0] ==
				SPI_HID_INPUT_REPORT_TYPE_REPORT_DESC ||
			shid->input.body[0] ==
				SPI_HID_INPUT_REPORT_TYPE_GET_FEATURE_RESP) {
			size_t response_length = (shid->input.body[1] |
					(shid->input.body[2] << 8)) +
					sizeof(shid->input.body);
			memcpy(shid->response.body, shid->input.body,
							response_length);
		}
		complete(&shid->output_done);
	}
}

/*
 * This function returns the length of the report descriptor, or a negative
 * error code if something went wrong.
 */
static int spi_hid_report_descriptor_request(struct spi_hid *shid)
{
	int ret;
	struct device *dev = &shid->spi->dev;
	struct spi_hid_output_report report = {
		.report_type = SPI_HID_OUTPUT_REPORT_TYPE_REPORT_DESC_REQUEST,
		.content_length = 0,
		.content_id = SPI_HID_OUTPUT_REPORT_CONTENT_ID_DESC_REQUEST,
		.content = NULL,
	};

	ret =  spi_hid_sync_request(shid, &report);
	if (ret) {
		dev_err(dev,
			"Expected report descriptor not received! Error %d\n",
			ret);
		spi_hid_error_handler(shid);
		goto out;
	}

	ret = (shid->response.body[1] | (shid->response.body[2] << 8));
	if (ret != shid->desc.report_descriptor_length) {
		dev_err(dev,
			"Received report descriptor length doesn't match device descriptor field, using min of the two\n");
		ret = min_t(unsigned int, ret,
			shid->desc.report_descriptor_length);
	}
out:
	return ret;
}

static void spi_hid_process_input_report(struct spi_hid *shid,
		struct spi_hid_input_buf *buf)
{
	struct spi_hid_input_header header;
	struct spi_hid_input_body body;
	struct device *dev = &shid->spi->dev;
	struct spi_hid_device_desc_raw *raw;
	int ret;

	trace_spi_hid_process_input_report(shid);

	spi_hid_populate_input_header(buf->header, &header);
	spi_hid_populate_input_body(buf->body, &body);

	if (body.content_length > header.report_length) {
		dev_err(dev, "Bad body length %d > %d\n", body.content_length,
							header.report_length);
		schedule_work(&shid->error_work);
		return;
	}

	switch (body.report_type) {
	case SPI_HID_INPUT_REPORT_TYPE_DATA:
		ret = spi_hid_input_report_handler(shid, buf);
		if (ret)
			schedule_work(&shid->error_work);
		break;
	case SPI_HID_INPUT_REPORT_TYPE_RESET_RESP:
		schedule_work(&shid->reset_work);
		break;
	case SPI_HID_INPUT_REPORT_TYPE_DEVICE_DESC:
		/* Mark the completion done to avoid timeout */
		spi_hid_response_handler(shid, buf);

		/* Reset attempts at every device descriptor fetch */
		shid->attempts = 0;

		raw = (struct spi_hid_device_desc_raw *)buf->content;

		/* Validate device descriptor length before parsing */
		if (body.content_length != SPI_HID_DEVICE_DESCRIPTOR_LENGTH) {
			dev_err(dev,
				"Invalid content length %d, expected %d\n",
				body.content_length,
				SPI_HID_DEVICE_DESCRIPTOR_LENGTH);
			schedule_work(&shid->error_work);
			break;
		}

		if (le16_to_cpu(raw->wDeviceDescLength) !=
					SPI_HID_DEVICE_DESCRIPTOR_LENGTH) {
			dev_err(dev,
				"Invalid wDeviceDescLength %d, expected %d\n",
				raw->wDeviceDescLength,
				SPI_HID_DEVICE_DESCRIPTOR_LENGTH);
			schedule_work(&shid->error_work);
			break;
		}

		spi_hid_parse_dev_desc(raw, &shid->desc);

		if (shid->desc.hid_version != SPI_HID_SUPPORTED_VERSION) {
			dev_err(dev,
				"Unsupported device descriptor version %4x\n",
				shid->desc.hid_version);
			schedule_work(&shid->error_work);
			break;
		}

		if (!shid->hid)
			schedule_work(&shid->create_device_work);
		else
			schedule_work(&shid->refresh_device_work);

		break;
	case SPI_HID_INPUT_REPORT_TYPE_SET_OUTPUT_REPORT_RESP:
		if (shid->desc.no_output_report_ack) {
			dev_err(dev, "Unexpected output report response\n");
			break;
		}
		fallthrough;
	case SPI_HID_INPUT_REPORT_TYPE_GET_FEATURE_RESP:
	case SPI_HID_INPUT_REPORT_TYPE_SET_FEATURE_RESP:
		if (!shid->ready) {
			dev_err(dev,
				"Unexpected response report while not ready: 0x%x\n",
				body.report_type);
			break;
		}
		fallthrough;
	case SPI_HID_INPUT_REPORT_TYPE_REPORT_DESC:
		spi_hid_response_handler(shid, buf);
		break;
	/*
	 * FIXME: sending GET_INPUT and COMMAND reports not supported, thus
	 * throw away responses to those, they should never come.
	 */
	case SPI_HID_INPUT_REPORT_TYPE_GET_INPUT_REPORT_RESP:
	case SPI_HID_INPUT_REPORT_TYPE_COMMAND_RESP:
		dev_err(dev, "Not a supported report type: 0x%x\n",
							body.report_type);
		break;
	default:
		dev_err(dev, "Unknown input report: 0x%x\n",
							body.report_type);
		schedule_work(&shid->error_work);
		break;
	}
}

static int spi_hid_bus_validate_header(struct spi_hid *shid,
					struct spi_hid_input_header *header)
{
	struct device *dev = &shid->spi->dev;

	if (header->version != SPI_HID_INPUT_HEADER_VERSION) {
		dev_err(dev, "Unknown input report version (v 0x%x)\n",
				header->version);
		return -EINVAL;
	}

	if (shid->desc.max_input_length != 0 &&
			header->report_length > shid->desc.max_input_length) {
		dev_err(dev, "Input report body size %u > max expected of %u\n",
				header->report_length,
				shid->desc.max_input_length);
		return -EMSGSIZE;
	}

	if (header->last_fragment_flag != 1) {
		dev_err(dev, "Multi-fragment reports not supported\n");
		return -EOPNOTSUPP;
	}

	if (header->sync_const != SPI_HID_INPUT_HEADER_SYNC_BYTE) {
		dev_err(dev, "Invalid input report sync constant (0x%x)\n",
				header->sync_const);
		return -EINVAL;
	}

	return 0;
}

static int spi_hid_create_device(struct spi_hid *shid)
{
	struct hid_device *hid;
	struct device *dev = &shid->spi->dev;
	int ret;

	hid = hid_allocate_device();

	if (IS_ERR(hid)) {
		dev_err(dev, "Failed to allocate hid device: %ld\n",
				PTR_ERR(hid));
		ret = PTR_ERR(hid);
		return ret;
	}

	hid->driver_data = shid->spi;
	hid->ll_driver = &spi_hid_ll_driver;
	hid->dev.parent = &shid->spi->dev;
	hid->bus = BUS_SPI;
	hid->version = shid->desc.hid_version;
	hid->vendor = shid->desc.vendor_id;
	hid->product = shid->desc.product_id;

	snprintf(hid->name, sizeof(hid->name), "spi %04hX:%04hX",
			hid->vendor, hid->product);
	strscpy(hid->phys, dev_name(&shid->spi->dev), sizeof(hid->phys));

	shid->hid = hid;

	ret = hid_add_device(hid);
	if (ret) {
		dev_err(dev, "Failed to add hid device: %d\n", ret);
		/*
		 * We likely got here because report descriptor request timed
		 * out. Let's disconnect and destroy the hid_device structure.
		 */
		hid = spi_hid_disconnect_hid(shid);
		if (hid)
			hid_destroy_device(hid);
		return ret;
	}

	return 0;
}

static void spi_hid_create_device_work(struct work_struct *work)
{
	struct spi_hid *shid =
		container_of(work, struct spi_hid, create_device_work);
	struct device *dev = &shid->spi->dev;
	u8 prev_state = shid->power_state;
	int ret;

	trace_spi_hid_create_device_work(shid);

	ret = spi_hid_create_device(shid);
	if (ret) {
		dev_err(dev, "Failed to create hid device\n");
		return;
	}

	spi_hid_suspend(shid);

	shid->attempts = 0;

	dev_dbg(dev, "%s: %s -> %s\n", __func__,
			spi_hid_power_mode_string(prev_state),
			spi_hid_power_mode_string(shid->power_state));
}

static void spi_hid_refresh_device_work(struct work_struct *work)
{
	struct spi_hid *shid =
		container_of(work, struct spi_hid, refresh_device_work);
	struct device *dev = &shid->spi->dev;
	struct hid_device *hid;
	int ret;
	u32 new_crc32;

	trace_spi_hid_refresh_device_work(shid);

	mutex_lock(&shid->lock);
	ret = spi_hid_report_descriptor_request(shid);
	mutex_unlock(&shid->lock);
	if (ret < 0) {
		dev_err(dev,
			"Refresh: failed report descriptor request, error %d",
			ret);
		return;
	}

	new_crc32 = crc32_le(0, (unsigned char const *)shid->response.content,
								(size_t)ret);
	if (new_crc32 == shid->report_descriptor_crc32) {
		shid->ready = true;
		sysfs_notify(&dev->kobj, NULL, "ready");
		return;
	}

	shid->report_descriptor_crc32 = new_crc32;
	shid->refresh_in_progress = true;

	hid = spi_hid_disconnect_hid(shid);
	if (hid)
		hid_destroy_device(hid);

	ret = spi_hid_create_device(shid);
	if (ret)
		dev_err(dev, "Failed to create hid device\n");

	shid->refresh_in_progress = false;
	shid->ready = true;
	sysfs_notify(&dev->kobj, NULL, "ready");
}

static void spi_hid_input_header_complete(void *_shid);

static void spi_hid_input_body_complete(void *_shid)
{
	struct spi_hid *shid = _shid;
	struct device *dev = &shid->spi->dev;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&shid->input_lock, flags);

	if (shid->power_state == SPI_HID_POWER_MODE_OFF) {
		dev_warn(dev,
			"input body complete called while device is off\n");
		goto out;
	}

	trace_spi_hid_input_body_complete(shid,
			shid->input_transfer[0].tx_buf,
			shid->input_transfer[0].len,
			shid->input_transfer[1].rx_buf,
			shid->input_transfer[1].len,
			shid->input_message.status);

	if (shid->input_message.status < 0) {
		dev_warn(dev, "error reading body, resetting %d\n",
				shid->input_message.status);
		shid->bus_error_count++;
		shid->bus_last_error = shid->input_message.status;
		schedule_work(&shid->error_work);
		goto out;
	}

	spi_hid_process_input_report(shid, &shid->input);

	if (--shid->input_transfer_pending) {
		struct spi_hid_input_buf *buf = &shid->input;

		trace_spi_hid_header_transfer(shid);
		ret = spi_hid_input_async(shid, buf->header,
				sizeof(buf->header),
				spi_hid_input_header_complete, true);
		if (ret)
			dev_err(dev, "failed to start header transfer %d\n",
									ret);
	}

out:
	spin_unlock_irqrestore(&shid->input_lock, flags);
}

static void spi_hid_input_header_complete(void *_shid)
{
	struct spi_hid *shid = _shid;
	struct device *dev = &shid->spi->dev;
	struct spi_hid_input_header header;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&shid->input_lock, flags);

	if (shid->power_state == SPI_HID_POWER_MODE_OFF) {
		dev_warn(dev,
			"input header complete called while device is off\n");
		goto out;
	}

	trace_spi_hid_input_header_complete(shid,
			shid->input_transfer[0].tx_buf,
			shid->input_transfer[0].len,
			shid->input_transfer[1].rx_buf,
			shid->input_transfer[1].len,
			shid->input_message.status);

	if (shid->input_message.status < 0) {
		dev_warn(dev, "error reading header, resetting, error %d\n",
				shid->input_message.status);
		shid->bus_error_count++;
		shid->bus_last_error = shid->input_message.status;
		schedule_work(&shid->error_work);
		goto out;
	}
	spi_hid_populate_input_header(shid->input.header, &header);

	ret = spi_hid_bus_validate_header(shid, &header);
	if (ret) {
		dev_err(dev, "failed to validate header: %d\n", ret);
		print_hex_dump(KERN_ERR, "spi_hid: header buffer: ",
						DUMP_PREFIX_NONE, 16, 1,
						shid->input.header,
						sizeof(shid->input.header),
						false);
		shid->bus_error_count++;
		shid->bus_last_error = ret;
		goto out;
	}

	ret = spi_hid_input_async(shid, shid->input.body, header.report_length,
			spi_hid_input_body_complete, false);
	if (ret)
		dev_err(dev, "failed body async transfer: %d\n", ret);

out:
	if (ret)
		shid->input_transfer_pending = 0;

	spin_unlock_irqrestore(&shid->input_lock, flags);
}

static int spi_hid_get_request(struct spi_hid *shid, u8 content_id)
{
	int ret;
	struct device *dev = &shid->spi->dev;
	struct spi_hid_output_report report = {
		.report_type = SPI_HID_OUTPUT_REPORT_TYPE_HID_GET_FEATURE,
		.content_length = 0,
		.content_id = content_id,
		.content = NULL,
	};

	ret = spi_hid_sync_request(shid, &report);
	if (ret) {
		dev_err(dev,
			"Expected get request response not received! Error %d\n",
			ret);
		schedule_work(&shid->error_work);
	}

	return ret;
}

static int spi_hid_set_request(struct spi_hid *shid,
		u8 *arg_buf, u16 arg_len, u8 content_id)
{
	struct spi_hid_output_report report = {
		.report_type = SPI_HID_OUTPUT_REPORT_TYPE_HID_SET_FEATURE,
		.content_length = arg_len,
		.content_id = content_id,
		.content = arg_buf,
	};

	return spi_hid_sync_request(shid, &report);
}

static irqreturn_t spi_hid_dev_irq(int irq, void *_shid)
{
	struct spi_hid *shid = _shid;
	struct device *dev = &shid->spi->dev;
	int ret = 0;

	spin_lock(&shid->input_lock);
	trace_spi_hid_dev_irq(shid, irq);

	if (shid->input_transfer_pending++)
		goto out;

	trace_spi_hid_header_transfer(shid);
	ret = spi_hid_input_async(shid, shid->input.header,
			sizeof(shid->input.header),
			spi_hid_input_header_complete, true);
	if (ret)
		dev_err(dev, "Failed to start header transfer: %d\n", ret);

out:
	spin_unlock(&shid->input_lock);

	return IRQ_HANDLED;
}

/* hid_ll_driver interface functions */

static int spi_hid_ll_start(struct hid_device *hid)
{
	struct spi_device *spi = hid->driver_data;
	struct spi_hid *shid = spi_get_drvdata(spi);

	if (shid->desc.max_input_length < HID_MIN_BUFFER_SIZE) {
		dev_err(&shid->spi->dev,
			"HID_MIN_BUFFER_SIZE > max_input_length (%d)\n",
			shid->desc.max_input_length);
		return -EINVAL;
	}

	return 0;
}

static void spi_hid_ll_stop(struct hid_device *hid)
{
	hid->claimed = 0;
}

static int spi_hid_ll_open(struct hid_device *hid)
{
	struct spi_device *spi = hid->driver_data;
	struct spi_hid *shid = spi_get_drvdata(spi);
	struct device *dev = &spi->dev;
	u8 prev_state = shid->power_state;

	if (shid->refresh_in_progress)
		return 0;

	spi_hid_resume(shid);

	dev_dbg(dev, "%s: %s -> %s\n", __func__,
			spi_hid_power_mode_string(prev_state),
			spi_hid_power_mode_string(shid->power_state));

	return 0;
}

static void spi_hid_ll_close(struct hid_device *hid)
{
	struct spi_device *spi = hid->driver_data;
	struct spi_hid *shid = spi_get_drvdata(spi);
	struct device *dev = &spi->dev;
	u8 prev_state = shid->power_state;

	if (shid->refresh_in_progress)
		return;

	spi_hid_suspend(shid);

	shid->attempts = 0;

	dev_dbg(dev, "%s: %s -> %s\n", __func__,
			spi_hid_power_mode_string(prev_state),
			spi_hid_power_mode_string(shid->power_state));
}

static int spi_hid_ll_power(struct hid_device *hid, int level)
{
	struct spi_device *spi = hid->driver_data;
	struct spi_hid *shid = spi_get_drvdata(spi);
	int ret = 0;

	mutex_lock(&shid->lock);
	if (!shid->hid)
		ret = -ENODEV;
	mutex_unlock(&shid->lock);

	return ret;
}

static int spi_hid_ll_parse(struct hid_device *hid)
{
	struct spi_device *spi = hid->driver_data;
	struct spi_hid *shid = spi_get_drvdata(spi);
	struct device *dev = &spi->dev;
	int ret, len;

	mutex_lock(&shid->lock);

	len = spi_hid_report_descriptor_request(shid);
	if (len < 0) {
		dev_err(dev, "Report descriptor request failed, %d\n", len);
		ret = len;
		goto out;
	}

	/*
	 * FIXME: below call returning 0 doesn't mean that the report descriptor
	 * is good. We might be caching a crc32 of a corrupted r. d. or who
	 * knows what the FW sent. Need to have a feedback loop about r. d.
	 * being ok and only then cache it.
	 */
	ret = hid_parse_report(hid, (__u8 *)shid->response.content, len);
	if (ret)
		dev_err(dev, "failed parsing report: %d\n", ret);
	else
		shid->report_descriptor_crc32 = crc32_le(0,
				(unsigned char const *)shid->response.content,
				len);

out:
	mutex_unlock(&shid->lock);

	return ret;
}

static int spi_hid_ll_raw_request(struct hid_device *hid,
		unsigned char reportnum, __u8 *buf, size_t len,
		unsigned char rtype, int reqtype)
{
	struct spi_device *spi = hid->driver_data;
	struct spi_hid *shid = spi_get_drvdata(spi);
	struct device *dev = &spi->dev;
	int ret;

	if (!shid->ready) {
		dev_err(&shid->spi->dev, "%s called in unready state\n",
								__func__);
		return -ENODEV;
	}

	mutex_lock(&shid->lock);

	switch (reqtype) {
	case HID_REQ_SET_REPORT:
		if (buf[0] != reportnum) {
			dev_err(dev, "report id mismatch\n");
			ret = -EINVAL;
			break;
		}

		ret = spi_hid_set_request(shid, &buf[1], len - 1,
				reportnum);
		if (ret) {
			dev_err(dev, "failed to set report\n");
			break;
		}

		ret = len;
		break;
	case HID_REQ_GET_REPORT:
		ret = spi_hid_get_request(shid, reportnum);
		if (ret) {
			dev_err(dev, "failed to get report\n");
			break;
		}

		ret = min_t(size_t, len,
			shid->response.body[1] | (shid->response.body[2] << 8));
		memcpy(buf, &shid->response.content, ret);
		break;
	default:
		dev_err(dev, "invalid request type\n");
		ret = -EIO;
	}

	mutex_unlock(&shid->lock);

	return ret;
}

static int spi_hid_ll_output_report(struct hid_device *hid,
		__u8 *buf, size_t len)
{
	int ret;
	struct spi_device *spi = hid->driver_data;
	struct spi_hid *shid = spi_get_drvdata(spi);
	struct device *dev = &spi->dev;
	struct spi_hid_output_report report = {
		.report_type = SPI_HID_OUTPUT_REPORT_TYPE_HID_OUTPUT_REPORT,
		.content_length = len - 1,
		.content_id = buf[0],
		.content = &buf[1],
	};

	mutex_lock(&shid->lock);
	if (!shid->ready) {
		dev_err(dev, "%s called in unready state\n", __func__);
		ret = -ENODEV;
		goto out;
	}

	if (shid->desc.no_output_report_ack)
		ret = spi_hid_send_output_report(shid, &report);
	else
		ret = spi_hid_sync_request(shid, &report);

	if (ret)
		dev_err(dev, "failed to send output report\n");

out:
	mutex_unlock(&shid->lock);

	if (ret > 0)
		return -ret;

	if (ret < 0)
		return ret;

	return len;
}

static struct hid_ll_driver spi_hid_ll_driver = {
	.start = spi_hid_ll_start,
	.stop = spi_hid_ll_stop,
	.open = spi_hid_ll_open,
	.close = spi_hid_ll_close,
	.power = spi_hid_ll_power,
	.parse = spi_hid_ll_parse,
	.output_report = spi_hid_ll_output_report,
	.raw_request = spi_hid_ll_raw_request,
};

static ssize_t ready_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct spi_hid *shid = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%s\n",
			shid->ready ? "ready" : "not ready");
}
static DEVICE_ATTR_RO(ready);

static ssize_t bus_error_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct spi_hid *shid = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d (%d)\n",
			shid->bus_error_count, shid->bus_last_error);
}
static DEVICE_ATTR_RO(bus_error_count);

static ssize_t regulator_error_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct spi_hid *shid = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d (%d)\n",
			shid->regulator_error_count,
			shid->regulator_last_error);
}
static DEVICE_ATTR_RO(regulator_error_count);

static ssize_t device_initiated_reset_count_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct spi_hid *shid = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", shid->dir_count);
}
static DEVICE_ATTR_RO(device_initiated_reset_count);

static const struct attribute *const spi_hid_attributes[] = {
	&dev_attr_ready.attr,
	&dev_attr_bus_error_count.attr,
	&dev_attr_regulator_error_count.attr,
	&dev_attr_device_initiated_reset_count.attr,
	NULL	/* Terminator */
};

static int spi_hid_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct spi_hid *shid;
	unsigned long irqflags;
	int ret;

	if (spi->irq <= 0) {
		dev_err(dev, "Missing IRQ\n");
		ret = spi->irq ?: -EINVAL;
		goto err0;
	}

	shid = devm_kzalloc(dev, sizeof(struct spi_hid), GFP_KERNEL);
	if (!shid) {
		ret = -ENOMEM;
		goto err0;
	}

	shid->spi = spi;
	shid->power_state = SPI_HID_POWER_MODE_ON;
	spi_set_drvdata(spi, shid);

	ret = sysfs_create_files(&dev->kobj, spi_hid_attributes);
	if (ret) {
		dev_err(dev, "Unable to create sysfs attributes\n");
		goto err0;
	}

	ret = spi_hid_of_populate_config(&shid->conf, dev);

	/* Using now populated conf let's pre-calculate the read approvals */
	spi_hid_populate_read_approvals(&shid->conf, shid->read_approval_header,
						shid->read_approval_body);

	mutex_init(&shid->lock);
	init_completion(&shid->output_done);

	spin_lock_init(&shid->input_lock);
	INIT_WORK(&shid->reset_work, spi_hid_reset_work);
	INIT_WORK(&shid->create_device_work, spi_hid_create_device_work);
	INIT_WORK(&shid->refresh_device_work, spi_hid_refresh_device_work);
	INIT_WORK(&shid->error_work, spi_hid_error_work);

	/*
	 * At the end of probe we initialize the device:
	 *   0) Default pinctrl in DT: assert reset, bias the interrupt line
	 *   1) sleep minimal reset delay
	 *   2) request IRQ
	 *   3) power up the device
	 *   4) sleep 5ms
	 *   5) deassert reset (high)
	 *   6) sleep 5ms
	 */

	spi_hid_of_sleep_minimal_reset_delay(&shid->conf);

	irqflags = irq_get_trigger_type(spi->irq) | IRQF_ONESHOT;
	ret = request_irq(spi->irq, spi_hid_dev_irq, irqflags,
			dev_name(&spi->dev), shid);
	if (ret)
		goto err1;

	ret = spi_hid_of_power_up(&shid->conf);
	if (ret) {
		dev_err(dev, "%s: could not power up\n", __func__);
		shid->regulator_error_count++;
		shid->regulator_last_error = ret;
		goto err1;
	}

	spi_hid_of_deassert_reset(&shid->conf);

	dev_err(dev, "%s: d3 -> %s\n", __func__,
			spi_hid_power_mode_string(shid->power_state));

	return 0;

err1:
	sysfs_remove_files(&dev->kobj, spi_hid_attributes);

err0:
	return ret;
}

static int spi_hid_remove(struct spi_device *spi)
{
	struct spi_hid *shid = spi_get_drvdata(spi);
	struct device *dev = &spi->dev;
	int ret;

	spi_hid_of_assert_reset(&shid->conf);
	ret = spi_hid_of_power_down(&shid->conf);
	if (ret) {
		dev_err(dev, "failed to disable regulator\n");
		shid->regulator_error_count++;
		shid->regulator_last_error = ret;
	}
	free_irq(spi->irq, shid);
	sysfs_remove_files(&dev->kobj, spi_hid_attributes);
	spi_hid_stop_hid(shid);

	return 0;
}

static const struct spi_device_id spi_hid_id_table[] = {
	{ "hid", 0 },
	{ "hid-over-spi", 0 },
	{ },
};
MODULE_DEVICE_TABLE(spi, spi_hid_id_table);

static struct spi_driver spi_hid_driver = {
	.driver = {
		.name	= "spi_hid",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(spi_hid_of_match),
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe		= spi_hid_probe,
	.remove		= spi_hid_remove,
	.id_table	= spi_hid_id_table,
};

module_spi_driver(spi_hid_driver);

MODULE_DESCRIPTION("HID over SPI transport driver");
MODULE_AUTHOR("Dmitry Antipov <dmanti@microsoft.com>");
MODULE_LICENSE("GPL");
