/* SPDX-License-Identifier: GPL-2.0+ */
/*
*
* Zhouyi AI Accelerator driver
*
* Copyright (C) 2020 Arm (China) Ltd.
* Copyright (C) 2021 Cai Huoqing
*/

/**
 * @file zynpu_io.h
 * ZYNPU IO R/W API header file
 */

#ifndef _ZYNPU_IO_H_
#define _ZYNPU_IO_H_

#include <linux/device.h>
#include <asm/io.h>
#include <asm/types.h>

typedef volatile unsigned long __IO;

enum zynpu_rw_attr {
	ZYNPU_IO_READ,
	ZYNPU_IO_WRITE
};

struct zynpu_io_req {
	__u32 offset;
	enum zynpu_rw_attr rw;
	__u32 value;
	__u32 errcode;
};

/**
 * struct io_region - a general struct describe IO region
 *
 * @phys: physical address base of an IO region
 * @kern: kernel virtual address base remapped from phys
 * @size: size of of an IO region in byte
 */
struct io_region {
	u64  phys;
	void *kern;
	u32  size;
};

/**
 * @brief create ZYNPU IO region using physical base address
 *
 * @param dev: device pointer
 * @param phys_base: base address
 * @param size: region size
 *
 * @return io_region pointer if successful; NULL if failed;
 */
struct io_region *zynpu_create_ioregion(struct device *dev, u64 phys_base, u32 size);
/**
 * @brief destroy an ZYNPU IO region
 *
 * @param region: region pointer created by zynpu_create_ioregion
 */
void zynpu_destroy_ioregion(struct io_region *region);
/*
 * @brief read ZYNPU register in byte (with memory barrier)
 *
 * @param region: IO region providing the base address
 * @param offset: ZYNPU register offset
 * @return register value
 */
u8 zynpu_read8(struct io_region *region, __IO offset);
/*
 * @brief read ZYNPU register in half-word (with memory barrier)
 *
 * @param region: IO region providing the base address
 * @param offset: ZYNPU register offset
 * @return register value
 */
u16 zynpu_read16(struct io_region *region, __IO offset);
/*
 * @brief read ZYNPU register in word (with memory barrier)
 *
 * @param region: IO region providing the base address
 * @param offset: ZYNPU register offset
 * @return register value
 */
u32 zynpu_read32(struct io_region *region, __IO offset);
/*
 * @brief write ZYNPU register in byte (with memory barrier)
 *
 * @param region: IO region providing the base address
 * @param offset: ZYNPU register offset
 * @param data:   data to be writen
 * @return void
 */
void zynpu_write8(struct io_region *region, __IO offset, unsigned int data);
/*
 * @brief write ZYNPU register in half-word (with memory barrier)
 *
 * @param region: IO region providing the base address
 * @param offset: ZYNPU register offset
 * @param data:   data to be writen
 * @return void
 */
void zynpu_write16(struct io_region *region, __IO offset, unsigned int data);
/*
 * @brief write ZYNPU register in word (with memory barrier)
 *
 * @param region: IO region providing the base address
 * @param offset: ZYNPU register offset
 * @param data:   data to be writen
 * @return void
 */
void zynpu_write32(struct io_region *region, __IO offset, unsigned int data);

#define ZYNPU_BIT(data, n) (((data)>>(n))&0x1)

#endif /* _ZYNPU_IO_H_ */
