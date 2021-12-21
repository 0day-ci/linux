// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(C) 2016-2020 Intel Corporation. All rights reserved. */

#include <linux/delay.h>
#include <linux/pm_runtime.h>

#include "dlb_main.h"
#include "dlb_regs.h"

/********************************/
/****** PCI BAR management ******/
/********************************/

int dlb_pf_map_pci_bar_space(struct dlb *dlb, struct pci_dev *pdev)
{
	dlb->hw.func_kva = pcim_iomap_table(pdev)[DLB_FUNC_BAR];
	dlb->hw.func_phys_addr = pci_resource_start(pdev, DLB_FUNC_BAR);

	if (!dlb->hw.func_kva) {
		dev_err(&pdev->dev, "Cannot iomap BAR 0 (size %llu)\n",
			pci_resource_len(pdev, 0));

		return -EIO;
	}

	dlb->hw.csr_kva = pcim_iomap_table(pdev)[DLB_CSR_BAR];
	dlb->hw.csr_phys_addr = pci_resource_start(pdev, DLB_CSR_BAR);

	if (!dlb->hw.csr_kva) {
		dev_err(&pdev->dev, "Cannot iomap BAR 2 (size %llu)\n",
			pci_resource_len(pdev, 2));

		return -EIO;
	}

	return 0;
}

/*******************************/
/****** Driver management ******/
/*******************************/

int dlb_pf_init_driver_state(struct dlb *dlb)
{
	mutex_init(&dlb->resource_mutex);

	/*
	 * Allow PF runtime power-management (forbidden by default by the PCI
	 * layer during scan). The driver puts the device into D3hot while
	 * there are no scheduling domains to service.
	 */
	pm_runtime_allow(&dlb->pdev->dev);

	return 0;
}

void dlb_pf_enable_pm(struct dlb *dlb)
{
	/*
	 * Clear the power-management-disable register to power on the bulk of
	 * the device's hardware.
	 */
	dlb_clr_pmcsr_disable(&dlb->hw);
}

#define DLB_READY_RETRY_LIMIT 1000
int dlb_pf_wait_for_device_ready(struct dlb *dlb, struct pci_dev *pdev)
{
	u32 retries = DLB_READY_RETRY_LIMIT;

	/* Allow at least 1s for the device to become active after power-on */
	do {
		u32 idle, pm_st, addr;

		addr = CM_CFG_PM_STATUS;

		pm_st = DLB_CSR_RD(&dlb->hw, addr);

		addr = CM_CFG_DIAGNOSTIC_IDLE_STATUS;

		idle = DLB_CSR_RD(&dlb->hw, addr);

		if (FIELD_GET(CM_CFG_PM_STATUS_PMSM, pm_st) == 1 &&
		    FIELD_GET(CM_CFG_DIAGNOSTIC_IDLE_STATUS_DLB_FUNC_IDLE, idle)
		    == 1)
			break;

		usleep_range(1000, 2000);
	} while (--retries);

	if (!retries) {
		dev_err(&pdev->dev, "Device idle test failed\n");
		return -EIO;
	}

	return 0;
}

void dlb_pf_init_hardware(struct dlb *dlb)
{
	/* Use sparse mode as default */
	dlb_hw_enable_sparse_ldb_cq_mode(&dlb->hw);
	dlb_hw_enable_sparse_dir_cq_mode(&dlb->hw);
}

/*****************************/
/****** Sysfs callbacks ******/
/*****************************/

#define DLB_TOTAL_SYSFS_SHOW(name, macro)		\
static ssize_t total_##name##_show(			\
	struct device *dev,				\
	struct device_attribute *attr,			\
	char *buf)					\
{							\
	int val = DLB_MAX_NUM_##macro;			\
							\
	return scnprintf(buf, PAGE_SIZE, "%d\n", val);	\
}

DLB_TOTAL_SYSFS_SHOW(num_sched_domains, DOMAINS)
DLB_TOTAL_SYSFS_SHOW(num_ldb_queues, LDB_QUEUES)
DLB_TOTAL_SYSFS_SHOW(num_ldb_ports, LDB_PORTS)
DLB_TOTAL_SYSFS_SHOW(num_dir_ports, DIR_PORTS)
DLB_TOTAL_SYSFS_SHOW(num_ldb_credits, LDB_CREDITS)
DLB_TOTAL_SYSFS_SHOW(num_dir_credits, DIR_CREDITS)
DLB_TOTAL_SYSFS_SHOW(num_atomic_inflights, AQED_ENTRIES)
DLB_TOTAL_SYSFS_SHOW(num_hist_list_entries, HIST_LIST_ENTRIES)

#define DLB_AVAIL_SYSFS_SHOW(name)			     \
static ssize_t avail_##name##_show(			     \
	struct device *dev,				     \
	struct device_attribute *attr,			     \
	char *buf)					     \
{							     \
	struct dlb *dlb = dev_get_drvdata(dev);		     \
	struct dlb_get_num_resources_args arg;		     \
	struct dlb_hw *hw = &dlb->hw;			     \
	int val;					     \
							     \
	mutex_lock(&dlb->resource_mutex);		     \
							     \
	val = dlb_hw_get_num_resources(hw, &arg);	     \
							     \
	mutex_unlock(&dlb->resource_mutex);		     \
							     \
	if (val)					     \
		return -1;				     \
							     \
	val = arg.name;					     \
							     \
	return scnprintf(buf, PAGE_SIZE, "%d\n", val);	     \
}

DLB_AVAIL_SYSFS_SHOW(num_sched_domains)
DLB_AVAIL_SYSFS_SHOW(num_ldb_queues)
DLB_AVAIL_SYSFS_SHOW(num_ldb_ports)
DLB_AVAIL_SYSFS_SHOW(num_dir_ports)
DLB_AVAIL_SYSFS_SHOW(num_ldb_credits)
DLB_AVAIL_SYSFS_SHOW(num_dir_credits)
DLB_AVAIL_SYSFS_SHOW(num_atomic_inflights)
DLB_AVAIL_SYSFS_SHOW(num_hist_list_entries)

static ssize_t max_ctg_hl_entries_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct dlb *dlb = dev_get_drvdata(dev);
	struct dlb_get_num_resources_args arg;
	struct dlb_hw *hw = &dlb->hw;
	int val;

	mutex_lock(&dlb->resource_mutex);

	val = dlb_hw_get_num_resources(hw, &arg);

	mutex_unlock(&dlb->resource_mutex);

	if (val)
		return -1;

	val = arg.max_contiguous_hist_list_entries;

	return scnprintf(buf, PAGE_SIZE, "%d\n", val);
}

/*
 * Device attribute name doesn't match the show function name, so we define our
 * own DEVICE_ATTR macro.
 */
#define DLB_DEVICE_ATTR_RO(_prefix, _name) \
struct device_attribute dev_attr_##_prefix##_##_name = {\
	.attr = { .name = __stringify(_name), .mode = 0444 },\
	.show = _prefix##_##_name##_show,\
}

static DLB_DEVICE_ATTR_RO(total, num_sched_domains);
static DLB_DEVICE_ATTR_RO(total, num_ldb_queues);
static DLB_DEVICE_ATTR_RO(total, num_ldb_ports);
static DLB_DEVICE_ATTR_RO(total, num_dir_ports);
static DLB_DEVICE_ATTR_RO(total, num_ldb_credits);
static DLB_DEVICE_ATTR_RO(total, num_dir_credits);
static DLB_DEVICE_ATTR_RO(total, num_atomic_inflights);
static DLB_DEVICE_ATTR_RO(total, num_hist_list_entries);

static struct attribute *dlb_total_attrs[] = {
	&dev_attr_total_num_sched_domains.attr,
	&dev_attr_total_num_ldb_queues.attr,
	&dev_attr_total_num_ldb_ports.attr,
	&dev_attr_total_num_dir_ports.attr,
	&dev_attr_total_num_ldb_credits.attr,
	&dev_attr_total_num_dir_credits.attr,
	&dev_attr_total_num_atomic_inflights.attr,
	&dev_attr_total_num_hist_list_entries.attr,
	NULL
};

static const struct attribute_group dlb_total_attr_group = {
	.attrs = dlb_total_attrs,
	.name = "total_resources",
};

static DLB_DEVICE_ATTR_RO(avail, num_sched_domains);
static DLB_DEVICE_ATTR_RO(avail, num_ldb_queues);
static DLB_DEVICE_ATTR_RO(avail, num_ldb_ports);
static DLB_DEVICE_ATTR_RO(avail, num_dir_ports);
static DLB_DEVICE_ATTR_RO(avail, num_ldb_credits);
static DLB_DEVICE_ATTR_RO(avail, num_dir_credits);
static DLB_DEVICE_ATTR_RO(avail, num_atomic_inflights);
static DLB_DEVICE_ATTR_RO(avail, num_hist_list_entries);
static DEVICE_ATTR_RO(max_ctg_hl_entries);

static struct attribute *dlb_avail_attrs[] = {
	&dev_attr_avail_num_sched_domains.attr,
	&dev_attr_avail_num_ldb_queues.attr,
	&dev_attr_avail_num_ldb_ports.attr,
	&dev_attr_avail_num_dir_ports.attr,
	&dev_attr_avail_num_ldb_credits.attr,
	&dev_attr_avail_num_dir_credits.attr,
	&dev_attr_avail_num_atomic_inflights.attr,
	&dev_attr_avail_num_hist_list_entries.attr,
	&dev_attr_max_ctg_hl_entries.attr,
	NULL
};

static const struct attribute_group dlb_avail_attr_group = {
	.attrs = dlb_avail_attrs,
	.name = "avail_resources",
};

static ssize_t dev_id_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct dlb *dlb = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", dlb->id);
}

/* [7:0]: device revision, [15:8]: device version */
#define DLB_SET_DEVICE_VERSION(ver, rev) (((ver) << 8) | (rev))

static ssize_t dev_ver_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	int ver;

	ver = DLB_SET_DEVICE_VERSION(2, 0);

	return scnprintf(buf, PAGE_SIZE, "%d\n", ver);
}

static DEVICE_ATTR_RO(dev_id);
static DEVICE_ATTR_RO(dev_ver);

static struct attribute *dlb_dev_id_attr[] = {
	&dev_attr_dev_id.attr,
	&dev_attr_dev_ver.attr,
	NULL
};

static const struct attribute_group dlb_dev_id_attr_group = {
	.attrs = dlb_dev_id_attr,
};

static const struct attribute_group *dlb_pf_attr_groups[] = {
	&dlb_dev_id_attr_group,
	&dlb_total_attr_group,
	&dlb_avail_attr_group,
	NULL,
};

int dlb_pf_sysfs_create(struct dlb *dlb)
{
	struct device *dev = &dlb->pdev->dev;

	return devm_device_add_groups(dev, dlb_pf_attr_groups);
}
