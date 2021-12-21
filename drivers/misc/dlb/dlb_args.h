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
#endif /* DLB_ARGS_H */
