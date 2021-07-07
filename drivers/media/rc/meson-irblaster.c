// SPDX-License-Identifier: GPL-2.0-only
/**
 * meson-irblaster.c - Amlogic Meson IR blaster driver
 *
 * Copyright (c) 2021, SberDevices. All Rights Reserved.
 *
 * Author: Viktor Prutyanov <viktor.prutyanov@phystech.edu>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT. See the GNU General Public License for more
 * details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/of_irq.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <media/rc-core.h>

#define DEVICE_NAME	"Meson IR blaster"
#define DRIVER_NAME	"meson-irblaster"

#define IRB_DEFAULT_CARRIER	38000
#define IRB_DEFAULT_DUTY_CYCLE	50

#define IRB_FIFO_LEN			128
#define IRB_DEFAULT_MAX_FIFO_LEVEL	96

#define IRB_ADDR0	0x0
#define IRB_ADDR1	0x4
#define IRB_ADDR2	0x8
#define IRB_ADDR3	0xc

#define IRB_MAX_DELAY	(1 << 10)
#define IRB_DELAY_MASK	(IRB_MAX_DELAY - 1)

/* IRCTRL_IR_BLASTER_ADDR0 */
#define IRB_MOD_CLK(x)		((x) << 12)
#define IRB_MOD_SYS_CLK		0
#define IRB_MOD_XTAL3_CLK	1
#define IRB_MOD_1US_CLK		2
#define IRB_MOD_10US_CLK	3
#define IRB_INIT_HIGH		BIT(2)
#define IRB_ENABLE		BIT(0)

/* IRCTRL_IR_BLASTER_ADDR2 */
#define IRB_MOD_COUNT(lo, hi)	((((lo) - 1) << 16) | ((hi) - 1))

/* IRCTRL_IR_BLASTER_ADDR2 */
#define IRB_WRITE_FIFO	BIT(16)
#define IRB_MOD_ENABLE	BIT(12)
#define IRB_TB_1US	(0x0 << 10)
#define IRB_TB_10US	(0x1 << 10)
#define IRB_TB_100US	(0x2 << 10)
#define IRB_TB_MOD_CLK	(0x3 << 10)

/* IRCTRL_IR_BLASTER_ADDR3 */
#define IRB_FIFO_THD_PENDING	BIT(16)
#define IRB_FIFO_IRQ_ENABLE	BIT(8)

static unsigned int max_fifo_level = IRB_DEFAULT_MAX_FIFO_LEVEL;
module_param(max_fifo_level, uint, 0444);
MODULE_PARM_DESC(max_fifo_level, "Max blaster FIFO filling level");

struct irblaster_dev {
	struct device *dev;
	unsigned int irq;
	void __iomem *reg_base;
	u32 *buf;
	unsigned int buf_len;
	unsigned int buf_head;
	unsigned int carrier;
	unsigned int duty_cycle;
	spinlock_t lock;
	struct completion completion;
	unsigned int max_fifo_level;
	unsigned int clk_nr;
	unsigned long clk_rate;
};

static void irb_set_mod(struct irblaster_dev *irb)
{
	unsigned int cnt = irb->clk_rate / irb->carrier;
	unsigned int pulse_cnt = cnt * irb->duty_cycle / 100;
	unsigned int space_cnt = cnt - pulse_cnt;

	dev_dbg(irb->dev, "F_mod = %uHz, T_mod = %luns, duty_cycle = %u%%\n",
		irb->carrier, NSEC_PER_SEC / irb->clk_rate * cnt,
		100 * pulse_cnt / cnt);

	writel(IRB_MOD_COUNT(pulse_cnt, space_cnt),
	       irb->reg_base + IRB_ADDR1);
}

static void irb_setup(struct irblaster_dev *irb)
{
	unsigned int fifo_irq_threshold = IRB_FIFO_LEN - irb->max_fifo_level;

	/*
	 * Disable the blaster, set modulator clock tick and set initialize
	 * output to be high. Set up carrier frequency and duty cycle. Then
	 * unset initialize output. Enable FIFO interrupt, set FIFO interrupt
	 * threshold. Finally, enable the blaster back.
	 */
	writel(~IRB_ENABLE & (IRB_MOD_CLK(irb->clk_nr) | IRB_INIT_HIGH),
	       irb->reg_base + IRB_ADDR0);
	irb_set_mod(irb);
	writel(readl(irb->reg_base + IRB_ADDR0) & ~IRB_INIT_HIGH,
	       irb->reg_base + IRB_ADDR0);
	writel(IRB_FIFO_IRQ_ENABLE | fifo_irq_threshold,
	       irb->reg_base + IRB_ADDR3);
	writel(readl(irb->reg_base + IRB_ADDR0) | IRB_ENABLE,
	       irb->reg_base + IRB_ADDR0);
}

static u32 irb_prepare_pulse(struct irblaster_dev *irb, unsigned int time)
{
	unsigned int delay;
	unsigned int tb = IRB_TB_MOD_CLK;
	unsigned int tb_us = USEC_PER_SEC / irb->carrier;

	delay = (DIV_ROUND_CLOSEST(time, tb_us) - 1) & IRB_DELAY_MASK;

	return ((IRB_WRITE_FIFO | IRB_MOD_ENABLE) | tb | delay);
}

static u32 irb_prepare_space(struct irblaster_dev *irb, unsigned int time)
{
	unsigned int delay;
	unsigned int tb = IRB_TB_100US;
	unsigned int tb_us = 100;

	if (time <= IRB_MAX_DELAY) {
		tb = IRB_TB_1US;
		tb_us = 1;
	} else if (time <= 10 * IRB_MAX_DELAY) {
		tb = IRB_TB_10US;
		tb_us = 10;
	} else if (time <= 100 * IRB_MAX_DELAY) {
		tb = IRB_TB_100US;
		tb_us = 100;
	}

	delay = (DIV_ROUND_CLOSEST(time, tb_us) - 1) & IRB_DELAY_MASK;

	return ((IRB_WRITE_FIFO & ~IRB_MOD_ENABLE) | tb | delay);
}

static void irb_send_buffer(struct irblaster_dev *irb)
{
	unsigned long flags;
	unsigned int nr = 0;

	spin_lock_irqsave(&irb->lock, flags);
	while (irb->buf_head < irb->buf_len && nr < irb->max_fifo_level) {
		writel(irb->buf[irb->buf_head], irb->reg_base + IRB_ADDR2);

		irb->buf_head++;
		nr++;
	}
	spin_unlock_irqrestore(&irb->lock, flags);
}

static bool irb_check_buf(struct irblaster_dev *irb,
			  unsigned int *buf, unsigned int len)
{
	unsigned int i;

	for (i = 0; i < len; i++) {
		unsigned int max_tb_us;
		/*
		 * Max space timebase is 100 us.
		 * Pulse timebase equals to carrier period.
		 */
		if (i % 2 == 0)
			max_tb_us = USEC_PER_SEC / irb->carrier;
		else
			max_tb_us = 100;

		if (buf[i] >= max_tb_us * IRB_MAX_DELAY)
			return false;
	}

	return true;
}

static void irb_fill_buf(struct irblaster_dev *irb, unsigned int *buf)
{
	unsigned int i;

	for (i = 0; i < irb->buf_len; i++) {
		if (i % 2 == 0)
			irb->buf[i] = irb_prepare_pulse(irb, buf[i]);
		else
			irb->buf[i] = irb_prepare_space(irb, buf[i]);
	}
}

static void irb_send(struct irblaster_dev *irb)
{
	reinit_completion(&irb->completion);

	dev_dbg(irb->dev, "tx started, buffer length = %u\n", len);
	irb_send_buffer(irb);
	wait_for_completion_interruptible(&irb->completion);
	dev_dbg(irb->dev, "tx completed\n");
}

static irqreturn_t irb_irqhandler(int irq, void *data)
{
	struct irblaster_dev *irb = data;

	writel(readl(irb->reg_base + IRB_ADDR3) & ~IRB_FIFO_THD_PENDING,
	       irb->reg_base + IRB_ADDR3);

	if (irb->buf_head < irb->buf_len)
		irb_send_buffer(irb);

	complete(&irb->completion);

	return IRQ_HANDLED;
}

static int irb_set_tx_carrier(struct rc_dev *rc, u32 carrier)
{
	struct irblaster_dev *irb = rc->priv;

	if (carrier == 0)
		return -EINVAL;

	irb->carrier = carrier;
	irb_set_mod(irb);

	return 0;
}

static int irb_set_tx_duty_cycle(struct rc_dev *rc, u32 duty_cycle)
{
	struct irblaster_dev *irb = rc->priv;

	irb->duty_cycle = duty_cycle;
	irb_set_mod(irb);

	return 0;
}

static int irb_tx_ir(struct rc_dev *rc, unsigned int *buf, unsigned int len)
{
	struct irblaster_dev *irb = rc->priv;

	if (!irb_check_buf(irb, buf, len))
		return -EINVAL;

	irb->buf = kmalloc_array(len, sizeof(u32), GFP_KERNEL);
	if (!irb->buf)
		return -ENOMEM;

	irb->buf_len = len;
	irb->buf_head = 0;
	irb_fill_buf(irb, buf);

	irb_send(irb);

	kfree(irb->buf);

	return len;
}

static int irb_mod_clock_probe(struct irblaster_dev *irb)
{
	struct device_node *np = irb->dev->of_node;
	struct clk *clock;
	const char *clock_name;

	if (!np)
		return -ENODEV;

	if (!of_property_read_string(np, "mod-clock", &clock_name)) {
		if (!strcmp(clock_name, "sysclk"))
			irb->clk_nr = IRB_MOD_SYS_CLK;
		else if (!strcmp(clock_name, "xtal"))
			irb->clk_nr = IRB_MOD_XTAL3_CLK;
		else
			return -EINVAL;

		clock = devm_clk_get(irb->dev, clock_name);
		if (IS_ERR(clock) || clk_prepare_enable(clock))
			return -ENODEV;
	} else {
		irb->clk_nr = IRB_MOD_1US_CLK;
	}

	switch (irb->clk_nr) {
	case IRB_MOD_SYS_CLK:
		irb->clk_rate = clk_get_rate(clock);
		break;
	case IRB_MOD_XTAL3_CLK:
		irb->clk_rate = clk_get_rate(clock) / 3;
		break;
	case IRB_MOD_1US_CLK:
		irb->clk_rate = 1000000;
		break;
	}

	dev_info(irb->dev, "F_clk = %luHz\n", irb->clk_rate);

	return 0;
}

static int __init irblaster_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct irblaster_dev *irb;
	struct rc_dev *rc;
	struct resource *range;
	int ret;

	irb = devm_kzalloc(dev, sizeof(*irb), GFP_KERNEL);
	if (!irb)
		return -ENOMEM;

	range = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!range) {
		dev_err(dev, "no memory resource found\n");
		return -ENODEV;
	}

	irb->reg_base = devm_ioremap_resource(dev, range);
	if (IS_ERR(irb->reg_base)) {
		dev_err(dev, "ioremap failed\n");
		return PTR_ERR(irb->reg_base);
	}

	irb->irq = platform_get_irq(pdev, 0);
	if (irb->irq < 0) {
		dev_err(dev, "no irq resource found\n");
		return -ENODEV;
	}

	if (max_fifo_level <= IRB_FIFO_LEN)
		irb->max_fifo_level = max_fifo_level;
	else {
		irb->max_fifo_level = IRB_FIFO_LEN;
		dev_warn(dev, "max FIFO level param truncated to %u",
			 IRB_FIFO_LEN);
	}

	irb->dev = dev;
	irb->carrier = IRB_DEFAULT_CARRIER;
	irb->duty_cycle = IRB_DEFAULT_DUTY_CYCLE;
	init_completion(&irb->completion);
	spin_lock_init(&irb->lock);

	ret = irb_mod_clock_probe(irb);
	if (ret) {
		dev_err(dev, "modulator clock setup failed\n");
		return ret;
	}
	irb_setup(irb);

	ret = devm_request_irq(dev, irb->irq,
			       irb_irqhandler,
			       IRQF_TRIGGER_RISING,
			       DRIVER_NAME, irb);
	if (ret) {
		dev_err(dev, "irq request failed\n");
		return ret;
	}

	rc = rc_allocate_device(RC_DRIVER_IR_RAW_TX);
	if (!rc)
		return -ENOMEM;

	rc->driver_name = DRIVER_NAME;
	rc->device_name = DEVICE_NAME;
	rc->priv = irb;

	rc->tx_ir = irb_tx_ir;
	rc->s_tx_carrier = irb_set_tx_carrier;
	rc->s_tx_duty_cycle = irb_set_tx_duty_cycle;

	ret = rc_register_device(rc);
	if (ret < 0) {
		dev_err(dev, "rc_dev registration failed\n");
		rc_free_device(rc);
		return ret;
	}

	platform_set_drvdata(pdev, rc);

	return 0;
}

static int irblaster_remove(struct platform_device *pdev)
{
	struct rc_dev *rc = platform_get_drvdata(pdev);

	rc_unregister_device(rc);

	return 0;
}

static const struct of_device_id irblaster_dt_match[] = {
	{
		.compatible = "amlogic,meson-g12a-irblaster",
	},
	{},
};
MODULE_DEVICE_TABLE(of, irblaster_dt_match);

static struct platform_driver irblaster_pd = {
	.remove = irblaster_remove,
	.driver = {
		.name = DRIVER_NAME,
		.owner  = THIS_MODULE,
		.of_match_table = irblaster_dt_match,
	},
};

module_platform_driver_probe(irblaster_pd, irblaster_probe);

MODULE_DESCRIPTION("Meson IR blaster driver");
MODULE_AUTHOR("Viktor Prutyanov <viktor.prutyanov@phystech.edu>");
MODULE_LICENSE("GPL");
