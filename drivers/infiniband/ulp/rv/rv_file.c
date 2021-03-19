// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Copyright(c) 2020 - 2021 Intel Corporation.
 */

#include <rdma/ib_cache.h>
#include <linux/cdev.h>

#include "rv.h"
#include "trace.h"

/* A workqueue for all */
static struct workqueue_struct *rv_wq;
static struct workqueue_struct *rv_wq2;
static struct workqueue_struct *rv_wq3;

/*
 * We expect relatively few jobs per node (typically 1)
 * and relatively few devices per node (typically 1 to 8)
 * so the list of job_dev's should be short and is only used
 * at job launch and shutdown.
 *
 * search key is job_key, dev_name, port_num; short list linear search ok
 * mutex avoids duplicate get_alloc adds, RCU protects list access.
 * See rv.h comments about "get_alloc" for more information.
 */
static struct list_head rv_job_dev_list;

void rv_queue_work(struct work_struct *work)
{
	queue_work(rv_wq, work);
}

void rv_queue_work2(struct work_struct *work)
{
	queue_work(rv_wq2, work);
}

void rv_queue_work3(struct work_struct *work)
{
	queue_work(rv_wq3, work);
}

void rv_flush_work2(void)
{
	flush_workqueue(rv_wq2);
}

void rv_job_dev_get(struct rv_job_dev *jdev)
{
	kref_get(&jdev->kref);
}

static void rv_job_dev_release(struct kref *kref)
{
	struct rv_job_dev *jdev = container_of(kref, struct rv_job_dev, kref);

	kfree_rcu(jdev, rcu);
}

void rv_job_dev_put(struct rv_job_dev *jdev)
{
	kref_put(&jdev->kref, rv_job_dev_release);
}

/*
 * confirm that we expected a REQ from this remote node on this port.
 * Note CM swaps src vs dest so dest is remote node here
 */
static struct rv_sconn *
rv_conn_match_req(struct rv_conn *conn,
		  const struct ib_cm_req_event_param *param,
		  struct rv_req_priv_data *priv_data)
{
	if (param->port != conn->ah.port_num)
		return NULL;
	if ((param->primary_path->rec_type == SA_PATH_REC_TYPE_IB &&
	     be16_to_cpu(param->primary_path->ib.dlid) != conn->ah.dlid) ||
	    (param->primary_path->rec_type == SA_PATH_REC_TYPE_OPA &&
	     be32_to_cpu(param->primary_path->opa.dlid) != conn->ah.dlid) ||
	    (conn->ah.is_global &&
	     cmp_gid(&param->primary_path->dgid, conn->ah.grh.dgid)))
		return NULL;

	if (priv_data->index >= conn->num_conn)
		return NULL;

	return &conn->sconn_arr[priv_data->index];
}

/*
 * Within an rv_job_dev, find the server rv_sconn which matches the incoming
 * CM request
 * We are holding the rv_job_dev_list rcu_read_lock
 * If found, the refcount for the rv_conn_info will be incremented.
 */
static struct rv_sconn *
rv_jdev_find_conn(struct rv_job_dev *jdev,
		  const struct ib_cm_req_event_param *param,
		  struct rv_req_priv_data *priv_data)
{
	struct rv_conn *conn;
	struct rv_sconn *sconn = NULL;

	rcu_read_lock();
	list_for_each_entry_rcu(conn, &jdev->conn_list, conn_entry) {
		WARN_ON(jdev != conn->jdev);
		sconn = rv_conn_match_req(conn, param, priv_data);
		if (!sconn)
			continue;
		if (!kref_get_unless_zero(&conn->kref))
			continue;
		break;
	}
	rcu_read_unlock();

	return sconn;
}

/*
 * Find the rv_sconn matching the received REQ
 * listener may be shared by rv_job_dev's so filter on dev 1st
 */
struct rv_sconn *
rv_find_sconn_from_req(struct ib_cm_id *id,
		       const struct ib_cm_req_event_param *param,
		       struct rv_req_priv_data *priv_data)
{
	struct rv_sconn *sconn = NULL;
	struct rv_listener *listener = id->context;
	struct rv_job_dev *jdev;

	rcu_read_lock();
	list_for_each_entry_rcu(jdev, &rv_job_dev_list, job_dev_entry) {
		if (listener->dev != jdev->dev)
			continue;
		if (priv_data->uid != jdev->uid)
			continue;
		if (priv_data->job_key_len != jdev->job_key_len ||
		    memcmp(priv_data->job_key, jdev->job_key,
			   jdev->job_key_len))
			continue;
		if (param->port != jdev->port_num ||
		    cmp_gid(&param->primary_path->sgid, jdev->loc_gid))
			continue;
		if (!rv_job_dev_has_users(jdev))
			continue;

		sconn = rv_jdev_find_conn(jdev, param, priv_data);
		if (sconn)
			break;
	}
	rcu_read_unlock();

	return sconn;
}
