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

#define to_nvdimm_pmu(_pmu)	container_of(_pmu, struct nvdimm_pmu, pmu)

static int nvdimm_pmu_event_init(struct perf_event *event)
{
	struct nvdimm_pmu *nd_pmu = to_nvdimm_pmu(event->pmu);

	/* test the event attr type for PMU enumeration */
	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* it does not support event sampling mode */
	if (is_sampling_event(event))
		return -EINVAL;

	/* no branch sampling */
	if (has_branch_stack(event))
		return -EOPNOTSUPP;

	/* jump to arch/platform specific callbacks if any */
	if (nd_pmu && nd_pmu->event_init)
		return nd_pmu->event_init(event, nd_pmu->dev);

	return 0;
}

static void nvdimm_pmu_read(struct perf_event *event)
{
	struct nvdimm_pmu *nd_pmu = to_nvdimm_pmu(event->pmu);

	/* jump to arch/platform specific callbacks if any */
	if (nd_pmu && nd_pmu->read)
		nd_pmu->read(event, nd_pmu->dev);
}

static void nvdimm_pmu_del(struct perf_event *event, int flags)
{
	struct nvdimm_pmu *nd_pmu = to_nvdimm_pmu(event->pmu);

	/* jump to arch/platform specific callbacks if any */
	if (nd_pmu && nd_pmu->del)
		nd_pmu->del(event, flags, nd_pmu->dev);
}

static int nvdimm_pmu_add(struct perf_event *event, int flags)
{
	struct nvdimm_pmu *nd_pmu = to_nvdimm_pmu(event->pmu);

	if (flags & PERF_EF_START)
		/* jump to arch/platform specific callbacks if any */
		if (nd_pmu && nd_pmu->add)
			return nd_pmu->add(event, flags, nd_pmu->dev);
	return 0;
}

int register_nvdimm_pmu(struct nvdimm_pmu *nd_pmu, struct platform_device *pdev)
{
	int rc;

	if (!nd_pmu || !pdev)
		return -EINVAL;

	nd_pmu->pmu.task_ctx_nr = perf_invalid_context;
	nd_pmu->pmu.event_init = nvdimm_pmu_event_init;
	nd_pmu->pmu.add = nvdimm_pmu_add;
	nd_pmu->pmu.del = nvdimm_pmu_del;
	nd_pmu->pmu.read = nvdimm_pmu_read;
	nd_pmu->pmu.name = nd_pmu->name;
	nd_pmu->pmu.attr_groups = nd_pmu->attr_groups;
	nd_pmu->pmu.capabilities = PERF_PMU_CAP_NO_INTERRUPT |
				PERF_PMU_CAP_NO_EXCLUDE;

	/*
	 * Adding platform_device->dev pointer to nvdimm_pmu, so that we can
	 * access that device data in PMU callbacks and also pass it to
	 * arch/platform specific code.
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
	/*
	 * nd_pmu will get free in arch/platform specific code once
	 * corresponding pmu get unregistered.
	 */
	perf_pmu_unregister(nd_pmu);
}
EXPORT_SYMBOL_GPL(unregister_nvdimm_pmu);
