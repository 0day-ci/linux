// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * nd_perf.c: NVDIMM Device Performance Monitoring Unit support
 *
 * Perf interface to expose nvdimm performance stats.
 *
 * Copyright (C) 2021 IBM Corporation
 */

#define pr_fmt(fmt) "nvdimm_pmu: " fmt

#include <linux/nd.h>

int register_nvdimm_pmu(struct nvdimm_pmu *nd_pmu, struct platform_device *pdev)
{
	int rc;

	if (!nd_pmu || !pdev)
		return -EINVAL;

	/* event functions like add/del/read/event_init should not be NULL */
	if (WARN_ON_ONCE(!(nd_pmu->event_init && nd_pmu->add && nd_pmu->del && nd_pmu->read)))
		return -EINVAL;

	nd_pmu->pmu.task_ctx_nr = perf_invalid_context;
	nd_pmu->pmu.name = nd_pmu->name;
	nd_pmu->pmu.event_init = nd_pmu->event_init;
	nd_pmu->pmu.add = nd_pmu->add;
	nd_pmu->pmu.del = nd_pmu->del;
	nd_pmu->pmu.read = nd_pmu->read;

	nd_pmu->pmu.attr_groups = nd_pmu->attr_groups;
	nd_pmu->pmu.capabilities = PERF_PMU_CAP_NO_INTERRUPT |
				PERF_PMU_CAP_NO_EXCLUDE;

	/*
	 * Add platform_device->dev pointer to nvdimm_pmu to access
	 * device data in events functions.
	 */
	nd_pmu->dev = &pdev->dev;

	rc = perf_pmu_register(&nd_pmu->pmu, nd_pmu->name, -1);
	if (rc)
		return rc;

	pr_info("%s NVDIMM performance monitor support registered\n",
		nd_pmu->name);

	return 0;
}
EXPORT_SYMBOL_GPL(register_nvdimm_pmu);

void unregister_nvdimm_pmu(struct pmu *nd_pmu)
{
	/* handle freeing of memory in arch specific code */
	perf_pmu_unregister(nd_pmu);
}
EXPORT_SYMBOL_GPL(unregister_nvdimm_pmu);
