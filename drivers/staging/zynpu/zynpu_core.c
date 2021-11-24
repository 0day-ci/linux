// SPDX-License-Identifier: GPL-2.0+
/*
*
* Zhouyi AI Accelerator driver
*
* Copyright (C) 2020 Arm (China) Ltd.
* Copyright (C) 2021 Cai Huoqing
*/

/**
 * @file zynpu.c
 * Implementation of the zynpu device struct creation & destroy
 */

#include <linux/of.h>
#include "zynpu.h"

extern struct zynpu_io_operation zhouyi_v1_ops;

struct zynpu_priv z1_platform_priv = {
	.version = ZYNPU_VERSION_ZHOUYI_V1,
	.core_ctrl = &zhouyi_v1_ops,
};

static int init_misc_dev(struct zynpu_priv *zynpu)
{
	int ret = 0;

	if (!zynpu)
		return -EINVAL;

	zynpu_fops_register(&zynpu->zynpu_fops);

	zynpu->misc = devm_kzalloc(zynpu->dev, sizeof(struct miscdevice), GFP_KERNEL);
	if (!zynpu->misc) {
		dev_err(zynpu->dev, "no memory in allocating misc struct\n");
		return -ENOMEM;
	}

	/* misc init */
	zynpu->misc->minor = MISC_DYNAMIC_MINOR;
	zynpu->misc->name = "zynpu";
	zynpu->misc->fops = &zynpu->zynpu_fops;
	zynpu->misc->mode = 0666;
	ret = misc_register(zynpu->misc);
	if (ret)
		dev_err(zynpu->dev, "ZYNPU misc register failed");

	return ret;
}

static void deinit_misc_dev(struct zynpu_priv *zynpu)
{
	if (zynpu)
		misc_deregister(zynpu->misc);
}

int init_zynpu_priv(struct zynpu_priv *zynpu, struct device *dev)
{
	int ret = 0;

	if ((!dev) || (!zynpu)) {
		dev_err(dev, "invalid input args dts/zynpu to be NULL\n");
		return -EINVAL;
	}

	zynpu->dev = dev;
	mutex_init(&zynpu->lock);
	zynpu->core0 = NULL;
	zynpu->misc = NULL;
	zynpu->is_suspend = 0;

	/* init memory manager */
	ret = zynpu_init_mm(&zynpu->mm, dev, zynpu->version);
	if (ret)
		goto err_handle;

	/* init misc device and fops */
	ret = init_misc_dev(zynpu);
	if (ret)
		goto err_handle;

	ret = zynpu_create_sysfs(zynpu);
	if (ret)
		goto err_handle;

	goto finish;

err_handle:
	deinit_zynpu_priv(zynpu);

finish:
	return ret;
}

static int
create_zynpu_core(int version, int irqnum, u64 zynpu_base0, u64 base0_size,
	u32 freq, zynpu_irq_uhandler_t uhandler, zynpu_irq_bhandler_t bhandler,
	void *zynpu_priv, struct device *dev, struct zynpu_core **p_core)
{
	int ret = 0;
	struct zynpu_core *core = NULL;

	if ((!base0_size) || (!zynpu_priv) || (!dev) || (!p_core)) {
		if (dev)
			dev_err(dev, "invalid input args base0_size/zynpu_priv/p_core to be NULL\n");
		return -EINVAL;
	}

	core = devm_kzalloc(dev, sizeof(struct zynpu_core), GFP_KERNEL);
	if (!core) {
		dev_err(dev, "no memory in allocating zynpu_core struct\n");
		return -ENOMEM;
	}

	core->base0 = NULL;
	core->irq_obj = NULL;

	/* init core */
	core->max_sched_num = 1;
	core->version = version;

	core->base0 = zynpu_create_ioregion(dev, zynpu_base0, base0_size);
	if (!core->base0) {
		dev_err(dev, "create IO region for core0 failed: base 0x%llx, size 0x%llx\n",
			zynpu_base0, base0_size);
		return -EFAULT;
	}

	core->irq_obj = zynpu_create_irq_object(irqnum, uhandler, bhandler,
		zynpu_priv, dev, "zynpu");
	if (!core->irq_obj) {
		dev_err(dev, "create IRQ object for core0 failed: IRQ 0x%x\n", irqnum);
		return -EFAULT;
	}

	core->freq_in_MHz = freq;
	core->dev = dev;

	/* success */
	*p_core = core;
	return ret;
}

static void destroy_zynpu_core(struct zynpu_core *core)
{
	if (core) {
		if (core->base0)
			zynpu_destroy_ioregion(core->base0);
		if (core->irq_obj)
			zynpu_destroy_irq_object(core->irq_obj);
	}
}

int zynpu_priv_init_core(struct zynpu_priv *zynpu, int irqnum, u64 base, u64 size)
{
	int ret = 0;

	if (!zynpu)
		return -EINVAL;

	ret = create_zynpu_core(zynpu->version, irqnum, base,
				size, 0, zynpu->core_ctrl->upper_half,
				zynpu->core_ctrl->bottom_half, zynpu,
				zynpu->dev, &zynpu->core0);
	if (ret)
		return ret;

	ret = zynpu_init_job_manager(&zynpu->job_manager, zynpu->dev,
				     zynpu->core0->max_sched_num);
	if (ret)
		return ret;

	return ret;
}

int zynpu_priv_add_mem_region(struct zynpu_priv *zynpu, u64 base, u64 size,
	enum zynpu_mem_type type)
{
	if (!zynpu)
		return -EINVAL;

	return zynpu_mm_add_region(&zynpu->mm, base, size, type);
}

int zynpu_priv_get_version(struct zynpu_priv *zynpu)
{
	if (zynpu)
		return zynpu->core0->version;
	return 0;
}

void zynpu_priv_enable_interrupt(struct zynpu_priv *zynpu)
{
	if (zynpu)
	       zynpu->core_ctrl->enable_interrupt(zynpu->core0);
}

void zynpu_priv_disable_interrupt(struct zynpu_priv *zynpu)
{
	if (zynpu)
	       zynpu->core_ctrl->disable_interrupt(zynpu->core0);
}

int zynpu_priv_trigger(struct zynpu_priv *zynpu, struct user_job_desc *udesc, int tid)
{
	if (zynpu)
		return zynpu->core_ctrl->trigger(zynpu->core0, udesc, tid);
	return -EINVAL;
}

bool zynpu_priv_is_idle(struct zynpu_priv *zynpu)
{
	if (zynpu)
		return zynpu->core_ctrl->is_idle(zynpu->core0);
	return 0;
}

int zynpu_priv_query_capability(struct zynpu_priv *zynpu, struct zynpu_cap *cap)
{
	if (zynpu)
		return zynpu->core_ctrl->query_capability(zynpu->core0, cap);
	return -EINVAL;
}

void zynpu_priv_io_rw(struct zynpu_priv *zynpu, struct zynpu_io_req *io_req)
{
	if (zynpu)
		zynpu->core_ctrl->io_rw(zynpu->core0, io_req);
}

void zynpu_priv_print_hw_id_info(struct zynpu_priv *zynpu)
{
	if (zynpu)
	       zynpu->core_ctrl->print_hw_id_info(zynpu->core0);
}

int deinit_zynpu_priv(struct zynpu_priv *zynpu)
{
	if (!zynpu)
		return 0;

	zynpu_destroy_sysfs(zynpu);

	zynpu_deinit_mm(&zynpu->mm);
	if (zynpu->misc)
		deinit_misc_dev(zynpu);
	if (zynpu->core0) {
		destroy_zynpu_core(zynpu->core0);
		zynpu_deinit_job_manager(&zynpu->job_manager);
	}

	return 0;
}