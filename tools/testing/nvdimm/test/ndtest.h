/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef NDTEST_H
#define NDTEST_H

#include <linux/platform_device.h>
#include <linux/libnvdimm.h>

/* SCM device is unable to persist memory contents */
#define PAPR_PMEM_UNARMED                   (1ULL << (63 - 0))
/* SCM device failed to persist memory contents */
#define PAPR_PMEM_SHUTDOWN_DIRTY            (1ULL << (63 - 1))
/* SCM device contents are not persisted from previous IPL */
#define PAPR_PMEM_EMPTY                     (1ULL << (63 - 3))
#define PAPR_PMEM_HEALTH_CRITICAL           (1ULL << (63 - 4))
/* SCM device will be garded off next IPL due to failure */
#define PAPR_PMEM_HEALTH_FATAL              (1ULL << (63 - 5))
/* SCM contents cannot persist due to current platform health status */
#define PAPR_PMEM_HEALTH_UNHEALTHY          (1ULL << (63 - 6))
/* SCM device is unable to persist memory contents in certain conditions */
#define PAPR_PMEM_HEALTH_NON_CRITICAL       (1ULL << (63 - 7))

/* Bits status indicators for health bitmap indicating unarmed dimm */
#define PAPR_PMEM_UNARMED_MASK (PAPR_PMEM_UNARMED |		\
				PAPR_PMEM_HEALTH_UNHEALTHY)

#define PAPR_PMEM_SAVE_FAILED                (1ULL << (63 - 10))

/* Bits status indicators for health bitmap indicating unflushed dimm */
#define PAPR_PMEM_BAD_SHUTDOWN_MASK (PAPR_PMEM_SHUTDOWN_DIRTY)

/* Bits status indicators for health bitmap indicating unrestored dimm */
#define PAPR_PMEM_BAD_RESTORE_MASK  (PAPR_PMEM_EMPTY)

/* Bit status indicators for smart event notification */
#define PAPR_PMEM_SMART_EVENT_MASK (PAPR_PMEM_HEALTH_CRITICAL | \
				    PAPR_PMEM_HEALTH_FATAL |	\
				    PAPR_PMEM_HEALTH_UNHEALTHY)

#define PAPR_PMEM_SAVE_MASK                (PAPR_PMEM_SAVE_FAILED)

struct ndtest_config;

/* DIMM Health extension flag bits */
#define PDSM_DIMM_HEALTH_RUN_GAUGE_VALID                (1 << 0)
#define PDSM_DIMM_HEALTH_MEDIA_TEMPERATURE_VALID        (1 << 1)
#define PDSM_DIMM_HEALTH_CTRL_TEMPERATURE_VALID         (1 << 2)
#define PDSM_DIMM_HEALTH_SHUTDOWN_COUNT_VALID		(1 << 3)
#define PDSM_DIMM_HEALTH_SPARES_VALID                   (1 << 4)
#define PDSM_DIMM_HEALTH_ALARM_VALID                    (1 << 5)

#define PAPR_PDSM_DIMM_HEALTHY           0

#define ND_PAPR_HEALTH_SPARE_TRIP       (1 << 0)
#define ND_PAPR_HEALTH_TEMP_TRIP        (1 << 1)
#define ND_PAPR_HEALTH_CTEMP_TRIP       (1 << 2)

/* DIMM Health inject flag bits */
#define ND_PAPR_HEALTH_INJECT_MTEMP     (1 << 0)
#define ND_PAPR_HEALTH_INJECT_SPARE     (1 << 1)
#define ND_PAPR_HEALTH_INJECT_FATAL     (1 << 2)
#define ND_PAPR_HEALTH_INJECT_SHUTDOWN  (1 << 3)

/* Various nvdimm health indicators */
#define PAPR_PDSM_DIMM_HEALTHY           0
#define PAPR_PDSM_DIMM_UNHEALTHY         1
#define PAPR_PDSM_DIMM_CRITICAL          2
#define PAPR_PDSM_DIMM_FATAL             3

enum papr_pdsm {
	PAPR_PDSM_MIN = 0x0,
	PAPR_PDSM_HEALTH,
	PAPR_PDSM_INJECT_SET = 11,
	PAPR_PDSM_INJECT_CLEAR = 12,
	PAPR_PDSM_INJECT_GET = 13,
	PAPR_PDSM_HEALTH_INJECT = 14,
	PAPR_PDSM_HEALTH_THRESHOLD = 15,
	PAPR_PDSM_HEALTH_THRESHOLD_SET = 16,
	PAPR_PDSM_MAX,
};

struct ndtest_priv {
	struct platform_device pdev;
	struct device_node *dn;
	struct list_head resources;
	struct nvdimm_bus_descriptor bus_desc;
	struct nvdimm_bus *bus;
	struct ndtest_config *config;

	dma_addr_t *dcr_dma;
	dma_addr_t *label_dma;
	dma_addr_t *dimm_dma;
};

struct ndtest_blk_mmio {
	void __iomem *base;
	u64 size;
	u64 base_offset;
	u32 line_size;
	u32 num_lines;
	u32 table_size;
};

struct ndtest_dimm {
	struct device *dev;
	struct nvdimm *nvdimm;
	struct ndtest_blk_mmio *mmio;
	struct nd_region *blk_region;

	dma_addr_t address;
	unsigned long long flags;
	unsigned long config_size;
	void *label_area;
	char *uuid_str;

	unsigned int size;
	unsigned int handle;
	unsigned int fail_cmd;
	unsigned int physical_id;
	unsigned int num_formats;
	int id;
	int fail_cmd_code;
	u8 no_alias;

	struct kernfs_node *notify_handle;

	/* SMART Health information */
	unsigned long long extension_flags;
	__u16 dimm_fuel_gauge;
	__u16 media_temperature;
	__u16 ctrl_temperature;
	__u8 spares;
	__u8 alarm_flags;

	/* SMART Health thresholds */
	__u16 media_temperature_threshold;
	__u16 ctrl_temperature_threshold;
	__u8 spares_threshold;
};

struct ndtest_mapping {
	u64 start;
	u64 size;
	u8 position;
	u8 dimm;
};

struct ndtest_region {
	struct nd_region *region;
	struct ndtest_mapping *mapping;
	u64 size;
	u8 type;
	u8 num_mappings;
	u8 range_index;
};

struct ndtest_config {
	struct ndtest_dimm *dimms;
	struct ndtest_region *regions;
	unsigned int dimm_count;
	unsigned int dimm_start;
	u8 num_regions;
};

#define ND_PDSM_PAYLOAD_MAX_SIZE 184

struct nd_papr_pdsm_health {
	union {
		struct {
			__u32 extension_flags;
			__u8 dimm_unarmed;
			__u8 dimm_bad_shutdown;
			__u8 dimm_bad_restore;
			__u8 dimm_scrubbed;
			__u8 dimm_locked;
			__u8 dimm_encrypted;
			__u16 dimm_health;

			/* Extension flag PDSM_DIMM_HEALTH_RUN_GAUGE_VALID */
			__u16 dimm_fuel_gauge;
			__u16 media_temperature;
			__u16 ctrl_temperature;
			__u8 spares;
			__u16 alarm_flags;
		};
		__u8 buf[ND_PDSM_PAYLOAD_MAX_SIZE];
	};
};

struct nd_papr_pdsm_health_threshold {
	union {
		struct {
			__u16 alarm_control;
			__u8 spares;
			__u16 media_temperature;
			__u16 ctrl_temperature;
			__u32 status;
		};
		__u8 buf[ND_PDSM_PAYLOAD_MAX_SIZE];
	};
};

struct nd_papr_pdsm_health_inject {
	union {
		struct {
			__u64 flags;
			__u8 mtemp_enable;
			__u16 media_temperature;
			__u8 ctemp_enable;
			__u16 ctrl_temperature;
			__u8 spares_enable;
			__u8 spares;
			__u8 fatal_enable;
			__u8 unsafe_shutdown_enable;
			__u32 status;
		};
		__u8 buf[ND_PDSM_PAYLOAD_MAX_SIZE];
	};
};

union nd_pdsm_payload {
	struct nd_papr_pdsm_health health;
	struct nd_papr_pdsm_health_inject inject;
	struct nd_papr_pdsm_health_threshold threshold;
	__u8 buf[ND_PDSM_PAYLOAD_MAX_SIZE];
} __packed;

struct nd_pkg_pdsm {
	__s32 cmd_status;       /* Out: Sub-cmd status returned back */
	__u16 reserved[2];      /* Ignored and to be set as '0' */
	union nd_pdsm_payload payload;
} __packed;

#endif /* NDTEST_H */
