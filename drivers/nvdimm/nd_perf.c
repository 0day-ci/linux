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

static ssize_t nvdimm_pmu_cpumask_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct pmu *pmu = dev_get_drvdata(dev);
	struct nvdimm_pmu *nd_pmu;

	nd_pmu = container_of(pmu, struct nvdimm_pmu, pmu);

	return cpumap_print_to_pagebuf(true, buf, cpumask_of(nd_pmu->cpu));
}

static int nvdimm_pmu_cpu_offline(unsigned int cpu, struct hlist_node *node)
{
	struct nvdimm_pmu *nd_pmu;
	u32 target;
	int nodeid;
	const struct cpumask *cpumask;

	nd_pmu = hlist_entry_safe(node, struct nvdimm_pmu, node);

	/* Clear it, incase given cpu is set in nd_pmu->arch_cpumask */
	cpumask_test_and_clear_cpu(cpu, &nd_pmu->arch_cpumask);

	/*
	 * If given cpu is not same as current designated cpu for
	 * counter access, just return.
	 */
	if (cpu != nd_pmu->cpu)
		return 0;

	/* Check for any active cpu in nd_pmu->arch_cpumask */
	target = cpumask_any(&nd_pmu->arch_cpumask);

	/*
	 * Incase we don't have any active cpu in nd_pmu->arch_cpumask,
	 * check in given cpu's numa node list.
	 */
	if (target >= nr_cpu_ids) {
		nodeid = cpu_to_node(cpu);
		cpumask = cpumask_of_node(nodeid);
		target = cpumask_any_but(cpumask, cpu);
	}
	nd_pmu->cpu = target;

	/* Migrate nvdimm pmu events to the new target cpu if valid */
	if (target >= 0 && target < nr_cpu_ids)
		perf_pmu_migrate_context(&nd_pmu->pmu, cpu, target);

	return 0;
}

static int nvdimm_pmu_cpu_online(unsigned int cpu, struct hlist_node *node)
{
	struct nvdimm_pmu *nd_pmu;

	nd_pmu = hlist_entry_safe(node, struct nvdimm_pmu, node);

	if (nd_pmu->cpu >= nr_cpu_ids)
		nd_pmu->cpu = cpu;

	return 0;
}

static int create_cpumask_attr_group(struct nvdimm_pmu *nd_pmu)
{
	struct perf_pmu_events_attr *attr;
	struct attribute **attrs;
	struct attribute_group *nvdimm_pmu_cpumask_group;

	attr = kzalloc(sizeof(*attr), GFP_KERNEL);
	if (!attr)
		return -ENOMEM;

	attrs = kzalloc(2 * sizeof(struct attribute *), GFP_KERNEL);
	if (!attrs) {
		kfree(attr);
		return -ENOMEM;
	}

	/* Allocate memory for cpumask attribute group */
	nvdimm_pmu_cpumask_group = kzalloc(sizeof(*nvdimm_pmu_cpumask_group), GFP_KERNEL);
	if (!nvdimm_pmu_cpumask_group) {
		kfree(attr);
		kfree(attrs);
		return -ENOMEM;
	}

	sysfs_attr_init(&attr->attr.attr);
	attr->attr.attr.name = "cpumask";
	attr->attr.attr.mode = 0444;
	attr->attr.show = nvdimm_pmu_cpumask_show;
	attrs[0] = &attr->attr.attr;
	attrs[1] = NULL;

	nvdimm_pmu_cpumask_group->attrs = attrs;
	nd_pmu->attr_groups[NVDIMM_PMU_CPUMASK_ATTR] = nvdimm_pmu_cpumask_group;
	return 0;
}

static int nvdimm_pmu_cpu_hotplug_init(struct nvdimm_pmu *nd_pmu)
{
	int nodeid, rc;
	const struct cpumask *cpumask;

	/*
	 * Incase cpu hotplug is not handled by arch specific code
	 * they can still provide required cpumask which can be used
	 * to get designatd cpu for counter access.
	 * Check for any active cpu in nd_pmu->arch_cpumask.
	 */
	if (!cpumask_empty(&nd_pmu->arch_cpumask)) {
		nd_pmu->cpu = cpumask_any(&nd_pmu->arch_cpumask);
	} else {
		/* pick active cpu from the cpumask of device numa node. */
		nodeid = dev_to_node(nd_pmu->dev);
		cpumask = cpumask_of_node(nodeid);
		nd_pmu->cpu = cpumask_any(cpumask);
	}

	rc = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN, "perf/nvdimm:online",
				     nvdimm_pmu_cpu_online, nvdimm_pmu_cpu_offline);

	if (rc < 0)
		return rc;

	nd_pmu->cpuhp_state = rc;

	/* Register the pmu instance for cpu hotplug */
	rc = cpuhp_state_add_instance_nocalls(nd_pmu->cpuhp_state, &nd_pmu->node);
	if (rc) {
		cpuhp_remove_multi_state(nd_pmu->cpuhp_state);
		return rc;
	}

	/* Create cpumask attribute group */
	rc = create_cpumask_attr_group(nd_pmu);
	if (rc) {
		cpuhp_state_remove_instance_nocalls(nd_pmu->cpuhp_state, &nd_pmu->node);
		cpuhp_remove_multi_state(nd_pmu->cpuhp_state);
		return rc;
	}

	return 0;
}

void nvdimm_pmu_free_hotplug_memory(struct nvdimm_pmu *nd_pmu)
{
	cpuhp_state_remove_instance_nocalls(nd_pmu->cpuhp_state, &nd_pmu->node);
	cpuhp_remove_multi_state(nd_pmu->cpuhp_state);

	if (nd_pmu->attr_groups[NVDIMM_PMU_CPUMASK_ATTR])
		kfree(nd_pmu->attr_groups[NVDIMM_PMU_CPUMASK_ATTR]->attrs);
	kfree(nd_pmu->attr_groups[NVDIMM_PMU_CPUMASK_ATTR]);
}

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

	/*
	 * Incase cpumask attribute is set it means cpu
	 * hotplug is handled by the arch specific code and
	 * we can skip calling hotplug_init.
	 */
	if (!nd_pmu->attr_groups[NVDIMM_PMU_CPUMASK_ATTR]) {
		/* init cpuhotplug */
		rc = nvdimm_pmu_cpu_hotplug_init(nd_pmu);
		if (rc) {
			pr_info("cpu hotplug feature failed for device: %s\n", nd_pmu->name);
			return rc;
		}
	}

	rc = perf_pmu_register(&nd_pmu->pmu, nd_pmu->name, -1);
	if (rc) {
		nvdimm_pmu_free_hotplug_memory(nd_pmu);
		return rc;
	}

	pr_info("%s NVDIMM performance monitor support registered\n",
		nd_pmu->name);

	return 0;
}
EXPORT_SYMBOL_GPL(register_nvdimm_pmu);

void unregister_nvdimm_pmu(struct nvdimm_pmu *nd_pmu)
{
	/* handle freeing of memory nd_pmu in arch specific code */
	perf_pmu_unregister(&nd_pmu->pmu);
	nvdimm_pmu_free_hotplug_memory(nd_pmu);
}
EXPORT_SYMBOL_GPL(unregister_nvdimm_pmu);
