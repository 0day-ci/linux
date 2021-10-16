/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Platform Firmware Runtime Update header
 *
 * Copyright(c) 2021 Intel Corporation. All rights reserved.
 */
#ifndef __PFRU_H__
#define __PFRU_H__

#include <linux/ioctl.h>
#include <linux/uuid.h>

#define PFRU_UUID		"ECF9533B-4A3C-4E89-939E-C77112601C6D"
#define PFRU_CODE_INJ_UUID		"B2F84B79-7B6E-4E45-885F-3FB9BB185402"
#define PFRU_DRV_UPDATE_UUID		"4569DD8C-75F1-429A-A3D6-24DE8097A0DF"

#define FUNC_STANDARD_QUERY	0
#define FUNC_QUERY_UPDATE_CAP	1
#define FUNC_QUERY_BUF		2
#define FUNC_START		3

#define CODE_INJECT_TYPE	1
#define DRIVER_UPDATE_TYPE	2

#define REVID_1		1
#define REVID_2		2

#define PFRU_MAGIC 0xEE

#define PFRU_IOC_SET_REV _IOW(PFRU_MAGIC, 0x01, unsigned int)
#define PFRU_IOC_STAGE _IOW(PFRU_MAGIC, 0x02, unsigned int)
#define PFRU_IOC_ACTIVATE _IOW(PFRU_MAGIC, 0x03, unsigned int)
#define PFRU_IOC_STAGE_ACTIVATE _IOW(PFRU_MAGIC, 0x04, unsigned int)
#define PFRU_IOC_QUERY_CAP _IOR(PFRU_MAGIC, 0x05, struct pfru_update_cap_info)

static inline int pfru_valid_revid(int id)
{
	return id == REVID_1 || id == REVID_2;
}

/* Capsule file payload header */
struct pfru_payload_hdr {
	__u32	sig;
	__u32	hdr_version;
	__u32	hdr_size;
	__u32	hw_ver;
	__u32	rt_ver;
	uuid_t	platform_id;
};

enum pfru_start_action {
	START_STAGE,
	START_ACTIVATE,
	START_STAGE_ACTIVATE,
};

enum pfru_dsm_status {
	DSM_SUCCEED,
	DSM_FUNC_NOT_SUPPORT,
	DSM_INVAL_INPUT,
	DSM_HARDWARE_ERR,
	DSM_RETRY_SUGGESTED,
	DSM_UNKNOWN,
	DSM_FUNC_SPEC_ERR,
};

struct pfru_update_cap_info {
	enum pfru_dsm_status status;
	__u32 update_cap;

	uuid_t code_type;
	__u32 fw_version;
	__u32 code_rt_version;

	uuid_t drv_type;
	__u32 drv_rt_version;
	__u32 drv_svn;

	uuid_t platform_id;
	uuid_t oem_id;

	char oem_info[];
};

struct pfru_com_buf_info {
	enum pfru_dsm_status status;
	enum pfru_dsm_status ext_status;
	__u64 addr_lo;
	__u64 addr_hi;
	__u32 buf_size;
};

struct pfru_updated_result {
	enum pfru_dsm_status status;
	enum pfru_dsm_status ext_status;
	__u64 low_auth_time;
	__u64 high_auth_time;
	__u64 low_exec_time;
	__u64 high_exec_time;
};

#define PFRU_LOG_UUID	"75191659-8178-4D9D-B88F-AC5E5E93E8BF"

/* Telemetry structures. */
struct pfru_log_data_info {
	enum pfru_dsm_status status;
	enum pfru_dsm_status ext_status;
	__u64 chunk1_addr_lo;
	__u64 chunk1_addr_hi;
	__u64 chunk2_addr_lo;
	__u64 chunk2_addr_hi;
	__u32 max_data_size;
	__u32 chunk1_size;
	__u32 chunk2_size;
	__u32 rollover_cnt;
	__u32 reset_cnt;
};

struct pfru_log_info {
	__u32 log_level;
	__u32 log_type;
	__u32 log_revid;
};

/* Two logs: history and execution log */
#define LOG_EXEC_IDX	0
#define LOG_HISTORY_IDX	1
#define NR_LOG_TYPE	2

#define LOG_ERR		0
#define LOG_WARN	1
#define LOG_INFO	2
#define LOG_VERB	4

#define FUNC_SET_LEV		1
#define FUNC_GET_LEV		2
#define FUNC_GET_DATA		3

#define LOG_NAME_SIZE		10

#define PFRU_LOG_IOC_SET_INFO _IOW(PFRU_MAGIC, 0x05, struct pfru_log_info)
#define PFRU_LOG_IOC_GET_INFO _IOR(PFRU_MAGIC, 0x06, struct pfru_log_info)
#define PFRU_LOG_IOC_GET_DATA_INFO _IOR(PFRU_MAGIC, 0x07, struct pfru_log_data_info)

#endif /* __PFRU_H__ */
