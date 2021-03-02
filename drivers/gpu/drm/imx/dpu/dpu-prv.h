/* SPDX-License-Identifier: GPL-2.0+ */

/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2020 NXP
 */

#ifndef __DPU_PRV_H__
#define __DPU_PRV_H__

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/irqdomain.h>

#include "dpu.h"

/* DPU common control registers */
#define DPU_COMCTRL_REG(offset)		(offset)

#define IPIDENTIFIER			DPU_COMCTRL_REG(0x0)
#define LOCKUNLOCK			DPU_COMCTRL_REG(0x40)
#define LOCKSTATUS			DPU_COMCTRL_REG(0x44)
#define USERINTERRUPTMASK(n)		DPU_COMCTRL_REG(0x48 + 4 * (n))
#define INTERRUPTENABLE(n)		DPU_COMCTRL_REG(0x50 + 4 * (n))
#define INTERRUPTPRESET(n)		DPU_COMCTRL_REG(0x58 + 4 * (n))
#define INTERRUPTCLEAR(n)		DPU_COMCTRL_REG(0x60 + 4 * (n))
#define INTERRUPTSTATUS(n)		DPU_COMCTRL_REG(0x68 + 4 * (n))
#define USERINTERRUPTENABLE(n)		DPU_COMCTRL_REG(0x80 + 4 * (n))
#define USERINTERRUPTPRESET(n)		DPU_COMCTRL_REG(0x88 + 4 * (n))
#define USERINTERRUPTCLEAR(n)		DPU_COMCTRL_REG(0x90 + 4 * (n))
#define USERINTERRUPTSTATUS(n)		DPU_COMCTRL_REG(0x98 + 4 * (n))
#define GENERALPURPOSE			DPU_COMCTRL_REG(0x100)

#define DPU_SAFETY_STREAM_OFFSET	4

/* shadow enable bit for several DPU units */
#define SHDEN				BIT(0)

/* Pixel Engine Configuration register fields */
#define CLKEN_MASK_SHIFT		24
#define CLKEN_MASK			(0x3 << CLKEN_MASK_SHIFT)
#define CLKEN(n)			((n) << CLKEN_MASK_SHIFT)

/* H/Vscaler register fields */
#define SCALE_FACTOR_MASK		0xfffff
#define SCALE_FACTOR(n)			((n) & 0xfffff)
#define PHASE_OFFSET_MASK		0x1fffff
#define PHASE_OFFSET(n)			((n) & 0x1fffff)
#define OUTPUT_SIZE_MASK		0x3fff0000
#define OUTPUT_SIZE(n)			((((n) - 1) << 16) & OUTPUT_SIZE_MASK)
#define FILTER_MODE_MASK		0x100
#define FILTER_MODE(n)			((n) << 8)
#define SCALE_MODE_MASK			0x10
#define SCALE_MODE(n)			((n) << 4)

enum dpu_irq {
	DPU_IRQ_STORE9_SHDLOAD		 = 0,
	DPU_IRQ_STORE9_FRAMECOMPLETE	 = 1,
	DPU_IRQ_STORE9_SEQCOMPLETE	 = 2,
	DPU_IRQ_EXTDST0_SHDLOAD		 = 3,
	DPU_IRQ_EXTDST0_FRAMECOMPLETE	 = 4,
	DPU_IRQ_EXTDST0_SEQCOMPLETE	 = 5,
	DPU_IRQ_EXTDST4_SHDLOAD		 = 6,
	DPU_IRQ_EXTDST4_FRAMECOMPLETE	 = 7,
	DPU_IRQ_EXTDST4_SEQCOMPLETE	 = 8,
	DPU_IRQ_EXTDST1_SHDLOAD		 = 9,
	DPU_IRQ_EXTDST1_FRAMECOMPLETE	 = 10,
	DPU_IRQ_EXTDST1_SEQCOMPLETE	 = 11,
	DPU_IRQ_EXTDST5_SHDLOAD		 = 12,
	DPU_IRQ_EXTDST5_FRAMECOMPLETE	 = 13,
	DPU_IRQ_EXTDST5_SEQCOMPLETE	 = 14,
	DPU_IRQ_DISENGCFG_SHDLOAD0	 = 15,
	DPU_IRQ_DISENGCFG_FRAMECOMPLETE0 = 16,
	DPU_IRQ_DISENGCFG_SEQCOMPLETE0	 = 17,
	DPU_IRQ_FRAMEGEN0_INT0		 = 18,
	DPU_IRQ_FRAMEGEN0_INT1		 = 19,
	DPU_IRQ_FRAMEGEN0_INT2		 = 20,
	DPU_IRQ_FRAMEGEN0_INT3		 = 21,
	DPU_IRQ_SIG0_SHDLOAD		 = 22,
	DPU_IRQ_SIG0_VALID		 = 23,
	DPU_IRQ_SIG0_ERROR		 = 24,
	DPU_IRQ_DISENGCFG_SHDLOAD1	 = 25,
	DPU_IRQ_DISENGCFG_FRAMECOMPLETE1 = 26,
	DPU_IRQ_DISENGCFG_SEQCOMPLETE1	 = 27,
	DPU_IRQ_FRAMEGEN1_INT0		 = 28,
	DPU_IRQ_FRAMEGEN1_INT1		 = 29,
	DPU_IRQ_FRAMEGEN1_INT2		 = 30,
	DPU_IRQ_FRAMEGEN1_INT3		 = 31,
	DPU_IRQ_SIG1_SHDLOAD		 = 32,
	DPU_IRQ_SIG1_VALID		 = 33,
	DPU_IRQ_SIG1_ERROR		 = 34,
	DPU_IRQ_RESERVED		 = 35,
	DPU_IRQ_CMDSEQ_ERROR		 = 36,
	DPU_IRQ_COMCTRL_SW0		 = 37,
	DPU_IRQ_COMCTRL_SW1		 = 38,
	DPU_IRQ_COMCTRL_SW2		 = 39,
	DPU_IRQ_COMCTRL_SW3		 = 40,
	DPU_IRQ_FRAMEGEN0_PRIMSYNC_ON	 = 41,
	DPU_IRQ_FRAMEGEN0_PRIMSYNC_OFF	 = 42,
	DPU_IRQ_FRAMEGEN0_SECSYNC_ON	 = 43,
	DPU_IRQ_FRAMEGEN0_SECSYNC_OFF	 = 44,
	DPU_IRQ_FRAMEGEN1_PRIMSYNC_ON	 = 45,
	DPU_IRQ_FRAMEGEN1_PRIMSYNC_OFF	 = 46,
	DPU_IRQ_FRAMEGEN1_SECSYNC_ON	 = 47,
	DPU_IRQ_FRAMEGEN1_SECSYNC_OFF	 = 48,
	DPU_IRQ_COUNT			 = 49,
};

enum dpu_unit_type {
	DPU_DISP,
	DPU_BLIT,
};

struct dpu_soc {
	struct device		*dev;

	struct device		*pd_dc_dev;
	struct device		*pd_pll0_dev;
	struct device		*pd_pll1_dev;
	struct device_link	*pd_dc_link;
	struct device_link	*pd_pll0_link;
	struct device_link	*pd_pll1_link;

	void __iomem		*comctrl_reg;

	struct clk		*clk_cfg;
	struct clk		*clk_axi;

	int			id;

	int			irq[DPU_IRQ_COUNT];

	struct irq_domain	*domain;

	struct dpu_constframe	*cf_priv[4];
	struct dpu_disengcfg	*dec_priv[2];
	struct dpu_extdst	*ed_priv[4];
	struct dpu_fetchunit	*fd_priv[3];
	struct dpu_fetchunit	*fe_priv[4];
	struct dpu_framegen	*fg_priv[2];
	struct dpu_fetchunit	*fl_priv[1];
	struct dpu_fetchunit	*fw_priv[2];
	struct dpu_gammacor	*gc_priv[2];
	struct dpu_hscaler	*hs_priv[3];
	struct dpu_layerblend	*lb_priv[4];
	struct dpu_tcon		*tcon_priv[2];
	struct dpu_vscaler	*vs_priv[3];
};

struct dpu_units {
	const unsigned int *ids;
	const enum dpu_unit_type *types;
	const unsigned long *ofss;
	const unsigned long *pec_ofss;	/* Pixel Engine Configuration */
	const unsigned int cnt;
	const char *name;

	/* software initialization */
	int (*init)(struct dpu_soc *dpu, unsigned int index,
		    unsigned int id, enum dpu_unit_type type,
		    unsigned long pec_base, unsigned long base);

	/* hardware initialization */
	void (*hw_init)(struct dpu_soc *dpu, unsigned int index);
};

void dpu_cf_hw_init(struct dpu_soc *dpu, unsigned int index);
void dpu_dec_hw_init(struct dpu_soc *dpu, unsigned int index);
void dpu_ed_hw_init(struct dpu_soc *dpu, unsigned int index);
void dpu_fd_hw_init(struct dpu_soc *dpu, unsigned int index);
void dpu_fe_hw_init(struct dpu_soc *dpu, unsigned int index);
void dpu_fg_hw_init(struct dpu_soc *dpu, unsigned int index);
void dpu_fl_hw_init(struct dpu_soc *dpu, unsigned int index);
void dpu_fw_hw_init(struct dpu_soc *dpu, unsigned int index);
void dpu_gc_hw_init(struct dpu_soc *dpu, unsigned int index);
void dpu_hs_hw_init(struct dpu_soc *dpu, unsigned int index);
void dpu_lb_hw_init(struct dpu_soc *dpu, unsigned int index);
void dpu_tcon_hw_init(struct dpu_soc *dpu, unsigned int index);
void dpu_vs_hw_init(struct dpu_soc *dpu, unsigned int index);

int dpu_cf_init(struct dpu_soc *dpu, unsigned int index,
		unsigned int id, enum dpu_unit_type type,
		unsigned long pec_base, unsigned long base);

int dpu_dec_init(struct dpu_soc *dpu, unsigned int index,
		 unsigned int id, enum dpu_unit_type type,
		 unsigned long unused, unsigned long base);

int dpu_ed_init(struct dpu_soc *dpu, unsigned int index,
		unsigned int id, enum dpu_unit_type type,
		unsigned long pec_base, unsigned long base);

int dpu_fd_init(struct dpu_soc *dpu, unsigned int index,
		unsigned int id, enum dpu_unit_type type,
		unsigned long pec_base, unsigned long base);

int dpu_fe_init(struct dpu_soc *dpu, unsigned int index,
		unsigned int id, enum dpu_unit_type type,
		unsigned long pec_base, unsigned long base);

int dpu_fg_init(struct dpu_soc *dpu, unsigned int index,
		unsigned int id, enum dpu_unit_type type,
		unsigned long unused, unsigned long base);

int dpu_fl_init(struct dpu_soc *dpu, unsigned int index,
		unsigned int id, enum dpu_unit_type type,
		unsigned long pec_base, unsigned long base);

int dpu_fw_init(struct dpu_soc *dpu, unsigned int index,
		unsigned int id, enum dpu_unit_type type,
		unsigned long pec_base, unsigned long base);

int dpu_gc_init(struct dpu_soc *dpu, unsigned int index,
		unsigned int id, enum dpu_unit_type type,
		unsigned long unused, unsigned long base);

int dpu_hs_init(struct dpu_soc *dpu, unsigned int index,
		unsigned int id, enum dpu_unit_type type,
		unsigned long pec_base, unsigned long base);

int dpu_lb_init(struct dpu_soc *dpu, unsigned int index,
		unsigned int id, enum dpu_unit_type type,
		unsigned long pec_base, unsigned long base);

int dpu_tcon_init(struct dpu_soc *dpu, unsigned int index,
		  unsigned int id, enum dpu_unit_type type,
		  unsigned long unused, unsigned long base);

int dpu_vs_init(struct dpu_soc *dpu, unsigned int index,
		unsigned int id, enum dpu_unit_type type,
		unsigned long pec_base, unsigned long base);

#endif /* __DPU_PRV_H__ */
