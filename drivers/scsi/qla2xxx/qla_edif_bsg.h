/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Marvell Fibre Channel HBA Driver
 * Copyright (C)  2018-	    Marvell
 *
 */
#ifndef __QLA_EDIF_BSG_H
#define __QLA_EDIF_BSG_H

/* BSG Vendor specific commands */
#define	ELS_MAX_PAYLOAD		1024
#ifndef	WWN_SIZE
#define WWN_SIZE		8       /* Size of WWPN, WWN & WWNN */
#endif
#define	VND_CMD_APP_RESERVED_SIZE	32

enum auth_els_sub_cmd {
	SEND_ELS = 0,
	SEND_ELS_REPLY,
	PULL_ELS,
};

struct extra_auth_els {
	enum auth_els_sub_cmd sub_cmd;
	uint32_t        extra_rx_xchg_address; // FC_ELS_ACC | FC_ELS_RJT
	uint8_t         extra_control_flags;
#define BSG_CTL_FLAG_INIT       0
#define BSG_CTL_FLAG_LS_ACC     1
#define BSG_CTL_FLAG_LS_RJT     2
#define BSG_CTL_FLAG_TRM        3
	uint8_t         extra_rsvd[3];
} __packed;

struct qla_bsg_auth_els_request {
	struct fc_bsg_request r;
	struct extra_auth_els e;
};

struct qla_bsg_auth_els_reply {
	struct fc_bsg_reply r;
	uint32_t rx_xchg_address;
};

struct app_id {
	int		app_vid;
	uint8_t		app_key[32];
} __packed;

struct app_start_reply {
	uint32_t	host_support_edif;	// 0=disable, 1=enable
	uint32_t	edif_enode_active;	// 0=disable, 1=enable
	uint32_t	edif_edb_active;	// 0=disable, 1=enable
	uint32_t	reserved[VND_CMD_APP_RESERVED_SIZE];
} __packed;

struct app_start {
	struct app_id	app_info;
	uint32_t	prli_to;	// timer plogi/prli to complete
	uint32_t	key_shred;	// timer before shredding old keys
	uint8_t         app_start_flags;
	uint8_t         reserved[VND_CMD_APP_RESERVED_SIZE - 1];
} __packed;

struct app_stop {
	struct app_id	app_info;
	char		buf[16];
} __packed;

struct app_plogi_reply {
	uint32_t	prli_status;  // 0=failed, 1=succeeded
	uint8_t		reserved[VND_CMD_APP_RESERVED_SIZE];
} __packed;

#define	RECFG_TIME	1
#define	RECFG_BYTES	2

struct app_rekey_cfg {
	struct app_id app_info;
	uint8_t	 rekey_mode;	// 1=time based (in sec), 2: bytes based
	port_id_t d_id;		// 000 = all entries; anything else
				//    specifies a specific d_id
	uint8_t	 force;		// 0=no force to change config if
				//    existing rekey mode changed,
				// 1=force to re auth and change
				//    existing rekey mode if different
	union {
		int64_t bytes;	// # of bytes before rekey, 0=no limit
		int64_t time;	// # of seconds before rekey, 0=no time limit
	} rky_units;

	uint8_t		reserved[VND_CMD_APP_RESERVED_SIZE];
} __packed;

struct app_pinfo_req {
	struct app_id app_info;
	uint8_t	 num_ports;	// space allocated for app_pinfo_reply_t.ports[]
	port_id_t remote_pid;
	uint8_t	 reserved[VND_CMD_APP_RESERVED_SIZE];
} __packed;

struct app_pinfo {
	port_id_t remote_pid;   // contains device d_id
	uint8_t	remote_wwpn[WWN_SIZE];
	uint8_t	remote_type;	// contains TGT or INIT
#define	VND_CMD_RTYPE_UNKNOWN		0
#define	VND_CMD_RTYPE_TARGET		1
#define	VND_CMD_RTYPE_INITIATOR		2
	uint8_t	remote_state;	// 0=bad, 1=good
	uint8_t	auth_state;	// 0=auth N/A (unsecured fcport),
				// 1=auth req'd
				// 2=auth done
	uint8_t	rekey_mode;	// 1=time based, 2=bytes based
	int64_t	rekey_count;	// # of times device rekeyed
	int64_t	rekey_config_value;     // orig rekey value (MB or sec)
					// (0 for no limit)
	int64_t	rekey_consumed_value;   // remaining MB/time,0=no limit

	uint8_t	reserved[VND_CMD_APP_RESERVED_SIZE];
} __packed;

/* AUTH States */
#define	VND_CMD_AUTH_STATE_UNDEF	0
#define	VND_CMD_AUTH_STATE_SESSION_SHUTDOWN	1
#define	VND_CMD_AUTH_STATE_NEEDED	2
#define	VND_CMD_AUTH_STATE_ELS_RCVD	3
#define	VND_CMD_AUTH_STATE_SAUPDATE_COMPL 4

struct app_pinfo_reply {
	uint8_t		port_count;	// possible value => 0 to 255
	uint8_t		reserved[VND_CMD_APP_RESERVED_SIZE];
	struct app_pinfo ports[0];	// variable - specified by app_pinfo_req num_ports
} __packed;

struct app_sinfo_req {
	struct app_id	app_info;
	uint8_t		num_ports;	// app space alloc for elem[]
	uint8_t		reserved[VND_CMD_APP_RESERVED_SIZE];
} __packed;

// temp data - actual data TBD
struct app_sinfo {
	uint8_t	remote_wwpn[WWN_SIZE];
	int64_t	rekey_count;	// # of times device rekeyed
	uint8_t	rekey_mode;	// 1=time based (in sec), 2: bytes based
	int64_t	tx_bytes;	// orig rekey value
	int64_t	rx_bytes;	// amount left
} __packed;

struct app_stats_reply {
	uint8_t		elem_count;	// possible value => 0 to 255
	struct app_sinfo elem[0];	// specified by app_sinfo_t elem_count
} __packed;

struct qla_sa_update_frame {
	struct app_id	app_info;
	uint16_t	flags;
#define SAU_FLG_INV		0x01	// delete key
#define SAU_FLG_TX		0x02	// 1=tx, 0 = rx
#define SAU_FLG_FORCE_DELETE	0x08	// force RX sa_index delete
#define SAU_FLG_GMAC_MODE	0x20	// GMAC mode is cleartext for the IO (i.e. NULL encryption)
#define SAU_FLG_KEY128          0x40
#define SAU_FLG_KEY256          0x80
	uint16_t        fast_sa_index:10,
			reserved:6;
	uint32_t	salt;
	uint32_t	spi;
	uint8_t		sa_key[32];
	uint8_t		node_name[WWN_SIZE];
	uint8_t		port_name[WWN_SIZE];
	port_id_t	port_id;
} __packed;

// used for edif mgmt bsg interface
#define	QL_VND_SC_UNDEF		0
#define	QL_VND_SC_SA_UPDATE	1	// sa key info
#define	QL_VND_SC_APP_START	2	// app started event
#define	QL_VND_SC_APP_STOP	3	// app stopped event
#define	QL_VND_SC_AUTH_OK	4	// plogi auth'd ok
#define	QL_VND_SC_AUTH_FAIL	5	// plogi auth bad
#define	QL_VND_SC_REKEY_CONFIG	6	// auth rekey set parms (time/data)
#define	QL_VND_SC_GET_FCINFO	7	// get port info
#define	QL_VND_SC_GET_STATS	8	// get edif stats

/* Application interface data structure for rtn data */
#define	EXT_DEF_EVENT_DATA_SIZE	64
struct edif_app_dbell {
	uint32_t	event_code;
	uint32_t	event_data_size;
	union  {
		port_id_t	port_id;
		uint8_t		event_data[EXT_DEF_EVENT_DATA_SIZE];
	};
} __packed;

struct edif_sa_update_aen {
	port_id_t port_id;
	uint32_t key_type;	/* Tx (1) or RX (2) */
	uint32_t status;	/* 0 succes,  1 failed, 2 timeout , 3 error */
	uint8_t		reserved[16];
} __packed;

#define	QL_VND_SA_STAT_SUCCESS	0
#define	QL_VND_SA_STAT_FAILED	1
#define	QL_VND_SA_STAT_TIMEOUT	2
#define	QL_VND_SA_STAT_ERROR	3

#define	QL_VND_RX_SA_KEY	1
#define	QL_VND_TX_SA_KEY	2

/* App defines for plogi auth'd ok and plogi auth bad requests */
struct auth_complete_cmd {
	struct app_id app_info;
#define PL_TYPE_WWPN    1
#define PL_TYPE_DID     2
	uint32_t    type;
	union {
		uint8_t  wwpn[WWN_SIZE];
		port_id_t d_id;
	} u;
	uint32_t reserved[VND_CMD_APP_RESERVED_SIZE];
} __packed;

#define RX_DELAY_DELETE_TIMEOUT 20			// 30 second timeout

#endif	/* QLA_EDIF_BSG_H */
