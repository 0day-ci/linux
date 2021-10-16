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

#endif /* __PFRU_H__ */
