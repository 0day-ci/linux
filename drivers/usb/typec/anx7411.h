/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Copyright(c) 2022, Analogix Semiconductor. All rights reserved.
 *
 */

#ifndef __ANX7411_H__
#define __ANX7411_H__

#define TCPC_ADDRESS1		0x58
#define TCPC_ADDRESS2		0x56
#define TCPC_ADDRESS3		0x54
#define TCPC_ADDRESS4		0x52
#define SPI_ADDRESS1		0x7e
#define SPI_ADDRESS2		0x6e
#define SPI_ADDRESS3		0x64
#define SPI_ADDRESS4		0x62

struct anx7411_i2c_select {
	u8 tcpc_address;
	u8 spi_address;
};

#define VENDOR_ID		0x1F29
#define PRODUCT_ID		0x7411

/* TCPC register define */

#define TCPC_ROLE_CONTROL	0x1A

#define TCPC_COMMAND		0x23
#define TCPC_CMD_I2C_IDLE	0xFF
#define TCPC_CMD_LOOK4CONN	0x99
#define SINK_CTRL_EN		0x55
#define SINK_CTRL_DIS		0x44

#define ANALOG_CTRL_10		0xAA

#define STATUS_LEN		2
#define ALERT_0			0xCB
#define RECEIVED_MSG		BIT(7)
#define SOFTWARE_INT		BIT(6)
#define MSG_LEN			32
#define HEADER_LEN		2
#define MSG_HEADER		0x00
#define MSG_TYPE		0x01
#define MSG_RAWDATA		0x02
#define MSG_LEN_MASK		0x1F

#define ALERT_1			0xCC
#define INTP_POW_ON		BIT(7)
#define INTP_POW_OFF		BIT(6)

#define VBUS_THRESHOLD_H	0xDD
#define VBUS_THRESHOLD_L	0xDE

#define FW_CTRL_0		0xF0
#define UNSTRUCT_VDM_EN		BIT(0)
#define DELAY_200MS		BIT(1)
#define VSAFE0			0
#define VSAFE1			BIT(2)
#define VSAFE2			BIT(3)
#define VSAFE3			(BIT(2) | BIT(3))
#define FRS_EN			BIT(7)

#define FW_PARAM		0xF1
#define DONGLE_IOP		BIT(0)

#define FW_CTRL_2		0xF7
#define SINK_CTRL_DIS_FLAG	BIT(5)

/* SPI register define */
#define OCM_CTRL_0		0x6E
#define OCM_RESET		BIT(6)

#define MAX_VOLTAGE		0xAC
#define MAX_POWER		0xAD
#define MIN_POWER		0xAE

#define REQUEST_VOLTAGE		0xAF
#define VOLTAGE_UNIT		100 /* mV per unit */

#define REQUEST_CURRENT		0xB1
#define CURRENT_UNIT		50 /* mA per unit */

#define CMD_SEND_BUF		0xC0
#define CMD_RECV_BUF		0xE0

#define REQ_VOL_20V_IN_100MV	0xC8
#define REQ_CUR_2_25A_IN_50MA	0x2D
#define REQ_CUR_3_25A_IN_50MA	0x41

#define DEF_5V			5000
#define DEF_1_5A		1500

enum anx7411_typec_message_type {
	TYPE_SRC_CAP = 0x00,
	TYPE_SNK_CAP = 0x01,
	TYPE_SNK_IDENTITY = 0x02,
	TYPE_SVID = 0x03,
	TYPE_SET_SNK_DP_CAP = 0x08,
	TYPE_PSWAP_REQ = 0x10,
	TYPE_DSWAP_REQ = 0x11,
	TYPE_VDM = 0x14,
	TYPE_OBJ_REQ = 0x16,
	TYPE_DP_ALT_ENTER = 0x19,
	TYPE_DP_DISCOVER_MODES_INFO = 0x27,
	TYPE_GET_DP_CONFIG = 0x29,
	TYPE_DP_CONFIGURE = 0x2A,
	TYPE_GET_DP_DISCOVER_MODES_INFO = 0x2E,
	TYPE_GET_DP_ALT_ENTER = 0x2F,
};

#define REQUEST_CURRENT		0xB1
#define REQUEST_VOLTAGE		0xAF

#define FW_CTRL_1		0xB2
#define AUTO_PD_EN		BIT(1)
#define TRYSRC_EN		BIT(2)
#define TRYSNK_EN		BIT(3)
#define FORCE_SEND_RDO		BIT(6)

#define FW_VER			0xB4
#define FW_SUBVER		0xB5

#define INT_MASK		0xB6
#define INT_STS			0xB7
#define OCM_BOOT_UP		BIT(0)
#define OC_OV_EVENT		BIT(1)
#define VCONN_CHANGE		BIT(2)
#define VBUS_CHANGE		BIT(3)
#define CC_STATUS_CHANGE	BIT(4)
#define DATA_ROLE_CHANGE	BIT(5)
#define PR_CONSUMER_GOT_POWER	BIT(6)
#define HPD_STATUS_CHANGE	BIT(7)

#define SYSTEM_STSTUS		0xB8
/* 0: SINK off; 1: SINK on */
#define SINK_STATUS		BIT(1)
/* 0: VCONN off; 1: VCONN on*/
#define VCONN_STATUS		BIT(2)
/* 0: vbus off; 1: vbus on*/
#define VBUS_STATUS		BIT(3)
/* 1: host; 0:device*/
#define DATA_ROLE		BIT(5)
/* 0: Chunking; 1: Unchunked*/
#define SUPPORT_UNCHUNKING	BIT(6)
/* 0: HPD low; 1: HPD high*/
#define HPD_STATUS		BIT(7)

#define DATA_DFP		1
#define DATA_UFP		2
#define POWER_SOURCE		1
#define POWER_SINK		2

#define CC_STATUS		0xB9
#define CC1_RD			BIT(0)
#define CC2_RD			BIT(4)
#define CC1_RA			BIT(1)
#define CC2_RA			BIT(5)
#define CC1_RD			BIT(0)
#define CC1_RP(cc)		(((cc) >> 2) & 0x03)
#define CC2_RP(cc)		(((cc) >> 6) & 0x03)

#define PD_REV_INIT		0xBA

#define PD_EXT_MSG_CTRL		0xBB
#define SRC_CAP_EXT_REPLY	BIT(0)
#define MANUFACTURER_INFO_REPLY	BIT(1)
#define BATTERY_STS_REPLY	BIT(2)
#define BATTERY_CAP_REPLY	BIT(3)
#define ALERT_REPLY		BIT(4)
#define STATUS_REPLY		BIT(5)
#define PPS_STATUS_REPLY	BIT(6)
#define SNK_CAP_EXT_REPLY	BIT(7)

#define NO_CONNECT		0x00
#define USB3_1_CONNECTED	0x01
#define DP_ALT_4LANES		0x02
#define USB3_1_DP_2LANES	0x03
#define CC1_CONNECTED		0x01
#define CC2_CONNECTED		0x02
#define SELECT_PIN_ASSIGMENT_C	0x04
#define SELECT_PIN_ASSIGMENT_D	0x08
#define SELECT_PIN_ASSIGMENT_E	0x10
#define SELECT_PIN_ASSIGMENT_U	0x00
#define REDRIVER_ADDRESS	0x20
#define REDRIVER_OFFSET		0x00

#define DP_SVID			0xFF01
#define VDM_ACK			0x40
#define VDM_CMD_RES		0x00
#define VDM_CMD_DIS_ID		0x01
#define VDM_CMD_DIS_SVID	0x02
#define VDM_CMD_DIS_MOD		0x03
#define VDM_CMD_ENTER_MODE	0x04
#define VDM_CMD_EXIT_MODE	0x05
#define VDM_CMD_ATTENTION	0x06
#define VDM_CMD_GET_STS		0x10
#define VDM_CMD_AND_ACK_MASK	0x5F

#define MAX_ALTMODE		2

#define HAS_SOURCE_CAP		BIT(0)
#define HAS_SINK_CAP		BIT(1)
#define HAS_SINK_WATT		BIT(2)

enum anx7411_psy_state {
	/* copy from drivers/usb/typec/tcpm */
	ANX7411_PSY_OFFLINE = 0,
	ANX7411_PSY_FIXED_ONLINE,

	/* private */
	/* PD keep in, but disconnct power to bq25700,
	 * this state can be active when higher capacity adapter plug in,
	 * and change to ONLINE state when higher capacity adapter plug out
	 */
	ANX7411_PSY_HANG = 0xff,
};

struct typec_params {
	int request_current; /* ma */
	int request_voltage; /* mv */
	int cc_connect;
	int cc_orientation_valid;
	int cc_status;
	int data_role;
	int power_role;
	int vconn_role;
	int dp_altmode_enter;
	int cust_altmode_enter;
	struct usb_role_switch *role_sw;
	struct typec_port *port;
	struct typec_partner *partner;
	struct typec_mux *typec_mux;
	struct typec_switch *typec_switch;
	struct typec_altmode *amode[MAX_ALTMODE];
	struct typec_altmode *port_amode[MAX_ALTMODE];
	struct typec_displayport_data data;
	int pin_assignment;
	struct typec_capability caps;
	u32 src_pdo[PDO_MAX_OBJECTS];
	u32 sink_pdo[PDO_MAX_OBJECTS];
	u8 caps_flags;
	u8 src_pdo_nr;
	u8 sink_pdo_nr;
	u8 sink_watt;
	u8 sink_voltage;
};

struct anx7411_data {
	int fw_version;
	int fw_subversion;
	struct i2c_client *tcpc_client;
	struct i2c_client *spi_client;
	struct gpio_desc *intp_gpiod;
	struct fwnode_handle *connector_fwnode;
	struct typec_params typec;
	int intp_irq;
	struct work_struct work;
	struct workqueue_struct *workqueue;
	/* Lock for interrupt work queue */
	struct mutex lock;

	enum anx7411_psy_state psy_online;
	enum power_supply_usb_type usb_type;
	struct power_supply *psy;
	struct power_supply_desc psy_desc;
	struct device *dev;
};

#endif /* __ANX7411_H__ */
