// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2020 NXP
 */

#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/sizes.h>

#include "dpu.h"
#include "dpu-prv.h"

static inline u32 dpu_comctrl_read(struct dpu_soc *dpu, unsigned int offset)
{
	return readl(dpu->comctrl_reg + offset);
}

static inline void dpu_comctrl_write(struct dpu_soc *dpu,
				     unsigned int offset, u32 value)
{
	writel(value, dpu->comctrl_reg + offset);
}

/* Constant Frame */
static const unsigned int cf_ids[] = {0, 1, 4, 5};
static const enum dpu_unit_type cf_types[] = {DPU_DISP, DPU_DISP,
					      DPU_DISP, DPU_DISP};
static const unsigned long cf_ofss[] = {0x4400, 0x5400, 0x4c00, 0x5c00};
static const unsigned long cf_pec_ofss[] = {0x960, 0x9e0, 0x9a0, 0xa20};

static const struct dpu_units dpu_cfs = {
	.ids = cf_ids,
	.types = cf_types,
	.ofss = cf_ofss,
	.pec_ofss = cf_pec_ofss,
	.cnt = ARRAY_SIZE(cf_ids),
	.name = "ConstFrame",
	.init = dpu_cf_init,
	.hw_init = dpu_cf_hw_init,
};

/* Display Engine Configuration */
static const unsigned int dec_ids[] = {0, 1};
static const enum dpu_unit_type dec_types[] = {DPU_DISP, DPU_DISP};
static const unsigned long dec_ofss[] = {0xb400, 0xb420};

static const struct dpu_units dpu_decs = {
	.ids = dec_ids,
	.types = dec_types,
	.ofss = dec_ofss,
	.pec_ofss = NULL,
	.cnt = ARRAY_SIZE(dec_ids),
	.name = "DisEngCfg",
	.init = dpu_dec_init,
	.hw_init = dpu_dec_hw_init,
};

/* External Destination */
static const unsigned int ed_ids[] = {0, 1, 4, 5};
static const enum dpu_unit_type ed_types[] = {DPU_DISP, DPU_DISP,
					      DPU_DISP, DPU_DISP};
static const unsigned long ed_ofss[] = {0x4800, 0x5800, 0x5000, 0x6000};
static const unsigned long ed_pec_ofss[] = {0x980, 0xa00, 0x9c0, 0xa40};

static const struct dpu_units dpu_eds = {
	.ids = ed_ids,
	.types = ed_types,
	.ofss = ed_ofss,
	.pec_ofss = ed_pec_ofss,
	.cnt = ARRAY_SIZE(ed_ids),
	.name = "ExtDst",
	.init = dpu_ed_init,
	.hw_init = dpu_ed_hw_init,
};

/* Fetch Decode */
static const unsigned int fd_ids[] = {0, 1, 9};
static const enum dpu_unit_type fd_types[] = {DPU_DISP, DPU_DISP, DPU_BLIT};
static const unsigned long fd_ofss[] = {0x6c00, 0x7800, 0x1000};
static const unsigned long fd_pec_ofss[] = {0xa80, 0xaa0, 0x820};

static const struct dpu_units dpu_fds = {
	.ids = fd_ids,
	.types = fd_types,
	.ofss = fd_ofss,
	.pec_ofss = fd_pec_ofss,
	.cnt = ARRAY_SIZE(fd_ids),
	.name = "FetchDecode",
	.init = dpu_fd_init,
	.hw_init = dpu_fd_hw_init,
};

/* Fetch ECO */
static const unsigned int fe_ids[] = {0, 1, 2, 9};
static const enum dpu_unit_type fe_types[] = {DPU_DISP, DPU_DISP,
					      DPU_DISP, DPU_BLIT};
static const unsigned long fe_ofss[] = {0x7400, 0x8000, 0x6800, 0x1c00};
static const unsigned long fe_pec_ofss[] = {0xa90, 0xab0, 0xa70, 0x850};

static const struct dpu_units dpu_fes = {
	.ids = fe_ids,
	.types = fe_types,
	.ofss = fe_ofss,
	.pec_ofss = fe_pec_ofss,
	.cnt = ARRAY_SIZE(fe_ids),
	.name = "FetchEco",
	.init = dpu_fe_init,
	.hw_init = dpu_fe_hw_init,
};

/* Frame Generator */
static const unsigned int fg_ids[] = {0, 1};
static const enum dpu_unit_type fg_types[] = {DPU_DISP, DPU_DISP};
static const unsigned long fg_ofss[] = {0xb800, 0xd400};

static const struct dpu_units dpu_fgs = {
	.ids = fg_ids,
	.types = fg_types,
	.ofss = fg_ofss,
	.pec_ofss = NULL,
	.cnt = ARRAY_SIZE(fg_ids),
	.name = "FrameGen",
	.init = dpu_fg_init,
	.hw_init = dpu_fg_hw_init,
};

/* Fetch Layer */
static const unsigned int fl_ids[] = {0};
static const enum dpu_unit_type fl_types[] = {DPU_DISP};
static const unsigned long fl_ofss[] = {0x8400};
static const unsigned long fl_pec_ofss[] = {0xac0};

static const struct dpu_units dpu_fls = {
	.ids = fl_ids,
	.types = fl_types,
	.ofss = fl_ofss,
	.pec_ofss = fl_pec_ofss,
	.cnt = ARRAY_SIZE(fl_ids),
	.name = "FetchLayer",
	.init = dpu_fl_init,
	.hw_init = dpu_fl_hw_init,
};

/* Fetch Warp */
static const unsigned int fw_ids[] = {2, 9};
static const enum dpu_unit_type fw_types[] = {DPU_DISP, DPU_BLIT};
static const unsigned long fw_ofss[] = {0x6400, 0x1800};
static const unsigned long fw_pec_ofss[] = {0xa60, 0x840};

static const struct dpu_units dpu_fws = {
	.ids = fw_ids,
	.types = fw_types,
	.ofss = fw_ofss,
	.pec_ofss = fw_pec_ofss,
	.cnt = ARRAY_SIZE(fw_ids),
	.name = "FetchWarp",
	.init = dpu_fw_init,
	.hw_init = dpu_fw_hw_init,
};

/* Gamma Correction */
static const unsigned int gc_ids[] = {0, 1};
static const enum dpu_unit_type gc_types[] = {DPU_DISP, DPU_DISP};
static const unsigned long gc_ofss[] = {0xc000, 0xdc00};

static const struct dpu_units dpu_gcs = {
	.ids = gc_ids,
	.types = gc_types,
	.ofss = gc_ofss,
	.pec_ofss = NULL,
	.cnt = ARRAY_SIZE(gc_ids),
	.name = "GammaCor",
	.init = dpu_gc_init,
	.hw_init = dpu_gc_hw_init,
};

/* Horizontal Scaler */
static const unsigned int hs_ids[] = {4, 5, 9};
static const enum dpu_unit_type hs_types[] = {DPU_DISP, DPU_DISP, DPU_BLIT};
static const unsigned long hs_ofss[] = {0x9000, 0x9c00, 0x3000};
static const unsigned long hs_pec_ofss[] = {0xb00, 0xb60, 0x8c0};

static const struct dpu_units dpu_hss = {
	.ids = hs_ids,
	.types = hs_types,
	.ofss = hs_ofss,
	.pec_ofss = hs_pec_ofss,
	.cnt = ARRAY_SIZE(hs_ids),
	.name = "HScaler",
	.init = dpu_hs_init,
	.hw_init = dpu_hs_hw_init,
};

/* Layer Blend */
static const unsigned int lb_ids[] = {0, 1, 2, 3};
static const enum dpu_unit_type lb_types[] = {DPU_DISP, DPU_DISP,
					      DPU_DISP, DPU_DISP};
static const unsigned long lb_ofss[] = {0xa400, 0xa800, 0xac00, 0xb000};
static const unsigned long lb_pec_ofss[] = {0xba0, 0xbc0, 0xbe0, 0xc00};

static const struct dpu_units dpu_lbs = {
	.ids = lb_ids,
	.types = lb_types,
	.ofss = lb_ofss,
	.pec_ofss = lb_pec_ofss,
	.cnt = ARRAY_SIZE(lb_ids),
	.name = "LayerBlend",
	.init = dpu_lb_init,
	.hw_init = dpu_lb_hw_init,
};

/* Timing Controller */
static const unsigned int tcon_ids[] = {0, 1};
static const enum dpu_unit_type tcon_types[] = {DPU_DISP, DPU_DISP};
static const unsigned long tcon_ofss[] = {0xc800, 0xe400};

static const struct dpu_units dpu_tcons = {
	.ids = tcon_ids,
	.types = tcon_types,
	.ofss = tcon_ofss,
	.pec_ofss = NULL,
	.cnt = ARRAY_SIZE(tcon_ids),
	.name = "TCON",
	.init = dpu_tcon_init,
	.hw_init = dpu_tcon_hw_init,
};

/* Vertical Scaler */
static const unsigned int vs_ids[] = {4, 5, 9};
static const enum dpu_unit_type vs_types[] = {DPU_DISP, DPU_DISP, DPU_BLIT};
static const unsigned long vs_ofss[] = {0x9400, 0xa000, 0x3400};
static const unsigned long vs_pec_ofss[] = {0xb20, 0xb80, 0x8e0};

static const struct dpu_units dpu_vss = {
	.ids = vs_ids,
	.types = vs_types,
	.ofss = vs_ofss,
	.pec_ofss = vs_pec_ofss,
	.cnt = ARRAY_SIZE(vs_ids),
	.name = "VScaler",
	.init = dpu_vs_init,
	.hw_init = dpu_vs_hw_init,
};

static const struct dpu_units *dpu_all_units[] = {
	&dpu_cfs,
	&dpu_decs,
	&dpu_eds,
	&dpu_fds,
	&dpu_fes,
	&dpu_fgs,
	&dpu_fls,
	&dpu_fws,
	&dpu_gcs,
	&dpu_hss,
	&dpu_lbs,
	&dpu_tcons,
	&dpu_vss,
};

static inline void dpu_detach_pm_domain(struct device **pd_dev,
					struct device_link **pd_link)
{
	if (!IS_ERR_OR_NULL(*pd_link))
		device_link_del(*pd_link);
	if (!IS_ERR_OR_NULL(*pd_dev))
		dev_pm_domain_detach(*pd_dev, true);

	*pd_dev = NULL;
	*pd_link = NULL;
}

static void dpu_detach_pm_domains(struct dpu_soc *dpu)
{
	dpu_detach_pm_domain(&dpu->pd_pll1_dev, &dpu->pd_pll1_link);
	dpu_detach_pm_domain(&dpu->pd_pll0_dev, &dpu->pd_pll0_link);
	dpu_detach_pm_domain(&dpu->pd_dc_dev, &dpu->pd_dc_link);
}

static inline int dpu_attach_pm_domain(struct dpu_soc *dpu,
				       struct device **pd_dev,
				       struct device_link **pd_link,
				       const char *name)
{
	u32 flags = DL_FLAG_STATELESS | DL_FLAG_PM_RUNTIME | DL_FLAG_RPM_ACTIVE;
	int ret;

	*pd_dev = dev_pm_domain_attach_by_name(dpu->dev, name);
	if (IS_ERR(*pd_dev)) {
		ret = PTR_ERR(*pd_dev);
		dev_err(dpu->dev,
			"failed to attach %s pd dev: %d\n", name, ret);
		return ret;
	}

	*pd_link = device_link_add(dpu->dev, *pd_dev, flags);
	if (IS_ERR(*pd_link)) {
		ret = PTR_ERR(*pd_link);
		dev_err(dpu->dev, "failed to add device link to %s pd dev: %d\n",
							name, ret);
		return ret;
	}

	return 0;
}

static int dpu_attach_pm_domains(struct dpu_soc *dpu)
{
	int ret;

	ret = dpu_attach_pm_domain(dpu, &dpu->pd_dc_dev, &dpu->pd_dc_link, "dc");
	if (ret)
		goto err_out;

	ret = dpu_attach_pm_domain(dpu, &dpu->pd_pll0_dev, &dpu->pd_pll0_link,
									"pll0");
	if (ret)
		goto err_out;

	ret = dpu_attach_pm_domain(dpu, &dpu->pd_pll1_dev, &dpu->pd_pll1_link,
									"pll1");
	if (ret)
		goto err_out;

	return 0;
err_out:
	dpu_detach_pm_domains(dpu);

	return ret;
}

static void dpu_units_addr_dbg(struct dpu_soc *dpu, unsigned long dpu_base)
{
	const struct dpu_units *us;
	int i, j;

	dev_dbg(dpu->dev, "Common Control: 0x%08lx\n", dpu_base);

	for (i = 0; i < ARRAY_SIZE(dpu_all_units); i++) {
		us = dpu_all_units[i];

		for (j = 0; j < us->cnt; j++) {
			if (us->pec_ofss) {
				dev_dbg(dpu->dev,
				  "%s%d: pixengcfg @ 0x%08lx, unit @ 0x%08lx\n",
					us->name, us->ids[j],
					dpu_base + us->pec_ofss[j],
					dpu_base + us->ofss[j]);
			} else {
				dev_dbg(dpu->dev,
					"%s%d: unit @ 0x%08lx\n", us->name,
					us->ids[j], dpu_base + us->ofss[j]);
			}
		}
	}
}

static int dpu_get_irqs(struct platform_device *pdev, struct dpu_soc *dpu)
{
	unsigned int i, j;

	/* do not get the reserved irq */
	for (i = 0, j = 0; i < DPU_IRQ_COUNT - 1; i++, j++) {
		if (i == DPU_IRQ_RESERVED)
			j++;

		dpu->irq[j] = platform_get_irq(pdev, i);
		if (dpu->irq[j] < 0) {
			dev_err_probe(dpu->dev, dpu->irq[j],
				      "failed to get irq\n");
			return dpu->irq[j];
		}
	}

	return 0;
}

static void dpu_irq_handle(struct irq_desc *desc, enum dpu_irq irq)
{
	struct dpu_soc *dpu = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int virq;
	u32 status;

	chained_irq_enter(chip, desc);

	status = dpu_comctrl_read(dpu, USERINTERRUPTSTATUS(irq / 32));
	status &= dpu_comctrl_read(dpu, USERINTERRUPTENABLE(irq / 32));

	if (status & BIT(irq % 32)) {
		virq = irq_linear_revmap(dpu->domain, irq);
		if (virq)
			generic_handle_irq(virq);
	}

	chained_irq_exit(chip, desc);
}

static void dpu_dec_framecomplete0_irq_handler(struct irq_desc *desc)
{
	dpu_irq_handle(desc, DPU_IRQ_DISENGCFG_FRAMECOMPLETE0);
}

static void dpu_dec_framecomplete1_irq_handler(struct irq_desc *desc)
{
	dpu_irq_handle(desc, DPU_IRQ_DISENGCFG_FRAMECOMPLETE1);
}

static void dpu_dec_seqcomplete0_irq_handler(struct irq_desc *desc)
{
	dpu_irq_handle(desc, DPU_IRQ_DISENGCFG_SEQCOMPLETE0);
}

static void dpu_dec_seqcomplete1_irq_handler(struct irq_desc *desc)
{
	dpu_irq_handle(desc, DPU_IRQ_DISENGCFG_SEQCOMPLETE1);
}

static void dpu_dec_shdload0_irq_handler(struct irq_desc *desc)
{
	dpu_irq_handle(desc, DPU_IRQ_DISENGCFG_SHDLOAD0);
}

static void dpu_dec_shdload1_irq_handler(struct irq_desc *desc)
{
	dpu_irq_handle(desc, DPU_IRQ_DISENGCFG_SHDLOAD1);
}

static void dpu_ed0_shdload_irq_handler(struct irq_desc *desc)
{
	dpu_irq_handle(desc, DPU_IRQ_EXTDST0_SHDLOAD);
}

static void dpu_ed1_shdload_irq_handler(struct irq_desc *desc)
{
	dpu_irq_handle(desc, DPU_IRQ_EXTDST1_SHDLOAD);
}

static void dpu_ed4_shdload_irq_handler(struct irq_desc *desc)
{
	dpu_irq_handle(desc, DPU_IRQ_EXTDST4_SHDLOAD);
}

static void dpu_ed5_shdload_irq_handler(struct irq_desc *desc)
{
	dpu_irq_handle(desc, DPU_IRQ_EXTDST5_SHDLOAD);
}

static void (* const dpu_irq_handler[DPU_IRQ_COUNT])(struct irq_desc *desc) = {
	[DPU_IRQ_EXTDST0_SHDLOAD]          = dpu_ed0_shdload_irq_handler,
	[DPU_IRQ_EXTDST4_SHDLOAD]          = dpu_ed4_shdload_irq_handler,
	[DPU_IRQ_EXTDST1_SHDLOAD]          = dpu_ed1_shdload_irq_handler,
	[DPU_IRQ_EXTDST5_SHDLOAD]          = dpu_ed5_shdload_irq_handler,
	[DPU_IRQ_DISENGCFG_SHDLOAD0]       = dpu_dec_shdload0_irq_handler,
	[DPU_IRQ_DISENGCFG_FRAMECOMPLETE0] = dpu_dec_framecomplete0_irq_handler,
	[DPU_IRQ_DISENGCFG_SEQCOMPLETE0]   = dpu_dec_seqcomplete0_irq_handler,
	[DPU_IRQ_DISENGCFG_SHDLOAD1]       = dpu_dec_shdload1_irq_handler,
	[DPU_IRQ_DISENGCFG_FRAMECOMPLETE1] = dpu_dec_framecomplete1_irq_handler,
	[DPU_IRQ_DISENGCFG_SEQCOMPLETE1]   = dpu_dec_seqcomplete1_irq_handler,
	[DPU_IRQ_RESERVED]                 = NULL, /* do not use */
};

int dpu_map_irq(struct dpu_soc *dpu, int irq)
{
	int virq = irq_linear_revmap(dpu->domain, irq);

	if (!virq)
		virq = irq_create_mapping(dpu->domain, irq);

	return virq;
}

static const unsigned long unused_irq[2] = {0x00000000, 0xfffe0008};

static void dpu_irq_hw_init(struct dpu_soc *dpu)
{
	int i;

	for (i = 0; i < DPU_IRQ_COUNT; i += 32) {
		/* mask and clear all interrupts */
		dpu_comctrl_write(dpu, USERINTERRUPTENABLE(i / 32), 0);
		dpu_comctrl_write(dpu, USERINTERRUPTCLEAR(i / 32),
					~unused_irq[i / 32]);
		dpu_comctrl_write(dpu, INTERRUPTENABLE(i / 32), 0);
		dpu_comctrl_write(dpu, INTERRUPTCLEAR(i / 32),
					~unused_irq[i / 32]);

		/* set all interrupts to user mode */
		dpu_comctrl_write(dpu, USERINTERRUPTMASK(i / 32),
					~unused_irq[i / 32]);
	}
}

static int dpu_irq_init(struct dpu_soc *dpu)
{
	struct device *dev = dpu->dev;
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;
	int ret, i;

	dpu->domain = irq_domain_add_linear(dev->of_node, DPU_IRQ_COUNT,
					    &irq_generic_chip_ops, dpu);
	if (!dpu->domain) {
		dev_err(dev, "failed to add irq domain\n");
		return -ENODEV;
	}

	ret = irq_alloc_domain_generic_chips(dpu->domain, 32, 1, "DPU",
					     handle_level_irq, 0, 0, 0);
	if (ret) {
		dev_err(dev, "failed to alloc generic irq chips: %d\n", ret);
		irq_domain_remove(dpu->domain);
		return ret;
	}

	for (i = 0; i < DPU_IRQ_COUNT; i += 32) {
		gc = irq_get_domain_generic_chip(dpu->domain, i);
		gc->reg_base = dpu->comctrl_reg;
		gc->unused = unused_irq[i / 32];
		ct = gc->chip_types;
		ct->chip.irq_ack = irq_gc_ack_set_bit;
		ct->chip.irq_mask = irq_gc_mask_clr_bit;
		ct->chip.irq_unmask = irq_gc_mask_set_bit;
		ct->regs.ack = USERINTERRUPTCLEAR(i / 32);
		ct->regs.mask = USERINTERRUPTENABLE(i / 32);
	}

	for (i = 0; i < DPU_IRQ_COUNT; i++) {
		if (!dpu_irq_handler[i])
			continue;

		irq_set_chained_handler_and_data(dpu->irq[i],
						 dpu_irq_handler[i],
						 dpu);
	}

	return ret;
}

static void dpu_irq_exit(struct dpu_soc *dpu)
{
	unsigned int i, irq;

	for (i = 0; i < DPU_IRQ_COUNT; i++) {
		if (!dpu_irq_handler[i])
			continue;

		irq_set_chained_handler_and_data(dpu->irq[i], NULL, NULL);
	}

	for (i = 0; i < DPU_IRQ_COUNT; i++) {
		irq = irq_linear_revmap(dpu->domain, i);
		if (irq)
			irq_dispose_mapping(irq);
	}

	irq_domain_remove(dpu->domain);
}

static void dpu_submodules_hw_init(struct dpu_soc *dpu)
{
	const struct dpu_units *us;
	int i, j;

	for (i = 0; i < ARRAY_SIZE(dpu_all_units); i++) {
		us = dpu_all_units[i];

		for (j = 0; j < us->cnt; j++)
			us->hw_init(dpu, j);
	}
}

static int dpu_submodules_init(struct dpu_soc *dpu, unsigned long dpu_base)
{
	const struct dpu_units *us;
	unsigned long pec_ofs;
	int i, j, ret;

	for (i = 0; i < ARRAY_SIZE(dpu_all_units); i++) {
		us = dpu_all_units[i];

		for (j = 0; j < us->cnt; j++) {
			pec_ofs = us->pec_ofss ? dpu_base + us->pec_ofss[j] : 0;

			ret = us->init(dpu, j, us->ids[j], us->types[j],
				       pec_ofs, dpu_base + us->ofss[j]);
			if (ret) {
				dev_err(dpu->dev,
					"failed to initialize %s%d: %d\n",
						us->name, us->ids[j], ret);
				return ret;
			}
		}
	}

	return 0;
}

static int platform_remove_devices_fn(struct device *dev, void *unused)
{
	struct platform_device *pdev = to_platform_device(dev);

	platform_device_unregister(pdev);

	return 0;
}

static void platform_device_unregister_children(struct platform_device *pdev)
{
	device_for_each_child(&pdev->dev, NULL, platform_remove_devices_fn);
}

struct dpu_platform_reg {
	struct dpu_client_platformdata pdata;
	const char *name;
};

static struct dpu_platform_reg client_reg[] = {
	{
	  .pdata = {
		.stream_id = 0,
		.dec_frame_complete_irq	= DPU_IRQ_DISENGCFG_FRAMECOMPLETE0,
		.dec_seq_complete_irq	= DPU_IRQ_DISENGCFG_SEQCOMPLETE0,
		.dec_shdld_irq		= DPU_IRQ_DISENGCFG_SHDLOAD0,
		.ed_cont_shdld_irq	= DPU_IRQ_EXTDST0_SHDLOAD,
		.ed_safe_shdld_irq	= DPU_IRQ_EXTDST4_SHDLOAD,
	   },
	  .name = "imx-dpu-crtc",
	}, {
	  .pdata = {
		.stream_id = 1,
		.dec_frame_complete_irq	= DPU_IRQ_DISENGCFG_FRAMECOMPLETE1,
		.dec_seq_complete_irq	= DPU_IRQ_DISENGCFG_SEQCOMPLETE1,
		.dec_shdld_irq		= DPU_IRQ_DISENGCFG_SHDLOAD1,
		.ed_cont_shdld_irq	= DPU_IRQ_EXTDST1_SHDLOAD,
		.ed_safe_shdld_irq	= DPU_IRQ_EXTDST5_SHDLOAD,
	  },
	  .name = "imx-dpu-crtc",
	}
};

static DEFINE_MUTEX(dpu_client_id_mutex);
static int dpu_client_id;

static int dpu_get_layerblends_for_plane_grp(struct dpu_soc *dpu,
					     struct dpu_plane_res *res)
{
	int i, ret;

	res->lb_cnt = dpu_lbs.cnt;

	res->lb = devm_kcalloc(dpu->dev, res->lb_cnt, sizeof(*res->lb),
								GFP_KERNEL);
	if (!res->lb)
		return -ENOMEM;

	for (i = 0; i < res->lb_cnt; i++) {
		res->lb[i] = dpu_lb_get(dpu, lb_ids[i]);
		if (IS_ERR(res->lb[i])) {
			ret = PTR_ERR(res->lb[i]);
			dev_err(dpu->dev, "failed to get %s%d: %d\n",
						dpu_lbs.name, lb_ids[i], ret);
			return ret;
		}
	}

	return 0;
}

static int
dpu_get_fetchunits_for_plane_grp(struct dpu_soc *dpu,
				 const struct dpu_units *us,
				 struct dpu_fetchunit ***fu,
				 unsigned int *cnt,
				 struct dpu_fetchunit *
						(*get)(struct dpu_soc *dpu,
						       unsigned int id))
{
	unsigned int fu_cnt = 0;
	int i, j, ret;

	for (i = 0; i < us->cnt; i++) {
		if (us->types[i] == DPU_DISP)
			fu_cnt++;
	}

	*cnt = fu_cnt;

	*fu = devm_kcalloc(dpu->dev, fu_cnt, sizeof(**fu), GFP_KERNEL);
	if (!(*fu))
		return -ENOMEM;

	for (i = 0, j = 0; i < us->cnt; i++) {
		if (us->types[i] != DPU_DISP)
			continue;

		(*fu)[j] = get(dpu, us->ids[i]);
		if (IS_ERR((*fu)[j])) {
			ret = PTR_ERR((*fu)[j]);
			dev_err(dpu->dev, "failed to get %s%d: %d\n",
						us->name, us->ids[i], ret);
			return ret;
		}
		j++;
	}

	return 0;
}

static void dpu_add_fetchunits_to_plane_grp_list(struct list_head *list,
						 struct dpu_fetchunit ***fu,
						 unsigned int *cnt)
{
	int i;

	for (i = *cnt - 1; i >= 0; i--)
		dpu_fu_add_to_list((*fu)[i], list);
}

static int dpu_get_plane_grp_res(struct dpu_soc *dpu,
				 struct dpu_plane_grp *grp)
{
	struct dpu_plane_res *res = &grp->res;
	struct {
		const struct dpu_units *us;
		struct dpu_fetchunit ***fu;
		unsigned int *cnt;
		struct dpu_fetchunit *(*get)(struct dpu_soc *dpu,
					     unsigned int id);
	} fus[] = {
		{&dpu_fds, &res->fd, &res->fd_cnt, dpu_fd_get},
		{&dpu_fls, &res->fl, &res->fl_cnt, dpu_fl_get},
		{&dpu_fws, &res->fw, &res->fw_cnt, dpu_fw_get},
	};
	int i, ret;

	INIT_LIST_HEAD(&grp->fu_list);

	ret = dpu_get_layerblends_for_plane_grp(dpu, res);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(fus); i++) {
		ret = dpu_get_fetchunits_for_plane_grp(dpu,
				fus[i].us, fus[i].fu, fus[i].cnt, fus[i].get);
		if (ret)
			return ret;
	}

	for (i = 0; i < ARRAY_SIZE(fus); i++)
		dpu_add_fetchunits_to_plane_grp_list(&grp->fu_list,
						     fus[i].fu, fus[i].cnt);

	grp->hw_plane_cnt = res->fd_cnt + res->fl_cnt + res->fw_cnt;

	return ret;
}

static void
dpu_put_fetchunits_for_plane_grp(struct dpu_fetchunit ***fu,
				 unsigned int *cnt,
				 void (*put)(struct dpu_fetchunit *fu))
{
	int i;

	for (i = 0; i < *cnt; i++)
		put((*fu)[i]);

	*cnt = 0;
}

static void dpu_put_plane_grp_res(struct dpu_plane_grp *grp)
{
	struct dpu_plane_res *res = &grp->res;
	struct list_head *l, *tmp;
	struct {
		struct dpu_fetchunit ***fu;
		unsigned int *cnt;
		void (*put)(struct dpu_fetchunit *fu);
	} fus[] = {
		{&res->fd, &res->fd_cnt, dpu_fd_put},
		{&res->fl, &res->fl_cnt, dpu_fl_put},
		{&res->fw, &res->fw_cnt, dpu_fw_put},
	};
	int i;

	grp->hw_plane_cnt = 0;

	list_for_each_safe(l, tmp, &grp->fu_list)
		list_del(l);

	for (i = 0; i < ARRAY_SIZE(fus); i++)
		dpu_put_fetchunits_for_plane_grp(fus[i].fu,
						 fus[i].cnt, fus[i].put);

	for (i = 0; i < res->lb_cnt; i++)
		dpu_lb_put(res->lb[i]);
	res->lb_cnt = 0;
}

static int dpu_add_client_devices(struct dpu_soc *dpu)
{
	struct device *dev = dpu->dev;
	struct dpu_platform_reg *reg;
	struct dpu_crtc_grp *crtc_grp;
	struct dpu_plane_grp *plane_grp;
	size_t client_cnt, reg_size;
	int i, id, ret;

	client_cnt = ARRAY_SIZE(client_reg);

	reg = devm_kcalloc(dev, client_cnt, sizeof(*reg), GFP_KERNEL);
	if (!reg)
		return -ENOMEM;

	crtc_grp = devm_kzalloc(dev, sizeof(*crtc_grp), GFP_KERNEL);
	if (!crtc_grp)
		return -ENOMEM;

	plane_grp = devm_kzalloc(dev, sizeof(*plane_grp), GFP_KERNEL);
	if (!plane_grp)
		return -ENOMEM;

	crtc_grp->plane_grp = plane_grp;

	mutex_lock(&dpu_client_id_mutex);
	id = dpu_client_id;
	dpu_client_id += client_cnt;
	mutex_unlock(&dpu_client_id_mutex);

	reg_size = client_cnt * sizeof(struct dpu_platform_reg);
	memcpy(reg, &client_reg[0], reg_size);

	ret = dpu_get_plane_grp_res(dpu, plane_grp);
	if (ret)
		goto err_get_plane_res;

	for (i = 0; i < client_cnt; i++) {
		struct platform_device *pdev;
		struct device_node *np;

		/* Associate subdevice with the corresponding port node. */
		np = of_graph_get_port_by_id(dev->of_node, i);
		if (!np) {
			dev_info(dev,
				"no port@%d node in %s, not using DISP%d\n",
				i, dev->of_node->full_name, i);
			continue;
		}

		reg[i].pdata.crtc_grp = crtc_grp;

		pdev = platform_device_alloc(reg[i].name, id++);
		if (!pdev) {
			ret = -ENOMEM;
			goto err_register;
		}

		pdev->dev.parent = dev;
		pdev->dev.of_node = np;
		pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
		pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;

		reg[i].pdata.of_node = np;
		ret = platform_device_add_data(pdev, &reg[i].pdata,
					       sizeof(reg[i].pdata));
		if (!ret)
			ret = platform_device_add(pdev);
		if (ret) {
			platform_device_put(pdev);
			goto err_register;
		}
	}

	return ret;

err_register:
	platform_device_unregister_children(to_platform_device(dev));
err_get_plane_res:
	dpu_put_plane_grp_res(plane_grp);

	return ret;
}

static int dpu_core_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct dpu_soc *dpu;
	struct resource *res;
	unsigned long dpu_base;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	dpu_base = res->start;

	dpu = devm_kzalloc(dev, sizeof(*dpu), GFP_KERNEL);
	if (!dpu)
		return -ENOMEM;

	dpu->dev = dev;

	dpu->id = of_alias_get_id(np, "dpu");
	if (dpu->id < 0) {
		dev_err(dev, "failed to get dpu node alias id: %d\n", dpu->id);
		return dpu->id;
	}

	dpu_units_addr_dbg(dpu, dpu_base);

	ret = dpu_get_irqs(pdev, dpu);
	if (ret)
		return ret;

	dpu->comctrl_reg = devm_ioremap(dev, dpu_base, SZ_512);
	if (!dpu->comctrl_reg)
		return -ENOMEM;

	ret = dpu_attach_pm_domains(dpu);
	if (ret)
		return ret;

	dpu->clk_cfg = devm_clk_get(dev, "cfg");
	if (IS_ERR(dpu->clk_cfg)) {
		ret = PTR_ERR(dpu->clk_cfg);
		dev_err_probe(dev, ret, "failed to get cfg clock\n");
		goto failed_clk_cfg_get;
	}

	dpu->clk_axi = devm_clk_get(dev, "axi");
	if (IS_ERR(dpu->clk_axi)) {
		ret = PTR_ERR(dpu->clk_axi);
		dev_err_probe(dev, ret, "failed to get axi clock\n");
		goto failed_clk_axi_get;
	}

	ret = dpu_irq_init(dpu);
	if (ret)
		goto failed_irq_init;

	ret = dpu_submodules_init(dpu, dpu_base);
	if (ret)
		goto failed_submodules_init;

	platform_set_drvdata(pdev, dpu);

	pm_runtime_enable(dev);

	ret = dpu_add_client_devices(dpu);
	if (ret) {
		dev_err(dev, "failed to add client devices: %d\n", ret);
		goto failed_add_clients;
	}

	return ret;

failed_add_clients:
	pm_runtime_disable(dev);
failed_submodules_init:
	dpu_irq_exit(dpu);
failed_irq_init:
failed_clk_axi_get:
failed_clk_cfg_get:
	dpu_detach_pm_domains(dpu);
	return ret;
}

static int dpu_core_remove(struct platform_device *pdev)
{
	struct dpu_soc *dpu = platform_get_drvdata(pdev);

	platform_device_unregister_children(pdev);
	pm_runtime_disable(dpu->dev);
	dpu_irq_exit(dpu);
	dpu_detach_pm_domains(dpu);

	return 0;
}

static int __maybe_unused dpu_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dpu_soc *dpu = platform_get_drvdata(pdev);

	clk_disable_unprepare(dpu->clk_axi);
	clk_disable_unprepare(dpu->clk_cfg);

	dev_dbg(dev, "suspended\n");

	return 0;
}

static int __maybe_unused dpu_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct dpu_soc *dpu = platform_get_drvdata(pdev);
	int ret;

	ret = clk_prepare_enable(dpu->clk_cfg);
	if (ret) {
		dev_err(dev, "failed to enable cfg clock: %d\n", ret);
		return ret;
	}
	ret = clk_prepare_enable(dpu->clk_axi);
	if (ret) {
		clk_disable_unprepare(dpu->clk_cfg);
		dev_err(dev, "failed to enable axi clock: %d\n", ret);
		return ret;
	}

	dpu_irq_hw_init(dpu);

	dpu_submodules_hw_init(dpu);

	dev_dbg(dev, "resumed\n");

	return ret;
}

static const struct dev_pm_ops dpu_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				     pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(dpu_runtime_suspend, dpu_runtime_resume, NULL)
};

const struct of_device_id dpu_dt_ids[] = {
	{ .compatible = "fsl,imx8qm-dpu" },
	{ .compatible = "fsl,imx8qxp-dpu" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dpu_dt_ids);

struct platform_driver dpu_core_driver = {
	.driver = {
		.pm = &dpu_pm_ops,
		.name = "dpu-core",
		.of_match_table = dpu_dt_ids,
	},
	.probe = dpu_core_probe,
	.remove = dpu_core_remove,
};
