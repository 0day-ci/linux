/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Header File for Firmware Upload User API
 *
 * Copyright (C) 2019-2021 Intel Corporation, Inc.
 *
 */

#ifndef _UAPI_LINUX_FW_UPLOAD__H
#define _UAPI_LINUX_FW_UPLOAD__H

#include <linux/types.h>
#include <linux/ioctl.h>

#define FW_UPLOAD_MAGIC 0xB9

/* Firmware upload progress codes */
#define FW_UPLOAD_PROG_IDLE		0
#define FW_UPLOAD_PROG_STARTING	1
#define FW_UPLOAD_PROG_PREPARING	2
#define FW_UPLOAD_PROG_WRITING		3
#define FW_UPLOAD_PROG_PROGRAMMING	4
#define FW_UPLOAD_PROG_MAX		5

/* Firmware upload error codes */
#define FW_UPLOAD_ERR_HW_ERROR		1
#define FW_UPLOAD_ERR_TIMEOUT		2
#define FW_UPLOAD_ERR_CANCELED		3
#define FW_UPLOAD_ERR_BUSY		4
#define FW_UPLOAD_ERR_INVALID_SIZE	5
#define FW_UPLOAD_ERR_RW_ERROR		6
#define FW_UPLOAD_ERR_WEAROUT		7
#define FW_UPLOAD_ERR_MAX		8

/**
 * FW_UPLOAD_WRITE - _IOW(FW_UPLOAD_MAGIC, 0,
 *				struct fw_upload_write)
 *
 * Upload a data buffer to the target device. The user must provide the
 * data buffer, size, and an eventfd file descriptor.
 *
 * Return: 0 on success, -errno on failure.
 */
struct fw_upload_write {
	/* Input */
	__u32 flags;		/* Zero for now */
	__u32 size;		/* Data size (in bytes) to be written */
	__s32 evtfd;		/* File descriptor for completion signal */
	__u64 buf;		/* User space address of source data */
};

#define FW_UPLOAD_WRITE	_IOW(FW_UPLOAD_MAGIC, 0, struct fw_upload_write)

/**
 * FW_UPLOAD_STATUS - _IOR(FW_UPLOAD_MAGIC, 1, struct fw_upload_status)
 *
 * Request status information for an ongoing update.
 *
 * Return: 0 on success, -errno on failure.
 */
struct fw_upload_status {
	/* Output */
	__u32 remaining_size;	/* size remaining to transfer */
	__u32 progress;		/* current progress of firmware upload */
	__u32 err_progress;	/* progress at time of error */
	__u32 err_code;		/* error code */
};

#define FW_UPLOAD_STATUS	_IOR(FW_UPLOAD_MAGIC, 1, struct fw_upload_status)

#endif /* _UAPI_LINUX_FW_UPLOAD_H */
