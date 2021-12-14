/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __TAG_QCA_H
#define __TAG_QCA_H

#define QCA_HDR_LEN	2
#define QCA_HDR_VERSION	0x2

#define QCA_HDR_RECV_VERSION		GENMASK(15, 14)
#define QCA_HDR_RECV_PRIORITY		GENMASK(13, 11)
#define QCA_HDR_RECV_TYPE		GENMASK(10, 6)
#define QCA_HDR_RECV_FRAME_IS_TAGGED	BIT(3)
#define QCA_HDR_RECV_SOURCE_PORT	GENMASK(2, 0)

/* Packet type for recv */
#define QCA_HDR_RECV_TYPE_NORMAL	0x0
#define QCA_HDR_RECV_TYPE_MIB		0x1
#define QCA_HDR_RECV_TYPE_RW_REG_ACK	0x2

#define QCA_HDR_XMIT_VERSION		GENMASK(15, 14)
#define QCA_HDR_XMIT_PRIORITY		GENMASK(13, 11)
#define QCA_HDR_XMIT_CONTROL		GENMASK(10, 8)
#define QCA_HDR_XMIT_FROM_CPU		BIT(7)
#define QCA_HDR_XMIT_DP_BIT		GENMASK(6, 0)

/* Packet type for xmit */
#define QCA_HDR_XMIT_TYPE_NORMAL	0x0
#define QCA_HDR_XMIT_TYPE_RW_REG	0x1

#define MDIO_CHECK_CODE_VAL		0x5

/* Specific define for in-band MDIO read/write with Ethernet packet */
#define QCA_HDR_MDIO_SEQ_LEN		4 /* 4 byte for the seq */
#define QCA_HDR_MDIO_COMMAND_LEN	4 /* 4 byte for the command */
#define QCA_HDR_MDIO_DATA1_LEN		4 /* First 4 byte for the mdio data */
#define QCA_HDR_MDIO_HEADER_LEN		(QCA_HDR_MDIO_SEQ_LEN + \
					QCA_HDR_MDIO_COMMAND_LEN + \
					QCA_HDR_MDIO_DATA1_LEN)

#define QCA_HDR_MDIO_DATA2_LEN		12 /* Other 12 byte for the mdio data */
#define QCA_HDR_MDIO_PADDING_LEN	34 /* Padding to reach the min Ethernet packet */

#define QCA_HDR_MDIO_PKG_LEN		(QCA_HDR_MDIO_HEADER_LEN + \
					QCA_HDR_LEN + \
					QCA_HDR_MDIO_DATA2_LEN + \
					QCA_HDR_MDIO_PADDING_LEN)

#define QCA_HDR_MDIO_SEQ_NUM		GENMASK(31, 0)  /* 63, 32 */
#define QCA_HDR_MDIO_CHECK_CODE		GENMASK(31, 29) /* 31, 29 */
#define QCA_HDR_MDIO_CMD		BIT(28)		/* 28 */
#define QCA_HDR_MDIO_LENGTH		GENMASK(23, 20) /* 23, 20 */
#define QCA_HDR_MDIO_ADDR		GENMASK(18, 0)  /* 18, 0 */

/* Special struct emulating a Ethernet header */
struct mdio_ethhdr {
	u32 command;		/* command bit 31:0 */
	u32 seq;		/* seq 63:32 */
	u32 mdio_data;		/* first 4byte mdio */
	__be16 hdr;		/* qca hdr */
} __packed;

#endif /* __TAG_QCA_H */
