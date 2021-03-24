/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2017-2021 Intel Corporation */

#ifndef __GNA_HW_H__
#define __GNA_HW_H__

#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/io.h>

/* GNA MMIO registers */
#define GNA_MMIO_STS		0x80
#define GNA_MMIO_CTRL		0x84
#define GNA_MMIO_PTC		0x8C
#define GNA_MMIO_PSC		0x90
#define GNA_MMIO_DESBASE	0xB0
#define GNA_MMIO_IBUFFS		0xB4

#define GNA_PT_ENTRY_SIZE		4
/* there are up to 1024 32-bit pointers in one page in Page Table (L1) */
#define GNA_PT_LENGTH           (PAGE_SIZE / GNA_PT_ENTRY_SIZE)

#define GNA_PGDIRN_LEN			64
#define GNA_PGDIR_ENTRIES		1024 /* 32-bit page addresses */
#define GNA_PGDIR_INVALID		1

#define GNA_CTRL_START_ACCEL		BIT(0)
#define GNA_CTRL_ACTIVE_LIST_EN		BIT(1)
#define GNA_CTRL_ABORT_CLR_ACCEL	BIT(2)
#define GNA_CTRL_OP_MODE		GENMASK(6, 5)
#define GNA_CTRL_COMP_INT_EN		BIT(8)
#define GNA_CTRL_ERR_INT_EN		BIT(10)
#define GNA_CTRL_COMP_STATS_EN		GENMASK(15, 12)

struct gna_mmu_info {
	u32 vamax_size;
	u32 rsvd_size;
	u32 pd_size;
};

struct gna_desc_info {
	u32 rsvd_size;
	u32 cfg_size;
	u32 desc_size;
	struct gna_mmu_info mmu_info;
};

struct gna_private;
struct gna_compute_cfg;

void gna_abort_hw(struct gna_private *gna_priv);
bool gna_hw_perf_enabled(struct gna_private *gna_priv);
int gna_parse_hw_status(struct gna_private *gna_priv, u32 hw_status);
void gna_print_error_status(struct gna_private *gna_priv, u32 hw_status);
void gna_start_scoring(struct gna_private *gna_priv, void __iomem *addr,
		       struct gna_compute_cfg *compute_cfg);

#define gna_reg_read(addr, offset)		readl((addr) + (offset))
#define gna_reg_write(addr, offset, value)	writel((value), (addr) + (offset))

#endif // __GNA_HW_H__
