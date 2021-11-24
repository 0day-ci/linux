/* SPDX-License-Identifier: GPL-2.0+ */
/*
*
* Zhouyi AI Accelerator driver
*
* Copyright (C) 2020 Arm (China) Ltd.
* Copyright (C) 2021 Cai Huoqing
*/

/**
 * @file zynpu_irq.h
 * Header of the interrupt request and handlers' abstraction
 */

#ifndef _ZYNPU_IRQ_H_
#define _ZYNPU_IRQ_H_

#include <linux/device.h>
#include <linux/workqueue.h>

typedef int  (*zynpu_irq_uhandler_t) (void *arg);
typedef void (*zynpu_irq_bhandler_t) (void *arg);
typedef void (*zynpu_irq_trigger_t) (void *arg);
typedef void (*zynpu_irq_ack_t) (void *arg);

/**
 * struct zynpu_irq_object - IRQ instance for each hw module in ZYNPU with interrupt function
 *
 * @irqnum: interrupt number used to request IRQ
 * @zynpu_priv: zynpu_priv struct pointer
 * @uhandler: real upper-half handler
 * @bhandler: real bottom-half handler
 * @work: work struct
 * @dev: device pointer
 * @zynpu_wq: workqueue struct pointer
 */
struct zynpu_irq_object {
	u32 irqnum;
	void *zynpu_priv;
	zynpu_irq_uhandler_t uhandler;
	zynpu_irq_bhandler_t bhandler;
	struct work_struct  work;
	struct device *dev;
	struct workqueue_struct *zynpu_wq;
};

/**
 * @brief initialize an ZYNPU IRQ object for a HW module with interrupt function
 *
 * @param irqnum: interrupt number
 * @param uhandler: upper-half handler
 * @param bhandler: bottom-half handler
 * @zynpu_priv: zynpu_priv struct pointer
 * @param description: irq object description string
 *
 * @return irq_object pointer if successful; NULL if failed;
 */
struct zynpu_irq_object *zynpu_create_irq_object(u32 irqnum, zynpu_irq_uhandler_t uhandler,
	zynpu_irq_bhandler_t bhandler, void *zynpu_priv, struct device *dev, char *description);
/**
 * @brief workqueue schedule API
 *
 * @param irq_obj: interrupt object
 *
 * @return void
 */
void zynpu_irq_schedulework(struct zynpu_irq_object *irq_obj);
/**
 * @brief workqueue flush API
 *
 * @param irq_obj: interrupt object
 *
 * @return void
 */
void zynpu_irq_flush_workqueue(struct zynpu_irq_object *irq_obj);
/**
 * @brief workqueue terminate API
 *
 * @param irq_obj: interrupt object
 *
 * @return void
 */
void zynpu_destroy_irq_object(struct zynpu_irq_object *irq_obj);

#endif //_ZYNPU_IRQ_H_