// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Copyright(c) 2020 - 2021 Intel Corporation.
 */

#include "rv.h"

/* A workqueue for all */
static struct workqueue_struct *rv_wq;

void rv_queue_work(struct work_struct *work)
{
	queue_work(rv_wq, work);
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
