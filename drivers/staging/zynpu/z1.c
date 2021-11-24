// SPDX-License-Identifier: GPL-2.0+
/*
*
* Zhouyi AI Accelerator driver
*
* Copyright (C) 2020 Arm (China) Ltd.
* Copyright (C) 2021 Cai Huoqing
*/

/**
 * @file z1.c
 * Implementation of the zhouyi v1 ZYNPU hardware cntrol interfaces
 */

#include "zynpu.h"
#include "zynpu_io.h"
#include "zhouyi.h"

/**
 * Zhouyi V1 ZYNPU Interrupts
 */
#define ZHOUYIV1_IRQ	      (ZHOUYI_IRQ)
#define ZHOUYIV1_IRQ_ENABLE_FLAG  (ZHOUYIV1_IRQ)
#define ZHOUYIV1_IRQ_DISABLE_FLAG (ZHOUYI_IRQ_NONE)

/**
 * Zhouyi V1 ZYNPU Specific Host Control Register Map
 */
#define ZHOUYI_INTR_CAUSE_REG_OFFSET	  0x20
#define ZHOUYI_L2_CACHE_FEATURE_REG_OFFSET    0x6C

static void zhouyi_v1_enable_interrupt(struct zynpu_core *core)
{
	if (core)
		zynpu_write32(core->base0, ZHOUYI_CTRL_REG_OFFSET,
                              ZHOUYIV1_IRQ_ENABLE_FLAG);
}

static void zhouyi_v1_disable_interrupt(struct zynpu_core *core)
{
	if (core)
		zynpu_write32(core->base0, ZHOUYI_CTRL_REG_OFFSET,
                              ZHOUYIV1_IRQ_DISABLE_FLAG);
}

static void zhouyi_v1_clear_qempty_interrupt(struct zynpu_core *core)
{
	if (core)
		zhouyi_clear_qempty_interrupt(core->base0);
}

static void zhouyi_v1_clear_done_interrupt(struct zynpu_core *core)
{
	if (core)
		zhouyi_clear_done_interrupt(core->base0);
}

static void zhouyi_v1_clear_excep_interrupt(struct zynpu_core *core)
{
	if (core)
		zhouyi_clear_excep_interrupt(core->base0);
}

static int zhouyi_v1_trigger(struct zynpu_core *core,
			     struct user_job_desc *udesc, int tid)
{
	int ret = 0;
	unsigned int phys_addr = 0;
	unsigned int phys_addr0 = 0;
	unsigned int phys_addr1 = 0;
	unsigned int start_pc = 0;

	if (!core) {
		ret = -EINVAL;
		goto finish;
	}

	/* Load data addr 0 register */
	phys_addr0 = (unsigned int)udesc->data_0_addr;
	zynpu_write32(core->base0, ZHOUYI_DATA_ADDR_0_REG_OFFSET, phys_addr0);

	/* Load data addr 1 register */
	phys_addr1 = (unsigned int)udesc->data_1_addr;
	zynpu_write32(core->base0, ZHOUYI_DATA_ADDR_1_REG_OFFSET, phys_addr1);

	/* Load interrupt PC */
	zynpu_write32(core->base0, ZHOUYI_INTR_PC_REG_OFFSET,
                      (unsigned int)udesc->intr_handler_addr);

	/* Load start PC register */
	/* use write back and invalidate DCache*/
        /* because HW does not implement invalidate option in Zhouyi-z1 */
	phys_addr = (unsigned int)udesc->start_pc_addr;
	start_pc = phys_addr | 0xD;
	zynpu_write32(core->base0, ZHOUYI_START_PC_REG_OFFSET, start_pc);

	if (tid)
		dev_info(core->dev,
                         "[%u] trigger Job 0x%x done: start pc = 0x%x, dreg0 = 0x%x, dreg1 = 0x%x",
		         tid, udesc->job_id, start_pc, phys_addr0, phys_addr1);
	else
		dev_info(core->dev,
                         "[IRQ] trigger Job 0x%x done: start pc = 0x%x, dreg0 = 0x%x, dreg1 = 0x%x",
		         udesc->job_id, start_pc, phys_addr0, phys_addr1);

finish:
	return ret;
}

static bool zhouyi_v1_is_idle(struct zynpu_core *core)
{
	if (!core) {
		pr_err("invalid input args core to be NULL!");
		return 0;
	}
	return ZYNPU_BIT(zynpu_read32(core->base0, ZHOUYI_STAT_REG_OFFSET), 16) &&
	       ZYNPU_BIT(zynpu_read32(core->base0, ZHOUYI_STAT_REG_OFFSET), 17) &&
	       ZYNPU_BIT(zynpu_read32(core->base0, ZHOUYI_STAT_REG_OFFSET), 18);
}

static int zhouyi_v1_read_status_reg(struct zynpu_core *core)
{
	return zhouyi_read_status_reg(core->base0);
}

static void zhouyi_v1_print_hw_id_info(struct zynpu_core *core)
{
	int ret = 0;

	if (!core) {
		pr_err("invalid input args io to be NULL!");
		return;
	}

	ret = zynpu_read32(core->base0, ZHOUYI_STAT_REG_OFFSET);
	dev_info(core->dev, "ZYNPU Initial Status: 0x%x.", ret);

	dev_info(core->dev, "###### ZHOUYI V1 HARDWARE INFORMATION #######");
	dev_info(core->dev, "# ISA Version Register: 0x%x",
		 zynpu_read32(core->base0, ZHOUYI_ISA_VERSION_REG_OFFSET));
	dev_info(core->dev, "# TPC Feature Register: 0x%x",
		 zynpu_read32(core->base0, ZHOUYI_TPC_FEATURE_REG_OFFSET));
	dev_info(core->dev, "# SPU Feature Register: 0x%x",
		 zynpu_read32(core->base0, ZHOUYI_SPU_FEATURE_REG_OFFSET));
	dev_info(core->dev, "# HWA Feature Register: 0x%x",
		 zynpu_read32(core->base0, ZHOUYI_HWA_FEATURE_REG_OFFSET));
	dev_info(core->dev, "# Revision ID Register: 0x%x",
		 zynpu_read32(core->base0, ZHOUYI_REVISION_ID_REG_OFFSET));
	dev_info(core->dev, "# Memory Hierarchy Feature Register: 0x%x",
		 zynpu_read32(core->base0, ZHOUYI_MEM_FEATURE_REG_OFFSET));
	dev_info(core->dev, "# Instruction RAM Feature Register:  0x%x",
		 zynpu_read32(core->base0, ZHOUYI_INST_RAM_FEATURE_REG_OFFSET));
	dev_info(core->dev, "# TEC Local SRAM Feature Register:   0x%x",
		 zynpu_read32(core->base0, ZHOUYI_LOCAL_SRAM_FEATURE_REG_OFFSET));
	dev_info(core->dev, "# Global SRAM Feature Register:      0x%x",
		 zynpu_read32(core->base0, ZHOUYI_GLOBAL_SRAM_FEATURE_REG_OFFSET));
	dev_info(core->dev, "# Instruction Cache Feature Register:0x%x",
		 zynpu_read32(core->base0, ZHOUYI_INST_CACHE_FEATURE_REG_OFFSET));
	dev_info(core->dev, "# Data Cache Feature Register:       0x%x",
		 zynpu_read32(core->base0, ZHOUYI_DATA_CACHE_FEATURE_REG_OFFSET));
	dev_info(core->dev, "# L2 Cache Feature Register:	 0x%x",
		 zynpu_read32(core->base0, ZHOUYI_L2_CACHE_FEATURE_REG_OFFSET));
	dev_info(core->dev, "#############################################");
}

static int zhouyi_v1_query_cap(struct zynpu_core *core, struct zynpu_cap *cap)
{
	if (!core)
		return 0;
	return zhouyi_query_cap(core->base0, cap);
}

static void zhouyi_v1_io_rw(struct zynpu_core *core, struct zynpu_io_req *io_req)
{
	zhouyi_io_rw(core->base0, io_req);
}

static int zhouyi_v1_upper_half(void *data)
{
	int ret = 0;
	struct zynpu_priv *zynpu = (struct zynpu_priv*)data;
	struct zynpu_core *core = zynpu->core0;

	if (!core) {
		ret = ZYNPU_ERRCODE_INTERNAL_NULLPTR;
		goto finish;
	}

	zhouyi_v1_disable_interrupt(core);
	ret = zhouyi_v1_read_status_reg(core);
	if (ret & ZHOUYI_IRQ_QEMPTY) {
		zhouyi_v1_clear_qempty_interrupt(core);
	}

	if (ret & ZHOUYI_IRQ_DONE) {
		zhouyi_v1_clear_done_interrupt(core);
		zynpu_job_manager_update_job_state_irq(zynpu, 0);
		zynpu_irq_schedulework(core->irq_obj);
	}

	if (ret & ZHOUYI_IRQ_EXCEP) {
		zhouyi_v1_clear_excep_interrupt(core);
		zynpu_job_manager_update_job_state_irq(zynpu,
                        zynpu_read32(core->base0, ZHOUYI_INTR_CAUSE_REG_OFFSET));
		zynpu_irq_schedulework(core->irq_obj);
	}
	zhouyi_v1_enable_interrupt(core);

	/* success */
	ret = 0;

finish:
	return ret;
}

static void zhouyi_v1_bottom_half(void *data)
{
	struct zynpu_priv* zynpu = (struct zynpu_priv*)data;
	zynpu_job_manager_update_job_queue_done_irq(&zynpu->job_manager);
}

struct zynpu_io_operation zhouyi_v1_ops = {
	.enable_interrupt = zhouyi_v1_enable_interrupt,
	.disable_interrupt = zhouyi_v1_disable_interrupt,
	.trigger = zhouyi_v1_trigger,
	.is_idle = zhouyi_v1_is_idle,
	.read_status_reg = zhouyi_v1_read_status_reg,
	.print_hw_id_info = zhouyi_v1_print_hw_id_info,
	.query_capability = zhouyi_v1_query_cap,
	.io_rw = zhouyi_v1_io_rw,
	.upper_half = zhouyi_v1_upper_half,
	.bottom_half = zhouyi_v1_bottom_half,
};
