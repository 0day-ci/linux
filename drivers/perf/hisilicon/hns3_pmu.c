// SPDX-License-Identifier: GPL-2.0-only
/*
 * This driver adds support for HNS3 PMU iEP device. Related perf events are
 * bandwidth, latency, packet rate, interrupt rate etc.
 *
 * Copyright (C) 2022 HiSilicon Limited
 */
#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/bug.h>
#include <linux/cpuhotplug.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/perf_event.h>
#include <linux/smp.h>

/* registers offset address */
#define HNS3_PMU_REG_GLOBAL_CTRL		0x0000
#define HNS3_PMU_REG_CLOCK_FREQ			0x0020
#define HNS3_PMU_REG_BDF			0x0FE0
#define HNS3_PMU_REG_VERSION			0x0FE4
#define HNS3_PMU_REG_DEVICE_ID			0x0FE8

#define HNS3_PMU_REG_EVENT_OFFSET		0x1000
#define HNS3_PMU_REG_EVENT_SIZE			0x1000
#define HNS3_PMU_REG_EVENT_CTRL_LOW		0x00
#define HNS3_PMU_REG_EVENT_CTRL_HIGH		0x04
#define HNS3_PMU_REG_EVENT_INTR_STATUS		0x08
#define HNS3_PMU_REG_EVENT_INTR_MASK		0x0C
#define HNS3_PMU_REG_EVENT_COUNTER		0x10
#define HNS3_PMU_REG_EVENT_EXT_COUNTER		0x18
#define HNS3_PMU_REG_EVENT_QID_CTRL		0x28
#define HNS3_PMU_REG_EVENT_QID_PARA		0x2C

#define HNS3_PMU_FILTER_SUPPORT_GLOBAL		BIT(0)
#define HNS3_PMU_FILTER_SUPPORT_PORT		BIT(1)
#define HNS3_PMU_FILTER_SUPPORT_PORT_TC		BIT(2)
#define HNS3_PMU_FILTER_SUPPORT_FUNC		BIT(3)
#define HNS3_PMU_FILTER_SUPPORT_FUNC_QUEUE	BIT(4)
#define HNS3_PMU_FILTER_SUPPORT_FUNC_INTR	BIT(5)

#define HNS3_PMU_FILTER_ALL_TC			0xF
#define HNS3_PMU_FILTER_ALL_QUEUE		0xFFFF

#define HNS3_PMU_CTRL_SUBEVENT_S		4
#define HNS3_PMU_CTRL_FILTER_MODE_S		24

#define HNS3_PMU_GLOBAL_START			BIT(0)

#define HNS3_PMU_EVENT_STATUS_RESET		BIT(11)
#define HNS3_PMU_EVENT_EN			BIT(12)
#define HNS3_PMU_EVENT_OVERFLOW_RESTART		BIT(15)

#define HNS3_PMU_QID_PARA_FUNC_S		0
#define HNS3_PMU_QID_PARA_QUEUE_S		16

#define HNS3_PMU_QID_CTRL_REQ_ENABLE		BIT(0)
#define HNS3_PMU_QID_CTRL_DONE			BIT(1)
#define HNS3_PMU_QID_CTRL_MISS			BIT(2)

#define HNS3_PMU_INTR_MASK_OVERFLOW		BIT(1)

#define HNS3_PMU_MAX_HW_EVENTS			8

/*
 * Each hardware event contains two registers(counter and ext_counter) for
 * bandwidth, packet rate, latency and interrupt rate. These two registers will
 * be triggered to run at the same when a hardware event is enabled.
 *
 * Performance of each hardware event is calculated by: counter / ext_counter.
 *
 * As process of performance data is not recommended put in driver, we expose
 * ext_counter as a separate event for userspace and use bit 16 to indicate it.
 * For example, event 0x00001 and 0x10001 are actually one event for hardware
 * because bit 0-15 are same. If the bit 16 of one event is 0 means to get
 * counter, otherwise means to get ext_counter.
 */
/* bandwidth events */
#define HNS3_PMU_EVT_BW_SSU_EGU_BYTE_NUM		0x00001
#define HNS3_PMU_EVT_BW_SSU_EGU_TIME			0x10001
#define HNS3_PMU_EVT_BW_SSU_RPU_BYTE_NUM		0x00002
#define HNS3_PMU_EVT_BW_SSU_RPU_TIME			0x10002
#define HNS3_PMU_EVT_BW_SSU_ROCE_BYTE_NUM		0x00003
#define HNS3_PMU_EVT_BW_SSU_ROCE_TIME			0x10003
#define HNS3_PMU_EVT_BW_ROCE_SSU_BYTE_NUM		0x00004
#define HNS3_PMU_EVT_BW_ROCE_SSU_TIME			0x10004
#define HNS3_PMU_EVT_BW_TPU_SSU_BYTE_NUM		0x00005
#define HNS3_PMU_EVT_BW_TPU_SSU_TIME			0x10005
#define HNS3_PMU_EVT_BW_RPU_RCBRX_BYTE_NUM		0x00006
#define HNS3_PMU_EVT_BW_RPU_RCBRX_TIME			0x10006
#define HNS3_PMU_EVT_BW_RCBTX_TXSCH_BYTE_NUM		0x00008
#define HNS3_PMU_EVT_BW_RCBTX_TXSCH_TIME		0x10008
#define HNS3_PMU_EVT_BW_WR_FBD_BYTE_NUM			0x00009
#define HNS3_PMU_EVT_BW_WR_FBD_TIME			0x10009
#define HNS3_PMU_EVT_BW_WR_EBD_BYTE_NUM			0x0000A
#define HNS3_PMU_EVT_BW_WR_EBD_TIME			0x1000A
#define HNS3_PMU_EVT_BW_RD_FBD_BYTE_NUM			0x0000B
#define HNS3_PMU_EVT_BW_RD_FBD_TIME			0x1000B
#define HNS3_PMU_EVT_BW_RD_EBD_BYTE_NUM			0x0000C
#define HNS3_PMU_EVT_BW_RD_EBD_TIME			0x1000C
#define HNS3_PMU_EVT_BW_RD_PAY_M0_BYTE_NUM		0x0000D
#define HNS3_PMU_EVT_BW_RD_PAY_M0_TIME			0x1000D
#define HNS3_PMU_EVT_BW_RD_PAY_M1_BYTE_NUM		0x0000E
#define HNS3_PMU_EVT_BW_RD_PAY_M1_TIME			0x1000E
#define HNS3_PMU_EVT_BW_WR_PAY_M0_BYTE_NUM		0x0000F
#define HNS3_PMU_EVT_BW_WR_PAY_M0_TIME			0x1000F
#define HNS3_PMU_EVT_BW_WR_PAY_M1_BYTE_NUM		0x00010
#define HNS3_PMU_EVT_BW_WR_PAY_M1_TIME			0x10010

/* packet rate events */
#define HNS3_PMU_EVT_PPS_IGU_SSU_PACKET_NUM		0x00100
#define HNS3_PMU_EVT_PPS_IGU_SSU_TIME			0x10100
#define HNS3_PMU_EVT_PPS_SSU_EGU_PACKET_NUM		0x00101
#define HNS3_PMU_EVT_PPS_SSU_EGU_TIME			0x10101
#define HNS3_PMU_EVT_PPS_SSU_RPU_PACKET_NUM		0x00102
#define HNS3_PMU_EVT_PPS_SSU_RPU_TIME			0x10102
#define HNS3_PMU_EVT_PPS_SSU_ROCE_PACKET_NUM		0x00103
#define HNS3_PMU_EVT_PPS_SSU_ROCE_TIME			0x10103
#define HNS3_PMU_EVT_PPS_ROCE_SSU_PACKET_NUM		0x00104
#define HNS3_PMU_EVT_PPS_ROCE_SSU_TIME			0x10104
#define HNS3_PMU_EVT_PPS_TPU_SSU_PACKET_NUM		0x00105
#define HNS3_PMU_EVT_PPS_TPU_SSU_TIME			0x10105
#define HNS3_PMU_EVT_PPS_RPU_RCBRX_PACKET_NUM		0x00106
#define HNS3_PMU_EVT_PPS_RPU_RCBRX_TIME			0x10106
#define HNS3_PMU_EVT_PPS_RCBTX_TPU_PACKET_NUM		0x00107
#define HNS3_PMU_EVT_PPS_RCBTX_TPU_TIME			0x10107
#define HNS3_PMU_EVT_PPS_RCBTX_TXSCH_PACKET_NUM		0x00108
#define HNS3_PMU_EVT_PPS_RCBTX_TXSCH_TIME		0x10108
#define HNS3_PMU_EVT_PPS_WR_FBD_PACKET_NUM		0x00109
#define HNS3_PMU_EVT_PPS_WR_FBD_TIME			0x10109
#define HNS3_PMU_EVT_PPS_WR_EBD_PACKET_NUM		0x0010A
#define HNS3_PMU_EVT_PPS_WR_EBD_TIME			0x1010A
#define HNS3_PMU_EVT_PPS_RD_FBD_PACKET_NUM		0x0010B
#define HNS3_PMU_EVT_PPS_RD_FBD_TIME			0x1010B
#define HNS3_PMU_EVT_PPS_RD_EBD_PACKET_NUM		0x0010C
#define HNS3_PMU_EVT_PPS_RD_EBD_TIME			0x1010C
#define HNS3_PMU_EVT_PPS_RD_PAY_M0_PACKET_NUM		0x0010D
#define HNS3_PMU_EVT_PPS_RD_PAY_M0_TIME			0x1010D
#define HNS3_PMU_EVT_PPS_RD_PAY_M1_PACKET_NUM		0x0010E
#define HNS3_PMU_EVT_PPS_RD_PAY_M1_TIME			0x1010E
#define HNS3_PMU_EVT_PPS_WR_PAY_M0_PACKET_NUM		0x0010F
#define HNS3_PMU_EVT_PPS_WR_PAY_M0_TIME			0x1010F
#define HNS3_PMU_EVT_PPS_WR_PAY_M1_PACKET_NUM		0x00110
#define HNS3_PMU_EVT_PPS_WR_PAY_M1_TIME			0x10110
#define HNS3_PMU_EVT_PPS_NICROH_TX_PRE_PACKET_NUM	0x00111
#define HNS3_PMU_EVT_PPS_NICROH_TX_PRE_TIME		0x10111
#define HNS3_PMU_EVT_PPS_NICROH_RX_PRE_PACKET_NUM	0x00112
#define HNS3_PMU_EVT_PPS_NICROH_RX_PRE_TIME		0x10112

/* latency events */
#define HNS3_PMU_EVT_DLY_TX_PUSH_TIME			0x00202
#define HNS3_PMU_EVT_DLY_TX_PUSH_PACKET_NUM		0x10202
#define HNS3_PMU_EVT_DLY_TX_TIME			0x00204
#define HNS3_PMU_EVT_DLY_TX_PACKET_NUM			0x10204
#define HNS3_PMU_EVT_DLY_SSU_TX_NIC_TIME		0x00206
#define HNS3_PMU_EVT_DLY_SSU_TX_NIC_PACKET_NUM		0x10206
#define HNS3_PMU_EVT_DLY_SSU_TX_ROCE_TIME		0x00207
#define HNS3_PMU_EVT_DLY_SSU_TX_ROCE_PACKET_NUM		0x10207
#define HNS3_PMU_EVT_DLY_SSU_RX_NIC_TIME		0x00208
#define HNS3_PMU_EVT_DLY_SSU_RX_NIC_PACKET_NUM		0x10208
#define HNS3_PMU_EVT_DLY_SSU_RX_ROCE_TIME		0x00209
#define HNS3_PMU_EVT_DLY_SSU_RX_ROCE_PACKET_NUM		0x10209
#define HNS3_PMU_EVT_DLY_RPU_TIME			0x0020E
#define HNS3_PMU_EVT_DLY_RPU_PACKET_NUM			0x1020E
#define HNS3_PMU_EVT_DLY_TPU_TIME			0x0020F
#define HNS3_PMU_EVT_DLY_TPU_PACKET_NUM			0x1020F
#define HNS3_PMU_EVT_DLY_RPE_TIME			0x00210
#define HNS3_PMU_EVT_DLY_RPE_PACKET_NUM			0x10210
#define HNS3_PMU_EVT_DLY_TPE_TIME			0x00211
#define HNS3_PMU_EVT_DLY_TPE_PACKET_NUM			0x10211
#define HNS3_PMU_EVT_DLY_TPE_PUSH_TIME			0x00212
#define HNS3_PMU_EVT_DLY_TPE_PUSH_PACKET_NUM		0x10212
#define HNS3_PMU_EVT_DLY_WR_FBD_TIME			0x00213
#define HNS3_PMU_EVT_DLY_WR_FBD_PACKET_NUM		0x10213
#define HNS3_PMU_EVT_DLY_WR_EBD_TIME			0x00214
#define HNS3_PMU_EVT_DLY_WR_EBD_PACKET_NUM		0x10214
#define HNS3_PMU_EVT_DLY_RD_FBD_TIME			0x00215
#define HNS3_PMU_EVT_DLY_RD_FBD_PACKET_NUM		0x10215
#define HNS3_PMU_EVT_DLY_RD_EBD_TIME			0x00216
#define HNS3_PMU_EVT_DLY_RD_EBD_PACKET_NUM		0x10216
#define HNS3_PMU_EVT_DLY_RD_PAY_M0_TIME			0x00217
#define HNS3_PMU_EVT_DLY_RD_PAY_M0_PACKET_NUM		0x10217
#define HNS3_PMU_EVT_DLY_RD_PAY_M1_TIME			0x00218
#define HNS3_PMU_EVT_DLY_RD_PAY_M1_PACKET_NUM		0x10218
#define HNS3_PMU_EVT_DLY_WR_PAY_M0_TIME			0x00219
#define HNS3_PMU_EVT_DLY_WR_PAY_M0_PACKET_NUM		0x10219
#define HNS3_PMU_EVT_DLY_WR_PAY_M1_TIME			0x0021A
#define HNS3_PMU_EVT_DLY_WR_PAY_M1_PACKET_NUM		0x1021A
#define HNS3_PMU_EVT_DLY_MSIX_WRITE_TIME		0x0021C
#define HNS3_PMU_EVT_DLY_MSIX_WRITE_PACKET_NUM		0x1021C

/* interrupt rate events */
#define HNS3_PMU_EVT_PPS_MSIX_NIC_INTR_NUM		0x00300
#define HNS3_PMU_EVT_PPS_MSIX_NIC_TIME			0x10300

/* filter mode supported by each bandwidth event */
#define HNS3_PMU_FILTER_BW_SSU_EGU		0x07
#define HNS3_PMU_FILTER_BW_SSU_RPU		0x1F
#define HNS3_PMU_FILTER_BW_SSU_ROCE		0x0F
#define HNS3_PMU_FILTER_BW_ROCE_SSU		0x0F
#define HNS3_PMU_FILTER_BW_TPU_SSU		0x1F
#define HNS3_PMU_FILTER_BW_RPU_RCBRX		0x11
#define HNS3_PMU_FILTER_BW_RCBTX_TXSCH		0x11
#define HNS3_PMU_FILTER_BW_WR_FBD		0x1B
#define HNS3_PMU_FILTER_BW_WR_EBD		0x11
#define HNS3_PMU_FILTER_BW_RD_FBD		0x01
#define HNS3_PMU_FILTER_BW_RD_EBD		0x1B
#define HNS3_PMU_FILTER_BW_RD_PAY_M0		0x01
#define HNS3_PMU_FILTER_BW_RD_PAY_M1		0x01
#define HNS3_PMU_FILTER_BW_WR_PAY_M0		0x01
#define HNS3_PMU_FILTER_BW_WR_PAY_M1		0x01

/* filter mode supported by each packet rate event */
#define HNS3_PMU_FILTER_PPS_IGU_SSU		0x07
#define HNS3_PMU_FILTER_PPS_SSU_EGU		0x07
#define HNS3_PMU_FILTER_PPS_SSU_RPU		0x1F
#define HNS3_PMU_FILTER_PPS_SSU_ROCE		0x0F
#define HNS3_PMU_FILTER_PPS_ROCE_SSU		0x0F
#define HNS3_PMU_FILTER_PPS_TPU_SSU		0x1F
#define HNS3_PMU_FILTER_PPS_RPU_RCBRX		0x11
#define HNS3_PMU_FILTER_PPS_RCBTX_TPU		0x1F
#define HNS3_PMU_FILTER_PPS_RCBTX_TXSCH		0x11
#define HNS3_PMU_FILTER_PPS_WR_FBD		0x1B
#define HNS3_PMU_FILTER_PPS_WR_EBD		0x11
#define HNS3_PMU_FILTER_PPS_RD_FBD		0x01
#define HNS3_PMU_FILTER_PPS_RD_EBD		0x1B
#define HNS3_PMU_FILTER_PPS_RD_PAY_M0		0x01
#define HNS3_PMU_FILTER_PPS_RD_PAY_M1		0x01
#define HNS3_PMU_FILTER_PPS_WR_PAY_M0		0x01
#define HNS3_PMU_FILTER_PPS_WR_PAY_M1		0x01
#define HNS3_PMU_FILTER_PPS_NICROH_TX_PRE	0x01
#define HNS3_PMU_FILTER_PPS_NICROH_RX_PRE	0x01

/* filter mode supported by each latency event */
#define HNS3_PMU_FILTER_DLY_TX_PUSH		0x01
#define HNS3_PMU_FILTER_DLY_TX			0x01
#define HNS3_PMU_FILTER_DLY_SSU_TX_NIC		0x07
#define HNS3_PMU_FILTER_DLY_SSU_TX_ROCE		0x07
#define HNS3_PMU_FILTER_DLY_SSU_RX_NIC		0x07
#define HNS3_PMU_FILTER_DLY_SSU_RX_ROCE		0x07
#define HNS3_PMU_FILTER_DLY_RPU			0x11
#define HNS3_PMU_FILTER_DLY_TPU			0x1F
#define HNS3_PMU_FILTER_DLY_RPE			0x01
#define HNS3_PMU_FILTER_DLY_TPE			0x0B
#define HNS3_PMU_FILTER_DLY_TPE_PUSH		0x1B
#define HNS3_PMU_FILTER_DLY_WR_FBD		0x1B
#define HNS3_PMU_FILTER_DLY_WR_EBD		0x11
#define HNS3_PMU_FILTER_DLY_RD_FBD		0x01
#define HNS3_PMU_FILTER_DLY_RD_EBD		0x1B
#define HNS3_PMU_FILTER_DLY_RD_PAY_M0		0x01
#define HNS3_PMU_FILTER_DLY_RD_PAY_M1		0x01
#define HNS3_PMU_FILTER_DLY_WR_PAY_M0		0x01
#define HNS3_PMU_FILTER_DLY_WR_PAY_M1		0x01
#define HNS3_PMU_FILTER_DLY_MSIX_WRITE		0x01

/* filter mode supported by each interrupt rate event */
#define HNS3_PMU_FILTER_INTR_MSIX_NIC		0x01

enum hns3_pmu_hw_filter_mode {
	HNS3_PMU_HW_FILTER_GLOBAL,
	HNS3_PMU_HW_FILTER_PORT,
	HNS3_PMU_HW_FILTER_PORT_TC,
	HNS3_PMU_HW_FILTER_FUNC,
	HNS3_PMU_HW_FILTER_FUNC_QUEUE,
	HNS3_PMU_HW_FILTER_FUNC_INTR,
};

struct hns3_pmu_event_attr {
	u32 event;
	u16 filter_support;
};

struct hns3_pmu {
	struct perf_event *hw_events[HNS3_PMU_MAX_HW_EVENTS];
	struct hlist_node node;
	struct pci_dev *pdev;
	struct pmu pmu;
	void __iomem *base;
	int irq;
	int on_cpu;
	u32 identifier;
	u32 hw_clk_freq; /* hardware clock frequency of PMU */
	/* maximum and minimun bdf allowed by PMU */
	u16 bdf_min;
	u16 bdf_max;
};

#define to_hns3_pmu(p)  (container_of((p), struct hns3_pmu, pmu))
#define attr_to_dattr(a) (container_of((a), struct device_attribute, attr))
#define dattr_to_eattr(d) (container_of((d), struct dev_ext_attribute, attr))

#define GET_PCI_DEVFN(bdf)  ((bdf) & 0xFF)

#define FILTER_CONDITION_PORT(port) ((1 << (port)) & 0xFF)
#define FILTER_CONDITION_PORT_TC(port, tc) (((port) << 3) | ((tc) & 0x07))
#define FILTER_CONDITION_FUNC_INTR(func, intr) (((intr) << 8) | (func))

#define BYTES_TO_BITS(bytes)		((bytes) * 8)

#define HNS3_PMU_FILTER_ATTR(_name, _config, _start, _end)            \
	static inline u64 hns3_get_##_name(struct perf_event *event)  \
	{                                                             \
		return FIELD_GET(GENMASK_ULL(_end, _start),           \
				 event->attr._config);                \
	}

HNS3_PMU_FILTER_ATTR(event, config, 0, 16);
HNS3_PMU_FILTER_ATTR(subevent, config, 0, 7);
HNS3_PMU_FILTER_ATTR(event_type, config, 8, 15);
HNS3_PMU_FILTER_ATTR(ext_counter_used, config, 16, 16);
HNS3_PMU_FILTER_ATTR(real_event, config, 0, 15);
HNS3_PMU_FILTER_ATTR(port, config1, 0, 3);
HNS3_PMU_FILTER_ATTR(tc, config1, 4, 7);
HNS3_PMU_FILTER_ATTR(bdf, config1, 8, 23);
HNS3_PMU_FILTER_ATTR(queue, config1, 24, 39);
HNS3_PMU_FILTER_ATTR(intr, config1, 40, 51);
HNS3_PMU_FILTER_ATTR(global, config1, 52, 52);

#define HNS3_BW_EVT_BYTE_NUM(_name)	(&(struct hns3_pmu_event_attr) {\
	HNS3_PMU_EVT_BW_##_name##_BYTE_NUM,				\
	HNS3_PMU_FILTER_BW_##_name})
#define HNS3_BW_EVT_TIME(_name)		(&(struct hns3_pmu_event_attr) {\
	HNS3_PMU_EVT_BW_##_name##_TIME,					\
	HNS3_PMU_FILTER_BW_##_name})
#define HNS3_PPS_EVT_PACKET_NUM(_name)	(&(struct hns3_pmu_event_attr) {\
	HNS3_PMU_EVT_PPS_##_name##_PACKET_NUM,				\
	HNS3_PMU_FILTER_PPS_##_name})
#define HNS3_PPS_EVT_TIME(_name)	(&(struct hns3_pmu_event_attr) {\
	HNS3_PMU_EVT_PPS_##_name##_TIME,				\
	HNS3_PMU_FILTER_PPS_##_name})
#define HNS3_DLY_EVT_TIME(_name)	(&(struct hns3_pmu_event_attr) {\
	HNS3_PMU_EVT_DLY_##_name##_TIME,				\
	HNS3_PMU_FILTER_DLY_##_name})
#define HNS3_DLY_EVT_PACKET_NUM(_name)	(&(struct hns3_pmu_event_attr) {\
	HNS3_PMU_EVT_DLY_##_name##_PACKET_NUM,				\
	HNS3_PMU_FILTER_DLY_##_name})
#define HNS3_INTR_EVT_INTR_NUM(_name)	(&(struct hns3_pmu_event_attr) {\
	HNS3_PMU_EVT_PPS_##_name##_INTR_NUM,				\
	HNS3_PMU_FILTER_INTR_##_name})
#define HNS3_INTR_EVT_TIME(_name)	(&(struct hns3_pmu_event_attr) {\
	HNS3_PMU_EVT_PPS_##_name##_TIME,				\
	HNS3_PMU_FILTER_INTR_##_name})

static ssize_t hns3_pmu_format_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct dev_ext_attribute *eattr;

	eattr = container_of(attr, struct dev_ext_attribute, attr);

	return sysfs_emit(buf, "%s\n", (char *)eattr->var);
}

static ssize_t hns3_pmu_event_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct hns3_pmu_event_attr *event;
	struct dev_ext_attribute *eattr;

	eattr = container_of(attr, struct dev_ext_attribute, attr);
	event = (struct hns3_pmu_event_attr *)eattr->var;

	return sysfs_emit(buf, "config=0x%05x\n", event->event);
}

static ssize_t hns3_pmu_filter_mode_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct hns3_pmu_event_attr *event;
	struct dev_ext_attribute *eattr;
	int len;

	eattr = container_of(attr, struct dev_ext_attribute, attr);
	event = (struct hns3_pmu_event_attr *)eattr->var;

	len = sysfs_emit_at(buf, 0, "filter mode supported: ");
	if (event->filter_support & HNS3_PMU_FILTER_SUPPORT_GLOBAL)
		len += sysfs_emit_at(buf, len, "global/");
	if (event->filter_support & HNS3_PMU_FILTER_SUPPORT_PORT)
		len += sysfs_emit_at(buf, len, "port/");
	if (event->filter_support & HNS3_PMU_FILTER_SUPPORT_PORT_TC)
		len += sysfs_emit_at(buf, len, "port-tc/");
	if (event->filter_support & HNS3_PMU_FILTER_SUPPORT_FUNC)
		len += sysfs_emit_at(buf, len, "func/");
	if (event->filter_support & HNS3_PMU_FILTER_SUPPORT_FUNC_QUEUE)
		len += sysfs_emit_at(buf, len, "func-queue/");
	if (event->filter_support & HNS3_PMU_FILTER_SUPPORT_FUNC_INTR)
		len += sysfs_emit_at(buf, len, "func-intr/");

	len += sysfs_emit_at(buf, len, "\n");

	return len;
}

#define HNS3_PMU_ATTR(_name, _func, _config)				\
	(&((struct dev_ext_attribute[]) {				\
		{ __ATTR(_name, 0444, _func, NULL), (void *)_config }	\
	})[0].attr.attr)

#define HNS3_PMU_FORMAT_ATTR(_name, _format) \
	HNS3_PMU_ATTR(_name, hns3_pmu_format_show, (void *)_format)
#define HNS3_PMU_EVENT_ATTR(_name, _event) \
	HNS3_PMU_ATTR(_name, hns3_pmu_event_show, (void *)_event)

#define HNS3_PMU_BW_EVT_PAIR(_name, _macro) \
	HNS3_PMU_EVENT_ATTR(_name##_byte_num, HNS3_BW_EVT_BYTE_NUM(_macro)), \
	HNS3_PMU_EVENT_ATTR(_name##_time, HNS3_BW_EVT_TIME(_macro))
#define HNS3_PMU_PPS_EVT_PAIR(_name, _macro) \
	HNS3_PMU_EVENT_ATTR(_name##_packet_num, HNS3_PPS_EVT_PACKET_NUM(_macro)), \
	HNS3_PMU_EVENT_ATTR(_name##_time, HNS3_PPS_EVT_TIME(_macro))
#define HNS3_PMU_DLY_EVT_PAIR(_name, _macro) \
	HNS3_PMU_EVENT_ATTR(_name##_time, HNS3_DLY_EVT_TIME(_macro)), \
	HNS3_PMU_EVENT_ATTR(_name##_packet_num, HNS3_DLY_EVT_PACKET_NUM(_macro))
#define HNS3_PMU_INTR_EVT_PAIR(_name, _macro) \
	HNS3_PMU_EVENT_ATTR(_name##_intr_num, HNS3_INTR_EVT_INTR_NUM(_macro)), \
	HNS3_PMU_EVENT_ATTR(_name##_time, HNS3_INTR_EVT_TIME(_macro))

static u8 hns3_pmu_hw_filter_modes[] = {
	HNS3_PMU_HW_FILTER_GLOBAL,
	HNS3_PMU_HW_FILTER_PORT,
	HNS3_PMU_HW_FILTER_PORT_TC,
	HNS3_PMU_HW_FILTER_FUNC,
	HNS3_PMU_HW_FILTER_FUNC_QUEUE,
	HNS3_PMU_HW_FILTER_FUNC_INTR
};

#define HNS3_PMU_SET_HW_FILTER(_hwc, _mode) \
	((_hwc)->addr_filters = (void *)&hns3_pmu_hw_filter_modes[(_mode)])

static ssize_t identifier_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct hns3_pmu *hns3_pmu = to_hns3_pmu(dev_get_drvdata(dev));

	return sysfs_emit(buf, "0x%x\n", hns3_pmu->identifier);
}
static DEVICE_ATTR_RO(identifier);

static ssize_t cpumask_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct hns3_pmu *hns3_pmu = to_hns3_pmu(dev_get_drvdata(dev));

	return sysfs_emit(buf, "%d\n", hns3_pmu->on_cpu);
}
static DEVICE_ATTR_RO(cpumask);

static ssize_t bdf_min_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct hns3_pmu *hns3_pmu = to_hns3_pmu(dev_get_drvdata(dev));

	return sysfs_emit(buf, "0x%4x\n", hns3_pmu->bdf_min);
}
static DEVICE_ATTR_RO(bdf_min);

static ssize_t bdf_max_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct hns3_pmu *hns3_pmu = to_hns3_pmu(dev_get_drvdata(dev));

	return sysfs_emit(buf, "0x%4x\n", hns3_pmu->bdf_max);
}
static DEVICE_ATTR_RO(bdf_max);

static ssize_t
hw_clk_freq_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct hns3_pmu *hns3_pmu = to_hns3_pmu(dev_get_drvdata(dev));

	return sysfs_emit(buf, "%u\n", hns3_pmu->hw_clk_freq);
}
static DEVICE_ATTR_RO(hw_clk_freq);

static struct attribute *hns3_pmu_events_attr[] = {
	/* bandwidth events */
	HNS3_PMU_BW_EVT_PAIR(bw_ssu_egu, SSU_EGU),
	HNS3_PMU_BW_EVT_PAIR(bw_ssu_rpu, SSU_RPU),
	HNS3_PMU_BW_EVT_PAIR(bw_ssu_roce, SSU_ROCE),
	HNS3_PMU_BW_EVT_PAIR(bw_roce_ssu, ROCE_SSU),
	HNS3_PMU_BW_EVT_PAIR(bw_tpu_ssu, TPU_SSU),
	HNS3_PMU_BW_EVT_PAIR(bw_rpu_rcbrx, RPU_RCBRX),
	HNS3_PMU_BW_EVT_PAIR(bw_rcbtx_txsch, RCBTX_TXSCH),
	HNS3_PMU_BW_EVT_PAIR(bw_wr_fbd, WR_FBD),
	HNS3_PMU_BW_EVT_PAIR(bw_wr_ebd, WR_EBD),
	HNS3_PMU_BW_EVT_PAIR(bw_rd_fbd, RD_FBD),
	HNS3_PMU_BW_EVT_PAIR(bw_rd_ebd, RD_EBD),
	HNS3_PMU_BW_EVT_PAIR(bw_rd_pay_m0, RD_PAY_M0),
	HNS3_PMU_BW_EVT_PAIR(bw_rd_pay_m1, RD_PAY_M1),
	HNS3_PMU_BW_EVT_PAIR(bw_wr_pay_m0, WR_PAY_M0),
	HNS3_PMU_BW_EVT_PAIR(bw_wr_pay_m1, WR_PAY_M1),

	/* packet rate events */
	HNS3_PMU_PPS_EVT_PAIR(pps_igu_ssu, IGU_SSU),
	HNS3_PMU_PPS_EVT_PAIR(pps_ssu_egu, SSU_EGU),
	HNS3_PMU_PPS_EVT_PAIR(pps_ssu_rpu, SSU_RPU),
	HNS3_PMU_PPS_EVT_PAIR(pps_ssu_roce, SSU_ROCE),
	HNS3_PMU_PPS_EVT_PAIR(pps_roce_ssu, ROCE_SSU),
	HNS3_PMU_PPS_EVT_PAIR(pps_tpu_ssu, TPU_SSU),
	HNS3_PMU_PPS_EVT_PAIR(pps_rpu_rcbrx, RPU_RCBRX),
	HNS3_PMU_PPS_EVT_PAIR(pps_rcbtx_tpu, RCBTX_TPU),
	HNS3_PMU_PPS_EVT_PAIR(pps_rcbtx_txsch, RCBTX_TXSCH),
	HNS3_PMU_PPS_EVT_PAIR(pps_wr_fbd, WR_FBD),
	HNS3_PMU_PPS_EVT_PAIR(pps_wr_ebd, WR_EBD),
	HNS3_PMU_PPS_EVT_PAIR(pps_rd_fbd, RD_FBD),
	HNS3_PMU_PPS_EVT_PAIR(pps_rd_ebd, RD_EBD),
	HNS3_PMU_PPS_EVT_PAIR(pps_rd_pay_m0, RD_PAY_M0),
	HNS3_PMU_PPS_EVT_PAIR(pps_rd_pay_m1, RD_PAY_M1),
	HNS3_PMU_PPS_EVT_PAIR(pps_wr_pay_m0, WR_PAY_M0),
	HNS3_PMU_PPS_EVT_PAIR(pps_wr_pay_m1, WR_PAY_M1),
	HNS3_PMU_PPS_EVT_PAIR(pps_intr_nicroh_tx_pre, NICROH_TX_PRE),
	HNS3_PMU_PPS_EVT_PAIR(pps_intr_nicroh_rx_pre, NICROH_RX_PRE),

	/* latency events */
	HNS3_PMU_DLY_EVT_PAIR(dly_tx_push_to_mac, TX_PUSH),
	HNS3_PMU_DLY_EVT_PAIR(dly_tx_normal_to_mac, TX),
	HNS3_PMU_DLY_EVT_PAIR(dly_ssu_tx_th_nic, SSU_TX_NIC),
	HNS3_PMU_DLY_EVT_PAIR(dly_ssu_tx_th_roce, SSU_TX_ROCE),
	HNS3_PMU_DLY_EVT_PAIR(dly_ssu_rx_th_nic, SSU_RX_NIC),
	HNS3_PMU_DLY_EVT_PAIR(dly_ssu_rx_th_roce, SSU_RX_ROCE),
	HNS3_PMU_DLY_EVT_PAIR(dly_rpu, RPU),
	HNS3_PMU_DLY_EVT_PAIR(dly_tpu, TPU),
	HNS3_PMU_DLY_EVT_PAIR(dly_rpe, RPE),
	HNS3_PMU_DLY_EVT_PAIR(dly_tpe_normal, TPE),
	HNS3_PMU_DLY_EVT_PAIR(dly_tpe_push, TPE_PUSH),
	HNS3_PMU_DLY_EVT_PAIR(dly_wr_fbd, WR_FBD),
	HNS3_PMU_DLY_EVT_PAIR(dly_wr_ebd, WR_EBD),
	HNS3_PMU_DLY_EVT_PAIR(dly_rd_fbd, RD_FBD),
	HNS3_PMU_DLY_EVT_PAIR(dly_rd_ebd, RD_EBD),
	HNS3_PMU_DLY_EVT_PAIR(dly_rd_pay_m0, RD_PAY_M0),
	HNS3_PMU_DLY_EVT_PAIR(dly_rd_pay_m1, RD_PAY_M1),
	HNS3_PMU_DLY_EVT_PAIR(dly_wr_pay_m0, WR_PAY_M0),
	HNS3_PMU_DLY_EVT_PAIR(dly_wr_pay_m1, WR_PAY_M1),
	HNS3_PMU_DLY_EVT_PAIR(dly_msix_write, MSIX_WRITE),

	/* interrupt rate events */
	HNS3_PMU_INTR_EVT_PAIR(pps_intr_msix_nic, MSIX_NIC),

	NULL
};

static struct attribute_group hns3_pmu_events_group = {
	.name = "events",
	.attrs = hns3_pmu_events_attr,
};

static struct attribute_group hns3_pmu_filter_mode_group = {
	.name = "filtermode",
};

static struct attribute *hns3_pmu_format_attr[] = {
	HNS3_PMU_FORMAT_ATTR(event, "config:0-16"),
	HNS3_PMU_FORMAT_ATTR(port, "config1:0-3"),
	HNS3_PMU_FORMAT_ATTR(tc, "config1:4-7"),
	HNS3_PMU_FORMAT_ATTR(bdf, "config1:8-23"),
	HNS3_PMU_FORMAT_ATTR(queue, "config1:24-39"),
	HNS3_PMU_FORMAT_ATTR(intr, "config1:40-51"),
	HNS3_PMU_FORMAT_ATTR(global, "config1:52-52"),
	NULL
};

static struct attribute_group hns3_pmu_format_group = {
	.name = "format",
	.attrs = hns3_pmu_format_attr,
};

static struct attribute *hns3_pmu_cpumask_attrs[] = {
	&dev_attr_cpumask.attr,
	NULL
};

static struct attribute_group hns3_pmu_cpumask_attr_group = {
	.attrs = hns3_pmu_cpumask_attrs,
};

static struct attribute *hns3_pmu_identifier_attrs[] = {
	&dev_attr_identifier.attr,
	NULL
};

static struct attribute_group hns3_pmu_identifier_attr_group = {
	.attrs = hns3_pmu_identifier_attrs,
};

static struct attribute *hns3_pmu_bdf_range_attrs[] = {
	&dev_attr_bdf_min.attr,
	&dev_attr_bdf_max.attr,
	NULL
};

static struct attribute_group hns3_pmu_bdf_range_attr_group = {
	.attrs = hns3_pmu_bdf_range_attrs,
};

static struct attribute *hns3_pmu_hw_clk_freq_attrs[] = {
	&dev_attr_hw_clk_freq.attr,
	NULL
};

static struct attribute_group hns3_pmu_hw_clk_freq_attr_group = {
	.attrs = hns3_pmu_hw_clk_freq_attrs,
};

static const struct attribute_group *hns3_pmu_attr_groups[] = {
	&hns3_pmu_events_group,
	&hns3_pmu_filter_mode_group,
	&hns3_pmu_format_group,
	&hns3_pmu_cpumask_attr_group,
	&hns3_pmu_identifier_attr_group,
	&hns3_pmu_bdf_range_attr_group,
	&hns3_pmu_hw_clk_freq_attr_group,
	NULL
};

static int hns3_pmu_init_filter_mode_group(void)
{
	u32 group_size = ARRAY_SIZE(hns3_pmu_events_attr);
	struct dev_ext_attribute *eattr_filter;
	struct dev_ext_attribute *eattr_event;
	struct device_attribute *dattr_event;
	struct attribute **attr_filter;
	struct attribute *attr_event;
	u32 i;

	attr_filter = kcalloc(group_size, sizeof(struct attribute *),
			      GFP_KERNEL);
	if (!attr_filter)
		return -ENOMEM;

	eattr_filter = kcalloc(group_size - 1, sizeof(struct dev_ext_attribute),
			       GFP_KERNEL);
	if (!eattr_filter) {
		kfree(attr_filter);
		return -ENOMEM;
	}

	for (i = 0; i < group_size - 1; i++) {
		attr_event = hns3_pmu_events_attr[i];
		dattr_event = attr_to_dattr(attr_event);
		eattr_event = dattr_to_eattr(dattr_event);

		memcpy(&eattr_filter[i], eattr_event,
		       sizeof(struct dev_ext_attribute));
		eattr_filter[i].attr.show = hns3_pmu_filter_mode_show;

		attr_filter[i] = &eattr_filter[i].attr.attr;
	}

	hns3_pmu_filter_mode_group.attrs = attr_filter;

	return 0;
}

static void hns3_pmu_uinit_filter_mode_group(void)
{
	struct dev_ext_attribute *eattr;
	struct device_attribute *dattr;
	struct attribute **attrs;

	attrs = hns3_pmu_filter_mode_group.attrs;
	dattr = attr_to_dattr(attrs[0]);
	eattr = dattr_to_eattr(dattr);

	kfree(eattr);
	kfree(attrs);

	hns3_pmu_filter_mode_group.attrs = NULL;
}

static u32 hns3_pmu_get_offset(u32 offset, u32 idx)
{
	return offset + HNS3_PMU_REG_EVENT_OFFSET +
	       HNS3_PMU_REG_EVENT_SIZE * idx;
}

static u32 hns3_pmu_readl(struct hns3_pmu *hns3_pmu, u32 reg_offset, u32 idx)
{
	u32 offset = hns3_pmu_get_offset(reg_offset, idx);

	return readl(hns3_pmu->base + offset);
}

static void hns3_pmu_writel(struct hns3_pmu *hns3_pmu, u32 reg_offset, u32 idx,
			    u32 val)
{
	u32 offset = hns3_pmu_get_offset(reg_offset, idx);

	writel(val, hns3_pmu->base + offset);
}

static u64 hns3_pmu_readq(struct hns3_pmu *hns3_pmu, u32 reg_offset, u32 idx)
{
	u32 offset = hns3_pmu_get_offset(reg_offset, idx);

	return readq(hns3_pmu->base + offset);
}

static void hns3_pmu_writeq(struct hns3_pmu *hns3_pmu, u32 reg_offset, u32 idx,
			    u64 val)
{
	u32 offset = hns3_pmu_get_offset(reg_offset, idx);

	writeq(val, hns3_pmu->base + offset);
}

static bool hns3_pmu_cmp_event(struct perf_event *target,
			       struct perf_event *event)
{
	return hns3_get_real_event(target) == hns3_get_real_event(event);
}

static int hns3_pmu_find_related_event(struct hns3_pmu *hns3_pmu,
				       struct perf_event *event)
{
	struct perf_event *sibling;
	int idx;

	for (idx = 0; idx < HNS3_PMU_MAX_HW_EVENTS; idx++) {
		sibling = hns3_pmu->hw_events[idx];
		if (!sibling)
			continue;

		if (!hns3_pmu_cmp_event(sibling, event))
			continue;

		/* Related events must be used in group */
		if (sibling->group_leader == event->group_leader)
			return idx;
		else
			return -EINVAL;
	}

	return idx;
}

static int hns3_pmu_get_event_idx(struct hns3_pmu *hns3_pmu)
{
	int idx;

	for (idx = 0; idx < HNS3_PMU_MAX_HW_EVENTS; idx++)
		if (!hns3_pmu->hw_events[idx])
			return idx;

	return -EBUSY;
}

static bool hns3_pmu_valid_bdf(struct hns3_pmu *hns3_pmu, u16 bdf)
{
	struct pci_dev *pdev;

	if (bdf < hns3_pmu->bdf_min || bdf > hns3_pmu->bdf_max) {
		pci_err(hns3_pmu->pdev, "Invalid EP device: %#x!\n", bdf);
		return false;
	}

	pdev = pci_get_domain_bus_and_slot(pci_domain_nr(hns3_pmu->pdev->bus),
					   PCI_BUS_NUM(bdf),
					   GET_PCI_DEVFN(bdf));
	if (!pdev) {
		pci_err(hns3_pmu->pdev, "Nonexistent EP device: %#x!\n", bdf);
		return false;
	}

	pci_dev_put(pdev);
	return true;
}

static void hns3_pmu_set_qid_para(struct hns3_pmu *hns3_pmu, u32 idx, u16 bdf,
				  u16 queue)
{
	u32 val;

	val = GET_PCI_DEVFN(bdf);
	val |= (u32)queue << HNS3_PMU_QID_PARA_QUEUE_S;
	hns3_pmu_writel(hns3_pmu, HNS3_PMU_REG_EVENT_QID_PARA, idx, val);
}

static bool hns3_pmu_qid_req_start(struct hns3_pmu *hns3_pmu, u32 idx)
{
	bool queue_id_valid = false;
	u32 reg_qid_ctrl, val;
	int err;

	/* enable queue id request */
	hns3_pmu_writel(hns3_pmu, HNS3_PMU_REG_EVENT_QID_CTRL, idx,
			HNS3_PMU_QID_CTRL_REQ_ENABLE);

	reg_qid_ctrl = hns3_pmu_get_offset(HNS3_PMU_REG_EVENT_QID_CTRL, idx);
	err = readl_poll_timeout(hns3_pmu->base + reg_qid_ctrl, val,
				 val & HNS3_PMU_QID_CTRL_DONE, 1, 1000);
	if (err == -ETIMEDOUT) {
		pci_err(hns3_pmu->pdev, "QID request timeout!\n");
		goto out;
	}

	queue_id_valid = (val & HNS3_PMU_QID_CTRL_MISS) == 0;

out:
	/* disable qid request and clear status */
	hns3_pmu_writel(hns3_pmu, HNS3_PMU_REG_EVENT_QID_CTRL, idx, 0);

	return queue_id_valid;
}

static bool hns3_pmu_valid_queue(struct hns3_pmu *hns3_pmu, u32 idx, u16 bdf,
				 u16 queue)
{
	hns3_pmu_set_qid_para(hns3_pmu, idx, bdf, queue);

	return hns3_pmu_qid_req_start(hns3_pmu, idx);
}

static struct hns3_pmu_event_attr *hns3_pmu_get_pmu_event(u32 event)
{
	struct hns3_pmu_event_attr *pmu_event;
	struct dev_ext_attribute *eattr;
	struct device_attribute *dattr;
	struct attribute *attr;
	u32 i;

	for (i = 0; i < ARRAY_SIZE(hns3_pmu_events_attr) - 1; i++) {
		attr = hns3_pmu_events_attr[i];
		dattr = container_of(attr, struct device_attribute, attr);
		eattr = container_of(dattr, struct dev_ext_attribute, attr);
		pmu_event = (struct hns3_pmu_event_attr *)eattr->var;

		if (event == pmu_event->event)
			return pmu_event;
	}

	return NULL;
}

static int hns3_pmu_set_func_mode(struct perf_event *event,
				  struct hns3_pmu *hns3_pmu)
{
	struct hw_perf_event *hwc = &event->hw;
	u16 bdf = hns3_get_bdf(event);

	if (!hns3_pmu_valid_bdf(hns3_pmu, bdf))
		return -ENOENT;

	HNS3_PMU_SET_HW_FILTER(hwc, HNS3_PMU_HW_FILTER_FUNC);

	return 0;
}

static int hns3_pmu_set_func_queue_mode(struct perf_event *event,
					struct hns3_pmu *hns3_pmu)
{
	struct hw_perf_event *hwc = &event->hw;
	u16 queue_id = hns3_get_queue(event);
	u16 bdf = hns3_get_bdf(event);

	if (!hns3_pmu_valid_bdf(hns3_pmu, bdf))
		return -ENOENT;

	if (!hns3_pmu_valid_queue(hns3_pmu, hwc->idx, bdf, queue_id)) {
		pci_err(hns3_pmu->pdev, "Invalid queue: %u\n", queue_id);
		return -ENOENT;
	}

	HNS3_PMU_SET_HW_FILTER(hwc, HNS3_PMU_HW_FILTER_FUNC_QUEUE);

	return 0;
}

static bool
hns3_pmu_is_enabled_global_mode(struct perf_event *event,
				struct hns3_pmu_event_attr *pmu_event)
{
	u8 global = hns3_get_global(event);

	return pmu_event->filter_support & HNS3_PMU_FILTER_SUPPORT_GLOBAL &&
	       !!global;
}

static bool hns3_pmu_is_enabled_func_mode(struct perf_event *event,
					  struct hns3_pmu_event_attr *pmu_event)
{
	u16 queue_id = hns3_get_queue(event);
	u16 bdf = hns3_get_bdf(event);

	return pmu_event->filter_support & HNS3_PMU_FILTER_SUPPORT_FUNC &&
	       queue_id == HNS3_PMU_FILTER_ALL_QUEUE && !!bdf;
}

static bool
hns3_pmu_is_enabled_func_queue_mode(struct perf_event *event,
				    struct hns3_pmu_event_attr *pmu_event)
{
	u16 queue_id = hns3_get_queue(event);
	u16 bdf = hns3_get_bdf(event);

	return pmu_event->filter_support & HNS3_PMU_FILTER_SUPPORT_FUNC_QUEUE &&
	       queue_id != HNS3_PMU_FILTER_ALL_QUEUE && !!bdf;
}

static bool hns3_pmu_is_enabled_port_mode(struct perf_event *event,
					  struct hns3_pmu_event_attr *pmu_event)
{
	u8 tc_id = hns3_get_tc(event);

	return pmu_event->filter_support & HNS3_PMU_FILTER_SUPPORT_PORT &&
	       tc_id == HNS3_PMU_FILTER_ALL_TC;
}

static bool
hns3_pmu_is_enabled_port_tc_mode(struct perf_event *event,
				 struct hns3_pmu_event_attr *pmu_event)
{
	u8 tc_id = hns3_get_tc(event);

	return pmu_event->filter_support & HNS3_PMU_FILTER_SUPPORT_PORT_TC &&
	       tc_id != HNS3_PMU_FILTER_ALL_TC;
}

static bool
hns3_pmu_is_enabled_func_intr_mode(struct perf_event *event,
				   struct hns3_pmu *hns3_pmu,
				   struct hns3_pmu_event_attr *pmu_event)
{
	u16 bdf = hns3_get_bdf(event);

	return pmu_event->filter_support & HNS3_PMU_FILTER_SUPPORT_FUNC_INTR &&
	       hns3_pmu_valid_bdf(hns3_pmu, bdf);
}

static int hns3_pmu_select_filter_mode(struct perf_event *event,
				       struct hns3_pmu *hns3_pmu)
{
	struct hns3_pmu_event_attr *pmu_event;
	struct hw_perf_event *hwc = &event->hw;
	u32 event_id = hns3_get_event(event);

	pmu_event = hns3_pmu_get_pmu_event(event_id);
	if (!pmu_event) {
		pci_err(hns3_pmu->pdev, "Invalid pmu event\n");
		return -ENOENT;
	}

	if (hns3_pmu_is_enabled_global_mode(event, pmu_event)) {
		HNS3_PMU_SET_HW_FILTER(hwc, HNS3_PMU_HW_FILTER_GLOBAL);
		return 0;
	}

	if (hns3_pmu_is_enabled_func_mode(event, pmu_event))
		return hns3_pmu_set_func_mode(event, hns3_pmu);

	if (hns3_pmu_is_enabled_func_queue_mode(event, pmu_event))
		return hns3_pmu_set_func_queue_mode(event, hns3_pmu);

	if (hns3_pmu_is_enabled_port_mode(event, pmu_event)) {
		HNS3_PMU_SET_HW_FILTER(hwc, HNS3_PMU_HW_FILTER_PORT);
		return 0;
	}

	if (hns3_pmu_is_enabled_port_tc_mode(event, pmu_event)) {
		HNS3_PMU_SET_HW_FILTER(hwc, HNS3_PMU_HW_FILTER_PORT_TC);
		return 0;
	}

	if (hns3_pmu_is_enabled_func_intr_mode(event, hns3_pmu, pmu_event)) {
		HNS3_PMU_SET_HW_FILTER(hwc, HNS3_PMU_HW_FILTER_FUNC_INTR);
		return 0;
	}

	return -ENOENT;
}

static bool hns3_pmu_validate_event_group(struct perf_event *event)
{
	struct perf_event *sibling, *leader = event->group_leader;
	struct perf_event *event_group[HNS3_PMU_MAX_HW_EVENTS];
	int counters = 1;
	int num;

	event_group[0] = leader;
	if (!is_software_event(leader)) {
		if (leader->pmu != event->pmu)
			return false;

		if (leader != event && !hns3_pmu_cmp_event(leader, event))
			event_group[counters++] = event;
	}

	for_each_sibling_event(sibling, event->group_leader) {
		if (is_software_event(sibling))
			continue;

		if (sibling->pmu != event->pmu)
			return false;

		for (num = 0; num < counters; num++) {
			if (hns3_pmu_cmp_event(event_group[num], sibling))
				break;
		}

		if (num == counters)
			event_group[counters++] = sibling;
	}

	return counters <= HNS3_PMU_MAX_HW_EVENTS;
}

static u32 hns3_pmu_get_filter_condition(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u16 intr_id = hns3_get_intr(event);
	u8 port_id = hns3_get_port(event);
	u16 bdf = hns3_get_bdf(event);
	u8 tc_id = hns3_get_tc(event);
	u8 filter_mode;
	u32 filter = 0;

	filter_mode = *(u8 *)hwc->addr_filters;
	switch (filter_mode) {
	case HNS3_PMU_HW_FILTER_PORT:
		filter = FILTER_CONDITION_PORT(port_id);
		break;
	case HNS3_PMU_HW_FILTER_PORT_TC:
		filter = FILTER_CONDITION_PORT_TC(port_id, tc_id);
		break;
	case HNS3_PMU_HW_FILTER_FUNC:
	case HNS3_PMU_HW_FILTER_FUNC_QUEUE:
		filter = GET_PCI_DEVFN(bdf);
		break;
	case HNS3_PMU_HW_FILTER_FUNC_INTR:
		filter = FILTER_CONDITION_FUNC_INTR(GET_PCI_DEVFN(bdf),
						    intr_id);
		break;
	default:
		break;
	}

	return filter;
}

static void hns3_pmu_config_filter(struct perf_event *event)
{
	struct hns3_pmu *hns3_pmu = to_hns3_pmu(event->pmu);
	u8 event_type = hns3_get_event_type(event);
	u8 subevent_id = hns3_get_subevent(event);
	struct hw_perf_event *hwc = &event->hw;
	u8 filter_mode = *(u8 *)hwc->addr_filters;
	u16 queue_id = hns3_get_queue(event);
	u16 bdf = hns3_get_bdf(event);
	u32 idx = hwc->idx;
	u32 val;

	val = event_type;
	val |= subevent_id << HNS3_PMU_CTRL_SUBEVENT_S;
	val |= filter_mode << HNS3_PMU_CTRL_FILTER_MODE_S;
	val |= HNS3_PMU_EVENT_OVERFLOW_RESTART;
	hns3_pmu_writel(hns3_pmu, HNS3_PMU_REG_EVENT_CTRL_LOW, idx, val);

	val = hns3_pmu_get_filter_condition(event);
	hns3_pmu_writel(hns3_pmu, HNS3_PMU_REG_EVENT_CTRL_HIGH, idx, val);

	if (filter_mode == HNS3_PMU_HW_FILTER_FUNC_QUEUE)
		hns3_pmu_set_qid_para(hns3_pmu, idx, bdf, queue_id);
}

static void hns3_pmu_enable_counter(struct hns3_pmu *hns3_pmu,
				    struct hw_perf_event *hwc)
{
	u32 idx = hwc->idx;
	u32 val;

	val = hns3_pmu_readl(hns3_pmu, HNS3_PMU_REG_EVENT_CTRL_LOW, idx);
	val |= HNS3_PMU_EVENT_EN;
	hns3_pmu_writel(hns3_pmu, HNS3_PMU_REG_EVENT_CTRL_LOW, idx, val);
}

static void hns3_pmu_disable_counter(struct hns3_pmu *hns3_pmu,
				     struct hw_perf_event *hwc)
{
	u32 idx = hwc->idx;
	u32 val;

	val = hns3_pmu_readl(hns3_pmu, HNS3_PMU_REG_EVENT_CTRL_LOW, idx);
	val &= ~HNS3_PMU_EVENT_EN;
	hns3_pmu_writel(hns3_pmu, HNS3_PMU_REG_EVENT_CTRL_LOW, idx, val);
}

static void hns3_pmu_enable_intr(struct hns3_pmu *hns3_pmu,
				 struct hw_perf_event *hwc)
{
	u32 idx = hwc->idx;
	u32 val;

	val = hns3_pmu_readl(hns3_pmu, HNS3_PMU_REG_EVENT_INTR_MASK, idx);
	val &= ~HNS3_PMU_INTR_MASK_OVERFLOW;
	hns3_pmu_writel(hns3_pmu, HNS3_PMU_REG_EVENT_INTR_MASK, idx, val);
}

static void hns3_pmu_disable_intr(struct hns3_pmu *hns3_pmu,
				  struct hw_perf_event *hwc)
{
	u32 idx = hwc->idx;
	u32 val;

	val = hns3_pmu_readl(hns3_pmu, HNS3_PMU_REG_EVENT_INTR_MASK, idx);
	val |= HNS3_PMU_INTR_MASK_OVERFLOW;
	hns3_pmu_writel(hns3_pmu, HNS3_PMU_REG_EVENT_INTR_MASK, idx, val);
}

static void hns3_pmu_clear_intr_status(struct hns3_pmu *hns3_pmu, u32 idx)
{
	u32 val;

	val = hns3_pmu_readl(hns3_pmu, HNS3_PMU_REG_EVENT_CTRL_LOW, idx);
	val |= HNS3_PMU_EVENT_STATUS_RESET;
	hns3_pmu_writel(hns3_pmu, HNS3_PMU_REG_EVENT_CTRL_LOW, idx, val);

	val = hns3_pmu_readl(hns3_pmu, HNS3_PMU_REG_EVENT_CTRL_LOW, idx);
	val &= ~HNS3_PMU_EVENT_STATUS_RESET;
	hns3_pmu_writel(hns3_pmu, HNS3_PMU_REG_EVENT_CTRL_LOW, idx, val);
}

static u64 hns3_pmu_read_counter(struct perf_event *event)
{
	struct hns3_pmu *hns3_pmu = to_hns3_pmu(event->pmu);

	return hns3_pmu_readq(hns3_pmu, event->hw.event_base, event->hw.idx);
}

static void hns3_pmu_write_counter(struct perf_event *event, u64 value)
{
	struct hns3_pmu *hns3_pmu = to_hns3_pmu(event->pmu);
	u32 idx = event->hw.idx;

	hns3_pmu_writeq(hns3_pmu, HNS3_PMU_REG_EVENT_COUNTER, idx, value);
	hns3_pmu_writeq(hns3_pmu, HNS3_PMU_REG_EVENT_EXT_COUNTER, idx, value);
}

static void hns3_pmu_init_counter(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;

	local64_set(&hwc->prev_count, 0);
	hns3_pmu_write_counter(event, 0);
}

static int hns3_pmu_event_init(struct perf_event *event)
{
	struct hns3_pmu *hns3_pmu = to_hns3_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int idx;
	int ret;

	if (event->attr.type != event->pmu->type)
		return -ENOENT;

	/* Sampling is not supported */
	if (is_sampling_event(event) || event->attach_state & PERF_ATTACH_TASK)
		return -EOPNOTSUPP;

	event->cpu = hns3_pmu->on_cpu;

	idx = hns3_pmu_get_event_idx(hns3_pmu);
	if (idx < 0) {
		pci_err(hns3_pmu->pdev, "Up to %u events are supported!\n",
			HNS3_PMU_MAX_HW_EVENTS);
		return -EBUSY;
	}

	hwc->idx = idx;

	ret = hns3_pmu_select_filter_mode(event, hns3_pmu);
	if (ret) {
		pci_err(hns3_pmu->pdev, "Invalid filter, ret = %d.\n", ret);
		return ret;
	}

	if (!hns3_pmu_validate_event_group(event)) {
		pci_err(hns3_pmu->pdev, "Invalid event group.\n");
		return -EINVAL;
	}

	if (hns3_get_ext_counter_used(event))
		hwc->event_base = HNS3_PMU_REG_EVENT_EXT_COUNTER;
	else
		hwc->event_base = HNS3_PMU_REG_EVENT_COUNTER;

	return 0;
}

static void hns3_pmu_read(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	u64 new_cnt, prev_cnt, delta;

	do {
		prev_cnt = local64_read(&hwc->prev_count);
		new_cnt = hns3_pmu_read_counter(event);
	} while (local64_cmpxchg(&hwc->prev_count, prev_cnt, new_cnt) !=
		 prev_cnt);

	delta = new_cnt - prev_cnt;
	local64_add(delta, &event->count);
}

static void hns3_pmu_start(struct perf_event *event, int flags)
{
	struct hns3_pmu *hns3_pmu = to_hns3_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	if (WARN_ON_ONCE(!(hwc->state & PERF_HES_STOPPED)))
		return;

	WARN_ON_ONCE(!(hwc->state & PERF_HES_UPTODATE));
	hwc->state = 0;

	hns3_pmu_config_filter(event);
	hns3_pmu_init_counter(event);
	hns3_pmu_enable_intr(hns3_pmu, hwc);
	hns3_pmu_enable_counter(hns3_pmu, hwc);

	perf_event_update_userpage(event);
}

static void hns3_pmu_stop(struct perf_event *event, int flags)
{
	struct hns3_pmu *hns3_pmu = to_hns3_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	hns3_pmu_disable_counter(hns3_pmu, hwc);
	hns3_pmu_disable_intr(hns3_pmu, hwc);

	WARN_ON_ONCE(hwc->state & PERF_HES_STOPPED);
	hwc->state |= PERF_HES_STOPPED;

	if (hwc->state & PERF_HES_UPTODATE)
		return;

	/* Read hardware counter and update the perf counter statistics */
	hns3_pmu_read(event);
	hwc->state |= PERF_HES_UPTODATE;
}

static int hns3_pmu_add(struct perf_event *event, int flags)
{
	struct hns3_pmu *hns3_pmu = to_hns3_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;
	int idx;

	hwc->state = PERF_HES_STOPPED | PERF_HES_UPTODATE;

	/* Check all working events to find a related event. */
	idx = hns3_pmu_find_related_event(hns3_pmu, event);
	if (idx < 0)
		return idx;

	/* Current event shares an enabled hardware event with related event */
	if (idx < HNS3_PMU_MAX_HW_EVENTS) {
		hwc->idx = idx;
		goto start_count;
	}

	idx = hns3_pmu_get_event_idx(hns3_pmu);
	if (idx < 0)
		return idx;

	hwc->idx = idx;
	hns3_pmu->hw_events[idx] = event;

start_count:
	if (flags & PERF_EF_START)
		hns3_pmu_start(event, PERF_EF_RELOAD);

	return 0;
}

static void hns3_pmu_del(struct perf_event *event, int flags)
{
	struct hns3_pmu *hns3_pmu = to_hns3_pmu(event->pmu);
	struct hw_perf_event *hwc = &event->hw;

	hns3_pmu_stop(event, PERF_EF_UPDATE);
	hns3_pmu->hw_events[hwc->idx] = NULL;
	perf_event_update_userpage(event);
}

static void hns3_pmu_enable(struct pmu *pmu)
{
	struct hns3_pmu *hns3_pmu = to_hns3_pmu(pmu);
	u32 val;

	val = readl(hns3_pmu->base + HNS3_PMU_REG_GLOBAL_CTRL);
	val |= HNS3_PMU_GLOBAL_START;
	writel(val, hns3_pmu->base + HNS3_PMU_REG_GLOBAL_CTRL);
}

static void hns3_pmu_disable(struct pmu *pmu)
{
	struct hns3_pmu *hns3_pmu = to_hns3_pmu(pmu);
	u32 val;

	val = readl(hns3_pmu->base + HNS3_PMU_REG_GLOBAL_CTRL);
	val &= ~HNS3_PMU_GLOBAL_START;
	writel(val, hns3_pmu->base + HNS3_PMU_REG_GLOBAL_CTRL);
}

static int hns3_pmu_alloc_pmu(struct pci_dev *pdev, struct hns3_pmu *hns3_pmu)
{
	u16 device_id;
	char *name;
	u32 val;
	int ret;

	hns3_pmu->base = pci_ioremap_bar(pdev, 2);
	if (!hns3_pmu->base) {
		pci_err(pdev, "ioremap failed for hns3_pmu resource\n");
		ret = -ENOMEM;
		goto err_ioremap_bar;
	}

	hns3_pmu->hw_clk_freq = readl(hns3_pmu->base + HNS3_PMU_REG_CLOCK_FREQ);

	val = readl(hns3_pmu->base + HNS3_PMU_REG_BDF);
	hns3_pmu->bdf_min = val & 0xffff;
	hns3_pmu->bdf_max = val >> 16;

	val = readl(hns3_pmu->base + HNS3_PMU_REG_DEVICE_ID);
	device_id = val & 0xffff;
	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "hns3_pmu_sicl_%u", device_id);
	if (!name) {
		ret = -ENOMEM;
		goto err_alloc_dev_name;
	}

	hns3_pmu->pdev = pdev;
	hns3_pmu->on_cpu = -1;
	hns3_pmu->identifier = readl(hns3_pmu->base + HNS3_PMU_REG_VERSION);
	hns3_pmu->pmu = (struct pmu) {
		.name		= name,
		.module		= THIS_MODULE,
		.event_init	= hns3_pmu_event_init,
		.pmu_enable	= hns3_pmu_enable,
		.pmu_disable	= hns3_pmu_disable,
		.add		= hns3_pmu_add,
		.del		= hns3_pmu_del,
		.start		= hns3_pmu_start,
		.stop		= hns3_pmu_stop,
		.read		= hns3_pmu_read,
		.task_ctx_nr	= perf_invalid_context,
		.attr_groups	= hns3_pmu_attr_groups,
		.capabilities	= PERF_PMU_CAP_NO_EXCLUDE,
	};

	return 0;

err_alloc_dev_name:
	iounmap(hns3_pmu->base);
err_ioremap_bar:
	pci_release_regions(pdev);

	return ret;
}

static irqreturn_t hns3_pmu_irq(int irq, void *data)
{
	struct hns3_pmu *hns3_pmu = data;
	u32 intr_status, idx;

	for (idx = 0; idx < HNS3_PMU_MAX_HW_EVENTS; idx++) {
		intr_status = hns3_pmu_readl(hns3_pmu,
					     HNS3_PMU_REG_EVENT_INTR_STATUS,
					     idx);

		/*
		 * As each counter will restart from 0 when it is overflowed,
		 * extra processing is no need, just clear interrupt status.
		 */
		if (intr_status)
			hns3_pmu_clear_intr_status(hns3_pmu, idx);
	}

	return IRQ_HANDLED;
}

static int hns3_pmu_online_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct hns3_pmu *hns3_pmu;

	hns3_pmu = hlist_entry_safe(node, struct hns3_pmu, node);
	if (!hns3_pmu)
		return -ENODEV;

	if (hns3_pmu->on_cpu == -1) {
		hns3_pmu->on_cpu = cpu;
		irq_set_affinity(hns3_pmu->irq, cpumask_of(cpu));
	}

	return 0;
}

static int hns3_pmu_offline_cpu(unsigned int cpu, struct hlist_node *node)
{
	struct hns3_pmu *hns3_pmu;
	unsigned int target;

	hns3_pmu = hlist_entry_safe(node, struct hns3_pmu, node);
	if (!hns3_pmu)
		return -ENODEV;

	/* Nothing to do if this CPU doesn't own the PMU */
	if (hns3_pmu->on_cpu != cpu)
		return 0;

	/* Choose a new CPU from all online cpus */
	target = cpumask_any_but(cpu_online_mask, cpu);
	if (target >= nr_cpu_ids)
		return 0;

	perf_pmu_migrate_context(&hns3_pmu->pmu, cpu, target);
	hns3_pmu->on_cpu = target;
	irq_set_affinity(hns3_pmu->irq, cpumask_of(target));

	return 0;
}

static void hns3_pmu_free_irq(void *data)
{
	struct pci_dev *pdev = data;

	pci_free_irq_vectors(pdev);
}

static int hns3_pmu_irq_register(struct pci_dev *pdev,
				 struct hns3_pmu *hns3_pmu)
{
	int irq, ret;

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
	if (ret < 0) {
		pci_err(pdev, "failed to enable MSI vectors, ret = %d.\n", ret);
		return ret;
	}

	ret = devm_add_action(&pdev->dev, hns3_pmu_free_irq, pdev);
	if (ret) {
		pci_err(pdev, "failed to add free irq action, ret = %d.\n", ret);
		return ret;
	}

	irq = pci_irq_vector(pdev, 0);
	ret = devm_request_irq(&pdev->dev, irq, hns3_pmu_irq, 0,
			       hns3_pmu->pmu.name, hns3_pmu);
	if (ret) {
		pci_err(pdev, "failed to register irq, ret = %d.\n", ret);
		return ret;
	}

	hns3_pmu->irq = irq;

	return 0;
}

static int hns3_pmu_init(struct pci_dev *pdev, struct hns3_pmu *hns3_pmu)
{
	int ret;

	ret = hns3_pmu_alloc_pmu(pdev, hns3_pmu);
	if (ret)
		return ret;

	ret = hns3_pmu_irq_register(pdev, hns3_pmu);
	if (ret)
		goto err_irq_register;

	ret = cpuhp_state_add_instance(CPUHP_AP_PERF_ARM_HNS3_PMU_ONLINE,
				       &hns3_pmu->node);
	if (ret) {
		pci_err(pdev, "failed to register hotplug, ret = %d.\n", ret);
		goto err_irq_register;
	}

	ret = perf_pmu_register(&hns3_pmu->pmu, hns3_pmu->pmu.name, -1);
	if (ret) {
		pci_err(pdev, "failed to register perf PMU, ret = %d.\n", ret);
		goto err_register_pmu;
	}

	return ret;

err_register_pmu:
	cpuhp_state_remove_instance(CPUHP_AP_PERF_ARM_HNS3_PMU_ONLINE,
				    &hns3_pmu->node);

err_irq_register:
	iounmap(hns3_pmu->base);

	return ret;
}

static void hns3_pmu_uninit(struct pci_dev *pdev)
{
	struct hns3_pmu *hns3_pmu = pci_get_drvdata(pdev);

	perf_pmu_unregister(&hns3_pmu->pmu);
	cpuhp_state_remove_instance(CPUHP_AP_PERF_ARM_HNS3_PMU_ONLINE,
				    &hns3_pmu->node);
	iounmap(hns3_pmu->base);
}

static int hns3_pmu_init_dev(struct pci_dev *pdev)
{
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret) {
		pci_err(pdev, "failed to enable pci device, ret = %d.\n", ret);
		return ret;
	}

	ret = pci_request_mem_regions(pdev, "hns3_pmu");
	if (ret < 0) {
		pci_err(pdev, "failed to request pci mem regions, ret = %d.\n",
			ret);
		return ret;
	}

	pci_set_master(pdev);

	return 0;
}

static void hns3_pmu_uninit_dev(struct pci_dev *pdev)
{
	pci_clear_master(pdev);
	pci_release_mem_regions(pdev);
}

static int hns3_pmu_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct hns3_pmu *hns3_pmu;
	int ret;

	hns3_pmu = devm_kzalloc(&pdev->dev, sizeof(*hns3_pmu), GFP_KERNEL);
	if (!hns3_pmu)
		return -ENOMEM;

	ret = hns3_pmu_init_dev(pdev);
	if (ret)
		return ret;

	ret = hns3_pmu_init(pdev, hns3_pmu);
	if (ret) {
		hns3_pmu_uninit_dev(pdev);
		return ret;
	}

	pci_set_drvdata(pdev, hns3_pmu);

	return ret;
}

static void hns3_pmu_remove(struct pci_dev *pdev)
{
	hns3_pmu_uninit(pdev);
	hns3_pmu_uninit_dev(pdev);
	pci_set_drvdata(pdev, NULL);
}

static const struct pci_device_id hns3_pmu_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_HUAWEI, 0xA22B) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, hns3_pmu_ids);

static struct pci_driver hns3_pmu_driver = {
	.name = "hns3_pmu",
	.id_table = hns3_pmu_ids,
	.probe = hns3_pmu_probe,
	.remove = hns3_pmu_remove,
};

static int __init hns3_pmu_module_init(void)
{
	int ret;

	ret = hns3_pmu_init_filter_mode_group();
	if (ret)
		return ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_PERF_ARM_HNS3_PMU_ONLINE,
				      "AP_PERF_ARM_HNS3_PMU_ONLINE",
				      hns3_pmu_online_cpu,
				      hns3_pmu_offline_cpu);
	if (ret) {
		pr_err("failed to setup HNS3 PMU hotplug, ret = %d.\n", ret);
		goto err_init;
	}

	ret = pci_register_driver(&hns3_pmu_driver);
	if (ret) {
		pr_err("failed to register pci driver, ret = %d.\n", ret);
		goto err_hp;
	}

	return 0;

err_hp:
	cpuhp_remove_multi_state(CPUHP_AP_PERF_ARM_HNS3_PMU_ONLINE);
err_init:
	hns3_pmu_uinit_filter_mode_group();
	return ret;
}
module_init(hns3_pmu_module_init);

static void __exit hns3_pmu_module_exit(void)
{
	pci_unregister_driver(&hns3_pmu_driver);
	cpuhp_remove_multi_state(CPUHP_AP_PERF_ARM_HNS3_PMU_ONLINE);
	hns3_pmu_uinit_filter_mode_group();
}
module_exit(hns3_pmu_module_exit);

MODULE_DESCRIPTION("HNS3 PMU driver");
MODULE_LICENSE("GPL v2");
