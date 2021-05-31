/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Marvell Fibre Channel HBA Driver
 * Copyright (c)  2021    Marvell
 */
#ifndef __QLA_EDIF_H
#define __QLA_EDIF_H

struct qla_scsi_host;

enum enode_flags_t {
	ENODE_ACTIVE = 0x1,	// means that app has started
};

struct pur_core {
	enum enode_flags_t	enode_flags;
	spinlock_t		pur_lock;       /* protects list */
	struct  list_head	head;
};

enum db_flags_t {
	EDB_ACTIVE = 0x1,
};

struct edif_dbell {
	enum db_flags_t		db_flags;
	spinlock_t		db_lock;	/* protects list */
	struct  list_head	head;
	struct	completion	dbell;		/* doorbell ring */
};

#define        MAX_PAYLOAD     1024
#define        PUR_GET         1

#define	LSTATE_OFF	1	// node not on list
#define	LSTATE_ON	2	// node on list
#define	LSTATE_DEST	3	// node destoyed

struct dinfo {
	int		nodecnt;	// create seq count
	int		lstate;		// node's list state
};

struct pur_ninfo {
	unsigned int	pur_pend:1;
	port_id_t       pur_sid;
	port_id_t	pur_did;
	uint8_t		vp_idx;
	short           pur_bytes_rcvd;
	unsigned short  pur_nphdl;
	unsigned int    pur_rx_xchg_address;
};

struct purexevent {
	struct  pur_ninfo	pur_info;
	unsigned char		*msgp;
	u32			msgp_len;
};

#define	N_UNDEF		0	// node not used/defined
#define	N_PUREX		1	// purex info
struct enode {
	struct list_head	list;
	struct dinfo		dinfo;
	uint32_t		ntype;
	union {
		struct purexevent	purexinfo;
	} u;
};
#endif	/* __QLA_EDIF_H */
