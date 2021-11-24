/* SPDX-License-Identifier: GPL-2.0+ */
/*
*
* Zhouyi AI Accelerator driver
*
* Copyright (C) 2020 Arm (China) Ltd.
* Copyright (C) 2021 Cai Huoqing
*/

/**
 * @file zhouyi.h
 * Header of the zhouyi ZYNPU hardware control and interrupt handle operations
 */

#ifndef _ZYNPU_ZHOUYI_H_
#define _ZYNPU_ZHOUYI_H_

#include <linux/device.h>
#include "zynpu_io.h"

/**
 * Zhouyi ZYNPU Common Interrupts
 */
#define ZHOUYI_IRQ_NONE   0x0
#define ZHOUYI_IRQ_QEMPTY 0x1
#define ZHOUYI_IRQ_DONE   0x2
#define ZHOUYI_IRQ_EXCEP  0x4

#define ZHOUYI_IRQ  (ZHOUYI_IRQ_QEMPTY | ZHOUYI_IRQ_DONE | ZHOUYI_IRQ_EXCEP)

#define ZHOUYI_ZYNPU_IDLE_STATUS   0x70000

/**
 * Zhouyi ZYNPU Common Host Control Register Map
 */
#define ZHOUYI_CTRL_REG_OFFSET		0x0
#define ZHOUYI_STAT_REG_OFFSET		0x4
#define ZHOUYI_START_PC_REG_OFFSET	    0x8
#define ZHOUYI_INTR_PC_REG_OFFSET	     0xC
#define ZHOUYI_IPI_CTRL_REG_OFFSET	    0x10
#define ZHOUYI_DATA_ADDR_0_REG_OFFSET	 0x14
#define ZHOUYI_DATA_ADDR_1_REG_OFFSET	 0x18
#define ZHOUYI_CLK_CTRL_REG_OFFSET	    0x3C
#define ZHOUYI_ISA_VERSION_REG_OFFSET	 0x40
#define ZHOUYI_TPC_FEATURE_REG_OFFSET	 0x44
#define ZHOUYI_SPU_FEATURE_REG_OFFSET	 0x48
#define ZHOUYI_HWA_FEATURE_REG_OFFSET	 0x4C
#define ZHOUYI_REVISION_ID_REG_OFFSET	 0x50
#define ZHOUYI_MEM_FEATURE_REG_OFFSET	 0x54
#define ZHOUYI_INST_RAM_FEATURE_REG_OFFSET    0x58
#define ZHOUYI_LOCAL_SRAM_FEATURE_REG_OFFSET  0x5C
#define ZHOUYI_GLOBAL_SRAM_FEATURE_REG_OFFSET 0x60
#define ZHOUYI_INST_CACHE_FEATURE_REG_OFFSET  0x64
#define ZHOUYI_DATA_CACHE_FEATURE_REG_OFFSET  0x68

struct zynpu_cap {
	__u32 isa_version;
	__u32 tpc_feature;
	__u32 aiff_feature;
	__u32 errcode;
};

int zhouyi_read_status_reg(struct io_region* io);
void zhouyi_clear_qempty_interrupt(struct io_region* io);
void zhouyi_clear_done_interrupt(struct io_region* io);
void zhouyi_clear_excep_interrupt(struct io_region* io);
int zhouyi_query_cap(struct io_region* io, struct zynpu_cap* cap);
void zhouyi_io_rw(struct io_region* io, struct zynpu_io_req* io_req);

#endif /* _ZYNPU_ZHOUYI_H_ */