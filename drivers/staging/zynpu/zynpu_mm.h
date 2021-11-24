/* SPDX-License-Identifier: GPL-2.0+ */
/*
*
* Zhouyi AI Accelerator driver
*
* Copyright (C) 2020 Arm (China) Ltd.
* Copyright (C) 2021 Cai Huoqing
*/

/**
 * @file zynpu_mm.h
 * Header of the ZYNPU memory management supports Address Space Extension (ASE)
 */

#ifndef _ZYNPU_MM_H_
#define _ZYNPU_MM_H_

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include "zynpu_session.h"

struct zynpu_mem_region;
struct zynpu_memory_manager;
typedef int (*alloc_in_region_t)(struct zynpu_memory_manager *mm, struct zynpu_mem_region *region,
	struct buf_request *buf_req, struct zynpu_buffer *buf);

enum zynpu_blk_state {
	ZYNPU_BLOCK_STATE_FREE,
	ZYNPU_BLOCK_STATE_ALLOCATED,
};

enum zynpu_asid {
	ZYNPU_ASE_ID_NONE = 0x0,
	ZYNPU_ASE_ID_0 = 0x1,
	ZYNPU_ASE_ID_1 = 0x2,
	ZYNPU_ASE_ID_2 = 0x4,
	ZYNPU_ASE_ID_3 = 0x8,
	ZYNPU_ASE_ID_ALL = 0xF,
};

enum zynpu_mem_type {
	ZYNPU_MEM_TYPE_SRAM,
	ZYNPU_MEM_TYPE_CMA,
	ZYNPU_MEM_TYPE_RESERVED,
};

struct zynpu_block {
	u64 pa;
	u64 bytes;
	int tid;
	enum zynpu_mm_data_type type;
	enum zynpu_blk_state state;
	struct list_head list;
};

struct zynpu_mem_region {
	int id;
	struct zynpu_block *blk_head;
	struct mutex lock;
	u64 pa;
	void *va;
	u64 tot_bytes;
	u64 tot_free_bytes;
	//int flag;
	enum zynpu_mem_type type;
	alloc_in_region_t alloc_in_region;
	struct list_head list;
};

struct zynpu_memory_manager {
	struct zynpu_mem_region *sram_head;
	int sram_cnt;
	struct zynpu_mem_region *ddr_head;
	int ddr_cnt;
	enum zynpu_asid sram_global;
	struct device *dev;
	int version;
};

/*
 * @brief initialize mm module during driver probe phase
 *
 * @param mm: memory manager struct allocated by user
 * @param dev: device struct pointer
 * @param zynpu_version: ZYNPU version
 *
 * @return 0 if successful; others if failed.
 */
int zynpu_init_mm(struct zynpu_memory_manager *mm, struct device *dev, int version);
/*
 * @brief initialize mm module during driver probe phase
 *
 * @param mm: memory manager struct allocated by user
 * @param base: base physical address of this region
 * @param bytes: size of this region (in bytes)
 * @param type: ZYNPU memory type
 *
 * @return 0 if successful; others if failed.
 */
int zynpu_mm_add_region(struct zynpu_memory_manager *mm, u64 base, u64 bytes,
	enum zynpu_mem_type type);
/*
 * @brief alloc memory buffer for user request
 *
 * @param mm: memory manager struct allocated by user
 * @param buf_req:  buffer request struct from userland
 * @param buf: successfully allocated buffer descriptor
 *
 * @return 0 if successful; others if failed.
 */
int zynpu_mm_alloc(struct zynpu_memory_manager *mm, struct buf_request *buf_req,
	struct zynpu_buffer *buf);
/*
 * @brief free buffer allocated by zynpu_mm_alloc
 *
 * @param mm: memory manager struct allocated by user
 * @param buf: buffer descriptor to be released
 *
 * @return 0 if successful; others if failed.
 */
int zynpu_mm_free(struct zynpu_memory_manager *mm, struct buf_desc *buf);
/*
 * @brief free all the allocated buffers of a session
 *
 * @param mm: mm struct pointer
 * @param session: session struct pointer
 *
 * @return 0 if successful; others if failed.
 */
int zynpu_mm_free_session_buffers(struct zynpu_memory_manager *mm,
	struct zynpu_session *session);
/*
 * @brief de-initialize mm module while kernel module unloading
 *
 * @param mm: memory manager struct allocated by user
 *
 * @return void
 */
void zynpu_deinit_mm(struct zynpu_memory_manager *mm);

#endif /* _ZYNPU_MM_H_ */