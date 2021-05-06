/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_GT_PM_DELAYED_WORK_H
#define INTEL_GT_PM_DELAYED_WORK_H

#include <linux/list.h>
#include <linux/workqueue.h>

struct intel_gt;

struct intel_gt_pm_delayed_work {
	struct list_head link;
	struct work_struct worker;
};

void intel_gt_pm_queue_delayed_work(struct intel_gt *gt);

void intel_gt_pm_add_delayed_work(struct intel_gt *gt,
				  struct intel_gt_pm_delayed_work *work);

#endif /* INTEL_GT_PM_DELAYED_WORK_H */
