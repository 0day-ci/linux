/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Header File for FPGA Image Load User API
 *
 * Copyright (C) 2019-2021 Intel Corporation, Inc.
 *
 */

#ifndef _UAPI_LINUX_FPGA_IMAGE_LOAD_H
#define _UAPI_LINUX_FPGA_IMAGE_LOAD_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define FPGA_IMAGE_LOAD_MAGIC 0xB9

/* Image load progress codes */
enum fpga_image_prog {
	FPGA_IMAGE_PROG_IDLE,
	FPGA_IMAGE_PROG_STARTING,
	FPGA_IMAGE_PROG_PREPARING,
	FPGA_IMAGE_PROG_WRITING,
	FPGA_IMAGE_PROG_PROGRAMMING,
	FPGA_IMAGE_PROG_MAX
};

/* Image error progress codes */
enum fpga_image_err {
	FPGA_IMAGE_ERR_NONE,
	FPGA_IMAGE_ERR_HW_ERROR,
	FPGA_IMAGE_ERR_TIMEOUT,
	FPGA_IMAGE_ERR_CANCELED,
	FPGA_IMAGE_ERR_BUSY,
	FPGA_IMAGE_ERR_INVALID_SIZE,
	FPGA_IMAGE_ERR_RW_ERROR,
	FPGA_IMAGE_ERR_WEAROUT,
	FPGA_IMAGE_ERR_MAX
};

#define FPGA_IMAGE_LOAD_WRITE	_IOW(FPGA_IMAGE_LOAD_MAGIC, 0, struct fpga_image_write)

/**
 * FPGA_IMAGE_LOAD_WRITE - _IOW(FPGA_IMAGE_LOAD_MAGIC, 0,
 *				struct fpga_image_write)
 *
 * Upload a data buffer to the target device. The user must provide the
 * data buffer, size, and an eventfd file descriptor.
 *
 * Return: 0 on success, -errno on failure.
 */
struct fpga_image_write {
	/* Input */
	__u32 flags;		/* Zero for now */
	__u32 size;		/* Data size (in bytes) to be written */
	__u64 buf;		/* User space address of source data */
};

#endif /* _UAPI_LINUX_FPGA_IMAGE_LOAD_H */
