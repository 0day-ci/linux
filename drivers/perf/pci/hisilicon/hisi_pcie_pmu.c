// SPDX-License-Identifier: GPL-2.0-only
/*
 * This driver adds support for PCIe PMU RCiEP device. Related
 * perf events are bandwidth, bandwidth utilization, latency
 * etc.
 *
 * Copyright (C) 2021 HiSilicon Limited
 * Author: Qi Liu<liuqi115@huawei.com>
 */
#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/bug.h>
#include <linux/cpuhotplug.h>
#include <linux/cpumask.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/perf_event.h>

/* Define registers */
#define HISI_PCIE_GLOBAL_CTRL		0x00
#define HISI_PCIE_EVENT_CTRL		0x010
#define HISI_PCIE_CNT			0x090
#define HISI_PCIE_EXT_CNT		0x110
#define HISI_PCIE_INT_STAT		0x150
#define HISI_PCIE_INT_MASK		0x154
#define HISI_PCIE_REG_BDF		0xfe0
#define HISI_PCIE_REG_VERSION		0xfe4
#define HISI_PCIE_REG_INFO		0xfe8
#define HISI_PCIE_REG_FREQ		0xfec

/* Define PCIE CTRL CMD */
#define HISI_PCIE_GLOBAL_EN		0x01
#define HISI_PCIE_GLOBAL_NONE		0
#define HISI_PCIE_EVENT_EN		BIT_ULL(20)
#define HISI_PCIE_RESET_CNT		BIT_ULL(22)
#define HISI_PCIE_DEFAULT_SET		BIT_ULL(34)
#define HISI_PCIE_THR_EN		BIT_ULL(26)
#define HISI_PCIE_TARGET_EN		BIT_ULL(32)
#define HISI_PCIE_TRIG_EN		BIT_ULL(52)

/* Define offsets in event ctrl regesiter */
#define HISI_PCIE_EVENT_M		GENMASK_ULL(7, 0)
#define HISI_PCIE_SUBEVENT_M		GENMASK_ULL(15, 8)
#define HISI_PCIE_THR_MODE_M		GENMASK_ULL(27, 27)
#define HISI_PCIE_THR_M			GENMASK_ULL(31, 28)
#define HISI_PCIE_TARGET_M		GENMASK_ULL(52, 36)
#define HISI_PCIE_TRIG_MODE_M		GENMASK_ULL(53, 53)
#define HISI_PCIE_TRIG_M		GENMASK_ULL(59, 56)

#define HISI_PCIE_MAX_COUNTERS		8
#define HISI_PCIE_REG_STEP		8
#define HISI_PCIE_EVENT_MAX		0xa2
#define HISI_PCIE_SUBEVENT_MAX		0x20
#define HISI_PCIE_THR_MAX_VAL		10
#define HISI_PCIE_TRIG_MAX_VAL		10
#define HISI_PCIE_COUNTER_BITS		64
#define HISI_PCIE_MAX_PERIOD		BIT_ULL(63)

struct hisi_pcie_pmu {
	struct perf_event *hw_events[HISI_PCIE_MAX_COUNTERS];
	struct hlist_node node;
	struct pci_dev *pdev;
	struct pmu pmu;
	void __iomem *base;
	int irq;
	u32 identifier;
	/* Minimum and maximum bdf of root ports monitored by PMU */
	u16 bdf_min;
	u16 bdf_max;
	int on_cpu;
};

#define to_pcie_pmu(p)  (container_of((p), struct hisi_pcie_pmu, pmu))
#define GET_PCI_DEVFN(bdf)  ((bdf) & 0xff)

#define HISI_PCIE_PMU_FILTER_ATTR(_name, _config, _hi, _lo)		  \
	static u64 hisi_pcie_get_##_name(struct perf_event *event)	  \
	{								  \
		return FIELD_GET(GENMASK(_hi, _lo), event->attr._config); \
	}								  \

HISI_PCIE_PMU_FILTER_ATTR(event, config, 7, 0);
HISI_PCIE_PMU_FILTER_ATTR(subevent, config, 15, 8);
HISI_PCIE_PMU_FILTER_ATTR(thr_len, config1, 3, 0);
HISI_PCIE_PMU_FILTER_ATTR(thr_mode, config1, 4, 4);
HISI_PCIE_PMU_FILTER_ATTR(trig_len, config1, 8, 5);
HISI_PCIE_PMU_FILTER_ATTR(trig_mode, config1, 9, 9);
HISI_PCIE_PMU_FILTER_ATTR(port, config2, 15, 0);
HISI_PCIE_PMU_FILTER_ATTR(bdf, config2, 31, 16);

#define HISI_PCIE_BUILD_EVENTS(name)					\
	static bool is_##name##_event(u32 idx)				\
	{								\
		return (idx >= name##_events_list[0] &&			\
			idx <= name##_events_list[1]) ||		\
			idx == name##_events_list[2];			\
	}								\

/*
 * The first element of event list is minimum index of TL-layer events
 * and the second element is maximum index. The third element is index
 * of a DL-layer event.
 */
static const u32 bw_events_list[] = {0x04, 0x08, 0x84};
static const u32 latency_events_list[] = {0x10, 0x15, 0x85};
static const u32 bus_util_events_list[] = {0x20, 0x24, 0x09};
static const u32 buf_util_events_list[] = {0x28, 0x2a, 0x33};

HISI_PCIE_BUILD_EVENTS(bw);
HISI_PCIE_BUILD_EVENTS(latency);
HISI_PCIE_BUILD_EVENTS(bus_util);
HISI_PCIE_BUILD_EVENTS(buf_util);

static ssize_t hisi_pcie_format_sysfs_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *eattr;

	eattr = container_of(attr, struct dev_ext_attribute, attr);

	return sysfs_emit(buf, "%s\n", (char *)eattr->var);
}

static ssize_t hisi_pcie_event_sysfs_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *eattr;

	eattr = container_of(attr, struct dev_ext_attribute, attr);

	return sysfs_emit(buf, "config=0x%lx\n", (unsigned long)eattr->var);
}

#define HISI_PCIE_PMU_ATTR(_name, _func, _config)			\
	(&((struct dev_ext_attribute[]) {				\
		{ __ATTR(_name, 0444, _func, NULL), (void *)_config }   \
	})[0].attr.attr)

#define HISI_PCIE_PMU_FORMAT_ATTR(_name, _format)			\
	HISI_PCIE_PMU_ATTR(_name, hisi_pcie_format_sysfs_show, (void *)_format)
#define HISI_PCIE_PMU_EVENT_ATTR(_name, _event)			\
	HISI_PCIE_PMU_ATTR(_name, hisi_pcie_event_sysfs_show, (void *)_event)

static ssize_t hisi_pcie_cpumask_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(dev_get_drvdata(dev));

	return sysfs_emit(buf, "%d\n", pcie_pmu->on_cpu);
}

static ssize_t hisi_pcie_identifier_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(dev_get_drvdata(dev));

	return sysfs_emit(buf, "0x%x\n", pcie_pmu->identifier);
}

static ssize_t hisi_pcie_bus_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(dev_get_drvdata(dev));

	return sysfs_emit(buf, "0x%02x\n", PCI_BUS_NUM(pcie_pmu->bdf_min));
}

static void hisi_pcie_parse_reg_value(struct hisi_pcie_pmu *pcie_pmu,
				      u32 reg_off, u16 *arg0, u16 *arg1)
{
	u32 val = readl(pcie_pmu->base + reg_off);

	*arg0 = val & 0xffff;
	*arg1 = (val & 0xffff0000) >> 16;
}

static u32 hisi_pcie_pmu_get_offset(u32 offset, u32 idx)
{
	return offset + HISI_PCIE_REG_STEP * idx;
}

static u32 hisi_pcie_pmu_readl(struct hisi_pcie_pmu *pcie_pmu, u32 reg_offset,
			       u32 idx)
{
	u32 offset = hisi_pcie_pmu_get_offset(reg_offset, idx);

	return readl(pcie_pmu->base + offset);
}

static void hisi_pcie_pmu_writel(struct hisi_pcie_pmu *pcie_pmu, u32 reg_offset,
				 u32 idx, u32 val)
{
	u32 offset = hisi_pcie_pmu_get_offset(reg_offset, idx);

	writel(val, pcie_pmu->base + offset);
}

static u64 hisi_pcie_pmu_readq(struct hisi_pcie_pmu *pcie_pmu, u32 reg_offset,
			       u32 idx)
{
	u32 offset = hisi_pcie_pmu_get_offset(reg_offset, idx);

	return readq(pcie_pmu->base + offset);
}

static void hisi_pcie_pmu_writeq(struct hisi_pcie_pmu *pcie_pmu, u32 reg_offset,
				 u32 idx, u64 val)
{
	u32 offset = hisi_pcie_pmu_get_offset(reg_offset, idx);

	writeq(val, pcie_pmu->base + offset);
}

static void hisi_pcie_pmu_config_filter(struct perf_event *event)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	u64 reg = HISI_PCIE_DEFAULT_SET;
	u64 port, trig_len, thr_len;
	u32 idx = hwc->idx;

	/* Config HISI_PCIE_EVENT_CTRL according to event and subevent. */
	reg |= FIELD_PREP(HISI_PCIE_EVENT_M, hisi_pcie_get_event(event)) |
	       FIELD_PREP(HISI_PCIE_SUBEVENT_M, hisi_pcie_get_subevent(event));

	/* Config HISI_PCIE_EVENT_CTRL according to ROOT PORT or EP device. */
	port = hisi_pcie_get_port(event);
	if (port)
		reg |= FIELD_PREP(HISI_PCIE_TARGET_M, port);
	else
		reg |= HISI_PCIE_TARGET_EN |
		       FIELD_PREP(HISI_PCIE_TARGET_M, hisi_pcie_get_bdf(event));

	/* Config HISI_PCIE_EVENT_CTRL according to trigger condition. */
	trig_len = hisi_pcie_get_trig_len(event);
	if (trig_len)
		reg |= FIELD_PREP(HISI_PCIE_TRIG_M, trig_len) |
		       FIELD_PREP(HISI_PCIE_TRIG_MODE_M,
		       hisi_pcie_get_trig_mode(event)) | HISI_PCIE_TRIG_EN;

	/* Config HISI_PCIE_EVENT_CTRL according to threshold condition. */
	thr_len = hisi_pcie_get_thr_len(event);
	if (thr_len)
		reg |= FIELD_PREP(HISI_PCIE_THR_M, thr_len) |
		       FIELD_PREP(HISI_PCIE_THR_MODE_M,
		       hisi_pcie_get_thr_mode(event)) | HISI_PCIE_THR_EN;

	hisi_pcie_pmu_writeq(pcie_pmu, HISI_PCIE_EVENT_CTRL, idx, reg);
}

static void hisi_pcie_pmu_clear_filter(struct perf_event *event)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	hisi_pcie_pmu_writeq(pcie_pmu, HISI_PCIE_EVENT_CTRL, hwc->idx,
			     HISI_PCIE_DEFAULT_SET);
}

static bool hisi_pcie_pmu_valid_port(struct hisi_pcie_pmu *pcie_pmu, u16 rp_bdf)
{
	return rp_bdf >= pcie_pmu->bdf_min && rp_bdf <= pcie_pmu->bdf_max;
}

static bool hisi_pcie_pmu_valid_requester_id(struct hisi_pcie_pmu *pcie_pmu,
					    u32 bdf)
{
	struct pci_dev *root_port, *pdev;

	pdev = pci_get_domain_bus_and_slot(pci_domain_nr(pcie_pmu->pdev->bus),
					   PCI_BUS_NUM(bdf),
					   GET_PCI_DEVFN(bdf));
	if (!pdev)
		return false;

	root_port = pcie_find_root_port(pdev);
	if (!root_port)
		return false;

	pci_dev_put(pdev);
	return hisi_pcie_pmu_valid_port(pcie_pmu, pci_dev_id(root_port));
}

static bool hisi_pcie_pmu_valid_filter(struct perf_event *event,
				       struct hisi_pcie_pmu *pcie_pmu)
{
	u32 subev_idx = hisi_pcie_get_subevent(event);
	u32 event_idx = hisi_pcie_get_event(event);
	u32 requester_id = hisi_pcie_get_bdf(event);

	if (subev_idx > HISI_PCIE_SUBEVENT_MAX ||
	    event_idx > HISI_PCIE_EVENT_MAX) {
		pci_err(pcie_pmu->pdev,
			"Max event index and max subevent index is: %d, %d.\n",
			HISI_PCIE_EVENT_MAX, HISI_PCIE_SUBEVENT_MAX);
		return false;
	}

	if (hisi_pcie_get_thr_len(event) > HISI_PCIE_THR_MAX_VAL)
		return false;

	if (hisi_pcie_get_trig_len(event) > HISI_PCIE_TRIG_MAX_VAL)
		return false;

	if (requester_id) {
		if (!hisi_pcie_pmu_valid_requester_id(pcie_pmu, requester_id))
			return false;
	}

	return true;
}

static bool hisi_pcie_pmu_validate_event_group(struct perf_event *event)
{
	struct perf_event *sibling, *leader = event->group_leader;
	int counters = 1;

	if (!is_software_event(leader)) {
		if (leader->pmu != event->pmu)
			return false;

		if (leader != event)
			counters++;
	}

	for_each_sibling_event(sibling, event->group_leader) {
		if (is_software_event(sibling))
			continue;

		if (sibling->pmu != event->pmu)
			return false;

		counters++;
	}

	return counters <= HISI_PCIE_MAX_COUNTERS;
}

static int hisi_pcie_pmu_event_init(struct perf_event *event)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(event->pmu);

	event->cpu = pcie_pmu->on_cpu;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* Sampling is not supported. */
	if (is_sampling_event(event) || event->attach_state & PERF_ATTACH_TASK)
		return -EOPNOTSUPP;

	if (!hisi_pcie_pmu_valid_filter(event, pcie_pmu)) {
		pci_err(pcie_pmu->pdev, "Invalid filter!\n");
		return -EINVAL;
	}

	if (!hisi_pcie_pmu_validate_event_group(event))
		return -EINVAL;

	return 0;
}

/*
 * The bandwidth, latency, bus utilization and buffer occupancy features are
 * calculated from data in HISI_PCIE_CNT and extended data in HISI_PCIE_EXT_CNT.
 * Other features are obtained only by HISI_PCIE_CNT.
 * So data and data_ext are processed in this function to get performanace
 * value like, bandwidth, latency, etc.
 */
static u64 hisi_pcie_pmu_get_performance(struct perf_event *event, u64 data,
					 u64 data_ext)
{
#define CONVERT_DW_TO_BYTE(x)	(sizeof(u32) * (x))
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(event->pmu);
	u64 us_per_cycle = readl(pcie_pmu->base + HISI_PCIE_REG_FREQ);
	u32 idx = hisi_pcie_get_event(event);

	if (!data_ext)
		return 0;

	/* Process data to set unit of bandwidth as "Byte/ms". */
	if (is_bw_event(idx)) {
		if (!data_ext / 1000)
			return 0;

		return (CONVERT_DW_TO_BYTE(data)) / (data_ext / 1000);
	}

	/* Process data to set unit of latency as "us". */
	if (is_latency_event(idx))
		return (data * us_per_cycle) / data_ext;

	if (is_bus_util_event(idx))
		return (data * us_per_cycle) / data_ext;

	if (is_buf_util_event(idx))
		return data / (data_ext * us_per_cycle);

	return data;
}

static void hisi_pcie_pmu_read_counter(struct perf_event *event, u64 *cnt,
				       u64 *cnt_ext)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(event->pmu);
	u32 idx = event->hw.idx;

	*cnt = hisi_pcie_pmu_readq(pcie_pmu, HISI_PCIE_CNT, idx);
	*cnt_ext = hisi_pcie_pmu_readq(pcie_pmu, HISI_PCIE_EXT_CNT, idx);
}

static void hisi_pcie_pmu_write_counter(struct perf_event *event, u64 val,
					u64 val_ext)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(event->pmu);
	u32 idx = event->hw.idx;

	hisi_pcie_pmu_writeq(pcie_pmu, HISI_PCIE_CNT, idx, val);
	hisi_pcie_pmu_writeq(pcie_pmu, HISI_PCIE_EXT_CNT, idx, val_ext);
}

static int hisi_pcie_pmu_get_event_idx(struct hisi_pcie_pmu *pcie_pmu)
{
	int idx;

	for (idx = 0; idx < HISI_PCIE_MAX_COUNTERS; idx++) {
		if (!pcie_pmu->hw_events[idx])
			return idx;
	}

	return -EINVAL;
}

static void hisi_pcie_pmu_event_update(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *hwc_ext = &hwc->extra_reg;
	u64 new_cnt_ext, prev_cnt_ext;
	u64 new_cnt, prev_cnt, delta;

	hwc_ext = &hwc->extra_reg;
	do {
		prev_cnt = local64_read(&hwc->prev_count);
		prev_cnt_ext = hwc_ext->config;
		hisi_pcie_pmu_read_counter(event, &new_cnt, &new_cnt_ext);
	} while (local64_cmpxchg(&hwc->prev_count, prev_cnt,
				 new_cnt) != prev_cnt);

	hwc_ext->config = new_cnt_ext;

	delta = hisi_pcie_pmu_get_performance(event, new_cnt - prev_cnt,
					      new_cnt_ext - prev_cnt_ext);
	local64_add(delta, &event->count);
}

static void hisi_pcie_pmu_read(struct perf_event *event)
{
	hisi_pcie_pmu_event_update(event);
}

static void hisi_pcie_pmu_set_period(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *hwc_ext;
	u64 val = BIT_ULL(HISI_PCIE_COUNTER_BITS - 1);

	hwc_ext = &hwc->extra_reg;
	local64_set(&hwc->prev_count, val);
	hwc_ext->config = 0;
	hisi_pcie_pmu_write_counter(event, val, 0);
}

static void hisi_pcie_pmu_enable_counter(struct hisi_pcie_pmu *pcie_pmu,
					 struct hw_perf_event *hwc)
{
	u32 idx = hwc->idx;
	u64 val;

	val = hisi_pcie_pmu_readq(pcie_pmu, HISI_PCIE_EVENT_CTRL, idx);
	val |= HISI_PCIE_EVENT_EN;
	hisi_pcie_pmu_writeq(pcie_pmu, HISI_PCIE_EVENT_CTRL, idx, val);
}

static void hisi_pcie_pmu_disable_counter(struct hisi_pcie_pmu *pcie_pmu,
					  struct hw_perf_event *hwc)
{
	u32 idx = hwc->idx;
	u64 val;

	val = hisi_pcie_pmu_readq(pcie_pmu, HISI_PCIE_EVENT_CTRL, idx);
	val &= ~HISI_PCIE_EVENT_EN;
	hisi_pcie_pmu_writeq(pcie_pmu, HISI_PCIE_EVENT_CTRL, idx, val);
}

static void hisi_pcie_pmu_enable_int(struct hisi_pcie_pmu *pcie_pmu,
				     struct hw_perf_event *hwc)
{
	u32 idx = hwc->idx;

	hisi_pcie_pmu_writel(pcie_pmu, HISI_PCIE_INT_MASK, idx, 0);
}

static void hisi_pcie_pmu_disable_int(struct hisi_pcie_pmu *pcie_pmu,
				      struct hw_perf_event *hwc)
{
	u32 idx = hwc->idx;

	hisi_pcie_pmu_writel(pcie_pmu, HISI_PCIE_INT_MASK, idx, 1);
}

static void hisi_pcie_pmu_reset_counter(struct hisi_pcie_pmu *pcie_pmu,
					struct hw_perf_event *hwc)
{
	u32 idx = hwc->idx;

	hisi_pcie_pmu_writeq(pcie_pmu, HISI_PCIE_EVENT_CTRL, idx,
			     HISI_PCIE_RESET_CNT);
	hisi_pcie_pmu_writeq(pcie_pmu, HISI_PCIE_EVENT_CTRL, idx,
			     HISI_PCIE_DEFAULT_SET);
}

static void hisi_pcie_pmu_start(struct perf_event *event, int flags)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	struct hw_perf_event_extra *hwc_ext;
	u64 prev_cnt, prev_cnt_ext;

	hwc_ext = &hwc->extra_reg;
	if (WARN_ON_ONCE(!(hwc->state & PERF_HES_STOPPED)))
		return;

	WARN_ON_ONCE(!(hwc->state & PERF_HES_UPTODATE));
	hwc->state = 0;

	hisi_pcie_pmu_config_filter(event);
	hisi_pcie_pmu_enable_counter(pcie_pmu, hwc);
	hisi_pcie_pmu_enable_int(pcie_pmu, hwc);
	hisi_pcie_pmu_set_period(event);

	if (flags & PERF_EF_RELOAD) {
		prev_cnt = local64_read(&hwc->prev_count);
		prev_cnt_ext = hwc_ext->config;
		hisi_pcie_pmu_write_counter(event, prev_cnt, prev_cnt_ext);
	}

	perf_event_update_userpage(event);
}

static void hisi_pcie_pmu_stop(struct perf_event *event, int flags)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	hisi_pcie_pmu_event_update(event);
	hisi_pcie_pmu_disable_int(pcie_pmu, hwc);
	hisi_pcie_pmu_disable_counter(pcie_pmu, hwc);
	hisi_pcie_pmu_clear_filter(event);
	WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
	hwc->state |= PERF_HES_STOPPED;

	if (hwc->state & PERF_HES_UPTODATE)
		return;

	hwc->state |= PERF_HES_UPTODATE;
}

static int hisi_pcie_pmu_add(struct perf_event *event, int flags)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int idx;

	hwc->state = PERF_HES_STOPPED | PERF_HES_UPTODATE;
	idx = hisi_pcie_pmu_get_event_idx(pcie_pmu);
	if (idx < 0)
		return idx;

	hwc->idx = idx;
	pcie_pmu->hw_events[idx] = event;

	/* Reset Counter to avoid interference caused by previous statistic. */
	hisi_pcie_pmu_reset_counter(pcie_pmu, hwc);

	if (flags & PERF_EF_START)
		hisi_pcie_pmu_start(event, PERF_EF_RELOAD);

	return 0;
}

static void hisi_pcie_pmu_del(struct perf_event *event, int flags)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	hisi_pcie_pmu_stop(event, PERF_EF_UPDATE);
	pcie_pmu->hw_events[hwc->idx] = NULL;
	perf_event_update_userpage(event);
}

static void hisi_pcie_pmu_enable(struct pmu *pmu)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(pmu);
	int num;

	for (num = 0; num < HISI_PCIE_MAX_COUNTERS; num++) {
		if (pcie_pmu->hw_events[num])
			break;
	}

	if (num == HISI_PCIE_MAX_COUNTERS)
		return;

	writel(HISI_PCIE_GLOBAL_EN, pcie_pmu->base + HISI_PCIE_GLOBAL_CTRL);
}

static void hisi_pcie_pmu_disable(struct pmu *pmu)
{
	struct hisi_pcie_pmu *pcie_pmu = to_pcie_pmu(pmu);

	writel(HISI_PCIE_GLOBAL_NONE, pcie_pmu->base + HISI_PCIE_GLOBAL_CTRL);
}

static irqreturn_t hisi_pcie_pmu_irq(int irq, void *data)
{
	struct hisi_pcie_pmu *pcie_pmu = data;
	irqreturn_t ret = IRQ_NONE;
	struct perf_event *event;
	u32 overflown;
	int idx;

	for (idx = 0; idx < HISI_PCIE_MAX_COUNTERS; idx++) {
		overflown = hisi_pcie_pmu_readl(pcie_pmu, HISI_PCIE_INT_STAT,
						idx);
		if (!overflown)
			continue;

		/* Clear status of interrupt. */
		hisi_pcie_pmu_writel(pcie_pmu, HISI_PCIE_INT_STAT, idx, 1);
		event = pcie_pmu->hw_events[idx];
		if (!event)
			continue;

		hisi_pcie_pmu_event_update(event);
		hisi_pcie_pmu_set_period(event);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static int hisi_pcie_pmu_irq_register(struct pci_dev *pdev,
				      struct hisi_pcie_pmu *pcie_pmu)
{
	int irq, ret;

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
	if (ret < 0) {
		pci_err(pdev, "Failed to enable MSI vectors, ret = %d!\n", ret);
		return ret;
	}

	irq = pci_irq_vector(pdev, 0);
	ret = request_irq(irq, hisi_pcie_pmu_irq,
			  IRQF_NOBALANCING | IRQF_NO_THREAD, "hisi_pcie_pmu",
			  pcie_pmu);
	if (ret) {
		pci_err(pdev, "Failed to register irq, ret = %d!\n", ret);
		pci_free_irq_vectors(pdev);
		return ret;
	}

	pcie_pmu->irq = irq;

	return 0;
}

static void hisi_pcie_pmu_irq_unregister(struct pci_dev *pdev,
					 struct hisi_pcie_pmu *pcie_pmu)
{
	free_irq(pcie_pmu->irq, pcie_pmu);
	pci_free_irq_vectors(pdev);
}

static int hisi_pcie_pmu_online_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct hisi_pcie_pmu *pcie_pmu = hlist_entry_safe(node,
					 struct hisi_pcie_pmu, node);

	if (pcie_pmu->on_cpu == -1) {
		pcie_pmu->on_cpu = cpu;
		WARN_ON(irq_set_affinity(pcie_pmu->irq, cpumask_of(cpu)));
	}

	return 0;
}

static int hisi_pcie_pmu_offline_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct hisi_pcie_pmu *pcie_pmu = hlist_entry_safe(node,
					 struct hisi_pcie_pmu, node);
	unsigned int target;

	/* Nothing to do if this CPU doesn't own the PMU */
	if (pcie_pmu->on_cpu != cpu)
		return 0;

	/* Choose a new CPU from all online cpus. */
	target = cpumask_first(cpu_online_mask);
	if (target >= nr_cpu_ids) {
		pci_err(pcie_pmu->pdev, "There is no cpu to set!\n");
		return 0;
	}

	perf_pmu_migrate_context(&pcie_pmu->pmu, cpu, target);
	/* Use this CPU for event counting */
	pcie_pmu->on_cpu = target;
	WARN_ON(irq_set_affinity(pcie_pmu->irq, cpumask_of(target)));

	return 0;
}

/*
 * Events with the "dl" suffix in their names count performance in DL layer,
 * otherswise, events count performance in TL layer.
 */
static struct attribute *hisi_pcie_pmu_events_attr[] = {
	HISI_PCIE_PMU_EVENT_ATTR(bw_rx_mwr, 0x0104),
	HISI_PCIE_PMU_EVENT_ATTR(bw_rx_mrd, 0x1005),
	HISI_PCIE_PMU_EVENT_ATTR(bw_tx_mwr, 0x0105),
	HISI_PCIE_PMU_EVENT_ATTR(bw_tx_mrd, 0x2004),
	HISI_PCIE_PMU_EVENT_ATTR(lat_rx_mwr, 0x0010),
	HISI_PCIE_PMU_EVENT_ATTR(lat_rx_mrd, 0x0210),
	HISI_PCIE_PMU_EVENT_ATTR(lat_tx_mrd, 0x0011),
	HISI_PCIE_PMU_EVENT_ATTR(lat_tx_cfg, 0x0111),
	HISI_PCIE_PMU_EVENT_ATTR(bw_rx_dl, 0x0184),
	HISI_PCIE_PMU_EVENT_ATTR(bw_tx_dl, 0x0384),
	NULL
};

static struct attribute_group hisi_pcie_pmu_events_group = {
	.name = "events",
	.attrs = hisi_pcie_pmu_events_attr,
};

static struct attribute *hisi_pcie_pmu_format_attr[] = {
	HISI_PCIE_PMU_FORMAT_ATTR(event, "config:0-7"),
	HISI_PCIE_PMU_FORMAT_ATTR(subevent, "config:8-15"),
	HISI_PCIE_PMU_FORMAT_ATTR(thr_len, "config1:0-3"),
	HISI_PCIE_PMU_FORMAT_ATTR(thr_mode, "config1:4"),
	HISI_PCIE_PMU_FORMAT_ATTR(trig_len, "config1:5-8"),
	HISI_PCIE_PMU_FORMAT_ATTR(trig_mode, "config1:9"),
	HISI_PCIE_PMU_FORMAT_ATTR(port, "config2:0-15"),
	HISI_PCIE_PMU_FORMAT_ATTR(bdf, "config2:16-31"),
	NULL
};

static struct attribute_group hisi_pcie_pmu_format_group = {
	.name = "format",
	.attrs = hisi_pcie_pmu_format_attr,
};

static struct device_attribute hisi_pcie_pmu_bus_attr =
	__ATTR(bus, 0444, hisi_pcie_bus_show, NULL);

static struct attribute *hisi_pcie_pmu_bus_attrs[] = {
	&hisi_pcie_pmu_bus_attr.attr,
	NULL
};

static struct attribute_group hisi_pcie_pmu_bus_attr_group = {
	.attrs = hisi_pcie_pmu_bus_attrs,
};

static struct device_attribute hisi_pcie_pmu_cpumask_attr =
	__ATTR(cpumask, 0444, hisi_pcie_cpumask_show, NULL);

static struct attribute *hisi_pcie_pmu_cpumask_attrs[] = {
	&hisi_pcie_pmu_cpumask_attr.attr,
	NULL
};

static struct attribute_group hisi_pcie_pmu_cpumask_attr_group = {
	.attrs = hisi_pcie_pmu_cpumask_attrs,
};

static struct device_attribute hisi_pcie_pmu_identifier_attr =
	__ATTR(identifier, 0444, hisi_pcie_identifier_show, NULL);

static struct attribute *hisi_pcie_pmu_identifier_attrs[] = {
	&hisi_pcie_pmu_identifier_attr.attr,
	NULL
};

static struct attribute_group hisi_pcie_pmu_identifier_attr_group = {
	.attrs = hisi_pcie_pmu_identifier_attrs,
};

static const struct attribute_group *hisi_pcie_pmu_attr_groups[] = {
	&hisi_pcie_pmu_events_group,
	&hisi_pcie_pmu_format_group,
	&hisi_pcie_pmu_bus_attr_group,
	&hisi_pcie_pmu_cpumask_attr_group,
	&hisi_pcie_pmu_identifier_attr_group,
	NULL
};

static int hisi_pcie_alloc_pmu(struct pci_dev *pdev,
			       struct hisi_pcie_pmu *pcie_pmu)
{
	u16 sicl_id, device_id;
	char *name;

	hisi_pcie_parse_reg_value(pcie_pmu, HISI_PCIE_REG_BDF,
				  &pcie_pmu->bdf_min, &pcie_pmu->bdf_max);
	hisi_pcie_parse_reg_value(pcie_pmu, HISI_PCIE_REG_INFO, &device_id,
				  &sicl_id);
	name = devm_kasprintf(&pdev->dev, GFP_KERNEL,
			      "hisi_pcie%u_%u", sicl_id, device_id);
	if (!name)
		return -ENOMEM;

	pcie_pmu->pdev = pdev;
	pcie_pmu->on_cpu = -1;
	pcie_pmu->identifier = readl(pcie_pmu->base + HISI_PCIE_REG_VERSION);
	pcie_pmu->pmu = (struct pmu) {
		.name		= name,
		.module		= THIS_MODULE,
		.event_init	= hisi_pcie_pmu_event_init,
		.pmu_enable	= hisi_pcie_pmu_enable,
		.pmu_disable	= hisi_pcie_pmu_disable,
		.add		= hisi_pcie_pmu_add,
		.del		= hisi_pcie_pmu_del,
		.start		= hisi_pcie_pmu_start,
		.stop		= hisi_pcie_pmu_stop,
		.read		= hisi_pcie_pmu_read,
		.task_ctx_nr	= perf_invalid_context,
		.attr_groups	= hisi_pcie_pmu_attr_groups,
		.capabilities	= PERF_PMU_CAP_NO_EXCLUDE,
	};

	return 0;
}

static int hisi_pcie_init_pmu(struct pci_dev *pdev,
			      struct hisi_pcie_pmu *pcie_pmu)
{
	int ret;

	pcie_pmu->base = pci_ioremap_bar(pdev, 2);
	if (!pcie_pmu->base) {
		pci_err(pdev, "Ioremap failed for pcie_pmu resource.\n");
		return -ENOMEM;
	}

	ret = hisi_pcie_alloc_pmu(pdev, pcie_pmu);
	if (ret)
		return ret;

	ret = hisi_pcie_pmu_irq_register(pdev, pcie_pmu);
	if (ret)
		goto err_set_pmu_fail;

	ret = cpuhp_state_add_instance(CPUHP_AP_PERF_ARM_HISI_PCIE_PMU_ONLINE,
				       &pcie_pmu->node);
	if (ret) {
		pci_err(pdev, "Failed to register hotplug, ret = %d.\n", ret);
		goto err_irq_unregister;
	}

	ret = perf_pmu_register(&pcie_pmu->pmu, pcie_pmu->pmu.name, -1);
	if (ret) {
		pci_err(pdev, "Failed to register PCIe PMU, ret = %d.\n", ret);
		goto err_hotplug_unregister;
	}

	return ret;

err_hotplug_unregister:
	cpuhp_state_remove_instance_nocalls(
		CPUHP_AP_PERF_ARM_HISI_PCIE_PMU_ONLINE, &pcie_pmu->node);

err_irq_unregister:
	hisi_pcie_pmu_irq_unregister(pdev, pcie_pmu);

err_set_pmu_fail:
	iounmap(pcie_pmu->base);

	return ret;
}

static void hisi_pcie_uninit_pmu(struct pci_dev *pdev)
{
	struct hisi_pcie_pmu *pcie_pmu = pci_get_drvdata(pdev);

	perf_pmu_unregister(&pcie_pmu->pmu);
	cpuhp_state_remove_instance_nocalls(
		CPUHP_AP_PERF_ARM_HISI_PCIE_PMU_ONLINE, &pcie_pmu->node);
	hisi_pcie_pmu_irq_unregister(pdev, pcie_pmu);
	iounmap(pcie_pmu->base);
}

static int hisi_pcie_init_dev(struct pci_dev *pdev)
{
	int ret;

	ret = pci_enable_device(pdev);
	if (ret) {
		pci_err(pdev, "Failed to enable pci device, ret = %d.\n", ret);
		return ret;
	}

	ret = pci_request_mem_regions(pdev, "hisi_pcie_pmu");
	if (ret < 0) {
		pci_err(pdev, "Failed to request pci mem regions, ret = %d.\n",
			ret);
		pci_disable_device(pdev);
		return ret;
	}

	pci_set_master(pdev);

	return 0;
}

static void hisi_pcie_uninit_dev(struct pci_dev *pdev)
{
	pci_clear_master(pdev);
	pci_release_mem_regions(pdev);
	pci_disable_device(pdev);
}

static int hisi_pcie_pmu_probe(struct pci_dev *pdev,
			       const struct pci_device_id *id)
{
	struct hisi_pcie_pmu *pcie_pmu;
	int ret;

	pcie_pmu = devm_kzalloc(&pdev->dev, sizeof(*pcie_pmu), GFP_KERNEL);
	if (!pcie_pmu)
		return -ENOMEM;

	ret = hisi_pcie_init_dev(pdev);
	if (ret)
		return ret;

	ret = hisi_pcie_init_pmu(pdev, pcie_pmu);
	if (ret)
		hisi_pcie_uninit_dev(pdev);

	pci_set_drvdata(pdev, pcie_pmu);

	return ret;
}

static void hisi_pcie_pmu_remove(struct pci_dev *pdev)
{
	hisi_pcie_uninit_pmu(pdev);
	hisi_pcie_uninit_dev(pdev);
	pci_set_drvdata(pdev, NULL);
}

static const struct pci_device_id hisi_pcie_pmu_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, 0xa12d) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, hisi_pcie_pmu_ids);

static struct pci_driver hisi_pcie_pmu_driver = {
	.name = "hisi_pcie_pmu",
	.id_table = hisi_pcie_pmu_ids,
	.probe = hisi_pcie_pmu_probe,
	.remove = hisi_pcie_pmu_remove,
};

static int __init hisi_pcie_module_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_PERF_ARM_HISI_PCIE_PMU_ONLINE,
				      "AP_PERF_ARM_HISI_PCIE_PMU_ONLINE",
				      hisi_pcie_pmu_online_cpu,
				      hisi_pcie_pmu_offline_cpu);
	if (ret) {
		pr_err("Failed to setup PCIE PMU hotplug, ret = %d.\n", ret);
		return ret;
	}

	ret = pci_register_driver(&hisi_pcie_pmu_driver);
	if (ret)
		cpuhp_remove_multi_state(
				CPUHP_AP_PERF_ARM_HISI_PCIE_PMU_ONLINE);

	return ret;
}
module_init(hisi_pcie_module_init);

static void __exit hisi_pcie_module_exit(void)
{
	pci_unregister_driver(&hisi_pcie_pmu_driver);
	cpuhp_remove_multi_state(CPUHP_AP_PERF_ARM_HISI_PCIE_PMU_ONLINE);
}
module_exit(hisi_pcie_module_exit);

MODULE_DESCRIPTION("HiSilicon PCIe PMU driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Qi Liu <liuqi115@huawei.com>");
