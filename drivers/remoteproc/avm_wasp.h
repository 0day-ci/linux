/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019-2020 Andreas Böhler
 * Copyright (c) 2021-2022 Daniel Kestrel
 */

#ifndef __AVM_WASP_H
#define __AVM_WASP_H

#define WASP_CHUNK_SIZE			14
#define M_REGS_WASP_INDEX_MAX		7

#define WASP_ADDR			0x07
#define WASP_TIMEOUT_COUNT		1000
#define WASP_WAIT_TIMEOUT_COUNT		20

#define WASP_WRITE_SLEEP_US		20000
#define WASP_WAIT_SLEEP			100
#define WASP_POLL_SLEEP_US		200
#define WASP_BOOT_SLEEP_US		20000

#define WASP_RESP_RETRY			0x0102
#define WASP_RESP_OK			0x0002
#define WASP_RESP_WAIT			0x0401
#define WASP_RESP_COMPLETED		0x0000
#define WASP_RESP_READY_TO_START	0x0202
#define WASP_RESP_STARTING		0x00c9

#define WASP_CMD_SET_PARAMS		0x0c01
#define WASP_CMD_SET_CHECKSUM_3390	0x0801
#define WASP_CMD_SET_CHECKSUM_X490	0x0401
#define WASP_CMD_SET_DATA		0x0e01
#define WASP_CMD_START_FIRMWARE_3390	0x0201
#define WASP_CMD_START_FIRMWARE_X490	0x0001
#define WASP_CMD_START_FIRMWARE2_X490	0x0101

static const u32 start_addr = 0xbd003000;
static const u32 exec_addr = 0xbd003000;

static u16 m_regs_wasp[] = {0x0, 0x2, 0x4, 0x6, 0x8, 0xA, 0xC, 0xE};

static const char mac_data[WASP_CHUNK_SIZE] = {0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
		0xaa, 0x04, 0x20, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00};

enum {
	MODEL_3390,
	MODEL_X490,
	MODEL_UNKNOWN
} m_model = MODEL_UNKNOWN;

#define ETHER_TYPE_ATH_ECPS_FRAME	0x88bd
#define BUF_SIZE			1056
#define COUNTER_INCR			4
#define SEND_LOOP_TIMEOUT_SECONDS	60

#define MAX_PAYLOAD_SIZE		1028
#define CHUNK_SIZE			1024
#define WASP_HEADER_LEN			14

#define PACKET_START			0x1200
#define CMD_FIRMWARE_DATA		0x0104
#define CMD_START_FIRMWARE		0xd400

#define RESP_DISCOVER			0x0000
#define RESP_CONFIG			0x1000
#define RESP_OK				0x0100
#define RESP_STARTING			0x0200
#define RESP_ERROR			0x0300

enum {
	DOWNLOAD_TYPE_UNKNOWN = 0,
	DOWNLOAD_TYPE_FIRMWARE,
	DOWNLOAD_TYPE_CONFIG
} m_download_type = DOWNLOAD_TYPE_UNKNOWN;

static const u32 m_load_addr = 0x81a00000;

static char wasp_mac[] = {0x00, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa};

struct wasp_packet {
	union {
		u8 data[MAX_PAYLOAD_SIZE + WASP_HEADER_LEN];
		struct __packed {
			u16	packet_start;
			u8	pad_one[5];
			u16	command;
			u16	response;
			u16	counter;
			u8	pad_two;
			u8	payload[MAX_PAYLOAD_SIZE];
		};
	};
} __packed;

#endif /* __AVM_WASP_H */
