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

/**
 * PFRU_IOC_SET_REV - _IOW(PFRU_MAGIC, 0x01, unsigned int)
 *
 * Return: 0 on success, -errno on failure
 *
 * Set the Revision ID for PFRU Runtime Update. It could be either 1 or 2.
 */
#define PFRU_IOC_SET_REV _IOW(PFRU_MAGIC, 0x01, unsigned int)
/**
 * PFRU_IOC_STAGE - _IOW(PFRU_MAGIC, 0x02, unsigned int)
 *
 * Return: 0 on success, -errno on failure
 *
 * Stage a capsule image from communication buffer and perform authentication.
 */
#define PFRU_IOC_STAGE _IOW(PFRU_MAGIC, 0x02, unsigned int)
/**
 * PFRU_IOC_ACTIVATE - _IOW(PFRU_MAGIC, 0x03, unsigned int)
 *
 * Return: 0 on success, -errno on failure
 *
 * Activate a previous staged capsule image.
 */
#define PFRU_IOC_ACTIVATE _IOW(PFRU_MAGIC, 0x03, unsigned int)
/**
 * PFRU_IOC_STAGE_ACTIVATE - _IOW(PFRU_MAGIC, 0x04, unsigned int)
 *
 * Return: 0 on success, -errno on failure
 *
 * Perform both stage and activation actions.
 */
#define PFRU_IOC_STAGE_ACTIVATE _IOW(PFRU_MAGIC, 0x04, unsigned int)
/**
 * PFRU_IOC_QUERY_CAP - _IOR(PFRU_MAGIC, 0x05,
 *			     struct pfru_update_cap_info)
 *
 * Return: 0 on success, -errno on failure.
 *
 * Retrieve information about the PFRU Runtime Update capability.
 * The information is a struct pfru_update_cap_info.
 */
#define PFRU_IOC_QUERY_CAP _IOR(PFRU_MAGIC, 0x05, struct pfru_update_cap_info)

static inline int pfru_valid_revid(int id)
{
	return id == REVID_1 || id == REVID_2;
}

/**
 * struct pfru_payload_hdr - Capsule file payload header.
 *
 * @sig: Signature of this capsule file.
 * @hdr_version: Revision of this header structure.
 * @hdr_size: Size of this header, including the OemHeader bytes.
 * @hw_ver: The supported firmware version.
 * @rt_ver: Version of the code injection image.
 * @platform_id: A platform specific GUID to specify the platform what
 *               this capsule image support.
 */
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

/**
 * struct pfru_update_cap_info - Runtime update capability information.
 *
 * @status: Indicator of whether this query succeed.
 * @update_cap: Bitmap to indicate whether the feature is supported.
 * @code_type: A buffer containing an image type GUID.
 * @fw_version: Platform firmware version.
 * @code_rt_version: Code injection runtime version for anti-rollback.
 * @drv_type: A buffer containing an image type GUID.
 * @drv_rt_version: The version of the driver update runtime code.
 * @drv_svn: The secure version number(SVN) of the driver update runtime code.
 * @platform_id: A buffer containing a platform ID GUID.
 * @oem_id: A buffer containing an OEM ID GUID.
 * @oem_info: A buffer containing the vendor specific information.
 */
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

/**
 * struct pfru_com_buf_info - Communication buffer information.
 *
 * @status: Indicator of whether this query succeed.
 * @ext_status: Implementation specific query result.
 * @addr_lo: Low 32bit physical address of the communication buffer to hold
 *           a runtime update package.
 * @addr_hi: High 32bit physical address of the communication buffer to hold
 *           a runtime update package.
 * @buf_size: Maximum size in bytes of the communication buffer.
 */
struct pfru_com_buf_info {
	enum pfru_dsm_status status;
	enum pfru_dsm_status ext_status;
	__u64 addr_lo;
	__u64 addr_hi;
	__u32 buf_size;
};

/**
 * struct pfru_updated_result - Platform firmware runtime update result information.
 * @status: Indicator of whether this update succeed.
 * @ext_status: Implementation specific update result.
 * @low_auth_time: Low 32bit value of image authentication time in nanosecond.
 * @high_auth_time: High 32bit value of image authentication time in nanosecond.
 * @low_exec_time: Low 32bit value of image execution time in nanosecond.
 * @high_exec_time: High 32bit value of image execution time in nanosecond.
 */
struct pfru_updated_result {
	enum pfru_dsm_status status;
	enum pfru_dsm_status ext_status;
	__u64 low_auth_time;
	__u64 high_auth_time;
	__u64 low_exec_time;
	__u64 high_exec_time;
};

#endif /* __PFRU_H__ */
