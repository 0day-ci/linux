/* SPDX-License-Identifier: GPL-2.0 */
/*
 * KMS driver for Loongson display controller
 */

/*
 * Authors:
 *      Sui Jingfeng <suijingfeng@loongson.cn>
 */

#ifndef __LSDC_IRQ_H__
#define __LSDC_IRQ_H__

irqreturn_t lsdc_irq_thread_cb(int irq, void *arg);
irqreturn_t lsdc_irq_handler_cb(int irq, void *arg);

#endif
