/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Authors: Cheng Xu <chengyou@linux.alibaba.com>
 *          Kai Shen <kaishen@linux.alibaba.com>
 * Copyright (c) 2020-2021, Alibaba Group.
 *
 * Authors: Bernard Metzler <bmt@zurich.ibm.com>
 * Copyright (c) 2008-2016, IBM Corporation
 */

#ifndef __ERDMA_CM_H__
#define __ERDMA_CM_H__

#include <net/sock.h>
#include <linux/tcp.h>

#include <rdma/iw_cm.h>


/* iWarp MPA protocol defs */
#define RDMAP_VERSION		1
#define DDP_VERSION		1
#define MPA_REVISION_1		1
#define MPA_MAX_PRIVDATA	RDMA_MAX_PRIVATE_DATA
#define MPA_KEY_REQ		"MPA ID Req F"
#define MPA_KEY_REP		"MPA ID Rep F"

struct mpa_rr_params {
	__be16 bits;
	__be16 pd_len;
};

/*
 * MPA request/response Hdr bits & fields
 */
enum {
	MPA_RR_FLAG_MARKERS  = __cpu_to_be16(0x8000),
	MPA_RR_FLAG_CRC      = __cpu_to_be16(0x4000),
	MPA_RR_FLAG_REJECT   = __cpu_to_be16(0x2000),
	MPA_RR_DESIRED_CC    = __cpu_to_be16(0x0f00),
	MPA_RR_RESERVED      = __cpu_to_be16(0x1000),
	MPA_RR_MASK_REVISION = __cpu_to_be16(0x00ff)
};

/*
 * MPA request/reply header
 */
struct mpa_rr {
	u8 key[16];
	struct mpa_rr_params params;
};

struct erdma_mpa_info {
	struct mpa_rr hdr;	/* peer mpa hdr in host byte order */
	char          *pdata;
	int           bytes_rcvd;
	u32           remote_qpn;
};

struct erdma_sk_upcalls {
	void (*sk_state_change)(struct sock *sk);
	void (*sk_data_ready)(struct sock *sk, int bytes);
	void (*sk_error_report)(struct sock *sk);
};
struct erdma_llp_info {
	struct socket           *sock;
	struct sockaddr_in      laddr;	/* redundant with socket info above */
	struct sockaddr_in      raddr;	/* dito, consider removal */
	struct erdma_sk_upcalls sk_def_upcalls;
};

struct erdma_dev;

enum erdma_cep_state {
	ERDMA_EPSTATE_IDLE = 1,
	ERDMA_EPSTATE_LISTENING,
	ERDMA_EPSTATE_CONNECTING,
	ERDMA_EPSTATE_AWAIT_MPAREQ,
	ERDMA_EPSTATE_RECVD_MPAREQ,
	ERDMA_EPSTATE_AWAIT_MPAREP,
	ERDMA_EPSTATE_RDMA_MODE,
	ERDMA_EPSTATE_CLOSED
};

struct erdma_cep {
	struct iw_cm_id *cm_id;
	struct erdma_dev *dev;

	struct list_head devq;
	/*
	 * The provider_data element of a listener IWCM ID
	 * refers to a list of one or more listener CEPs
	 */
	struct list_head listenq;
	struct erdma_cep *listen_cep;
	struct erdma_qp *qp;
	spinlock_t lock;
	wait_queue_head_t waitq;
	struct kref ref;
	enum erdma_cep_state state;
	short in_use;
	struct erdma_cm_work *mpa_timer;
	struct list_head work_freelist;
	struct erdma_llp_info llp;
	struct erdma_mpa_info mpa;
	int ord;
	int ird;
	int sk_error;
	int pd_len;
	void *private_storage;

	/* Saved upcalls of socket llp.sock */
	void (*sk_state_change)(struct sock *sk);
	void (*sk_data_ready)(struct sock *sk);
	void (*sk_error_report)(struct sock *sk);

	bool is_connecting;
};

#define MPAREQ_TIMEOUT	(HZ*20)
#define MPAREP_TIMEOUT	(HZ*10)
#define CONNECT_TIMEOUT  (HZ*10)

enum erdma_work_type {
	ERDMA_CM_WORK_ACCEPT	= 1,
	ERDMA_CM_WORK_READ_MPAHDR,
	ERDMA_CM_WORK_CLOSE_LLP,		/* close socket */
	ERDMA_CM_WORK_PEER_CLOSE,		/* socket indicated peer close */
	ERDMA_CM_WORK_MPATIMEOUT,
	ERDMA_CM_WORK_CONNECTED,
	ERDMA_CM_WORK_CONNECTTIMEOUT
};

struct erdma_cm_work {
	struct delayed_work	work;
	struct list_head	list;
	enum erdma_work_type	type;
	struct erdma_cep	*cep;
};

#define to_sockaddr_in(a) (*(struct sockaddr_in *)(&(a)))

extern int erdma_connect(struct iw_cm_id *id, struct iw_cm_conn_param *param);
extern int erdma_accept(struct iw_cm_id *id, struct iw_cm_conn_param *param);
extern int erdma_reject(struct iw_cm_id *id, const void *pdata, u8 plen);
extern int erdma_create_listen(struct iw_cm_id *id, int backlog);
extern int erdma_destroy_listen(struct iw_cm_id *id);

extern void erdma_cep_get(struct erdma_cep *ceq);
extern void erdma_cep_put(struct erdma_cep *ceq);
extern int erdma_cm_queue_work(struct erdma_cep *ceq, enum erdma_work_type type);

extern int erdma_cm_init(void);
extern void erdma_cm_exit(void);

#define sk_to_cep(sk)	((struct erdma_cep *)((sk)->sk_user_data))

#endif
