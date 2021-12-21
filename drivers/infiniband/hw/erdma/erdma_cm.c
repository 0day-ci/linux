// SPDX-License-Identifier: GPL-2.0
/*
 * Authors: Cheng Xu <chengyou@linux.alibaba.com>
 *          Kai Shen <kaishen@linux.alibaba.com>
 * Copyright (c) 2020-2021, Alibaba Group.
 *
 * Authors: Bernard Metzler <bmt@zurich.ibm.com>
 *          Fredy Neeser <nfd@zurich.ibm.com>
 * Copyright (c) 2008-2016, IBM Corporation
 */

#include <linux/errno.h>
#include <linux/inetdevice.h>
#include <linux/net.h>
#include <linux/inetdevice.h>
#include <net/addrconf.h>
#include <linux/tcp.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <net/sock.h>

#include <rdma/iw_cm.h>
#include <rdma/ib_smi.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_verbs.h>

#include "erdma.h"
#include "erdma_cm.h"
#include "erdma_verbs.h"

static bool mpa_crc_strict = 1;
module_param(mpa_crc_strict, bool, 0644);
static bool mpa_crc_required;
module_param(mpa_crc_required, bool, 0644);

MODULE_PARM_DESC(mpa_crc_required, "MPA CRC required");
MODULE_PARM_DESC(mpa_crc_strict, "MPA CRC off enforced");

static void erdma_cm_llp_state_change(struct sock *sk);
static void erdma_cm_llp_data_ready(struct sock *sk);
static void erdma_cm_llp_error_report(struct sock *sk);

static void erdma_sk_assign_cm_upcalls(struct sock *sk)
{
	write_lock_bh(&sk->sk_callback_lock);
	sk->sk_state_change = erdma_cm_llp_state_change;
	sk->sk_data_ready = erdma_cm_llp_data_ready;
	sk->sk_error_report = erdma_cm_llp_error_report;
	write_unlock_bh(&sk->sk_callback_lock);
}

static void erdma_sk_save_upcalls(struct sock *sk)
{
	struct erdma_cep *cep = sk_to_cep(sk);

	WARN_ON(!cep);

	write_lock_bh(&sk->sk_callback_lock);
	cep->sk_state_change = sk->sk_state_change;
	cep->sk_data_ready = sk->sk_data_ready;
	cep->sk_error_report = sk->sk_error_report;
	write_unlock_bh(&sk->sk_callback_lock);
}

static void erdma_sk_restore_upcalls(struct sock *sk, struct erdma_cep *cep)
{
	sk->sk_state_change = cep->sk_state_change;
	sk->sk_data_ready = cep->sk_data_ready;
	sk->sk_error_report = cep->sk_error_report;
	sk->sk_user_data = NULL;
}

static void erdma_socket_disassoc(struct socket *s)
{
	struct sock	*sk = s->sk;
	struct erdma_cep	*cep;

	if (sk) {
		write_lock_bh(&sk->sk_callback_lock);
		cep = sk_to_cep(sk);
		if (cep) {
			erdma_sk_restore_upcalls(sk, cep);
			erdma_cep_put(cep);
		} else
			pr_warn("cannot restore sk callbacks: no ep\n");
		write_unlock_bh(&sk->sk_callback_lock);
	} else
		pr_warn("cannot restore sk callbacks: no sk\n");
}

static inline int kernel_peername(struct socket *s, struct sockaddr_in *addr)
{
	return s->ops->getname(s, (struct sockaddr *)addr, 1);
}

static inline int kernel_localname(struct socket *s, struct sockaddr_in *addr)
{
	return s->ops->getname(s, (struct sockaddr *)addr, 0);
}

static void erdma_cep_socket_assoc(struct erdma_cep *cep, struct socket *s)
{
	cep->llp.sock = s;
	erdma_cep_get(cep);
	s->sk->sk_user_data = cep;

	erdma_sk_save_upcalls(s->sk);
	erdma_sk_assign_cm_upcalls(s->sk);
}


static struct erdma_cep *erdma_cep_alloc(struct erdma_dev  *dev)
{
	struct erdma_cep *cep = kzalloc(sizeof(*cep), GFP_KERNEL);

	if (cep) {
		unsigned long flags;

		INIT_LIST_HEAD(&cep->listenq);
		INIT_LIST_HEAD(&cep->devq);
		INIT_LIST_HEAD(&cep->work_freelist);

		kref_init(&cep->ref);
		cep->state = ERDMA_EPSTATE_IDLE;
		init_waitqueue_head(&cep->waitq);
		spin_lock_init(&cep->lock);
		cep->dev = dev;

		spin_lock_irqsave(&dev->lock, flags);
		list_add_tail(&cep->devq, &dev->cep_list);
		spin_unlock_irqrestore(&dev->lock, flags);
		atomic_inc(&dev->num_cep);
	}
	return cep;
}

static void erdma_cm_free_work(struct erdma_cep *cep)
{
	struct list_head	*w, *tmp;
	struct erdma_cm_work	*work;

	list_for_each_safe(w, tmp, &cep->work_freelist) {
		work = list_entry(w, struct erdma_cm_work, list);
		list_del(&work->list);
		kfree(work);
	}
}

static void erdma_cancel_mpatimer(struct erdma_cep *cep)
{
	spin_lock_bh(&cep->lock);
	if (cep->mpa_timer) {
		if (cancel_delayed_work(&cep->mpa_timer->work)) {
			erdma_cep_put(cep);
			kfree(cep->mpa_timer); /* not needed again */
		}
		cep->mpa_timer = NULL;
	}
	spin_unlock_bh(&cep->lock);
}

static void erdma_put_work(struct erdma_cm_work *work)
{
	INIT_LIST_HEAD(&work->list);
	spin_lock_bh(&work->cep->lock);
	list_add(&work->list, &work->cep->work_freelist);
	spin_unlock_bh(&work->cep->lock);
}

static void erdma_cep_set_inuse(struct erdma_cep *cep)
{
	unsigned long flags;
	int ret;
retry:
	spin_lock_irqsave(&cep->lock, flags);

	if (cep->in_use) {
		spin_unlock_irqrestore(&cep->lock, flags);
		ret = wait_event_interruptible(cep->waitq, !cep->in_use);
		if (signal_pending(current))
			flush_signals(current);
		goto retry;
	} else {
		cep->in_use = 1;
		spin_unlock_irqrestore(&cep->lock, flags);
	}
}

static void erdma_cep_set_free(struct erdma_cep *cep)
{
	unsigned long flags;

	spin_lock_irqsave(&cep->lock, flags);
	cep->in_use = 0;
	spin_unlock_irqrestore(&cep->lock, flags);

	wake_up(&cep->waitq);
}


static void __erdma_cep_dealloc(struct kref *ref)
{
	struct erdma_cep *cep = container_of(ref, struct erdma_cep, ref);
	struct erdma_dev *dev = cep->dev;
	unsigned long flags;

	WARN_ON(cep->listen_cep);

	/* kfree(NULL) is save */
	if (cep->private_storage != NULL)
		kfree(cep->private_storage);
	if (cep->private_storage != NULL)
		kfree(cep->mpa.pdata);
	spin_lock_bh(&cep->lock);
	if (!list_empty(&cep->work_freelist))
		erdma_cm_free_work(cep);
	spin_unlock_bh(&cep->lock);

	spin_lock_irqsave(&dev->lock, flags);
	list_del(&cep->devq);
	spin_unlock_irqrestore(&dev->lock, flags);
	atomic_dec(&dev->num_cep);
	kfree(cep);
}

static struct erdma_cm_work *erdma_get_work(struct erdma_cep *cep)
{
	struct erdma_cm_work    *work = NULL;
	unsigned long           flags;

	spin_lock_irqsave(&cep->lock, flags);
	if (!list_empty(&cep->work_freelist)) {
		work = list_entry(cep->work_freelist.next, struct erdma_cm_work,
				  list);
		list_del_init(&work->list);
	}
	spin_unlock_irqrestore(&cep->lock, flags);
	return work;
}

static int erdma_cm_alloc_work(struct erdma_cep *cep, int num)
{
	struct erdma_cm_work        *work;

	if (!list_empty(&cep->work_freelist)) {
		pr_err("ERROR: Not init work_freelist.\n");
		return -ENOMEM;
	}

	while (num--) {
		work = kmalloc(sizeof(*work), GFP_KERNEL);
		if (!work) {
			if (!(list_empty(&cep->work_freelist)))
				erdma_cm_free_work(cep);
			return -ENOMEM;
		}
		work->cep = cep;
		INIT_LIST_HEAD(&work->list);
		list_add(&work->list, &cep->work_freelist);
	}
	return 0;
}

/*
 * erdma_cm_upcall()
 *
 * Upcall to IWCM to inform about async connection events
 */
static int erdma_cm_upcall(struct erdma_cep *cep, enum iw_cm_event_type reason,
			   int status)
{
	struct iw_cm_event event;
	struct iw_cm_id *cm_id;

	memset(&event, 0, sizeof(event));
	event.status = status;
	event.event = reason;

	if (reason == IW_CM_EVENT_CONNECT_REQUEST ||
	    reason == IW_CM_EVENT_CONNECT_REPLY) {
		u16 pd_len = be16_to_cpu(cep->mpa.hdr.params.pd_len);

		if (pd_len) {
			event.private_data_len = pd_len;
			event.private_data = cep->mpa.pdata;
			if (cep->mpa.pdata == NULL)
				event.private_data_len = 0;
		}

		to_sockaddr_in(event.local_addr) = cep->llp.laddr;
		to_sockaddr_in(event.remote_addr) = cep->llp.raddr;
	}
	if (reason == IW_CM_EVENT_CONNECT_REQUEST) {
		event.ird = cep->dev->attrs.max_ird;
		event.ord = cep->dev->attrs.max_ord;
		event.provider_data = cep;
		cm_id = cep->listen_cep->cm_id;
	} else
		cm_id = cep->cm_id;

	if (!cep->is_connecting && reason == IW_CM_EVENT_CONNECT_REPLY)
		return 0;

	cep->is_connecting = false;

	return cm_id->event_handler(cm_id, &event);
}

/*
 * erdma_qp_cm_drop()
 *
 * Drops established LLP connection if present and not already
 * scheduled for dropping. Called from user context, SQ workqueue
 * or receive IRQ. Caller signals if socket can be immediately
 * closed (basically, if not in IRQ).
 */
void erdma_qp_cm_drop(struct erdma_qp *qp, int schedule)
{
	struct erdma_cep *cep = qp->cep;

	if (!qp->cep)
		return;

	if (schedule)
		erdma_cm_queue_work(cep, ERDMA_CM_WORK_CLOSE_LLP);
	else {
		erdma_cep_set_inuse(cep);

		if (cep->state == ERDMA_EPSTATE_CLOSED)
			goto out;

		if (cep->cm_id) {
			switch (cep->state) {

			case ERDMA_EPSTATE_AWAIT_MPAREP:
				erdma_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY,
					      -EINVAL);
				break;

			case ERDMA_EPSTATE_RDMA_MODE:
				erdma_cm_upcall(cep, IW_CM_EVENT_CLOSE, 0);

				break;

			case ERDMA_EPSTATE_IDLE:
			case ERDMA_EPSTATE_LISTENING:
			case ERDMA_EPSTATE_CONNECTING:
			case ERDMA_EPSTATE_AWAIT_MPAREQ:
			case ERDMA_EPSTATE_RECVD_MPAREQ:
			case ERDMA_EPSTATE_CLOSED:
			default:

				break;
			}
			cep->cm_id->rem_ref(cep->cm_id);
			cep->cm_id = NULL;
			erdma_cep_put(cep);
		}
		cep->state = ERDMA_EPSTATE_CLOSED;

		if (cep->llp.sock) {
			erdma_socket_disassoc(cep->llp.sock);
			sock_release(cep->llp.sock);
			cep->llp.sock = NULL;
		}
		if (cep->qp) {
			WARN_ON(qp != cep->qp);
			cep->qp = NULL;
			erdma_qp_put(qp);
		}
out:
		erdma_cep_set_free(cep);
	}
}


void erdma_cep_put(struct erdma_cep *cep)
{
	WARN_ON(kref_read(&cep->ref) < 1);
	kref_put(&cep->ref, __erdma_cep_dealloc);
}

void erdma_cep_get(struct erdma_cep *cep)
{
	kref_get(&cep->ref);
}

static inline int ksock_recv(struct socket *sock, char *buf, size_t size,
			     int flags)
{
	struct kvec iov = {buf, size};
	struct msghdr msg = {.msg_name = NULL, .msg_flags = flags};

	return kernel_recvmsg(sock, &msg, &iov, 1, size, flags);
}

static inline void __mpa_rr_set_cc(u16 *bits, u16 cc)
{
	*bits = (*bits & ~MPA_RR_DESIRED_CC)
		| (cc & MPA_RR_DESIRED_CC);
}

static inline u8 __mpa_rr_cc(u16 mpa_rr_bits)
{
	u16 rev = (mpa_rr_bits & MPA_RR_DESIRED_CC);

	return (u8)rev;
}

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
 * Expects params->pd_len in host byte order
 */
static int erdma_send_mpareqrep(struct erdma_cep *cep, const void *pdata,
			      u8 pd_len)
{
	struct socket	*s = cep->llp.sock;
	struct mpa_rr	*rr = &cep->mpa.hdr;
	struct kvec	iov[2];
	struct msghdr	msg;
	int		ret;

	memset(&msg, 0, sizeof(msg));

	rr->params.pd_len = cpu_to_be16(pd_len);

	iov[0].iov_base = rr;
	iov[0].iov_len = sizeof(*rr);

	if (pd_len) {
		iov[1].iov_base = (char *)pdata;
		iov[1].iov_len = pd_len;

		ret =  kernel_sendmsg(s, &msg, iov, 2, pd_len + sizeof(*rr));
	} else
		ret =  kernel_sendmsg(s, &msg, iov, 1, sizeof(*rr));

	return ret < 0 ? ret : 0;
}

/*
 * Receive MPA Request/Reply header.
 *
 * Returns 0 if complete MPA Request/Reply haeder including
 * eventual private data was received. Returns -EAGAIN if
 * header was partially received or negative error code otherwise.
 *
 * Context: May be called in process context only
 */
static int erdma_recv_mpa_rr(struct erdma_cep *cep)
{
	struct mpa_rr	*hdr = &cep->mpa.hdr;
	struct socket	*s = cep->llp.sock;
	u16		pd_len;
	int		rcvd, to_rcv;

	if (cep->mpa.bytes_rcvd < sizeof(struct mpa_rr)) {

		rcvd = ksock_recv(s, (char *)hdr + cep->mpa.bytes_rcvd,
				  sizeof(struct mpa_rr) -
				  cep->mpa.bytes_rcvd, MSG_DONTWAIT);
		/* we use DONTWAIT mode, so EAGAIN may appear. */
		if (rcvd == -EAGAIN)
			return -EAGAIN;

		if (rcvd <= 0)
			return -ECONNABORTED;

		cep->mpa.bytes_rcvd += rcvd;

		if (cep->mpa.bytes_rcvd < sizeof(struct mpa_rr))
			return -EAGAIN;

		if (be16_to_cpu(hdr->params.pd_len) > MPA_MAX_PRIVDATA)
			return -EPROTO;
	}
	pd_len = be16_to_cpu(hdr->params.pd_len);

	/*
	 * At least the MPA Request/Reply header (frame not including
	 * private data) has been received.
	 * Receive (or continue receiving) any private data.
	 */
	to_rcv = pd_len - (cep->mpa.bytes_rcvd - sizeof(struct mpa_rr));

	if (!to_rcv) {
		/*
		 * We must have hdr->params.pd_len == 0 and thus received a
		 * complete MPA Request/Reply frame.
		 * Check against peer protocol violation.
		 */
		u32 word;

		rcvd = ksock_recv(s, (char *)&word, sizeof(word), MSG_DONTWAIT);
		if (rcvd == -EAGAIN)
			return 0;

		if (rcvd == 0)
			return -EPIPE;

		if (rcvd < 0)
			return rcvd;
		return -EPROTO;
	}

	/*
	 * At this point, we must have hdr->params.pd_len != 0.
	 * A private data buffer gets allocated if hdr->params.pd_len != 0.
	 */
	if (!cep->mpa.pdata) {
		cep->mpa.pdata = kmalloc(pd_len + 4, GFP_KERNEL);
		if (!cep->mpa.pdata)
			return -ENOMEM;
	}
	rcvd = ksock_recv(s, cep->mpa.pdata + cep->mpa.bytes_rcvd
			  - sizeof(struct mpa_rr), to_rcv + 4, MSG_DONTWAIT);

	if (rcvd < 0)
		return rcvd;

	if (rcvd > to_rcv)
		return -EPROTO;

	cep->mpa.bytes_rcvd += rcvd;

	if (to_rcv == rcvd)
		return 0;

	return -EAGAIN;
}


/*
 * erdma_proc_mpareq()
 *
 * Read MPA Request from socket and signal new connection to IWCM
 * if success. Caller must hold lock on corresponding listening CEP.
 */
static int erdma_proc_mpareq(struct erdma_cep *cep)
{
	struct mpa_rr      *req;
	int                ret;

	ret = erdma_recv_mpa_rr(cep);
	if (ret)
		goto out;

	req = &cep->mpa.hdr;

	if (__mpa_rr_revision(req->params.bits) > MPA_REVISION_1) {
		/* allow for 0 and 1 only */
		ret = -EPROTO;
		goto out;
	}

	if (memcmp(req->key, MPA_KEY_REQ, 12)) {
		ret = -EPROTO;
		goto out;
	}

	cep->mpa.remote_qpn = *(u32 *)&req->key[12];
	/*
	 * Prepare for sending MPA reply
	 */
	memcpy(req->key, MPA_KEY_REP, 12);

	if (req->params.bits & MPA_RR_FLAG_MARKERS ||
	    (req->params.bits & MPA_RR_FLAG_CRC &&
	    !mpa_crc_required && mpa_crc_strict)) {
		req->params.bits &= ~MPA_RR_FLAG_MARKERS;
		req->params.bits |= MPA_RR_FLAG_REJECT; /* reject */

		if (!mpa_crc_required && mpa_crc_strict)
			req->params.bits &= ~MPA_RR_FLAG_CRC;

		kfree(cep->mpa.pdata);
		cep->mpa.pdata = NULL;

		(void)erdma_send_mpareqrep(cep, NULL, 0);
		ret = -EOPNOTSUPP;
		goto out;
	}
	/*
	 * Enable CRC if requested by module initialization
	 */
	if (!(req->params.bits & MPA_RR_FLAG_CRC) && mpa_crc_required)
		req->params.bits |= MPA_RR_FLAG_CRC;

	cep->state = ERDMA_EPSTATE_RECVD_MPAREQ;

	/* Keep reference until IWCM accepts/rejects */
	erdma_cep_get(cep);
	ret = erdma_cm_upcall(cep, IW_CM_EVENT_CONNECT_REQUEST, 0);
	if (ret)
		erdma_cep_put(cep);
out:
	return ret;
}

static int erdma_proc_mpareply(struct erdma_cep *cep)
{
	struct erdma_qp_attrs qp_attrs;
	struct erdma_qp *qp = cep->qp;
	struct mpa_rr *rep;
	int ret;

	ret = erdma_recv_mpa_rr(cep);
	if (ret != -EAGAIN)
		erdma_cancel_mpatimer(cep);
	if (ret)
		goto out_err;

	rep = &cep->mpa.hdr;

	if (__mpa_rr_revision(rep->params.bits) > MPA_REVISION_1) {
		/* allow for 0 and 1 only */
		ret = -EPROTO;
		goto out_err;
	}
	if (memcmp(rep->key, MPA_KEY_REP, 12)) {
		ret = -EPROTO;
		goto out_err;
	}

	cep->mpa.remote_qpn = *(u32 *)&rep->key[12];

	if (rep->params.bits & MPA_RR_FLAG_REJECT) {
		(void)erdma_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY, -ECONNRESET);
		ret = -ECONNRESET;
		goto out;
	}

	if ((rep->params.bits & MPA_RR_FLAG_MARKERS)
		|| (mpa_crc_required && !(rep->params.bits & MPA_RR_FLAG_CRC))
		|| (mpa_crc_strict && !mpa_crc_required
			&& (rep->params.bits & MPA_RR_FLAG_CRC))) {

		(void)erdma_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY,
			-ECONNREFUSED);
		ret = -EINVAL;
		goto out;
	}
	memset(&qp_attrs, 0, sizeof(qp_attrs));
	qp_attrs.irq_size = cep->ird;
	qp_attrs.orq_size = cep->ord;
	qp_attrs.llp_stream_handle = cep->llp.sock;
	qp_attrs.state = ERDMA_QP_STATE_RTS;

	down_write(&qp->state_lock);
	if (qp->attrs.state > ERDMA_QP_STATE_RTS) {
		ret = -EINVAL;
		up_write(&qp->state_lock);
		goto out_err;
	}

	qp->qp_type = ERDMA_QP_TYPE_CLIENT;
	qp->cc_method = __mpa_rr_cc(rep->params.bits) == qp->dev->cc_method ?
			qp->dev->cc_method : COMPROMISE_CC;
	ret = erdma_modify_qp_internal(qp, &qp_attrs, ERDMA_QP_ATTR_STATE |
						      ERDMA_QP_ATTR_LLP_HANDLE |
						      ERDMA_QP_ATTR_MPA);

	up_write(&qp->state_lock);

	if (!ret) {
		ret = erdma_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY, 0);
		if (!ret)
			cep->state = ERDMA_EPSTATE_RDMA_MODE;

		goto out;
	}

out_err:
	(void)erdma_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY, -EINVAL);
out:
	return ret;
}

/*
 * erdma_accept_newconn - accept an incoming pending connection
 *
 */
static void erdma_accept_newconn(struct erdma_cep *cep)
{
	struct socket    *s       = cep->llp.sock;
	struct socket    *new_s   = NULL;
	struct erdma_cep *new_cep = NULL;
	int              ret       = 0; /* debug only. should disappear */

	if (cep->state != ERDMA_EPSTATE_LISTENING)
		goto error;

	new_cep = erdma_cep_alloc(cep->dev);
	if (!new_cep)
		goto error;

	if (erdma_cm_alloc_work(new_cep, 6) != 0)
		goto error;

	/*
	 * Copy saved socket callbacks from listening CEP
	 * and assign new socket with new CEP
	 */
	new_cep->sk_state_change = cep->sk_state_change;
	new_cep->sk_data_ready   = cep->sk_data_ready;
	new_cep->sk_error_report = cep->sk_error_report;

	ret = kernel_accept(s, &new_s, O_NONBLOCK);
	if (ret != 0)
		goto error;

	new_cep->llp.sock = new_s;
	new_s->sk->sk_user_data = new_cep;
	erdma_cep_get(new_cep);

	tcp_sock_set_nodelay(new_s->sk);

	ret = kernel_peername(new_s, &new_cep->llp.raddr);
	if (ret < 0)
		goto error;

	ret = kernel_localname(new_s, &new_cep->llp.laddr);
	if (ret < 0)
		goto error;

	new_cep->state = ERDMA_EPSTATE_AWAIT_MPAREQ;

	ret = erdma_cm_queue_work(new_cep, ERDMA_CM_WORK_MPATIMEOUT);
	if (ret)
		goto error;
	/*
	 * See erdma_proc_mpareq() etc. for the use of new_cep->listen_cep.
	 */
	new_cep->listen_cep = cep;
	erdma_cep_get(cep);

	if (atomic_read(&new_s->sk->sk_rmem_alloc)) {
		/*
		 * MPA REQ already queued
		 */
		erdma_cep_set_inuse(new_cep);
		ret = erdma_proc_mpareq(new_cep);
		erdma_cep_set_free(new_cep);

		if (ret != -EAGAIN) {
			erdma_cep_put(cep);
			new_cep->listen_cep = NULL;
			if (ret)
				goto error;
		}
	}
	return;

error:
	if (new_cep) {
		new_cep->state = ERDMA_EPSTATE_CLOSED;
		erdma_cancel_mpatimer(new_cep);

		erdma_cep_put(new_cep);
		new_cep->llp.sock = NULL;
	}

	if (new_s) {
		erdma_socket_disassoc(new_s);
		sock_release(new_s);
	}
}

static int erdma_newconn_connected(struct erdma_cep *cep)
{
	struct socket    *s       = cep->llp.sock;
	int              ret;
	int              qpn;

	ret = kernel_peername(s, &cep->llp.raddr);
	if (ret < 0)
		goto error;

	ret = kernel_localname(s, &cep->llp.laddr);
	if (ret < 0)
		goto error;

	cep->mpa.hdr.params.bits = 0;
	__mpa_rr_set_revision(&cep->mpa.hdr.params.bits, MPA_REVISION_1);
	__mpa_rr_set_cc(&cep->mpa.hdr.params.bits, cep->dev->cc_method);

	if (mpa_crc_required)
		cep->mpa.hdr.params.bits |= MPA_RR_FLAG_CRC;

	qpn = QP_ID(cep->qp);
	memcpy(cep->mpa.hdr.key, MPA_KEY_REQ, 12);
	memcpy(&cep->mpa.hdr.key[12], &qpn, 4);

	ret = erdma_send_mpareqrep(cep, cep->private_storage, cep->pd_len);

	cep->mpa.hdr.params.pd_len = 0;

	if (ret >= 0) {
		cep->state = ERDMA_EPSTATE_AWAIT_MPAREP;
		ret = erdma_cm_queue_work(cep, ERDMA_CM_WORK_MPATIMEOUT);
		if (!ret)
			return 0;
	}

error:
	return ret;
}

static void erdma_cm_work_handler(struct work_struct *w)
{
	struct erdma_cm_work *work;
	struct erdma_cep     *cep;
	int                  release_cep = 0, ret = 0;

	work = container_of(w, struct erdma_cm_work, work.work);
	cep = work->cep;

	erdma_cep_set_inuse(cep);

	switch (work->type) {
	case ERDMA_CM_WORK_CONNECTED:
		erdma_cancel_mpatimer(cep);
		if (cep->state == ERDMA_EPSTATE_CONNECTING) {
			ret = erdma_newconn_connected(cep);
			if (ret) {
				erdma_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY, -EIO);
				release_cep = 1;
			}
		}

		break;
	case ERDMA_CM_WORK_CONNECTTIMEOUT:
		if (cep->state == ERDMA_EPSTATE_CONNECTING) {
			cep->mpa_timer = NULL;
			erdma_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY,
					-ETIMEDOUT);
			release_cep = 1;
		}
		break;
	case ERDMA_CM_WORK_ACCEPT:

		erdma_accept_newconn(cep);
		break;

	case ERDMA_CM_WORK_READ_MPAHDR:
		switch (cep->state) {
		case ERDMA_EPSTATE_AWAIT_MPAREQ:
			if (cep->listen_cep) {
				erdma_cep_set_inuse(cep->listen_cep);

				if (cep->listen_cep->state ==
				    ERDMA_EPSTATE_LISTENING)
					ret = erdma_proc_mpareq(cep);
				else
					ret = -EFAULT;

				erdma_cep_set_free(cep->listen_cep);

				if (ret != -EAGAIN) {
					erdma_cep_put(cep->listen_cep);
					cep->listen_cep = NULL;
					if (ret)
						erdma_cep_put(cep);
				}
			}
			break;

		case ERDMA_EPSTATE_AWAIT_MPAREP:
			ret = erdma_proc_mpareply(cep);
			break;
		default:
			break;
		}
		if (ret && ret != -EAGAIN)
			release_cep = 1;
		break;
	case ERDMA_CM_WORK_CLOSE_LLP:
		if (cep->cm_id)
			erdma_cm_upcall(cep, IW_CM_EVENT_CLOSE, 0);
		release_cep = 1;
		break;
	case ERDMA_CM_WORK_PEER_CLOSE:
		if (cep->cm_id) {
			switch (cep->state) {
			case ERDMA_EPSTATE_CONNECTING:
			case ERDMA_EPSTATE_AWAIT_MPAREP:
				/*
				 * MPA reply not received, but connection drop
				 */
				erdma_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY,
					      -ECONNRESET);
				break;
			case ERDMA_EPSTATE_RDMA_MODE:
				/*
				 * NOTE: IW_CM_EVENT_DISCONNECT is given just
				 *       to transition IWCM into CLOSING.
				 *       FIXME: is that needed?
				 */
				erdma_cm_upcall(cep, IW_CM_EVENT_DISCONNECT, 0);
				erdma_cm_upcall(cep, IW_CM_EVENT_CLOSE, 0);
				break;
			default:
				break;
			}
		} else {
			switch (cep->state) {
			case ERDMA_EPSTATE_RECVD_MPAREQ:
				break;
			case ERDMA_EPSTATE_AWAIT_MPAREQ:
				/*
				 * Socket close before MPA request received.
				 */
				if (cep->listen_cep) {
					erdma_cep_put(cep->listen_cep);
					cep->listen_cep = NULL;
				}
				break;
			default:
				break;
			}
		}
		release_cep = 1;
		break;
	case ERDMA_CM_WORK_MPATIMEOUT:
		cep->mpa_timer = NULL;
		if (cep->state == ERDMA_EPSTATE_AWAIT_MPAREP) {
			/*
			 * MPA request timed out:
			 * Hide any partially received private data and signal
			 * timeout
			 */
			cep->mpa.hdr.params.pd_len = 0;

			if (cep->cm_id)
				erdma_cm_upcall(cep, IW_CM_EVENT_CONNECT_REPLY, -ETIMEDOUT);
			release_cep = 1;
		} else if (cep->state == ERDMA_EPSTATE_AWAIT_MPAREQ) {
			/*
			 * No MPA request received after peer TCP stream setup.
			 */
			if (cep->listen_cep) {
				erdma_cep_put(cep->listen_cep);
				cep->listen_cep = NULL;
			}

			erdma_cep_put(cep);
			release_cep = 1;
		}
		break;
	default:
		pr_err("ERROR: work task type:%u.\n", work->type);
		break;
	}

	if (release_cep) {
		erdma_cancel_mpatimer(cep);
		cep->state = ERDMA_EPSTATE_CLOSED;
		if (cep->qp) {
			struct erdma_qp *qp = cep->qp;
			/*
			 * Serialize a potential race with application
			 * closing the QP and calling erdma_qp_cm_drop()
			 */
			erdma_qp_get(qp);
			erdma_cep_set_free(cep);

			erdma_qp_llp_close(qp);
			erdma_qp_put(qp);

			erdma_cep_set_inuse(cep);
			cep->qp = NULL;
			erdma_qp_put(qp);
		}
		if (cep->llp.sock) {
			erdma_socket_disassoc(cep->llp.sock);
			sock_release(cep->llp.sock);
			cep->llp.sock = NULL;
		}

		if (cep->cm_id) {
			cep->cm_id->rem_ref(cep->cm_id);
			cep->cm_id = NULL;
			if (cep->state != ERDMA_EPSTATE_LISTENING)
				erdma_cep_put(cep);
		}
	}
	erdma_cep_set_free(cep);
	erdma_put_work(work);
	erdma_cep_put(cep);
}

static struct workqueue_struct *erdma_cm_wq;

int erdma_cm_queue_work(struct erdma_cep *cep, enum erdma_work_type type)
{
	struct erdma_cm_work *work = erdma_get_work(cep);
	unsigned long delay = 0;

	if (!work)
		return -ENOMEM;

	work->type = type;
	work->cep = cep;

	erdma_cep_get(cep);

	INIT_DELAYED_WORK(&work->work, erdma_cm_work_handler);

	if (type == ERDMA_CM_WORK_MPATIMEOUT) {
		cep->mpa_timer = work;

		if (cep->state == ERDMA_EPSTATE_AWAIT_MPAREP)
			delay = MPAREQ_TIMEOUT;
		else
			delay = MPAREP_TIMEOUT;
	} else if (type == ERDMA_CM_WORK_CONNECTTIMEOUT) {
		cep->mpa_timer = work;

		delay = CONNECT_TIMEOUT;
	}

	queue_delayed_work(erdma_cm_wq, &work->work, delay);

	return 0;
}

static void erdma_cm_llp_data_ready(struct sock *sk)
{
	struct erdma_cep *cep;

	read_lock(&sk->sk_callback_lock);

	cep = sk_to_cep(sk);
	if (!cep)
		goto out;

	switch (cep->state) {
	case ERDMA_EPSTATE_RDMA_MODE:
	case ERDMA_EPSTATE_LISTENING:
		break;
	case ERDMA_EPSTATE_AWAIT_MPAREQ:
	case ERDMA_EPSTATE_AWAIT_MPAREP:
		erdma_cm_queue_work(cep, ERDMA_CM_WORK_READ_MPAHDR);
		break;
	default:
		break;
	}
out:
	read_unlock(&sk->sk_callback_lock);
}

static void erdma_cm_llp_error_report(struct sock *sk)
{
	struct erdma_cep *cep = sk_to_cep(sk);

	if (cep) {
		cep->sk_error = sk->sk_err;
		cep->sk_error_report(sk);
	}
}

static void erdma_cm_llp_state_change(struct sock *sk)
{
	struct erdma_cep *cep;
	struct socket *s;
	void (*orig_state_change)(struct sock *sk);

	read_lock(&sk->sk_callback_lock);

	cep = sk_to_cep(sk);
	if (!cep) {
		read_unlock(&sk->sk_callback_lock);
		return;
	}
	orig_state_change = cep->sk_state_change;

	s = sk->sk_socket;

	switch (sk->sk_state) {
	case TCP_ESTABLISHED:
		if (cep->state == ERDMA_EPSTATE_CONNECTING)
			erdma_cm_queue_work(cep, ERDMA_CM_WORK_CONNECTED);
		else
			erdma_cm_queue_work(cep, ERDMA_CM_WORK_ACCEPT);
		break;
	case TCP_CLOSE:
	case TCP_CLOSE_WAIT:
		if (cep->state != ERDMA_EPSTATE_LISTENING)
			erdma_cm_queue_work(cep, ERDMA_CM_WORK_PEER_CLOSE);
		break;
	default:
		break;
	}
	read_unlock(&sk->sk_callback_lock);
	orig_state_change(sk);
}


static int kernel_bindconnect(struct socket *s,
			      struct sockaddr *laddr, int laddrlen,
			      struct sockaddr *raddr, int raddrlen, int flags)
{
	int err;
	struct sock *sk = s->sk;

	/*
	 * Make address available again asap.
	 */
	sock_set_reuseaddr(s->sk);

	err = s->ops->bind(s, laddr, laddrlen);
	if (err < 0) {
		pr_info("try port (%u) failed\n", ((struct sockaddr_in *)laddr)->sin_port);
		/* Try to alloc port, not use RDMA port. */
		((struct sockaddr_in *)laddr)->sin_port = 0;
		err = s->ops->bind(s, laddr, laddrlen);
		if (err < 0)
			goto done;
		pr_info("alloc source port %u.\n", inet_sk(sk)->inet_num);
	}

	err = s->ops->connect(s, raddr, raddrlen, flags);
	if (err < 0)
		goto done;

	err = s->ops->getname(s, laddr, 0);
done:
	return err;
}


int erdma_connect(struct iw_cm_id *id, struct iw_cm_conn_param *params)
{
	struct erdma_dev *dev = to_edev(id->device);
	struct erdma_qp *qp;
	struct erdma_cep *cep = NULL;
	struct socket *s = NULL;
	struct sockaddr *laddr, *raddr;
	u16 pd_len = params->private_data_len;
	int ret;

	if (pd_len > MPA_MAX_PRIVDATA)
		return -EINVAL;

	qp = find_qp_by_qpn(dev, params->qpn);
	if (!qp)
		return -ENOENT;

	laddr = (struct sockaddr *)&id->m_local_addr;
	raddr = (struct sockaddr *)&id->m_remote_addr;

	qp->attrs.sip = ntohl(to_sockaddr_in(id->local_addr).sin_addr.s_addr);
	qp->attrs.origin_sport = ntohs(to_sockaddr_in(id->local_addr).sin_port);
	qp->attrs.dip = ntohl(to_sockaddr_in(id->remote_addr).sin_addr.s_addr);
	qp->attrs.dport = ntohs(to_sockaddr_in(id->m_remote_addr).sin_port);

	ret = sock_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, &s);
	if (ret < 0)
		goto error_put_qp;

	cep = erdma_cep_alloc(dev);
	if (!cep) {
		ret = -ENOMEM;
		goto error_release_sock;
	}

	erdma_cep_set_inuse(cep);

	/* Associate QP with CEP */
	erdma_cep_get(cep);
	qp->cep = cep;
	erdma_qp_get(qp);
	cep->qp = qp;

	/* Associate cm_id with CEP */
	id->add_ref(id);
	cep->cm_id = id;

	ret = erdma_cm_alloc_work(cep, 6);
	if (ret != 0) {
		ret = -ENOMEM;
		goto error_release_cep;
	}

	cep->ird = params->ird;
	cep->ord = params->ord;
	cep->state = ERDMA_EPSTATE_CONNECTING;
	cep->is_connecting = true;

	erdma_cep_socket_assoc(cep, s);

	cep->pd_len = pd_len;
	cep->private_storage = kmalloc(pd_len, GFP_KERNEL);
	if (!cep->private_storage) {
		ret = -ENOMEM;
		goto error_disasssoc;
	}

	memcpy(cep->private_storage, params->private_data, params->private_data_len);

	ret = kernel_bindconnect(s, laddr, sizeof(*laddr), raddr,
				sizeof(*raddr), O_NONBLOCK);
	if (ret != -EINPROGRESS && ret != 0) {
		goto error_disasssoc;
	} else if (ret == 0) {
		ret = erdma_cm_queue_work(cep, ERDMA_CM_WORK_CONNECTED);
	} else {
		ret = erdma_cm_queue_work(cep, ERDMA_CM_WORK_CONNECTTIMEOUT);
		if (ret)
			goto error_disasssoc;
	}

	erdma_cep_set_free(cep);
	return 0;

error_disasssoc:
	kfree(cep->private_storage);
	cep->private_storage = NULL;
	cep->pd_len = 0;

	erdma_socket_disassoc(s);

error_release_cep:
	/* disassoc with cm_id */
	cep->cm_id = NULL;
	id->rem_ref(id);

	/* disassoc with qp */
	qp->cep = NULL;
	erdma_cep_put(cep);
	cep->qp = NULL;
	erdma_qp_put(qp);

	cep->state = ERDMA_EPSTATE_CLOSED;

	erdma_cep_set_free(cep);

	/* release the cep. */
	erdma_cep_put(cep);

error_release_sock:
	if (s)
		sock_release(s);
error_put_qp:
	erdma_qp_put(qp);

	return ret;
}

int erdma_accept(struct iw_cm_id *id, struct iw_cm_conn_param *params)
{
	struct erdma_dev *dev = to_edev(id->device);
	struct erdma_cep *cep = (struct erdma_cep *)id->provider_data;
	struct erdma_qp *qp;
	struct erdma_qp_attrs qp_attrs;
	int ret;

	erdma_cep_set_inuse(cep);
	erdma_cep_put(cep);

	/* Free lingering inbound private data */
	if (cep->mpa.hdr.params.pd_len) {
		cep->mpa.hdr.params.pd_len = 0;
		kfree(cep->mpa.pdata);
		cep->mpa.pdata = NULL;
	}
	erdma_cancel_mpatimer(cep);

	if (cep->state != ERDMA_EPSTATE_RECVD_MPAREQ) {
		if (cep->state == ERDMA_EPSTATE_CLOSED) {
			erdma_cep_set_free(cep);
			erdma_cep_put(cep);
			return -ECONNRESET;
		}
		return -EBADFD;
	}

	qp = find_qp_by_qpn(dev, params->qpn);
	if (!qp)
		return -ENOENT;

	down_write(&qp->state_lock);
	if (qp->attrs.state > ERDMA_QP_STATE_RTS) {
		ret = -EINVAL;
		up_write(&qp->state_lock);
		goto error;
	}

	if (params->ord > dev->attrs.max_ord ||
	    params->ird > dev->attrs.max_ord) {
		ret = -EINVAL;
		up_write(&qp->state_lock);
		goto error;
	}

	if (params->private_data_len > MPA_MAX_PRIVDATA) {
		ret = -EINVAL;
		up_write(&qp->state_lock);
		goto error;
	}

	cep->cm_id = id;
	id->add_ref(id);

	memset(&qp_attrs, 0, sizeof(qp_attrs));
	qp_attrs.orq_size = params->ord;
	qp_attrs.irq_size = params->ird;
	qp_attrs.llp_stream_handle = cep->llp.sock;

	qp_attrs.state = ERDMA_QP_STATE_RTS;

	qp->attrs.sip = ntohl(cep->llp.laddr.sin_addr.s_addr);
	qp->attrs.origin_sport = ntohs(cep->llp.laddr.sin_port);
	qp->attrs.dip = ntohl(cep->llp.raddr.sin_addr.s_addr);
	qp->attrs.dport = ntohs(cep->llp.raddr.sin_port);
	qp->attrs.sport = ntohs(cep->llp.laddr.sin_port);

	/* Associate QP with CEP */
	erdma_cep_get(cep);
	qp->cep = cep;

	erdma_qp_get(qp);
	cep->qp = qp;

	cep->state = ERDMA_EPSTATE_RDMA_MODE;

	qp->qp_type = ERDMA_QP_TYPE_SERVER;
	qp->private_data_len = params->private_data_len;
	qp->cc_method = __mpa_rr_cc(cep->mpa.hdr.params.bits) == qp->dev->cc_method ?
		qp->dev->cc_method : COMPROMISE_CC;

	/* move to rts */
	ret = erdma_modify_qp_internal(qp, &qp_attrs, ERDMA_QP_ATTR_STATE |
				       ERDMA_QP_ATTR_LLP_HANDLE |
				       ERDMA_QP_ATTR_MPA);
	up_write(&qp->state_lock);

	if (ret)
		goto error;

	__mpa_rr_set_cc(&cep->mpa.hdr.params.bits, qp->dev->cc_method);
	memcpy(&cep->mpa.hdr.key[12], (u32 *)&QP_ID(qp), 4);
	ret = erdma_send_mpareqrep(cep, params->private_data,
				params->private_data_len);

	if (!ret) {
		ret = erdma_cm_upcall(cep, IW_CM_EVENT_ESTABLISHED, 0);
		if (ret)
			goto error;

		erdma_cep_set_free(cep);

		return 0;
	}

error:
	erdma_socket_disassoc(cep->llp.sock);
	sock_release(cep->llp.sock);
	cep->llp.sock = NULL;

	cep->state = ERDMA_EPSTATE_CLOSED;

	if (cep->cm_id) {
		cep->cm_id->rem_ref(id);
		cep->cm_id = NULL;
	}
	if (qp->cep) {
		erdma_cep_put(cep);
		qp->cep = NULL;
	}

	cep->qp = NULL;
	erdma_qp_put(qp);

	erdma_cep_set_free(cep);
	erdma_cep_put(cep);

	return ret;
}

/*
 * erdma_reject()
 *
 * Local connection reject case. Send private data back to peer,
 * close connection and dereference connection id.
 */
int erdma_reject(struct iw_cm_id *id, const void *pdata, u8 plen)
{
	struct erdma_cep	*cep = (struct erdma_cep *)id->provider_data;

	erdma_cep_set_inuse(cep);
	erdma_cep_put(cep);

	erdma_cancel_mpatimer(cep);

	if (cep->state != ERDMA_EPSTATE_RECVD_MPAREQ) {
		if (cep->state == ERDMA_EPSTATE_CLOSED) {
			erdma_cep_set_free(cep);
			erdma_cep_put(cep); /* should be last reference */

			return -ECONNRESET;
		}
		return -EBADFD;
	}

	if (__mpa_rr_revision(cep->mpa.hdr.params.bits) == MPA_REVISION_1) {
		cep->mpa.hdr.params.bits |= MPA_RR_FLAG_REJECT; /* reject */
		(void)erdma_send_mpareqrep(cep, pdata, plen);
	}
	erdma_socket_disassoc(cep->llp.sock);
	sock_release(cep->llp.sock);
	cep->llp.sock = NULL;

	cep->state = ERDMA_EPSTATE_CLOSED;

	erdma_cep_set_free(cep);
	erdma_cep_put(cep);

	return 0;
}

int erdma_create_listen(struct iw_cm_id *id, int backlog)
{
	struct socket *s;
	struct erdma_cep *cep = NULL;
	int ret = 0;
	struct erdma_dev *dev = to_edev(id->device);
	int addr_family = id->local_addr.ss_family;

	if (addr_family != AF_INET)
		return -EAFNOSUPPORT;

	ret = sock_create(addr_family, SOCK_STREAM, IPPROTO_TCP, &s);
	if (ret < 0)
		return ret;

	/*
	 * Allow binding local port when still in TIME_WAIT from last close.
	 */
	sock_set_reuseaddr(s->sk);

	if (addr_family == AF_INET) {
		struct sockaddr_in *laddr = &to_sockaddr_in(id->local_addr);
		u8 *l_ip, *r_ip;

		l_ip = (u8 *) &to_sockaddr_in(id->local_addr).sin_addr.s_addr;
		r_ip = (u8 *) &to_sockaddr_in(id->remote_addr).sin_addr.s_addr;

		/* For wildcard addr, limit binding to current device only */
		if (ipv4_is_zeronet(laddr->sin_addr.s_addr))
			s->sk->sk_bound_dev_if = dev->netdev->ifindex;

		ret = s->ops->bind(s, (struct sockaddr *)laddr, sizeof(struct sockaddr_in));
	} else {
		ret = -EAFNOSUPPORT;
		goto error;
	}

	if (ret != 0)
		goto error;

	cep = erdma_cep_alloc(dev);
	if (!cep) {
		ret = -ENOMEM;
		goto error;
	}
	erdma_cep_socket_assoc(cep, s);

	ret = erdma_cm_alloc_work(cep, backlog);
	if (ret != 0)
		goto error;

	ret = s->ops->listen(s, backlog);
	if (ret != 0)
		goto error;

	memcpy(&cep->llp.laddr, &id->local_addr, sizeof(cep->llp.laddr));
	memcpy(&cep->llp.raddr, &id->remote_addr, sizeof(cep->llp.raddr));

	cep->cm_id = id;
	id->add_ref(id);

	if (!id->provider_data) {
		id->provider_data = kmalloc(sizeof(struct list_head), GFP_KERNEL);
		if (!id->provider_data) {
			ret = -ENOMEM;
			goto error;
		}
		INIT_LIST_HEAD((struct list_head *)id->provider_data);
	}

	list_add_tail(&cep->listenq, (struct list_head *)id->provider_data);
	cep->state = ERDMA_EPSTATE_LISTENING;

	return 0;

error:
	if (cep) {
		erdma_cep_set_inuse(cep);

		if (cep->cm_id) {
			cep->cm_id->rem_ref(cep->cm_id);
			cep->cm_id = NULL;
		}
		cep->llp.sock = NULL;
		erdma_socket_disassoc(s);
		cep->state = ERDMA_EPSTATE_CLOSED;

		erdma_cep_set_free(cep);
		erdma_cep_put(cep);
	}
	sock_release(s);

	return ret;
}

static void erdma_drop_listeners(struct iw_cm_id *id)
{
	struct list_head	*p, *tmp;
	/*
	 * In case of a wildcard rdma_listen on a multi-homed device,
	 * a listener's IWCM id is associated with more than one listening CEP.
	 */
	list_for_each_safe(p, tmp, (struct list_head *)id->provider_data) {
		struct erdma_cep *cep = list_entry(p, struct erdma_cep, listenq);

		list_del(p);

		erdma_cep_set_inuse(cep);

		if (cep->cm_id) {
			cep->cm_id->rem_ref(cep->cm_id);
			cep->cm_id = NULL;
		}
		if (cep->llp.sock) {
			erdma_socket_disassoc(cep->llp.sock);
			sock_release(cep->llp.sock);
			cep->llp.sock = NULL;
		}
		cep->state = ERDMA_EPSTATE_CLOSED;
		erdma_cep_set_free(cep);
		erdma_cep_put(cep);
	}
}

int erdma_destroy_listen(struct iw_cm_id *id)
{
	if (!id->provider_data)
		return 0;

	erdma_drop_listeners(id);
	kfree(id->provider_data);
	id->provider_data = NULL;

	return 0;
}

int erdma_cm_init(void)
{
	erdma_cm_wq = create_singlethread_workqueue("erdma_cm_wq");
	if (!erdma_cm_wq)
		return -ENOMEM;

	return 0;
}

void erdma_cm_exit(void)
{
	if (erdma_cm_wq) {
		flush_workqueue(erdma_cm_wq);
		destroy_workqueue(erdma_cm_wq);
	}
}
