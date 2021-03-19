/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Copyright(c) 2020 - 2021 Intel Corporation.
 */

#ifndef __RV_H__
#define __RV_H__
#include <linux/types.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_sa.h>
#include <rdma/ib_cm.h>
#include <rdma/rdma_cm.h>
#include <linux/mutex.h>
#include <linux/socket.h>
#include <linux/timer.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_cm.h>
#include <linux/moduleparam.h>

#include "rv_mr_cache.h"
#include <rdma/rv_user_ioctls.h>

/*
 * Lock Heirachy
 * In order that locks can be acquired:
 * mm->mmap_lock - held during mmap call and mmu_notifier_register
 * rv_user.mutex
 * rv_job_dev_list_mutex
 * rv_job_dev.conn_list_mutex
 * rv_device.listener_mutex
 * rv_sconn.mutex - we can never hold this while calling destroy_cm_id
 *	because destroy cm_id will wait for handlers via it's mutex and deadlock
 * rv_job_dev_list rcu
 * rv_dev_list_lock
 * rv_job_dev.conn_list rcu - no sub locks
 * rv_device.listener_lock - no sub locks
 * rv_user.umrs.cache.lock - no sub locks
 * rv_job_dev.user_array_lock - no sub locks
 * ring.lock - no sub locks
 * mr_pd_uobject_lock - no sub locks
 * rv_conn.next_lock - no sub locks
 * rv_sconn.drain_lock - no sub locks
 */

/*
 * Our goal is to allocate shared resources, but all processes are equal peers.
 * So we use a get_alloc approach where processes attempt to get a resource
 * reference (lookup and get) and if it's not on the list, it is allocated and
 * added.  This is used for rv_job_dev, rv_listener and rv_conn.
 * Each such list is protected by a mutex to prevent duplicate additions
 * and an RCU for actual list access.  The mutex also protects
 * get_alloc/list_del races.  All get_alloc calls are in premptable
 * app ioctl call context.
 * rv_listener list is rarely searched, so it simply uses a mutex and spinlock.
 */

/*
 * When working with IB CM, Async events and timeouts we can't predict if and
 * when their callbacks will occur so they can't have a reference held in
 * advance. This presents opportunities for the callback to race with rv_conn
 * destruction. To solve this problem, we use rv_conn_get_check to get a
 * reference to rv_conn only if rv_conn.kref != 0. rv_conn and rv_sconn
 * destruction uses destroy_cm_id and del_timer_sync to stop all callbacks (and
 * allow any outstanding handlers to finish) before it proceeds to free rv_conn.
 * This ensures the cm_id, etc can remain valid enough for the callback to
 * call rv_conn_get_check and decide whether to simply ignore the event.
 * It also allows the rv_sconn to be the ib_cm, CQ, QP and timer context and
 * avoid searches. Once the callback has the reference, it is protected
 * from other threads calling rv_conn_release.
 */

#define DRIVER_NAME "rv"

#define RV_INVALID -1

/* For errors to surface on the console */
#define rv_err(idx, fmt, ...) \
	pr_err("[%s:%s %d]: " fmt, DRIVER_NAME, __func__, idx, ##__VA_ARGS__)

#define rv_ptr_err(prefix, ptr, fmt, ...) \
	pr_err("[%s:%s %s 0x%p]: " fmt, DRIVER_NAME, __func__, prefix, ptr, \
	       ##__VA_ARGS__)

/* For debugging*/
#define rv_dbg(idx, fmt, ...) \
	pr_debug("[%s:%s %d]: " fmt, DRIVER_NAME, __func__, idx, ##__VA_ARGS__)

/* For general console info */
#define rv_info(idx, fmt, ...) \
	pr_info("[%s:%s %d]: " fmt, DRIVER_NAME, __func__, idx, ##__VA_ARGS__)

/* For debugging with any pointer */
#define rv_ptr_dbg(prefix, ptr, fmt, ...) \
	pr_debug("[%s:%s %s 0x%p]: " fmt, DRIVER_NAME, __func__, prefix, ptr, \
		 ##__VA_ARGS__)

#define rv_ptr_info(prefix, ptr, fmt, ...) \
	pr_info("[%s:%s %s 0x%p]: " fmt, DRIVER_NAME, __func__, prefix, ptr, \
		##__VA_ARGS__)

/* For debugging with rv_conn_info */
#define rv_conn_err(ptr, fmt, ...) rv_ptr_err("sconn", ptr, fmt, ##__VA_ARGS__)
#define rv_conn_info(ptr, fmt, ...) rv_ptr_info("sconn", ptr, fmt,\
						##__VA_ARGS__)
#define rv_conn_dbg(ptr, fmt, ...) rv_ptr_dbg("sconn", ptr, fmt, ##__VA_ARGS__)

/* For debugging with rv_device */
#define rv_dev_err(ptr, fmt, ...) rv_ptr_err("dev", ptr, fmt, ##__VA_ARGS__)
#define rv_dev_info(ptr, fmt, ...) rv_ptr_info("dev", ptr, fmt, ##__VA_ARGS__)
#define rv_dev_dbg(ptr, fmt, ...) rv_ptr_dbg("dev", ptr, fmt, ##__VA_ARGS__)

/* For debugging with ib cm id */
#define rv_cm_err(ptr, fmt, ...) rv_ptr_err("cm_id", ptr, fmt, ##__VA_ARGS__)
#define rv_cm_info(ptr, fmt, ...) rv_ptr_info("cm_id", ptr, fmt, ##__VA_ARGS__)
#define rv_cm_dbg(ptr, fmt, ...) rv_ptr_dbg("cm_id", ptr, fmt, ##__VA_ARGS__)

struct rv_device;

/*
 * A listener can handle more than 1 job on a given dev.
 * There is a listener for each unique service_id on each dev and
 * could be as few as 1 even for multiple jobs.
 * For IB, a listener services all ports on a given HCA.
 * listener_entry - entry on rv_device.listener_list
 * dev - device being listened on
 * cm_id - actual IB CM listener, stores service_id here
 */
struct rv_listener {
	struct list_head listener_entry;
	struct rv_device *dev;
	struct ib_cm_id *cm_id;
	struct kref kref;
};

/*
 * For each physical device, RV establishes a single rv_device which
 * is shared across all jobs, listeners, etc.
 * ib_dev - underlying RDMA device
 * dev_entry - entry on rv_dev_list
 *
 * IB cm listener management:
 *	listener_mutex, listener_lock, listener_list
 *
 * user_list - list of rv_user (protected by rv_dev_list_lock)
 */
struct rv_device {
	struct ib_device *ib_dev;
	struct list_head dev_entry;
	struct kref kref;
	struct ib_event_handler event_handler;

	struct mutex listener_mutex; /* avoid duplicate add in get_alloc */
	spinlock_t listener_lock; /* protect list search, add, remove */
	struct list_head listener_list;

	struct list_head user_list;
};

#define RV_CONN_MAX_ACTIVE_WQ_ENTRIES 100 /* used for conn handling */

#define RV_RESOLVER_RETRY 5		/* max retries (0 == try once) */
#define RV_RESOLVER_TIMEOUT 10000	/* in milliseconds */

/* duration client spends in RV_DELAY before re-attempt reconnect */
#define RV_RECONNECT_DELAY msecs_to_jiffies(100)

/* private data in REQ and REP is used to negotiate features and exchange
 * additional informaton.
 * REQ.ver is the version of the sender and defines the priv_data structure.
 * Newer versions of struct should be a superset of older versions.
 * REP.ver is the version the listener is accepting and defines REP priv_data.
 * New client, old listerner:
 *	listener accepts REQ >= it's version, responds with it's version in REP
 * Old client, new listener:
 *	listener accepts REQ, responds with REP matching client's version
 * Protocol ver is now 2.  For simplicity protocol 0 and 1 (pre-releases) are
 * not accepted.
 */

#define RV_PRIVATE_DATA_MAGIC 0x00125550534d2121ULL
#define RV_PRIVATE_DATA_VER 2

/*
 * Private data used in CM REQ
 * laid out for good alignment of fields without packing
 * index indicates index of rv_sconn within it's rv_conn and differentiates
 * the multiple connection REQs for num_conn > 1
 * The job_key selects the rv_job_dev on receiver side
 * uid is 32b
 */
struct rv_req_priv_data {
	u64 magic;
	u32 ver;
	u16 resv1;
	u8 index;
	u8 job_key_len;
	u8 job_key[RV_MAX_JOB_KEY_LEN];
	uid_t uid;
};

/*
 * Private data used in CM REP
 * laid out for good alignment of fields without packing
 */
struct rv_rep_priv_data {
	u64 magic;
	u32 ver;
};

/*
 * RV_INIT - client side create_conn done
 * RV_WAITING - server side create_conn done, listening
 * RV_RESOLVING - client side resolving ethernet dmac via ARP
 * RV_CONNECTING - client or server side going through CM states
 * RV_CONNECTED - connection established
 * RV_DISCONNECTING - connection teardown
 * RV_DELAY- RV delay before restarting client connection
 * RV_ERROR - connection lost or failed
 * destroying - ref count == 0
 */
enum rv_sconn_state {
	RV_INIT = 1,
	RV_WAITING,
	RV_RESOLVING,
	RV_CONNECTING,
	RV_CONNECTED,
	RV_DISCONNECTING,
	RV_DELAY,
	RV_ERROR,
};

/*
 * Meaning of each state and status of key fields once in state and
 * have unlocked mutex.
 * In all states (except while destroying) immutable fields in rv_conn valid.
 *
 * WAS_CONNECTED flag essentially creates a superstate indicating connection
 * recovery for WAITING, RESOLVING, and CONNECTING.  During connection
 * recovery, conn_timer is running for reconnect_timeout and once it fires,
 * the connect recovery is aborted and moved to ERROR
 *
 * If WAS_CONNECTED, post write in wrong state returns EAGAIN instead of EINVAL
 *
 * A complication is identifying when a connection is down for a receiving
 * end of a traffic pattern.  The receiver may see no packets and can't tell
 * a path down from an idle QP.  To address this periodic RDMA Write zero
 * can be sent if no traffic has been sent or received for a while.  This
 * situation is no worse that user space UD PSM as the job failure may
 * occur over an out of band network to kill the job.  A gap is the client side
 * of a connection desiring recovery, which requires the heatbeat to recognize.
 *
 * If the receiver happens to also be the server side of rv_sconn, we may
 * get a REQ while in connected because the sender may get a QP timeout
 * long before the receiver heartbeat notices.  We treat this as disconnect
 * and if appropriate (likely) begin connection recovery.
 *
 * RV_INIT: initial state for client side connections (after 1st create_conn)
 *	cm_id, primary_path are NULL
 *	dev_addr, resolver_retry_left  uninitialized
 *	qp in RESET state, no outstanding CQEs nor WQEs
 *	conn_timer not running (no reconnect_timeout)
 *	delay_timer not running
 *	hb_timer not running
 *	Next States:
 *		RESOLVING - user calls cm_connect
 *		destroying - user close
 * RV_WAITING: initial state for server side connections (after 1st create_conn)
 *	a listener exists at rv_job_dev level, rv_cm_server_handler
 *	cm_id NULL
 *	start_time is set when enter state
 *	qp in RESET state, no outstanding CQEs nor WQEs
 *	if WAS_CONNECTED, conn_timer is running for reconnect_timeout
 *	delay_timer not running
 *	hb_timer not running
 *	DRAINING, SQ_DRAINED and RQ_DRAINED flags clear
 *	Next States:
 *		CONNECTING - inbound REQ
 *		ERROR - reconnect_timeout expires
 *		destroying - user close
 * RV_RESOLVING: 1st step in establishing a client connection (ARP)
 *	For non-ethernet, this is a brief transient state (only inside mutex)
 *	cm_id established (client side), rv_cm_handler
 *	resolver_retry_left valid
 *	primary_path != NULL, but contents incomplete
 *		dmac, route_resolved, hop__limit uninitialized
 *	dev_addr undefined
 *	start_time is set when enter state
 *	a rdma_resolve_ip callback is scheduled
 *	qp in RESET state, no outstanding CQEs nor WQEs
 *	if WAS_CONNECTED, conn_timer is running for reconnect_timeout
 *	delay_timer not running
 *	hb_timer not running
 *	DRAINING, SQ_DRAINED and RQ_DRAINED flags clear
 *	Next States:
 *		CONNECTING - resolution successfully complete
 *		ERROR - resolving hard fail or retry exceeded or connect timeout
 *		RESOLVING - cb error and < retry limit
 *		DELAY - cb error > retrylimit and WAS_CONNECTED and reconnect
 *		destroying - user close
 * RV_CONNECTING: client or server connection in hands of IB CM
 *	cm_id established (either side), rv_cm_handler
 *	primary_path NULL
 *	dev_addr valid on client side
 *	resolver_retry_left undefined
 *	start_time is set when enter state
 *	CM may have a rep or rtu outstanding
 *	qp progressing through RESET->INIT->RTR->RTS via IB CM states/events
 *	if WAS_CONNECTED, conn_timer is running for reconnect_timeout
 *	delay_timer not running
 *	hb_timer not running
 *	DRAINING, SQ_DRAINED and RQ_DRAINED flags clear
 *	Next States:
 *		CONNECTED - client gets REP, server gets RTU or Established
 *		ERROR - REJ, REQ err, REP err, DREQ, QP event or connect timeout
 *		DISCONNECTING - WAS_CONNECTED and REQ err, REJ, REP err, QP evt
 *				or REQ
 *		destroying - user close
 * RV_CONNECTED: client or server connection usable
 *	cm_id established (either side), rv_cm_handler
 *	primary_path NULL
 *	dev_addr valid on client side
 *	resolver_retry_left undefined
 *	no outbound CM operations outstanding
 *	start_time is set when enter state
 *	qp in RTS, has recv WQEs posted
 *	qp may have outstanding CQEs and WQEs
 *	conn_timer not running (no reconnect_timeout)
 *	delay_timer not running
 *	hb_timer may be running (client side only)
 *	WAS_CONNECTED is set on entry
 *	DRAINING, SQ_DRAINED and RQ_DRAINED flags clear
 *	heartbeat timer running
 *		record post SQ and RQ CQE count when start timer
 *		when fires, if counts same, send 0 len RDMA Write
 *		TBD - set larger than timeout*retry or simply track SQ so
 *			don't overflow SQ
 *		TBD - stagger heartbeat sends on different sconns
 *	Next States:
 *		ERROR - no reconnect and get DREQ, QP event, REQ
 *		DISCONNECTING - reconnect and get DREQ, QP event (send DREQ),
 *				get REQ (remote end reconnecting)
 *		destroying - user close
 * RV_DISCONNECTING: client or server connection cleanup in prep for reconnect
 *	cm_id established (either side), rv_cm_handler
 *	primary_path NULL
 *	dev_addr valid on client side
 *	resolver_retry_left undefined
 *	DREP or DREQ outbound CM operations may be outstanding
 *	start_time is set when enter state
 *	qp in ERROR, SCONN_DRAINING flag set,
 *		- waiting for "drain indicator" WQEs' CQEs on RQ and SQ
 *		- drain indicator CQEs set SQ_DRAINED or RQ_DRAINED
 *	qp may have other outstanding CQEs and WQEs
 *	if WAS_CONNECTED, conn_timer may be is running for reconnect_timeout
 *	delay_timer not running
 *	hb_timer not running
 *	Note: we do not depend on DREP and ignore DREQ Err or DREP Err
 *		path to remote node may be down
 *	Next States:
 *		ERROR - unable to queue CM_id destroy or reset QP
 *			or connect timeout
 *		DELAY - SQ & RQ drain finish for client
 *		WAITING - SQ & RQ drain finish for server (reset QP,
 *				cm_id NULL and queued for destroy)
 *		destroying - user close
 * RV_DELAY: client reconnection delay
 *	cm_id established, rv_cm_handler
 *	primary_path NULL
 *	dev_addr valid on client side
 *	resolver_retry_left undefined
 *	DREP or DREQ outbound CM operations may be outstanding on prior cm_id
 *	start_time is set when enter state
 *	qp in RESET
 *	prior qp has no outstanding CQEs and WQEs
 *	if WAS_CONNECTED, conn_timer may be is running for reconnect_timeout
 *	delay_timer is running
 *	hb_timer not running
 *	DRAINING, SQ_DRAINED and RQ_DRAINED flags clear
 *	while in this state all other errors are ignored
 *	Next States:
 *		RESOLVING - timer expires
 *		ERROR - reconnect timeout
 *		destroying - user close
 * RV_ERROR: terminal state for lost/failed connection
 *	may enter from any state
 *	cm_id may be NULL or established with rv_cm_handler
 *	CM may have an outstanding message
 *		typically REJ, DREQ or DREP, but could be REQ, REP or RTU
 *	primary_path NULL
 *	dev_addr, resolver_retry_left undefined
 *	no rdma_resolve_ip callback scheduled (cancel when enter RV_ERROR)
 *	qp in ERROR, SCONN_DRAINING flag set or qp in RESET or no QP
 *	SQ_DRAINED and RQ_DRAINED flags may be progressing on drain
 *	qp may have outstanding CQEs and WQEs
 *	conn_timer not running (no reconnect_timeout)
 *	delay_timer not running
 *	hb_timer not running
 *	Next States:
 *		destroying - user close
 * destroying: rv_conn kref is 0
 *	may enter from any state
 *	on entry:
 *		cm_id may be NULL or established with rv_cm_handler
 *		a rdma_resolve_ip callback may be scheduled
 *		qp may have outstanding CQEs and WQEs and callbacks active
 *		conn_timer may be running (reconnect_timeout or reconnect delay)
 *	all fields (including immitable) will be released via rv_sconn_deinit
 *	no way out other than kfree of parent rv_conn
 *	may prempt, does not require mutex (kref==0 protects races with cb)
 *	Next States:
 *		N/A - conn/sconn freed
 *
 * typical client side state sequence:
 *	RV_INIT->RV_RESOLVING->RV_CONNECTING->RV_CONNECTED->destroying
 * typical server side state sequence:
 *	RV_WAITING->RV_CONNECTING->RV_CONNECTED->destroying
 *
 * typical client side recovery state sequence:
 *	...->RV_CONNECTED->DISCONNECTING->DELAY->RV_RESOLVING->...
 * typical server side recovery state sequence:
 *	...->RV_CONNECTED->DISCONNECTING->WAITING->...
 */

/*
 * rv_sconn.flags bit numbers
 *	RV_SCONN_SERVER	- server vs client side (immutable)
 *	RV_SCONN_WAS_CONNECTED - was RV_CONNECTED at least once
 *	RV_SCONN_DRAINING - started draining in DISCONNECTING
 *	RV_SCONN_SQ_DRAINED - SQ drained in DISCONNECTING
 *	RV_SCONN_RQ_DRAINED - RQ drained in DISCONNECTING
 *	RV_SCONN_ASYNC - got async event
 */
#define RV_SCONN_SERVER		0
#define RV_SCONN_WAS_CONNECTED	1
#define RV_SCONN_DRAINING	2
#define RV_SCONN_SQ_DRAINED	3
#define RV_SCONN_RQ_DRAINED	4

/*
 * rv_sconn.stats.cm_evt_cnt[] classify events > CM_APR_RECEIVED as unexpected.
 * Beyond that are only SIDR events
 */
#define RV_CM_EVENT_MAX IB_CM_APR_RECEIVED
#define RV_CM_EVENT_UNEXP ((enum ib_cm_event_type)(RV_CM_EVENT_MAX + 1))

/*
 * a single QP/connection
 * mutex - protects state driven fields
 *
 * These are set once at create and can be read without lock
 *	index - unique index for connection within rv_conn
 *	qp, send_cq, recv_cq, max_send_wr (from ib_qp_cap)
 *	cqe - for recv completions
 *
 * Fields below require the mutex and their validity depends on the current
 * value of rv_sconn.state and the RV_SCONN_SERVER flag.
 *
 * Basic fields:
 *	state, flags
 *	start_time - when started waiting, resolving or connecting
 *	cm_id - our actual connection
 *	path - from PSM.  Consistency checked on listener, for client connect
 *		we use path.dlid != 0 to identify if path has been initialized
 *
 * Client only fields:
 *	resolver_retry_left, primary_path, dev_addr
 *	For RoCE our resolver step fills in dev_addr with resolved RoCE HW
 *	addresses (aka MAC address)
 *
 * Async QP Draining,
 *	drain_lock -  protects these and enter;test of RC_SCONN_*DRAIN* flags
 *	drain_lock, rdrain_cqe, sdrain_cqe, drain_work (for drain done handler)
 *	drain_timer - for drain timeout
 *	done_wr_list - most recent completed pend_wr's
 *	done_wr_count - number of entries on list
 *
 * reconnect_timeout timer: conn_timer, timer_work
 * RV_DELAY timer: delay_timer, delay_work
 * Heartbeat: hb_timer, act_count (activity since last hb), hb_cqe, hb_work
 *
 * Stats:
 *	all but atomic CQE stats protected by rv_sconn.mutex
 * connection:
 *	*_time values are in microseconds
 *	max_* is the largest observed for all reconnects on this sconn
 *	cm_evt_cnt - extra +1 to include EVENT_MAX and +1 for UNEXP
 *	stats for each CM packet explicitly send (req, rep, rtu, dreq, drep)
 *	initial connect: wait_time, resolve_time, connect_time
 *		connect time does not include wait_time nor resolve_time
 *		resolve - attempts, resolve_fail - unexpected local issues
 *	connected_time - total time connected (after initial + after recovery)
 *	conn_recovery - # times recovered connection
 *	connect recovery: rewait_time, reresolve_time, reconnect_time
 *		reconnect time does not include wait_time nor resolve_time
 *		reresolve - attempts, reresolve_fail - unexpected local issues
 * data movement:
 *	post_* is SQ posts (success, fail, payload byte count)
 *	outstand_send_write - current send writes waiting for CQE
 *	send_write_cqe_* is SQ cqes (RDMA Write w/Immediate)
 *	recv_write_cqe_* is RQ cqes (RDMA Write w/Immediate)
 *	recv_cqe_fail - RQ CQEs with bad status (opcode undefined)
 *	*_hb_* - heartbeat
 */

struct rv_sconn {
	struct mutex mutex; /* lock for state driven fields */
	u8 index;
	struct ib_qp *qp;
	struct ib_cq *send_cq;
	struct ib_cq *recv_cq;
	struct ib_cqe cqe;
	struct rv_conn *parent;

	unsigned long flags;
	enum rv_sconn_state state;
	ktime_t start_time;
	struct ib_cm_id *cm_id;
	struct ib_user_path_rec path;

	u32 resolver_retry_left;
	struct sa_path_rec *primary_path;
	struct rdma_dev_addr dev_addr;

	/* protects these & enter;test RC_SCONN_*DRAIN* flags */
	spinlock_t drain_lock;
	struct ib_cqe rdrain_cqe;
	struct ib_cqe sdrain_cqe;
	struct work_struct drain_work;
	struct timer_list drain_timer;

	struct timer_list conn_timer;
	struct work_struct timer_work;

	struct timer_list delay_timer;
	struct work_struct delay_work;

	struct timer_list hb_timer;
	u64 act_count;
	struct ib_cqe hb_cqe;
	struct work_struct hb_work;

	struct {
		u32 cm_evt_cnt[RV_CM_EVENT_MAX + 2];
		u32 req_sent;
		u32 rep_sent;
		u32 rtu_sent;
		u32 rej_sent;
		u32 dreq_sent;
		u32 drep_sent;
		u64 wait_time;
		u64 resolve_time;
		u64 connect_time;
		u64 connected_time;
		u32 resolve;
		u32 resolve_fail;
		u32 conn_recovery;
		u64 rewait_time;
		u64 reresolve_time;
		u64 reconnect_time;
		u64 max_rewait_time;
		u64 max_reresolve_time;
		u64 max_reconnect_time;
		u32 reresolve;
		u32 reresolve_fail;
		u64 post_write;
		u64 post_write_fail;
		u64 post_write_bytes;
		u64 post_hb;
		u64 post_hb_fail;

		atomic_t outstand_send_write;
		atomic64_t send_write_cqe;
		atomic64_t send_write_cqe_fail;
		atomic64_t recv_write_cqe;
		atomic64_t recv_write_bytes;
		atomic64_t recv_cqe_fail;
		atomic64_t send_hb_cqe;
		atomic64_t send_hb_cqe_fail;
		atomic64_t recv_hb_cqe;
	} stats;
};

/*
 * A load balanced multi QP connection using multiple underlying connections
 * and is shared by multiple rv_user's.
 * num_conn, jdev, ah and rem_addr are immutable (set once at create)
 * Entry in rv_job_dev.conn_list: conn_entry, rcu
 * sconn round robin IO: next_lock, next
 *	next_lock also protects read;inc of sconn->stats.outstand_send_write
 */
struct rv_conn {
	u8 num_conn;
	struct rv_job_dev *jdev;
	struct ib_uverbs_ah_attr ah;
	u32 rem_addr;

	struct list_head conn_entry;
	struct rcu_head rcu;

	struct kref kref;
	struct work_struct put_work;

	spinlock_t next_lock; /* protect rv_conn.next & read;inc outstand_wr */
	u8 next;
	struct work_struct free_work;

	struct rv_sconn sconn_arr[];
};

/*
 * from IBTA 1.4 Vol1 section A3.2.3.4.  Externally Admined Service IDs
 * 1h:OUI:OUI:OUI:hh:hh:hh:hh
 * using Intel 00:12:55 OUI
 */
#define RV_DFLT_SERVICE_ID 0x1000125500000001ULL

/*
 * the set of MRs registered by a single rv_user (on a single NIC)
 * These are cached for efficiency.
 * When using kernel rendezvous QPs (eg. rv_conn) these MRs are
 * registered with the rv_job_dev.pd.
 * When using user space QPs, we register with the user supplied pd
 * cache has it's own lock
 * jdev, rv_inx , CQs, QP set once at alloc
 * need parent rv_user.mutex for: cqe, post REG_MR to QP (doit_reg_mem), stats
 */
struct rv_user_mrs {
	struct rv_mr_cache cache;
	struct rv_job_dev *jdev;
	int rv_inx; /* our creator, for logging */

	struct kref kref;
	struct work_struct put_work;

	struct {
		u64 failed;	/* cache miss and failed to register */
	} stats;
};

/*
 * Resources shared among all rv_user's using a given NIC port within a job
 * for a given kuid.  On the wire we use uid to limit jobs to a single user.
 * This approach has similarities to the hfi1 and Omni-Path jkey concept
 * but we are not restricted to the 16b HW jkey field here, so we use all
 * 32b of uid on the wire.
 *
 * There will typically be 1-8 NICs per node and 1-2 concurrent jobs
 * with one rv_user per CPU core per NIC.
 *
 * kref tracks total jdev references while user_kref is the subset of
 * references representing attached rv_user objects. user_kref <= kref.
 * To get_alloc a jdev we must successfully get both. Once user_kref
 * is zero, the jdev is removed from rv_job_dev_list and future attach
 * or inbound REQ processing will not find it anymore.  At this point it is
 * destined for destruction as soon as the remaining callbacks with a reference
 * occur/finish. jdev get_alloc searches which see (and skip) jdevs with
 * user_kref==0 but kfef>0 only happen when a new job starts without a unique
 * job key, as the previous job is cleaning up. Such cases are rare.
 * By decoupling the jobs into different jdev's the new job may also have
 * different attach and connection params.
 *
 * These fields are immutable and can be accessed without a lock:
 *	kuid, uid, dev, pd, dev_name, port_num,
 *	num_conn - number of rv_sconn per rv_conn
 *	index_bits - number of high bits of RDMA immed data to hold rv index
 *	loc_gid_index - SGID - only used on client rv_sconn
 *	loc_addr - abstraction of address compared with rem_addr to select
 *		client/server mode for each rv_sconn at conn_create time.
 *	log_gid - SGID - to double check loc_gid_index still is same GID
 *	service_id, q_depth
 *	qp_depth - max send and recv WQEs to use (N/A space for drain)
 *	reconnect_timeout (seconds), hb_interval (milliseconds),
 *	sgid_attr - local NIC gid & address for use by resolver
 *	max_users - 1<<index_bits
 *
 * job_dev_entry, rcu - entry on rv_job_dev_list
 * conn_list - list of shared rv_conn, protected by RCU and conn_list_mutex
 *	conn_list_mutex prevents duplicate add in get_alloc
 * listener - created and shared when 1st server rv_conn is added
 * user_array[max_users] - rv_users sharing this jdev. user_array_lock protects
 *	rv_user.index - subscript to this array for given rv_user so
 *		RDMA recv immediate data can simply index to find which
 *		rv_user to deliver the completion to.
 *	user_array_next - where to start next search for free slot
 */
struct rv_job_dev {
	uid_t uid;
	kuid_t kuid;
	struct rv_device *dev;
	struct ib_pd *pd;
	char dev_name[RV_MAX_DEV_NAME_LEN];
	u8 port_num;
	u8 num_conn;
	u8 index_bits;
	u16 loc_gid_index;
	u32 loc_addr;
	u8 loc_gid[16];
	u8 job_key[RV_MAX_JOB_KEY_LEN];
	u8 job_key_len;
	u64 service_id;
	u32 q_depth;
	u32 qp_depth;
	u32 reconnect_timeout;
	u32 hb_interval;
	const struct ib_gid_attr *sgid_attr;
	int max_users;

	struct kref kref;
	struct list_head job_dev_entry;
	struct rcu_head rcu;

	struct mutex conn_list_mutex; /* prevent duplicate add in get_alloc */
	struct list_head conn_list;
	struct rv_listener *listener;

	spinlock_t user_array_lock;/* protects add/remove from user_array */
	u32 user_array_next;
	struct kref user_kref;
	struct rv_user *user_array[];
};

/*
 * rv_user represents a single open fd from a user
 * In multi-rail a process may have multiple rv_user (separate open/close)
 *
 * mutex - prevents concurrent ATTACH ioctl and protects conn_list
 *	also protects doit_reg vs self and vs doit_dreg races
 * attached - set last during ATTACH after dev/jdev, rdma_mode and cq_entries
 *	have been set.  We have no detach.
 * inx - immutable ID assignd to rv_user strictly for logging
 * rdma_mode - indicates USER (MRs only) or KERNEL (jdev, conns, etc) attach
 *	For rdma_mode KERNEL these are also valid:
 *		context, cq_entries
 *		index - rv_user index within rv_job_dev.user_array[]
 *		cqr - event ring to deliver send and recv RDMA completion
 *			events to PSM (only if ATTACH.cq_entries!=0)
 * rv_user_mrs  - MRs registered by this rv_user
 * conn_xa - ID indexed list of rv_conn (entries assigned on create_conn)
 *	ID 0 is reserved, PSM uses this value to indicate uninitialized rv intf
 *	rv_user.mutex protects, so no need to use xas_lock.
 * user_entry - entry in rv_device.user_list
 * compl - completion for detach
 */
struct rv_user {
	struct mutex mutex; /* single thread most actions for a user */

	int inx;

	u8 rdma_mode;
	u8 attached;
	u8 was_attached;
	union {
		struct rv_device *dev;
		struct rv_job_dev *jdev;
	};

	u64 context;
	u32 cq_entries;
	u16 index;
	struct rv_user_ring *cqr;
	struct rv_user_mrs *umrs;

	struct xarray conn_xa;

	struct list_head user_entry;
	struct completion compl;
};

/*
 * an event ring for use by a single rv_user
 * allows events to be efficiently passed from rv to PSM for PSM polling
 * Immutable fields set on alloc:
 *	rv_inx - our creator's rv_user.inx (for logging)
 *	num_entries, page, order
 * lock - protects kernel posting to ring and stats
 * hdr - mmapped into PSM
 * stats - index for each is rv_event_type excluding RV_TEST_EVENT
 */
struct rv_user_ring {
	int rv_inx;
	u32 num_entries;
	unsigned long page;
	unsigned int order;

	spinlock_t lock; /* protects posting to ring and stats*/
	struct rv_ring_header *hdr;
	struct {
		u64 cqe[2];
		u64 cqe_fail[2];
		u64 bytes[2];
	} stats;
};

/*
 * an inflight RDMA write on behalf of an rv_user
 *
 * user_index - rv_user index within rv_job_dev.user_array[]
 * umrs - MR cache holding the local MR
 * mrc, loc_addr - local MR (with lkey), source memory address for RDMA Write
 * rkey, rem_addr - dest memory address for RDMA Write
 * wr_id - for completion event to PSM (supplied by PSM)
 * done_wr_entry - Entry in rv_sconn.done_wr_list
 */
struct rv_pend_write {
	struct ib_cqe cqe;
	u16 user_index;
	struct rv_user_mrs *umrs;
	struct rv_sconn *sconn;

	struct rv_mr_cached *mrc;
	u64 loc_addr;
	u32 rkey;
	u64 rem_addr;
	size_t length;
	u32 immed;
	u64 wr_id;
};

extern unsigned int enable_user_mr;

/* Prototypes */
struct rv_device *rv_device_get_add_user(char *dev_name, struct rv_user *rv);
void rv_device_get(struct rv_device *dev);
void rv_device_put(struct rv_device *dev);
int rv_device_del_user(struct rv_user *rv);

void rv_listener_get(struct rv_listener *listener);
void rv_listener_put(struct rv_listener *listener);
struct rv_listener *rv_listener_get_alloc(struct rv_device *dev,
					  u64 service_id,
					  ib_cm_handler handler);

int rv_file_init(void);
void rv_file_uninit(void);
void rv_queue_work(struct work_struct *work);
void rv_queue_work2(struct work_struct *work);
void rv_flush_work2(void);
void rv_queue_work3(struct work_struct *work);

int doit_reg_mem(struct rv_user *rv, unsigned long arg);
int doit_dereg_mem(struct rv_user *rv, unsigned long arg);
void rv_user_mrs_get(struct rv_user_mrs *umrs);
void rv_user_mrs_put(struct rv_user_mrs *umrs);
struct rv_user_mrs *rv_user_mrs_alloc(struct rv_user *rv, u32 cache_size);
void rv_user_mrs_attach(struct rv_user *rv);
int rv_drv_api_dereg_mem(struct mr_info *mr);

int rv_drv_prepost_recv(struct rv_sconn *sconn);
void rv_recv_done(struct ib_cq *cq, struct ib_wc *wc);
void rv_report_cqe_error(struct ib_cq *cq, struct ib_wc *wc,
			 struct rv_sconn *sconn, const char *opname);
int doit_post_rdma_write(struct rv_user *rv, unsigned long arg);

static inline struct rv_conn *user_conn_find(struct rv_user *rv, u64 handle)
{
	return xa_load(&rv->conn_xa, handle);
}

void rv_conn_put(struct rv_conn *conn);
int rv_conn_get_check(struct rv_conn *conn);
void rv_conn_get(struct rv_conn *conn);
int doit_conn_create(struct rv_user *rv, unsigned long arg);
int doit_conn_connect(struct rv_user *rv, unsigned long arg);
int doit_conn_connected(struct rv_user *rv, unsigned long arg);
int doit_conn_get_conn_count(struct rv_user *rv, unsigned long arg);
int doit_conn_get_stats(struct rv_user *rv, unsigned long arg);
int cmp_gid(const void *gid1, const void *gid2);

void rv_job_dev_get(struct rv_job_dev *jdev);
void rv_job_dev_put(struct rv_job_dev *jdev);

static inline bool rv_job_dev_has_users(struct rv_job_dev *jdev)
{
	return kref_read(&jdev->user_kref);
}

static inline bool rv_jdev_protocol_roce(const struct rv_job_dev *jdev)
{
	return rdma_protocol_roce(jdev->dev->ib_dev, jdev->port_num);
}

struct rv_sconn *
rv_find_sconn_from_req(struct ib_cm_id *id,
		       const struct ib_cm_req_event_param *param,
		       struct rv_req_priv_data *priv_data);

void rv_detach_user(struct rv_user *rv);

#endif /* __RV_H__ */
