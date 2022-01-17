/* SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause */

/* Authors: Cheng Xu <chengyou@linux.alibaba.com> */
/*          Kai Shen <kaishen@linux.alibaba.com> */
/* Copyright (c) 2020-2022, Alibaba Group. */

/* Authors: Bernard Metzler <bmt@zurich.ibm.com> */
/*          Greg Joyce <greg@opengridcomputing.com> */
/* Copyright (c) 2008-2019, IBM Corporation */
/* Copyright (c) 2017, Open Grid Computing, Inc. */

#ifndef __ERDMA_CM_H__
#define __ERDMA_CM_H__

#include <net/sock.h>
#include <linux/tcp.h>

#include <rdma/iw_cm.h>

/* iWarp MPA protocol defs */
#define MPA_REVISION_EXT_1   129
#define MPA_MAX_PRIVDATA     RDMA_MAX_PRIVATE_DATA
#define MPA_KEY_REQ          "MPA ID Req Frame"
#define MPA_KEY_REP          "MPA ID Rep Frame"
#define MPA_KEY_SIZE         16
#define MPA_DEFAULT_HDR_LEN  28

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
	MPA_RR_RESERVED      = __cpu_to_be16(0x1f00),
	MPA_RR_MASK_REVISION = __cpu_to_be16(0x00ff)
};

static inline void __mpa_rr_set_revision(u16 *bits, u8 rev)
{
	*bits = (*bits & ~MPA_RR_MASK_REVISION)
		| (cpu_to_be16(rev) & MPA_RR_MASK_REVISION);
}

static inline u8 __mpa_rr_revision(u16 mpa_rr_bits)
{
	u16 rev = mpa_rr_bits & MPA_RR_MASK_REVISION;

	return (u8)be16_to_cpu(rev);
}

/*
 * MPA request/reply header
 */
struct mpa_rr {
	u8 key[16];
	struct mpa_rr_params params;
};

struct erdma_mpa_ext {
	__be32 cookie;
	__be32 bits;
};

#define MPA_EXT_DESIRED_CC_MASK GENMASK(31, 28)
#define MPA_EXT_RSVD_MASK       GENMASK(27, 0)

static inline void __mpa_ext_set_cc(u32 *bits, u16 cc)
{
	*bits |= FIELD_PREP(MPA_EXT_DESIRED_CC_MASK, cc);
}

static inline u8 __mpa_ext_cc(__be32 mpa_ext_bits)
{
	return (u8)FIELD_GET(MPA_EXT_DESIRED_CC_MASK, __be32_to_cpu(mpa_ext_bits));
}

struct erdma_mpa_info {
	struct mpa_rr hdr;  /* peer mpa hdr in host byte order */
	struct erdma_mpa_ext ext_data;
	char *pdata;
	int bytes_rcvd;
};

struct erdma_sk_upcalls {
	void (*sk_state_change)(struct sock *sk);
	void (*sk_data_ready)(struct sock *sk, int bytes);
	void (*sk_error_report)(struct sock *sk);
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
	spinlock_t lock;
	struct kref ref;
	int in_use;
	wait_queue_head_t waitq;
	enum erdma_cep_state state;

	struct list_head listenq;
	struct erdma_cep *listen_cep;

	struct erdma_qp *qp;
	struct socket *sock;

	struct erdma_cm_work *mpa_timer;
	struct list_head work_freelist;

	struct erdma_mpa_info mpa;
	int ord;
	int ird;
	int pd_len;
	void *private_storage;

	/* Saved upcalls of socket llp.sock */
	void (*sk_state_change)(struct sock *sk);
	void (*sk_data_ready)(struct sock *sk);
	void (*sk_error_report)(struct sock *sk);
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
	struct delayed_work  work;
	struct list_head     list;
	enum erdma_work_type type;
	struct erdma_cep     *cep;
};

#define to_sockaddr_in(a) (*(struct sockaddr_in *)(&(a)))

static inline int getname_peer(struct socket *s, struct sockaddr_storage *a)
{
	return s->ops->getname(s, (struct sockaddr *)a, 1);
}

static inline int getname_local(struct socket *s, struct sockaddr_storage *a)
{
	return s->ops->getname(s, (struct sockaddr *)a, 0);
}

static inline int ksock_recv(struct socket *sock, char *buf, size_t size, int flags)
{
	struct kvec iov = {buf, size};
	struct msghdr msg = {.msg_name = NULL, .msg_flags = flags};

	return kernel_recvmsg(sock, &msg, &iov, 1, size, flags);
}

int erdma_connect(struct iw_cm_id *id, struct iw_cm_conn_param *param);
int erdma_accept(struct iw_cm_id *id, struct iw_cm_conn_param *param);
int erdma_reject(struct iw_cm_id *id, const void *pdata, u8 plen);
int erdma_create_listen(struct iw_cm_id *id, int backlog);
int erdma_destroy_listen(struct iw_cm_id *id);

void erdma_cep_get(struct erdma_cep *ceq);
void erdma_cep_put(struct erdma_cep *ceq);
int erdma_cm_queue_work(struct erdma_cep *ceq, enum erdma_work_type type);

int erdma_cm_init(void);
void erdma_cm_exit(void);

#define sk_to_cep(sk)	((struct erdma_cep *)((sk)->sk_user_data))

#endif
