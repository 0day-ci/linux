// SPDX-License-Identifier: GPL-2.0-only
// Copyright(c) 2017-2021 Intel Corporation

#include <linux/pci.h>

#include <uapi/misc/intel/gna.h>

#include "gna_device.h"
#include "gna_driver.h"
#include "gna_hw.h"

int gna_parse_hw_status(struct gna_private *gna_priv, u32 hw_status)
{
	int status;

	if (hw_status & GNA_ERROR) {
		dev_dbg(gna_priv->misc.this_device, "GNA completed with errors: %#x\n", hw_status);
		status = -EIO;
	} else if (hw_status & GNA_STS_SCORE_COMPLETED) {
		status = 0;
		dev_dbg(gna_priv->misc.this_device, "GNA completed successfully: %#x\n", hw_status);
	} else {
		dev_err(gna_priv->misc.this_device, "GNA not completed, status: %#x\n", hw_status);
		status = -ENODATA;
	}

	return status;
}

void gna_print_error_status(struct gna_private *gna_priv, u32 hw_status)
{
	if (hw_status & GNA_STS_PARAM_OOR)
		dev_dbg(gna_priv->misc.this_device, "GNA error: Param Out Range Error\n");

	if (hw_status & GNA_STS_VA_OOR)
		dev_dbg(gna_priv->misc.this_device, "GNA error: VA Out of Range Error\n");

	if (hw_status & GNA_STS_PCI_MMU_ERR)
		dev_dbg(gna_priv->misc.this_device, "GNA error: PCI MMU Error\n");

	if (hw_status & GNA_STS_PCI_DMA_ERR)
		dev_dbg(gna_priv->misc.this_device, "GNA error: PCI MMU Error\n");

	if (hw_status & GNA_STS_PCI_UNEXCOMPL_ERR)
		dev_dbg(gna_priv->misc.this_device, "GNA error: PCI Unexpected Completion Error\n");

	if (hw_status & GNA_STS_SATURATE)
		dev_dbg(gna_priv->misc.this_device, "GNA error: Saturation Reached !\n");
}

bool gna_hw_perf_enabled(struct gna_private *gna_priv)
{
	void __iomem *addr = gna_priv->bar0_base;
	u32 ctrl = gna_reg_read(addr, GNA_MMIO_CTRL);

	return FIELD_GET(GNA_CTRL_COMP_STATS_EN, ctrl) ? true : false;
}

void gna_start_scoring(struct gna_private *gna_priv, void __iomem *addr,
		       struct gna_compute_cfg *compute_cfg)
{
	u32 ctrl = gna_reg_read(addr, GNA_MMIO_CTRL);

	ctrl |= GNA_CTRL_START_ACCEL | GNA_CTRL_COMP_INT_EN | GNA_CTRL_ERR_INT_EN;

	ctrl &= ~GNA_CTRL_COMP_STATS_EN;
	ctrl |= FIELD_PREP(GNA_CTRL_COMP_STATS_EN,
			compute_cfg->hw_perf_encoding & FIELD_MAX(GNA_CTRL_COMP_STATS_EN));

	ctrl &= ~GNA_CTRL_ACTIVE_LIST_EN;
	ctrl |= FIELD_PREP(GNA_CTRL_ACTIVE_LIST_EN,
			compute_cfg->active_list_on & FIELD_MAX(GNA_CTRL_ACTIVE_LIST_EN));

	ctrl &= ~GNA_CTRL_OP_MODE;
	ctrl |= FIELD_PREP(GNA_CTRL_OP_MODE,
			compute_cfg->gna_mode & FIELD_MAX(GNA_CTRL_OP_MODE));

	gna_reg_write(addr, GNA_MMIO_CTRL, ctrl);

	dev_dbg(gna_priv->misc.this_device, "scoring started...\n");
}

static void gna_clear_saturation(struct gna_private *gna_priv)
{
	void __iomem *addr = gna_priv->bar0_base;
	u32 val;

	val = gna_reg_read(addr, GNA_MMIO_STS);
	if (val & GNA_STS_SATURATE) {
		dev_dbg(gna_priv->misc.this_device, "saturation reached\n");
		dev_dbg(gna_priv->misc.this_device, "status: %#x\n", val);

		val = val & GNA_STS_SATURATE;
		gna_reg_write(addr, GNA_MMIO_STS, val);
	}
}

void gna_abort_hw(struct gna_private *gna_priv)
{
	void __iomem *addr = gna_priv->bar0_base;
	u32 val;
	int i;

	/* saturation bit in the GNA status register needs
	 * to be explicitly cleared.
	 */
	gna_clear_saturation(gna_priv);

	val = gna_reg_read(addr, GNA_MMIO_STS);
	dev_dbg(gna_priv->misc.this_device, "status before abort: %#x\n", val);

	val = gna_reg_read(addr, GNA_MMIO_CTRL);
	val |= GNA_CTRL_ABORT_CLR_ACCEL;
	gna_reg_write(addr, GNA_MMIO_CTRL, val);

	i = 100;
	do {
		val = gna_reg_read(addr, GNA_MMIO_STS);
		if ((val & 0x1) == 0)
			break;
	} while (--i);

	if (i == 0)
		dev_err(gna_priv->misc.this_device, "abort did not complete\n");
}
