// SPDX-License-Identifier: GPL-2.0
/*
 * ILITEK Touch IC driver for 23XX, 25XX and Lego series
 *
 * Copyright (C) 2011 ILI Technology Corporation.
 * Copyright (C) 2020 Luca Hsu <luca_hsu@ilitek.com>
 * Copyright (C) 2021 Joe Hung <joe_hung@ilitek.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/errno.h>
#include <linux/acpi.h>
#include <linux/input/touchscreen.h>
#include <asm/unaligned.h>
#include <linux/firmware.h>


#define ILITEK_TS_NAME					"ilitek_ts"
#define BL_V1_8						0x108
#define BL_V1_7						0x107
#define BL_V1_6						0x106

#define ILITEK_TP_CMD_GET_TP_RES			0x20
#define ILITEK_TP_CMD_GET_SCRN_RES			0x21
#define ILITEK_TP_CMD_SET_IC_SLEEP			0x30
#define ILITEK_TP_CMD_SET_IC_WAKE			0x31
#define ILITEK_TP_CMD_GET_FW_VER			0x40
#define ILITEK_TP_CMD_GET_PRL_VER			0x42
#define ILITEK_TP_CMD_GET_MCU_VER			0x61
#define ILITEK_TP_CMD_GET_IC_MODE			0xC0

#define ILITEK_TP_CMD_SET_MOD_CTRL			0xF0
#define ILITEK_TP_CMD_GET_SYS_BUSY			0x80
#define ILITEK_TP_CMD_SET_W_FLASH			0xCC
#define ILITEK_TP_CMD_SET_AP_MODE			0xC1
#define ILITEK_TP_CMD_SET_BL_MODE			0xC2
#define ILITEK_TP_CMD_GET_BLK_CRC			0xCD
#define ILITEK_TP_CMD_SET_W_DATA			0xC3
#define ILITEK_TP_CMD_SET_DATA_LEN			0xC9

#define REPORT_COUNT_ADDRESS				61
#define ILITEK_SUPPORT_MAX_POINT			40

#define ILITEK_CRC_POLY					0x8408
#define ILITEK_HEX_UPGRADE_SIZE				(256 * 1024)
#define ILITEK_UPGRADE_LEN				2048
#define MOD_BL						0x55
#define MOD_AP						0x5A

#define ENTER_NORMAL_MODE				0
#define ENTER_SUSPEND_MODE				3

struct ilitek_protocol_info {
	u16 ver;
	u8 ver_major;
};

struct ilitek_block_info {
	u32 start;
	u32 end;
	u16 ic_crc;
	u16 fw_crc;
	bool chk_crc;
};

struct ilitek_upgrade_info {
	u16 fw_mcu_ver;
	u32 map_ver;
	u32 blk_num;
	u32 fw_size;
	struct ilitek_block_info *blk;
};

struct ilitek_ts_data {
	struct i2c_client		*client;
	struct gpio_desc		*reset_gpio;
	struct input_dev		*input_dev;
	struct touchscreen_properties	prop;

	const struct ilitek_protocol_map *ptl_cb_func;
	struct ilitek_protocol_info	ptl;

	char				product_id[30];
	u16				mcu_ver;
	u8				ic_mode;
	u8				firmware_ver[8];

	s32				reset_time;
	s32				screen_max_x;
	s32				screen_max_y;
	s32				screen_min_x;
	s32				screen_min_y;
	s32				max_tp;

	/* FW Upgrade */
	struct ilitek_upgrade_info	upg;
};

struct ilitek_protocol_map {
	u16 cmd;
	const char *name;
	int (*func)(struct ilitek_ts_data *ts, u16 cmd, u8 *inbuf, u8 *outbuf);
};

enum ilitek_cmds {
	/* common cmds */
	GET_PTL_VER = 0,
	GET_FW_VER,
	GET_SCRN_RES,
	GET_TP_RES,
	GET_IC_MODE,
	GET_MCU_VER,
	SET_IC_SLEEP,
	SET_IC_WAKE,

	SET_MOD_CTRL,
	GET_SYS_BUSY,
	SET_FLASH_AP,
	SET_BL_MODE,
	SET_AP_MODE,
	GET_BLK_CRC,
	SET_DATA_LEN,
	SET_FLASH_BL,
	SET_W_DATA,

	/* ALWAYS keep at the end */
	MAX_CMD_CNT
};

/* ILITEK I2C R/W APIs */
static int ilitek_i2c_write_and_read(struct ilitek_ts_data *ts,
				     u8 *cmd, int write_len, int delay,
				     u8 *data, int read_len)
{
	int error;
	struct i2c_client *client = ts->client;
	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = write_len,
			.buf = cmd,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = read_len,
			.buf = data,
		},
	};

	if (delay == 0 && write_len > 0 && read_len > 0) {
		error = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
		if (error < 0)
			return error;
	} else {
		if (write_len > 0) {
			error = i2c_transfer(client->adapter, msgs, 1);
			if (error < 0)
				return error;
		}
		if (delay > 0)
			mdelay(delay);

		if (read_len > 0) {
			error = i2c_transfer(client->adapter, msgs + 1, 1);
			if (error < 0)
				return error;
		}
	}

	return 0;
}

/* ILITEK ISR APIs */
static void ilitek_touch_down(struct ilitek_ts_data *ts, unsigned int id,
			      unsigned int x, unsigned int y)
{
	struct input_dev *input = ts->input_dev;

	input_mt_slot(input, id);
	input_mt_report_slot_state(input, MT_TOOL_FINGER, true);

	touchscreen_report_pos(input, &ts->prop, x, y, true);
}

static int ilitek_process_and_report_v6(struct ilitek_ts_data *ts)
{
	int error = 0;
	u8 buf[512];
	int packet_len = 5;
	int packet_max_point = 10;
	int report_max_point;
	int i, count;
	struct input_dev *input = ts->input_dev;
	struct device *dev = &ts->client->dev;
	unsigned int x, y, status, id;

	error = ilitek_i2c_write_and_read(ts, NULL, 0, 0, buf, 64);
	if (error) {
		dev_err(dev, "get touch info failed, err:%d\n", error);
		goto err_sync_frame;
	}

	report_max_point = buf[REPORT_COUNT_ADDRESS];
	if (report_max_point > ts->max_tp) {
		dev_err(dev, "FW report max point:%d > panel info. max:%d\n",
			report_max_point, ts->max_tp);
		error = -EINVAL;
		goto err_sync_frame;
	}

	count = DIV_ROUND_UP(report_max_point, packet_max_point);
	for (i = 1; i < count; i++) {
		error = ilitek_i2c_write_and_read(ts, NULL, 0, 0,
						  buf + i * 64, 64);
		if (error) {
			dev_err(dev, "get touch info. failed, cnt:%d, err:%d\n",
				count, error);
			goto err_sync_frame;
		}
	}

	for (i = 0; i < report_max_point; i++) {
		status = buf[i * packet_len + 1] & 0x40;
		if (!status)
			continue;

		id = buf[i * packet_len + 1] & 0x3F;

		x = get_unaligned_le16(buf + i * packet_len + 2);
		y = get_unaligned_le16(buf + i * packet_len + 4);

		if (x > ts->screen_max_x || x < ts->screen_min_x ||
		    y > ts->screen_max_y || y < ts->screen_min_y) {
			dev_warn(dev, "invalid position, X[%d,%u,%d], Y[%d,%u,%d]\n",
				 ts->screen_min_x, x, ts->screen_max_x,
				 ts->screen_min_y, y, ts->screen_max_y);
			continue;
		}

		ilitek_touch_down(ts, id, x, y);
	}

err_sync_frame:
	input_mt_sync_frame(input);
	input_sync(input);
	return error;
}

/* APIs of cmds for ILITEK Touch IC */
static int api_protocol_set_cmd(struct ilitek_ts_data *ts,
				u16 idx, u8 *inbuf, u8 *outbuf)
{
	u16 cmd;
	int error;

	if (idx >= MAX_CMD_CNT)
		return -EINVAL;

	cmd = ts->ptl_cb_func[idx].cmd;
	error = ts->ptl_cb_func[idx].func(ts, cmd, inbuf, outbuf);
	if (error)
		return error;

	return 0;
}

static int ilitek_check_busy(struct ilitek_ts_data *ts, u32 timeout)
{
	int error;
	u8 buf[2];
	u32 t = 0;

	do {
		error = api_protocol_set_cmd(ts, GET_SYS_BUSY, NULL, buf);
		if (error)
			return error;

		if ((buf[0] & 0x51) == 0x50)
			return 0;

		msleep(20);
		t += 20;
	} while (timeout > t);

	return -EBUSY;
}

static int ilitek_set_flash_BL1_8(struct ilitek_ts_data *ts, u32 start, u32 end)
{
	u8 inbuf[64];

	inbuf[3] = start & 0xFF;
	inbuf[4] = (start >> 8) & 0xFF;
	inbuf[5] = (start >> 16) & 0xFF;
	inbuf[6] = end & 0xFF;
	inbuf[7] = (end >> 8) & 0xFF;
	inbuf[8] = (end >> 16) & 0xFF;
	return api_protocol_set_cmd(ts, SET_FLASH_BL, inbuf, NULL);
}

static int ilitek_set_data_len(struct ilitek_ts_data *ts, u32 data_len)
{
	u8 inbuf[3];

	inbuf[1] = data_len & 0xFF;
	inbuf[2] = (data_len >> 8) & 0xFF;
	return api_protocol_set_cmd(ts, SET_DATA_LEN, inbuf, NULL);
}

static int ilitek_get_blk_crc(struct ilitek_ts_data *ts, u32 start, u32 end,
			      u8 type, u16 *crc)
{
	u8 inbuf[8], outbuf[2];
	int error;

	inbuf[1] = type;
	inbuf[2] = start & 0xFF;
	inbuf[3] = (start >> 8) & 0xFF;
	inbuf[4] = (start >> 16) & 0xFF;
	inbuf[5] = end & 0xFF;
	inbuf[6] = (end >> 8) & 0xFF;
	inbuf[7] = (end >> 16) & 0xFF;

	error = api_protocol_set_cmd(ts, GET_BLK_CRC, inbuf, outbuf);
	if (error < 0)
		return error;

	*crc = get_unaligned_le16(outbuf);
	return 0;
}

static int ilitek_switch_BLmode(struct ilitek_ts_data *ts, bool to_BLmode)
{
	int error, i;
	struct device *dev = &ts->client->dev;
	u8 outbuf[64];

	error = api_protocol_set_cmd(ts, GET_IC_MODE, NULL, outbuf);
	if (error < 0)
		return error;

	dev_dbg(dev, "change mode:%x to %x\n", ts->ic_mode,
		(to_BLmode) ? MOD_BL : MOD_AP);

	if ((ts->ic_mode == MOD_AP && !to_BLmode) ||
	    (ts->ic_mode == MOD_BL && to_BLmode))
		return 0;

	for (i = 0; i < 5; i++) {
		error = api_protocol_set_cmd(ts, SET_FLASH_AP, NULL, NULL);
		if (error < 0)
			return error;
		msleep(20);

		error = api_protocol_set_cmd(ts, (to_BLmode) ? SET_BL_MODE :
					     SET_AP_MODE, NULL, NULL);
		if (error < 0)
			return error;

		msleep(500 + i * 100);

		error = api_protocol_set_cmd(ts, GET_IC_MODE, NULL, outbuf);
		if (error < 0)
			return error;

		if ((ts->ic_mode == MOD_AP && !to_BLmode) ||
		    (ts->ic_mode == MOD_BL && to_BLmode))
			return 0;
	}

	dev_err(dev, "switch mode failed, current mode:%X\n", ts->ic_mode);

	return -EFAULT;
}

static int ilitek_set_testmode(struct ilitek_ts_data *ts, bool testmode)
{
	u8 inbuf[3];
	int error;


	if (testmode)
		inbuf[1] = ENTER_SUSPEND_MODE;
	else
		inbuf[1] = ENTER_NORMAL_MODE;

	error = api_protocol_set_cmd(ts, SET_MOD_CTRL, inbuf, NULL);
	if (error < 0)
		return error;

	return 0;
}

static int api_protocol_get_ptl_ver(struct ilitek_ts_data *ts,
				    u16 cmd, u8 *inbuf, u8 *outbuf)
{
	int error;
	u8 buf[64];

	buf[0] = cmd;
	error = ilitek_i2c_write_and_read(ts, buf, 1, 5, outbuf, 3);
	if (error)
		return error;

	ts->ptl.ver = get_unaligned_be16(outbuf);
	ts->ptl.ver_major = outbuf[0];

	return 0;
}

static int api_protocol_get_mcu_ver(struct ilitek_ts_data *ts,
				    u16 cmd, u8 *inbuf, u8 *outbuf)
{
	int error;
	u8 buf[64];

	buf[0] = cmd;
	error = ilitek_i2c_write_and_read(ts, buf, 1, 5, outbuf, 32);
	if (error)
		return error;

	ts->mcu_ver = get_unaligned_le16(outbuf);
	memset(ts->product_id, 0, sizeof(ts->product_id));
	memcpy(ts->product_id, outbuf + 6, 26);

	return 0;
}

static int api_protocol_get_fw_ver(struct ilitek_ts_data *ts,
				   u16 cmd, u8 *inbuf, u8 *outbuf)
{
	int error;
	u8 buf[64];

	buf[0] = cmd;
	error = ilitek_i2c_write_and_read(ts, buf, 1, 5, outbuf, 8);
	if (error)
		return error;

	memcpy(ts->firmware_ver, outbuf, 8);

	return 0;
}

static int api_protocol_get_scrn_res(struct ilitek_ts_data *ts,
				     u16 cmd, u8 *inbuf, u8 *outbuf)
{
	int error;
	u8 buf[64];

	buf[0] = cmd;
	error = ilitek_i2c_write_and_read(ts, buf, 1, 5, outbuf, 8);
	if (error)
		return error;

	ts->screen_min_x = get_unaligned_le16(outbuf);
	ts->screen_min_y = get_unaligned_le16(outbuf + 2);
	ts->screen_max_x = get_unaligned_le16(outbuf + 4);
	ts->screen_max_y = get_unaligned_le16(outbuf + 6);

	return 0;
}

static int api_protocol_get_tp_res(struct ilitek_ts_data *ts,
				   u16 cmd, u8 *inbuf, u8 *outbuf)
{
	int error;
	u8 buf[64];

	buf[0] = cmd;
	error = ilitek_i2c_write_and_read(ts, buf, 1, 5, outbuf, 15);
	if (error)
		return error;

	ts->max_tp = outbuf[8];
	if (ts->max_tp > ILITEK_SUPPORT_MAX_POINT) {
		dev_err(&ts->client->dev, "Invalid MAX_TP:%d from FW\n",
			ts->max_tp);
		return -EINVAL;
	}

	return 0;
}

static int api_protocol_get_ic_mode(struct ilitek_ts_data *ts,
				    u16 cmd, u8 *inbuf, u8 *outbuf)
{
	int error;
	u8 buf[64];

	buf[0] = cmd;
	error = ilitek_i2c_write_and_read(ts, buf, 1, 5, outbuf, 2);
	if (error)
		return error;

	ts->ic_mode = outbuf[0];
	return 0;
}

static int api_protocol_set_ic_sleep(struct ilitek_ts_data *ts,
				     u16 cmd, u8 *inbuf, u8 *outbuf)
{
	u8 buf[64];

	buf[0] = cmd;
	return ilitek_i2c_write_and_read(ts, buf, 1, 0, NULL, 0);
}

static int api_protocol_set_ic_wake(struct ilitek_ts_data *ts,
				    u16 cmd, u8 *inbuf, u8 *outbuf)
{
	u8 buf[64];

	buf[0] = cmd;
	return ilitek_i2c_write_and_read(ts, buf, 1, 0, NULL, 0);
}

static int api_protocol_set_mode_ctrl(struct ilitek_ts_data *ts, u16 cmd,
				      u8 *inbuf, u8 *outbuf)
{
	inbuf[0] = cmd;
	inbuf[2] = 0;

	return ilitek_i2c_write_and_read(ts, inbuf, 3, 100, NULL, 0);
}

static int api_protocol_get_sys_busy(struct ilitek_ts_data *ts,
				     u16 cmd, u8 *inbuf, u8 *outbuf)
{
	u8 buf[64];

	buf[0] = cmd;
	return ilitek_i2c_write_and_read(ts, buf, 1, 1, outbuf, 1);
}

static int api_protocol_set_write_flash_ap(struct ilitek_ts_data *ts,
					   u16 cmd, u8 *inbuf, u8 *outbuf)
{
	u8 buf[64];

	buf[0] = cmd;
	buf[1] = 0x5A;
	buf[2] = 0xA5;
	return ilitek_i2c_write_and_read(ts, buf, 3, 0, NULL, 0);
}

static int api_protocol_set_write_flash_bl(struct ilitek_ts_data *ts,
					   u16 cmd, u8 *inbuf, u8 *outbuf)
{
	inbuf[0] = cmd;
	inbuf[1] = 0x5A;
	inbuf[2] = 0xA5;
	return ilitek_i2c_write_and_read(ts, inbuf, 9, 0, NULL, 0);
}

static int api_protocol_set_bl_mode(struct ilitek_ts_data *ts,
				    u16 cmd, u8 *inbuf, u8 *outbuf)
{
	u8 buf[64];

	buf[0] = cmd;
	return ilitek_i2c_write_and_read(ts, buf, 1, 0, NULL, 0);
}

static int api_protocol_set_ap_mode(struct ilitek_ts_data *ts,
				    u16 cmd, u8 *inbuf, u8 *outbuf)
{
	u8 buf[64];

	buf[0] = cmd;
	return ilitek_i2c_write_and_read(ts, buf, 1, 0, NULL, 0);
}

static int api_protocol_get_blk_crc(struct ilitek_ts_data *ts,
				    u16 cmd, u8 *inbuf, u8 *outbuf)
{
	int ret = 0;

	inbuf[0] = cmd;

	/* Ask FW to calculate CRC first */
	if (inbuf[1] == 0) {
		ret = ilitek_i2c_write_and_read(ts, inbuf, 8, 0, NULL, 0);
		if (ret < 0)
			return ret;
		ret = ilitek_check_busy(ts, 2500);
		if (ret < 0)
			return ret;
	}

	inbuf[1] = 1;
	return ilitek_i2c_write_and_read(ts, inbuf, 2, 1, outbuf, 2);
}

static int api_protocol_set_data_length(struct ilitek_ts_data *ts,
					u16 cmd, u8 *inbuf, u8 *outbuf)
{

	inbuf[0] = cmd;
	return ilitek_i2c_write_and_read(ts, inbuf, 3, 0, NULL, 0);
}

static int api_protocol_set_write_data(struct ilitek_ts_data *ts, u16 cmd,
				       u8 *inbuf, u8 *outbuf)
{

	inbuf[0] = cmd;

	return ilitek_i2c_write_and_read(ts, inbuf, ILITEK_UPGRADE_LEN + 1, 0,
					 NULL, 0);
}

static const struct ilitek_protocol_map ptl_func_map[] = {
	/* common cmds */
	[GET_PTL_VER] = {
		ILITEK_TP_CMD_GET_PRL_VER, "GET_PTL_VER",
		api_protocol_get_ptl_ver
	},
	[GET_FW_VER] = {
		ILITEK_TP_CMD_GET_FW_VER, "GET_FW_VER",
		api_protocol_get_fw_ver
	},
	[GET_SCRN_RES] = {
		ILITEK_TP_CMD_GET_SCRN_RES, "GET_SCRN_RES",
		api_protocol_get_scrn_res
	},
	[GET_TP_RES] = {
		ILITEK_TP_CMD_GET_TP_RES, "GET_TP_RES",
		api_protocol_get_tp_res
	},
	[GET_IC_MODE] = {
		ILITEK_TP_CMD_GET_IC_MODE, "GET_IC_MODE",
		api_protocol_get_ic_mode
	},
	[GET_MCU_VER] = {
		ILITEK_TP_CMD_GET_MCU_VER, "GET_MOD_VER",
		api_protocol_get_mcu_ver
	},
	[SET_IC_SLEEP] = {
		ILITEK_TP_CMD_SET_IC_SLEEP, "SET_IC_SLEEP",
		api_protocol_set_ic_sleep
	},
	[SET_IC_WAKE] = {
		ILITEK_TP_CMD_SET_IC_WAKE, "SET_IC_WAKE",
		api_protocol_set_ic_wake
	},
	[SET_MOD_CTRL] = {
		ILITEK_TP_CMD_SET_MOD_CTRL, "SET_MOD_CTRL",
		api_protocol_set_mode_ctrl
	},
	[GET_SYS_BUSY] = {
		ILITEK_TP_CMD_GET_SYS_BUSY, "GET_SYS_BUSY",
		api_protocol_get_sys_busy
	},
	[SET_FLASH_AP] = {
		ILITEK_TP_CMD_SET_W_FLASH, "SET_FLASH_AP",
		api_protocol_set_write_flash_ap
	},
	[SET_BL_MODE] = {
		ILITEK_TP_CMD_SET_BL_MODE, "SET_BL_MODE",
		api_protocol_set_bl_mode
	},
	[SET_AP_MODE] = {
		ILITEK_TP_CMD_SET_AP_MODE, "SET_AP_MODE",
		api_protocol_set_ap_mode
	},
	[GET_BLK_CRC] = {
		ILITEK_TP_CMD_GET_BLK_CRC, "GET_BLK_CRC",
		api_protocol_get_blk_crc
	},
	[SET_DATA_LEN] = {
		ILITEK_TP_CMD_SET_DATA_LEN, "SET_DATA_LEN",
		api_protocol_set_data_length
	},
	[SET_FLASH_BL] = {
		ILITEK_TP_CMD_SET_W_FLASH, "SET_FLASH_BL",
		api_protocol_set_write_flash_bl
	},
	[SET_W_DATA] = {
		ILITEK_TP_CMD_SET_W_DATA, "SET_W_DATA",
		api_protocol_set_write_data
	},
};

/* Probe APIs */
static void ilitek_reset(struct ilitek_ts_data *ts, int delay)
{
	if (ts->reset_gpio) {
		gpiod_set_value(ts->reset_gpio, 1);
		mdelay(10);
		gpiod_set_value(ts->reset_gpio, 0);
		mdelay(delay);
	}
}

static int ilitek_protocol_init(struct ilitek_ts_data *ts)
{
	int error;
	u8 outbuf[64];

	ts->ptl_cb_func = ptl_func_map;
	ts->reset_time = 600;

	error = api_protocol_set_cmd(ts, GET_PTL_VER, NULL, outbuf);
	if (error)
		return error;

	/* Protocol v3 is not support currently */
	if (ts->ptl.ver_major == 0x3 ||
	    ts->ptl.ver == BL_V1_6 ||
	    ts->ptl.ver == BL_V1_7)
		return -EINVAL;

	return 0;
}

static int ilitek_read_tp_info(struct ilitek_ts_data *ts, bool boot)
{
	u8 outbuf[256];
	int error;

	error = api_protocol_set_cmd(ts, GET_PTL_VER, NULL, outbuf);
	if (error)
		return error;

	error = api_protocol_set_cmd(ts, GET_MCU_VER, NULL, outbuf);
	if (error)
		return error;

	error = api_protocol_set_cmd(ts, GET_FW_VER, NULL, outbuf);
	if (error)
		return error;

	if (boot) {
		error = api_protocol_set_cmd(ts, GET_SCRN_RES, NULL,
					     outbuf);
		if (error)
			return error;
	}

	error = api_protocol_set_cmd(ts, GET_TP_RES, NULL, outbuf);
	if (error)
		return error;

	error = api_protocol_set_cmd(ts, GET_IC_MODE, NULL, outbuf);
	if (error)
		return error;

	return 0;
}

static int ilitek_input_dev_init(struct device *dev, struct ilitek_ts_data *ts)
{
	int error;
	struct input_dev *input;

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	ts->input_dev = input;
	input->name = ILITEK_TS_NAME;
	input->id.bustype = BUS_I2C;

	__set_bit(INPUT_PROP_DIRECT, input->propbit);

	input_set_abs_params(input, ABS_MT_POSITION_X,
			     ts->screen_min_x, ts->screen_max_x, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y,
			     ts->screen_min_y, ts->screen_max_y, 0, 0);

	touchscreen_parse_properties(input, true, &ts->prop);

	error = input_mt_init_slots(input, ts->max_tp,
				    INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (error) {
		dev_err(dev, "initialize MT slots failed, err:%d\n", error);
		return error;
	}

	error = input_register_device(input);
	if (error) {
		dev_err(dev, "register input device failed, err:%d\n", error);
		return error;
	}

	return 0;
}

static irqreturn_t ilitek_i2c_isr(int irq, void *dev_id)
{
	struct ilitek_ts_data *ts = dev_id;
	int error;

	error = ilitek_process_and_report_v6(ts);
	if (error < 0) {
		dev_err(&ts->client->dev, "[%s] err:%d\n", __func__, error);
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

static ssize_t firmware_version_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ilitek_ts_data *ts = i2c_get_clientdata(client);

	return scnprintf(buf, PAGE_SIZE,
			 "fw version: [%02X%02X.%02X%02X.%02X%02X.%02X%02X]\n",
			 ts->firmware_ver[0], ts->firmware_ver[1],
			 ts->firmware_ver[2], ts->firmware_ver[3],
			 ts->firmware_ver[4], ts->firmware_ver[5],
			 ts->firmware_ver[6], ts->firmware_ver[7]);
}
static DEVICE_ATTR_RO(firmware_version);

static ssize_t product_id_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ilitek_ts_data *ts = i2c_get_clientdata(client);

	return scnprintf(buf, PAGE_SIZE, "product id: [%04X], module: [%s]\n",
			 ts->mcu_ver, ts->product_id);
}
static DEVICE_ATTR_RO(product_id);

static u32 ilitek_find_endaddr(u32 start, u32 end, u8 *buf, bool ap)
{
	u32 addr;
	char ap_tag[16] = "ILITek AP CRC   ";
	char blk_tag[16] = "ILITek END TAG  ";
	char tag[32];

	memset(tag, 0xff, 32);
	if (ap)
		memcpy(tag + 16, ap_tag, 16);
	else
		memcpy(tag + 16, blk_tag, 16);

	for (addr = start; addr <= end - 32 &&
	     addr < ILITEK_HEX_UPGRADE_SIZE - 32; addr++) {
		if (!strncmp(buf + addr, tag, 32))
			return addr + 33;
	}

	return end;
}

static int ilitek_info_mapping(struct ilitek_ts_data *ts, u32 addr, u8 *buf)
{
	u32 idx, i, start, end;
	struct device *dev = &ts->client->dev;

	idx = addr + 0x06;
	ts->upg.fw_mcu_ver = get_unaligned_le16(buf + idx);

	idx = addr + 0x00;
	ts->upg.map_ver = buf[idx + 2] << 16 | buf[idx + 1] << 8 | buf[idx];

	if (ts->upg.map_ver < 0x10000)
		return -EINVAL;

	ts->upg.blk_num = buf[addr + 0x50];

	ts->upg.blk = kcalloc(ts->upg.blk_num, sizeof(struct ilitek_block_info),
			      GFP_KERNEL);
	if (!ts->upg.blk)
		return -ENOMEM;

	for (i = 0; i < ts->upg.blk_num; i++) {
		idx = addr + 0x54 + 3 * i;
		start = buf[idx + 2] << 16 | buf[idx + 1] << 8 | buf[idx];

		if (i == ts->upg.blk_num - 1)
			idx = addr + 123;
		else
			idx += 3;

		end = buf[idx + 2] << 16 | buf[idx + 1] << 8 | buf[idx];

		if (i == 0)
			end = ilitek_find_endaddr(start, end, buf, true);
		else
			end = ilitek_find_endaddr(start, end, buf, false);

		ts->upg.blk[i].start = start;
		ts->upg.blk[i].end = end;

		dev_dbg(dev, "block[%u] start: %#x, end: %#x\n", i, start, end);
	}

	return 0;
}

static u16 ilitek_update_crc(u16 crc, u8 newbyte)
{
	u8 i;

	crc = crc ^ newbyte;

	for (i = 0; i < 8; i++) {
		if (crc & 0x01) {
			crc = crc >> 1;
			crc ^= ILITEK_CRC_POLY;
		} else {
			crc = crc >> 1;
		}
	}
	return crc;
}

static u16 ilitek_get_fw_crc(u32 start, u32 end, u8 *buf)
{
	u16 CRC = 0;
	u32 i = 0;

	for (i = start; i <= end - 2; i++)
		CRC = ilitek_update_crc(CRC, buf[i]);

	return CRC;
}

static int ilitek_check_blk(struct ilitek_ts_data *ts, u8 *buf, bool *update)
{
	u32 i;
	int error;
	struct device *dev = &ts->client->dev;
	struct ilitek_block_info *blk = ts->upg.blk;

	for (i = 0; i < ts->upg.blk_num; i++) {
		error = ilitek_get_blk_crc(ts, blk[i].start, blk[i].end, 0,
					   &blk[i].ic_crc);
		if (error < 0) {
			dev_err(dev, "get blk crc failed, ret:%d\n", error);
			return error;
		}

		blk[i].fw_crc = ilitek_get_fw_crc(blk[i].start,
						   blk[i].end, buf);

		if (blk[i].ic_crc != blk[i].fw_crc) {
			blk[i].chk_crc = false;
			*update = true;
		} else
			blk[i].chk_crc = true;

		dev_dbg(dev, "block[%u]: start:%#x, end:%#x, ic crc:%#x, dri crc:%#x\n",
			i, blk[i].start, blk[i].end,
			blk[i].ic_crc, blk[i].fw_crc);
	}

	return 0;
}

static int ilitek_program_blk(struct ilitek_ts_data *ts, u8 *buf,
			      u32 cnt, const u32 len)
{
	int error;
	u32 i;
	u8 *inbuf;
	struct device *dev = &ts->client->dev;
	struct ilitek_block_info *blk = ts->upg.blk;

	inbuf = kmalloc(len + 1, GFP_KERNEL);
	if (!inbuf)
		return -ENOMEM;
	memset(inbuf, 0xff, len + 1);

	error = ilitek_set_flash_BL1_8(ts, blk[cnt].start, blk[cnt].end);
	if (error < 0)
		goto err_free;

	for (i = blk[cnt].start; i < blk[cnt].end; i += len) {
		memcpy(inbuf + 1, buf + i, len);

		error = api_protocol_set_cmd(ts, SET_W_DATA, inbuf, NULL);
		if (error < 0)
			goto err_free;

		error = ilitek_check_busy(ts, 2000);
		if (error < 0) {
			dev_err(dev, "check busy failed, ret:%d\n", error);
			goto err_free;
		}
	}

	error = ilitek_get_blk_crc(ts, blk[cnt].start, blk[cnt].end, 1,
				   &blk[cnt].ic_crc);
	if (error < 0) {
		dev_err(dev, "get blk crc failed, ret:%d\n", error);
		goto err_free;
	}

	if (blk[cnt].ic_crc != blk[cnt].fw_crc) {
		dev_err(dev, "ic_crc:%x dri_crc:%x not matched\n",
			blk[cnt].ic_crc, blk[cnt].fw_crc);
		error = -EFAULT;
	}

err_free:
	kfree(inbuf);
	return error;
}

static int ilitek_upgrade_BL1_8(struct ilitek_ts_data *ts, u8 *buf)
{
	int error;
	u32 cnt;
	struct device *dev = &ts->client->dev;
	struct ilitek_block_info *blk = ts->upg.blk;

	error = ilitek_set_data_len(ts, ILITEK_UPGRADE_LEN);
	if (error < 0)
		return error;

	for (cnt = 0; cnt < ts->upg.blk_num; cnt++) {
		if (blk[cnt].chk_crc == true)
			continue;

		error = ilitek_program_blk(ts, buf, cnt, ILITEK_UPGRADE_LEN);
		if (error < 0) {
			dev_err(dev, "upgrade failed, ret:%d\n", error);
			return error;
		}
	}

	return 0;
}

static int ilitek_upgrade_firmware(struct ilitek_ts_data *ts, u8 *buf)
{
	int error, retry = 2;
	struct device *dev = &ts->client->dev;
	u8 outbuf[64];
	bool need_update = false;

	/* Check MCU version between device and firmware file */
	if (ts->upg.fw_mcu_ver != ts->mcu_ver) {
		dev_err(dev, "MCU version (MCU:%x and FW:%x) not match\n",
			 ts->mcu_ver, ts->upg.fw_mcu_ver);
		return -EINVAL;
	}

Retry:
	if (--retry < 0) {
		dev_err(dev, "retry 2 times upgrade fail, ret:%d\n", error);
		return error;
	}

	ilitek_reset(ts, ts->reset_time);

	error = ilitek_set_testmode(ts, true);
	if (error < 0)
		goto Retry;

	error = ilitek_check_busy(ts, 1000);
	if (error < 0)
		goto Retry;

	error = ilitek_check_blk(ts, buf, &need_update);
	if (error < 0)
		goto Retry;

	if (need_update) {
		error = ilitek_switch_BLmode(ts, true);
		if (error < 0)
			goto Retry;

		/* get bootloader version */
		error = api_protocol_set_cmd(ts, GET_PTL_VER, NULL, outbuf);
		if (error < 0)
			goto Retry;

		error = ilitek_upgrade_BL1_8(ts, buf);
		if (error < 0)
			goto Retry;

		error = ilitek_switch_BLmode(ts, false);
		if (error < 0)
			goto Retry;
	}

	/* switch back to normal mode after reset */
	ilitek_reset(ts, ts->reset_time);

	error = ilitek_protocol_init(ts);
	if (error < 0)
		goto Retry;

	error = ilitek_read_tp_info(ts, true);
	if (error < 0)
		goto Retry;

	return 0;
}

static int ilitek_parse_hex(struct ilitek_ts_data *ts, u32 *fw_size, u8 *fw_buf)
{
	int error;
	char *fw_file;
	const struct firmware *fw;
	struct device *dev = &ts->client->dev;
	u32 i, len, addr, type, exaddr = 0;
	u8 info[4], data[16];

	fw_file = kasprintf(GFP_KERNEL, "ilitek_%04x.hex", ts->mcu_ver);
	if (!fw_file)
		return -ENOMEM;

	error = request_firmware(&fw, fw_file, dev);
	kfree(fw_file);
	if (error) {
		dev_err(dev, "request firmware:%s failed, ret:%d\n",
			fw_file, error);
		return error;
	}

	for (i = 0; i < fw->size; i++) {
		if (fw->data[i] == ':' ||
		    fw->data[i] == 0x0D ||
		    fw->data[i] == 0x0A)
			continue;

		error = hex2bin(info, fw->data + i, sizeof(info));
		if (error)
			goto release_fw;

		len = info[0];
		addr = get_unaligned_be16(info + 1);
		type = info[3];

		error = hex2bin(data, fw->data + i + 8, len);
		if (error)
			goto release_fw;

		switch (type) {
		case 0x01:
			goto release_fw;
		case 0x02:
			exaddr = get_unaligned_be16(data);
			exaddr <<= 4;
			break;
		case 0x04:
			exaddr = get_unaligned_be16(data);
			exaddr <<= 16;
			break;
		case 0xAC:
		case 0xAD:
			break;
		case 0x00:
			addr += exaddr;
			memcpy(fw_buf + addr, data, len);
			*fw_size = addr + len;
			break;
		default:
			dev_err(dev, "unexpected type:%x in hex\n", type);
			goto err_invalid;
		}

		i += 10 + (len * 2);
	}

err_invalid:
	error = -EINVAL;

release_fw:
	release_firmware(fw);

	return error;
}


static int ilitek_update_fw_v6(struct ilitek_ts_data *ts)
{
	u8 *fw_buf;
	int error;
	struct device *dev = &ts->client->dev;

	fw_buf = vmalloc(ILITEK_HEX_UPGRADE_SIZE);
	if (!fw_buf)
		return -ENOMEM;
	memset(fw_buf, 0xFF, ILITEK_HEX_UPGRADE_SIZE);

	error = ilitek_parse_hex(ts, &ts->upg.fw_size, fw_buf);
	if (error < 0) {
		dev_err(dev, "parse fw file failed, err:%d\n", error);
		goto err_free_buf;
	}

	error = ilitek_info_mapping(ts, 0x3020, fw_buf);
	if (error < 0) {
		dev_err(dev, "check hex mapping fail, ret:%d\n", error);
		goto err_free_buf;
	}

	error = ilitek_upgrade_firmware(ts, fw_buf);
	if (error < 0) {
		dev_err(dev, "upgrade fail, ret:%d\n", error);
		goto err_free_buf;
	}

	dev_dbg(dev, "update fw success\n");

err_free_buf:
	vfree(fw_buf);
	kfree(ts->upg.blk);

	return error;
}

static ssize_t update_fw_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	int error;
	struct i2c_client *client = to_i2c_client(dev);
	struct ilitek_ts_data *ts = i2c_get_clientdata(client);

	disable_irq(client->irq);

	error = ilitek_update_fw_v6(ts);

	enable_irq(client->irq);

	if (error < 0) {
		dev_err(dev, "ILITEK FW upgrade failed, ret:%d\n", error);
		return error;
	}

	dev_dbg(dev, "firmware upgrade success !\n");

	return count;
}
static DEVICE_ATTR_WO(update_fw);

static struct attribute *ilitek_sysfs_attrs[] = {
	&dev_attr_firmware_version.attr,
	&dev_attr_product_id.attr,
	&dev_attr_update_fw.attr,
	NULL
};

static struct attribute_group ilitek_attrs_group = {
	.attrs = ilitek_sysfs_attrs,
};

static int ilitek_ts_i2c_probe(struct i2c_client *client,
			       const struct i2c_device_id *id)
{
	struct ilitek_ts_data *ts;
	struct device *dev = &client->dev;
	int error;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(dev, "i2c check functionality failed\n");
		return -ENXIO;
	}

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->client = client;
	i2c_set_clientdata(client, ts);

	ts->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ts->reset_gpio)) {
		error = PTR_ERR(ts->reset_gpio);
		dev_err(dev, "request gpiod failed: %d", error);
		return error;
	}

	ilitek_reset(ts, 1000);

	error = ilitek_protocol_init(ts);
	if (error) {
		dev_err(dev, "protocol init failed: %d", error);
		return error;
	}

	error = ilitek_read_tp_info(ts, true);
	if (error) {
		dev_err(dev, "read tp info failed: %d", error);
		return error;
	}

	error = ilitek_input_dev_init(dev, ts);
	if (error) {
		dev_err(dev, "input dev init failed: %d", error);
		return error;
	}

	error = devm_request_threaded_irq(dev, ts->client->irq,
					  NULL, ilitek_i2c_isr, IRQF_ONESHOT,
					  "ilitek_touch_irq", ts);
	if (error) {
		dev_err(dev, "request threaded irq failed: %d\n", error);
		return error;
	}

	error = devm_device_add_group(dev, &ilitek_attrs_group);

	if (error) {
		dev_err(dev, "sysfs create group failed: %d\n", error);
		return error;
	}

	return 0;
}

static int __maybe_unused ilitek_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ilitek_ts_data *ts = i2c_get_clientdata(client);
	int error;

	disable_irq(client->irq);

	if (!device_may_wakeup(dev)) {
		error = api_protocol_set_cmd(ts, SET_IC_SLEEP, NULL, NULL);
		if (error)
			return error;
	}

	return 0;
}

static int __maybe_unused ilitek_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ilitek_ts_data *ts = i2c_get_clientdata(client);
	int error;

	if (!device_may_wakeup(dev)) {
		error = api_protocol_set_cmd(ts, SET_IC_WAKE, NULL, NULL);
		if (error)
			return error;

		ilitek_reset(ts, ts->reset_time);
	}

	enable_irq(client->irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(ilitek_pm_ops, ilitek_suspend, ilitek_resume);

static const struct i2c_device_id ilitek_ts_i2c_id[] = {
	{ ILITEK_TS_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, ilitek_ts_i2c_id);

#ifdef CONFIG_ACPI
static const struct acpi_device_id ilitekts_acpi_id[] = {
	{ "ILTK0001", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, ilitekts_acpi_id);
#endif

#ifdef CONFIG_OF
static const struct of_device_id ilitek_ts_i2c_match[] = {
	{.compatible = "ilitek,ili2130",},
	{.compatible = "ilitek,ili2131",},
	{.compatible = "ilitek,ili2132",},
	{.compatible = "ilitek,ili2316",},
	{.compatible = "ilitek,ili2322",},
	{.compatible = "ilitek,ili2323",},
	{.compatible = "ilitek,ili2326",},
	{.compatible = "ilitek,ili2520",},
	{.compatible = "ilitek,ili2521",},
	{ },
};
MODULE_DEVICE_TABLE(of, ilitek_ts_i2c_match);
#endif

static struct i2c_driver ilitek_ts_i2c_driver = {
	.driver = {
		.name = ILITEK_TS_NAME,
		.pm = &ilitek_pm_ops,
		.of_match_table = of_match_ptr(ilitek_ts_i2c_match),
		.acpi_match_table = ACPI_PTR(ilitekts_acpi_id),
	},
	.probe = ilitek_ts_i2c_probe,
	.id_table = ilitek_ts_i2c_id,
};
module_i2c_driver(ilitek_ts_i2c_driver);

MODULE_AUTHOR("ILITEK");
MODULE_DESCRIPTION("ILITEK I2C Touchscreen Driver");
MODULE_LICENSE("GPL");
