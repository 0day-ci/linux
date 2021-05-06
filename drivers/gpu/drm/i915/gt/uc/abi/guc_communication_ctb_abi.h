/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2021 Intel Corporation
 */

#ifndef _ABI_GUC_COMMUNICATION_CTB_ABI_H
#define _ABI_GUC_COMMUNICATION_CTB_ABI_H

#include <linux/types.h>
#include <linux/build_bug.h>

#include "guc_messages_abi.h"

/**
 * DOC: CT Buffer
 *
 * TBD
 */

/**
 * DOC: CTB Descriptor
 *
 *  +---+-------+--------------------------------------------------------------+
 *  |   | Bits  | Description                                                  |
 *  +===+=======+==============================================================+
 *  | 0 |  31:0 | **HEAD** - offset (in dwords) to the last dword that was     |
 *  |   |       | read from the `CT Buffer`_.                                  |
 *  |   |       | It can only be updated by the receiver.                      |
 *  +---+-------+--------------------------------------------------------------+
 *  | 1 |  31:0 | **TAIL** - offset (in dwords) to the last dword that was     |
 *  |   |       | written to the `CT Buffer`_.                                 |
 *  |   |       | It can only be updated by the sender.                        |
 *  +---+-------+--------------------------------------------------------------+
 *  | 2 |  31:0 | **STATUS** - status of the CTB                               |
 *  |   |       |                                                              |
 *  |   |       |   - _`GUC_CTB_STATUS_NO_ERROR` = 0 (normal operation)        |
 *  |   |       |   - _`GUC_CTB_STATUS_OVERFLOW` = 1 (head/tail too large)     |
 *  |   |       |   - _`GUC_CTB_STATUS_UNDERFLOW` = 2 (truncated message)      |
 *  |   |       |   - _`GUC_CTB_STATUS_MISMATCH` = 4 (head/tail modified)      |
 *  |   |       |   - _`GUC_CTB_STATUS_NO_BACKCHANNEL` = 8                     |
 *  |   |       |   - _`GUC_CTB_STATUS_MALFORMED_MSG` = 16                     |
 *  +---+-------+--------------------------------------------------------------+
 *  |...|       | RESERVED = MBZ                                               |
 *  +---+-------+--------------------------------------------------------------+
 *  | 15|  31:0 | RESERVED = MBZ                                               |
 *  +---+-------+--------------------------------------------------------------+
 */

struct guc_ct_buffer_desc {
	u32 head;
	u32 tail;
	u32 status;
#define GUC_CTB_STATUS_NO_ERROR				0
#define GUC_CTB_STATUS_OVERFLOW				(1 << 0)
#define GUC_CTB_STATUS_UNDERFLOW			(1 << 1)
#define GUC_CTB_STATUS_MISMATCH				(1 << 2)
#define GUC_CTB_STATUS_NO_BACKCHANNEL			(1 << 3)
#define GUC_CTB_STATUS_MALFORMED_MSG			(1 << 4)
	u32 reserved[13];
} __packed;
static_assert(sizeof(struct guc_ct_buffer_desc) == 64);

/**
 * DOC: CTB based communication
 *
 * The CTB (command transport buffer) communication between Host and GuC
 * is based on u32 data stream written to the shared buffer. One buffer can
 * be used to transmit data only in one direction (one-directional channel).
 *
 * Current status of the each buffer is stored in the buffer descriptor.
 * Buffer descriptor holds tail and head fields that represents active data
 * stream. The tail field is updated by the data producer (sender), and head
 * field is updated by the data consumer (receiver)::
 *
 *      +------------+
 *      | DESCRIPTOR |          +=================+============+========+
 *      +============+          |                 | MESSAGE(s) |        |
 *      | address    |--------->+=================+============+========+
 *      +------------+
 *      | head       |          ^-----head--------^
 *      +------------+
 *      | tail       |          ^---------tail-----------------^
 *      +------------+
 *      | size       |          ^---------------size--------------------^
 *      +------------+
 *
 * Each message in data stream starts with the single u32 treated as a header,
 * followed by optional set of u32 data that makes message specific payload::
 *
 *      +------------+---------+---------+---------+
 *      |         MESSAGE                          |
 *      +------------+---------+---------+---------+
 *      |   msg[0]   |   [1]   |   ...   |  [n-1]  |
 *      +------------+---------+---------+---------+
 *      |   MESSAGE  |       MESSAGE PAYLOAD       |
 *      +   HEADER   +---------+---------+---------+
 *      |            |    0    |   ...   |    n    |
 *      +======+=====+=========+=========+=========+
 *      | 31:16| code|         |         |         |
 *      +------+-----+         |         |         |
 *      |  15:5|flags|         |         |         |
 *      +------+-----+         |         |         |
 *      |   4:0|  len|         |         |         |
 *      +------+-----+---------+---------+---------+
 *
 *                   ^-------------len-------------^
 *
 * The message header consists of:
 *
 * - **len**, indicates length of the message payload (in u32)
 * - **code**, indicates message code
 * - **flags**, holds various bits to control message handling
 */

/*
 * Definition of the command transport message header (DW0)
 *
 * bit[4..0]	message len (in dwords)
 * bit[7..5]	reserved
 * bit[8]	response (G2H only)
 * bit[8]	write fence to desc (H2G only)
 * bit[9]	write status to H2G buff (H2G only)
 * bit[10]	send status back via G2H (H2G only)
 * bit[15..11]	reserved
 * bit[31..16]	action code
 */
#define GUC_CT_MSG_LEN_SHIFT			0
#define GUC_CT_MSG_LEN_MASK			0x1F
#define GUC_CT_MSG_IS_RESPONSE			(1 << 8)
#define GUC_CT_MSG_WRITE_FENCE_TO_DESC		(1 << 8)
#define GUC_CT_MSG_WRITE_STATUS_TO_BUFF		(1 << 9)
#define GUC_CT_MSG_SEND_STATUS			(1 << 10)
#define GUC_CT_MSG_ACTION_SHIFT			16
#define GUC_CT_MSG_ACTION_MASK			0xFFFF

#endif /* _ABI_GUC_COMMUNICATION_CTB_ABI_H */
