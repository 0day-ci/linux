/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(C) 2016-2020 Intel Corporation. All rights reserved. */

#ifndef __DLB_ARGS_H
#define __DLB_ARGS_H

struct dlb_cmd_response {
	__u32 status; /* Interpret using enum dlb_error */
	__u32 id;
};

#define DLB_DEVICE_VERSION(x) (((x) >> 8) & 0xFF)
#define DLB_DEVICE_REVISION(x) ((x) & 0xFF)

/*****************************************************/
/* 'dlb' device level control/access data structures */
/*****************************************************/

/*
 * dlb_create_sched_domain_args: Used to create a DLB 2.0 scheduling domain
 *	and reserve its hardware resources.
 *
 * Output parameters:
 * @response.status: Detailed error code. In certain cases, such as if the
 *	request arg is invalid, the driver won't set status.
 * @response.id: domain ID.
 * @domain_fd: file descriptor for performing the domain's reset operation.
 *
 * Input parameters:
 * @num_ldb_queues: Number of load-balanced queues.
 * @num_ldb_ports: Number of load-balanced ports that can be allocated from
 *	any class-of-service with available ports.
 * @num_dir_ports: Number of directed ports. A directed port has one directed
 *	queue, so no num_dir_queues argument is necessary.
 * @num_atomic_inflights: This specifies the amount of temporary atomic QE
 *	storage for the domain. This storage is divided among the domain's
 *	load-balanced queues that are configured for atomic scheduling.
 * @num_hist_list_entries: Amount of history list storage. This is divided
 *	among the domain's CQs.
 * @num_ldb_credits: Amount of load-balanced QE storage (QED). QEs occupy this
 *	space until they are scheduled to a load-balanced CQ. One credit
 *	represents the storage for one QE.
 * @num_dir_credits: Amount of directed QE storage (DQED). QEs occupy this
 *	space until they are scheduled to a directed CQ. One credit represents
 *	the storage for one QE.
 */
struct dlb_create_sched_domain_args {
	/* Output parameters */
	struct dlb_cmd_response response;
	__u32 domain_fd;
	/* Input parameters */
	__u32 num_ldb_queues;
	__u32 num_ldb_ports;
	__u32 num_dir_ports;
	__u32 num_atomic_inflights;
	__u32 num_hist_list_entries;
	__u32 num_ldb_credits;
	__u32 num_dir_credits;
};

/*************************************************/
/* 'domain' level control/access data structures */
/*************************************************/

/*
 * dlb_create_ldb_queue_args: Used to configure a load-balanced queue.
 *
 * Output parameters:
 * @response.status: Detailed error code. In certain cases, such as if the
 *	request arg is invalid, the driver won't set status.
 * @response.id: Queue ID.
 *
 * Input parameters:
 * @num_atomic_inflights: This specifies the amount of temporary atomic QE
 *	storage for this queue. If zero, the queue will not support atomic
 *	scheduling.
 * @num_sequence_numbers: This specifies the number of sequence numbers used
 *	by this queue. If zero, the queue will not support ordered scheduling.
 *	If non-zero, the queue will not support unordered scheduling.
 * @num_qid_inflights: The maximum number of QEs that can be inflight
 *	(scheduled to a CQ but not completed) at any time. If
 *	num_sequence_numbers is non-zero, num_qid_inflights must be set equal
 *	to num_sequence_numbers.
 * @lock_id_comp_level: Lock ID compression level. Specifies the number of
 *	unique lock IDs the queue should compress down to. Valid compression
 *	levels: 0, 64, 128, 256, 512, 1k, 2k, 4k, 64k. If lock_id_comp_level is
 *	0, the queue won't compress its lock IDs.
 * @depth_threshold: DLB sets two bits in the received QE to indicate the
 *	depth of the queue relative to the threshold before scheduling the
 *	QE to a CQ:
 *	- 2’b11: depth > threshold
 *	- 2’b10: threshold >= depth > 0.75 * threshold
 *	- 2’b01: 0.75 * threshold >= depth > 0.5 * threshold
 *	- 2’b00: depth <= 0.5 * threshold
 */
struct dlb_create_ldb_queue_args {
	/* Output parameters */
	struct dlb_cmd_response response;
	/* Input parameters */
	__u32 num_sequence_numbers;
	__u32 num_qid_inflights;
	__u32 num_atomic_inflights;
	__u32 lock_id_comp_level;
	__u32 depth_threshold;
};

/*
 * dlb_create_dir_queue_args: Used to configure a directed queue.
 *
 * Output parameters:
 * @response.status: Detailed error code. In certain cases, such as if the
 *	request arg is invalid, the driver won't set status.
 * @response.id: Queue ID.
 *
 * Input parameters:
 * @port_id: Port ID. If the corresponding directed port is already created,
 *	specify its ID here. Else this argument must be 0xFFFFFFFF to indicate
 *	that the queue is being created before the port.
 * @depth_threshold: DLB sets two bits in the received QE to indicate the
 *	depth of the queue relative to the threshold before scheduling the
 *	QE to a CQ:
 *	- 2’b11: depth > threshold
 *	- 2’b10: threshold >= depth > 0.75 * threshold
 *	- 2’b01: 0.75 * threshold >= depth > 0.5 * threshold
 *	- 2’b00: depth <= 0.5 * threshold
 */
struct dlb_create_dir_queue_args {
	/* Output parameters */
	struct dlb_cmd_response response;
	/* Input parameters */
	__s32 port_id;
	__u32 depth_threshold;
};

/*
 * dlb_get_ldb_queue_depth_args: Used to get a load-balanced queue's depth.
 *
 * Output parameters:
 * @response.status: Detailed error code. In certain cases, such as if the
 *	request arg is invalid, the driver won't set status.
 * @response.id: queue depth.
 *
 * Input parameters:
 * @queue_id: The load-balanced queue ID.
 */
struct dlb_get_ldb_queue_depth_args {
	/* Output parameters */
	struct dlb_cmd_response response;
	/* Input parameters */
	__u32 queue_id;
};

/*
 * dlb_get_dir_queue_depth_args: Used to get a directed queue's depth.
 *
 * Output parameters:
 * @response.status: Detailed error code. In certain cases, such as if the
 *	request arg is invalid, the driver won't set status.
 * @response.id: queue depth.
 *
 * Input parameters:
 * @queue_id: The directed queue ID.
 */
struct dlb_get_dir_queue_depth_args {
	/* Output parameters */
	struct dlb_cmd_response response;
	/* Input parameters */
	__u32 queue_id;
};

/*
 * dlb_create_ldb_port_args: Used to configure a load-balanced port.
 *
 * Output parameters:
 * @response.status: Detailed error code. In certain cases, such as if the
 *	request arg is invalid, the driver won't set status.
 * @response.id: port ID.
 *
 * Input parameters:
 * @cq_depth: Depth of the port's CQ. Must be a power-of-two between 8 and
 *	1024, inclusive.
 * @cq_depth_threshold: CQ depth interrupt threshold. A value of N means that
 *	the CQ interrupt won't fire until there are N or more outstanding CQ
 *	tokens.
 * @num_hist_list_entries: Number of history list entries. This must be
 *	greater than or equal cq_depth.
 */

struct dlb_create_ldb_port_args {
	/* Output parameters */
	struct dlb_cmd_response response;
	__u32 pp_fd;
	__u32 cq_fd;
	/* Input parameters */
	__u16 cq_depth;
	__u16 cq_depth_threshold;
	__u16 cq_history_list_size;
};

/*
 * dlb_create_dir_port_args: Used to configure a directed port.
 *
 * Output parameters:
 * @response.status: Detailed error code. In certain cases, such as if the
 *	request arg is invalid, the driver won't set status.
 * @response.id: Port ID.
 *
 * Input parameters:
 * @cq_depth: Depth of the port's CQ. Must be a power-of-two between 8 and
 *	1024, inclusive.
 * @cq_depth_threshold: CQ depth interrupt threshold. A value of N means that
 *	the CQ interrupt won't fire until there are N or more outstanding CQ
 *	tokens.
 * @qid: Queue ID. If the corresponding directed queue is already created,
 *	specify its ID here. Else this argument must be 0xFFFFFFFF to indicate
 *	that the port is being created before the queue.
 */
struct dlb_create_dir_port_args {
	/* Output parameters */
	struct dlb_cmd_response response;
	__u32 pp_fd;
	__u32 cq_fd;
	/* Input parameters */
	__u16 cq_depth;
	__u16 cq_depth_threshold;
	__s32 queue_id;
};

/*
 * dlb_get_port_fd_args: Used to get file descriptor to mmap a producer port
 *	(PP) or a consumer queue (CQ)
 *
 *	The port must have been previously created in the device's configfs.
 *	The fd is used to mmap the PP/CQ region.
 *
 * Output parameters:
 * @response.status: Detailed error code. In certain cases, such as if the
 *	request arg is invalid, the driver won't set status.
 * @response.id: fd.
 *
 * Input parameters:
 * @port_id: port ID.
 */
struct dlb_get_port_fd_args {
	/* Output parameters */
	struct dlb_cmd_response response;
	/* Input parameters */
	__u32 port_id;
};

/*
 * dlb_start_domain_args: Used to mark the end of the domain configuration. This
 *	must be called before passing QEs into the device, and no configuration
 *	via configfs can be done once the domain has started. Sending QEs into the
 *	device before starting the domain will result in undefined behavior.
 * Input parameters:
 * - (None)
 *
 * Output parameters:
 * @response.status: Detailed error code. In certain cases, such as if the
 *	configfs request arg is invalid, the driver won't set status.
 */
struct dlb_start_domain_args {
	/* Output parameters */
	struct dlb_cmd_response response;
};

/*
 * Mapping sizes for memory mapping the consumer queue (CQ) memory space, and
 * producer port (PP) MMIO space.
 */
#define DLB_CQ_SIZE 65536
#define DLB_PP_SIZE 4096

#endif /* __DLB_ARGS_H */
