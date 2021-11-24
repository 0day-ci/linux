// SPDX-License-Identifier: GPL-2.0+
/*
*
* Zhouyi AI Accelerator driver
*
* Copyright (C) 2020 Arm (China) Ltd.
* Copyright (C) 2021 Cai Huoqing
*/

/**
 * @file zynpu_irq.c
 * Implementation of the interrupt request and handlers' abstraction
 */

#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include "zynpu_irq.h"
#include "zynpu.h"

static irqreturn_t zynpu_irq_handler_upper_half(int irq, void *dev_id)
{
	int ret = 0;
	struct zynpu_irq_object *irq_obj = NULL;
	struct zynpu_priv *zynpu = NULL;

	if (!dev_id)
		return IRQ_NONE;

	zynpu = (struct zynpu_priv *)(((struct device *)dev_id)->driver_data);
	irq_obj = zynpu->core0->irq_obj;
	ret = irq_obj->uhandler(zynpu);
	if (ret)
		return IRQ_NONE;


	return IRQ_HANDLED;
}

static void zynpu_irq_handler_bottom_half(struct work_struct *work)
{
	struct zynpu_irq_object *irq_obj = NULL;

	if (work) {
		irq_obj = container_of(work, struct zynpu_irq_object, work);
		irq_obj->bhandler(irq_obj->zynpu_priv);
	}
}

struct zynpu_irq_object *zynpu_create_irq_object(u32 irqnum, zynpu_irq_uhandler_t uhandler,
	zynpu_irq_bhandler_t bhandler, void *zynpu_priv, struct device *dev, char *description)
{
	int ret = 0;
	struct zynpu_irq_object *irq_obj = NULL;

	if ((!zynpu_priv) || (!dev) || (!description)) {
		ret = ZYNPU_ERRCODE_INTERNAL_NULLPTR;
		goto finish;
	}

	irq_obj = kzalloc(sizeof(struct zynpu_irq_object), GFP_KERNEL);
	if (!irq_obj)
		goto finish;

	irq_obj->zynpu_wq = NULL;
	irq_obj->irqnum = 0;
	irq_obj->dev = dev;

	irq_obj->zynpu_wq = create_singlethread_workqueue("zynpu");
	if (!irq_obj->zynpu_wq)
		goto err_handle;

	INIT_WORK(&irq_obj->work, zynpu_irq_handler_bottom_half);

	ret = request_irq(irqnum, zynpu_irq_handler_upper_half,
			  IRQF_SHARED | IRQF_TRIGGER_RISING, description, dev);
	if (ret) {
		dev_err(dev, "request IRQ (num %u) failed! (errno = %d)", irqnum, ret);
		goto err_handle;
	}

	irq_obj->irqnum = irqnum;
	irq_obj->uhandler = uhandler;
	irq_obj->bhandler = bhandler;
	irq_obj->zynpu_priv = zynpu_priv;

	/* success */
	goto finish;

err_handle:
	zynpu_destroy_irq_object(irq_obj);
	irq_obj = NULL;

finish:
	return irq_obj;
}

void zynpu_irq_schedulework(struct zynpu_irq_object *irq_obj)
{
	if (irq_obj)
		queue_work(irq_obj->zynpu_wq, &irq_obj->work);
}

void zynpu_irq_flush_workqueue(struct zynpu_irq_object *irq_obj)
{
	/* only one workqueue currently */
	flush_workqueue(irq_obj->zynpu_wq);
}

void zynpu_destroy_irq_object(struct zynpu_irq_object *irq_obj)
{
	if (irq_obj) {
		if (irq_obj->zynpu_wq) {
			flush_workqueue(irq_obj->zynpu_wq);
			destroy_workqueue(irq_obj->zynpu_wq);
			irq_obj->zynpu_wq = NULL;
		}
		if (irq_obj->irqnum)
			free_irq(irq_obj->irqnum, irq_obj->dev);
		kfree(irq_obj);
		flush_scheduled_work();
	}
}