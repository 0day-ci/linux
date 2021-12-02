// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, Linaro Limited
 * Copyright (c) 2010-2020, The Linux Foundation. All rights reserved.
 */

#include <linux/atomic.h>
#include <linux/cpu_pm.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/irqchip/arm-gic-v3.h>
#include <linux/irqdomain.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/soc/qcom/irq.h>
#include <linux/spinlock.h>

/*
 * vMPM register layout:
 *
 *    31                              0
 *    +--------------------------------+
 *    |            TIMER0              | 0x00
 *    +--------------------------------+
 *    |            TIMER1              | 0x04
 *    +--------------------------------+
 *    |            ENABLE0             | 0x08
 *    +--------------------------------+
 *    |              ...               | ...
 *    +--------------------------------+
 *    |            ENABLEn             |
 *    +--------------------------------+
 *    |          FALLING_EDGE0         |
 *    +--------------------------------+
 *    |              ...               |
 *    +--------------------------------+
 *    |            STATUSn             |
 *    +--------------------------------+
 *
 * n = DIV_ROUND_UP(pin_num, 32)
 *
 */
#define MPM_REG_ENABLE		0
#define MPM_REG_FALLING_EDGE	1
#define MPM_REG_RISING_EDGE	2
#define MPM_REG_POLARITY	3
#define MPM_REG_STATUS		4

#define MPM_NO_PARENT_IRQ	~0UL

/* MPM pin and its GIC hwirq */
struct mpm_pin {
	int pin;
	irq_hw_number_t hwirq;
};

struct mpm_data {
	unsigned int pin_num;
	const struct mpm_pin *gic_pins;
};

struct qcom_mpm_priv {
	void __iomem *base;
	raw_spinlock_t lock;
	struct mbox_client mbox_client;
	struct mbox_chan *mbox_chan;
	const struct mpm_data *data;
	unsigned int reg_stride;
	struct irq_domain *domain;
	struct notifier_block pm_nb;
	atomic_t cpus_in_pm;
};

static inline u32
qcom_mpm_read(struct qcom_mpm_priv *priv, unsigned int reg, unsigned int index)
{
	unsigned int offset = (reg * priv->reg_stride + index + 2) * 4;

	return readl_relaxed(priv->base + offset);
}

static inline void
qcom_mpm_write(struct qcom_mpm_priv *priv, unsigned int reg,
	       unsigned int index, u32 val)
{
	unsigned int offset = (reg * priv->reg_stride + index + 2) * 4;

	writel_relaxed(val, priv->base + offset);

	/* Ensure the write is completed */
	wmb();
}

static inline void qcom_mpm_enable_irq(struct irq_data *d, bool en)
{
	struct qcom_mpm_priv *priv = d->chip_data;
	int pin = d->hwirq;
	unsigned int index = pin / 32;
	unsigned int shift = pin % 32;
	u32 val;

	raw_spin_lock(&priv->lock);

	val = qcom_mpm_read(priv, MPM_REG_ENABLE, index);
	if (en)
		val |= BIT(shift);
	else
		val &= ~BIT(shift);
	qcom_mpm_write(priv, MPM_REG_ENABLE, index, val);

	raw_spin_unlock(&priv->lock);
}

static void qcom_mpm_mask(struct irq_data *d)
{
	qcom_mpm_enable_irq(d, false);

	if (d->parent_data)
		irq_chip_mask_parent(d);
}

static void qcom_mpm_unmask(struct irq_data *d)
{
	qcom_mpm_enable_irq(d, true);

	if (d->parent_data)
		irq_chip_unmask_parent(d);
}

static inline void
mpm_set_type(struct qcom_mpm_priv *priv, bool set, unsigned int reg,
	     unsigned int index, unsigned int shift)
{
	u32 val;

	raw_spin_lock(&priv->lock);

	val = qcom_mpm_read(priv, reg, index);
	if (set)
		val |= BIT(shift);
	else
		val &= ~BIT(shift);
	qcom_mpm_write(priv, reg, index, val);

	raw_spin_unlock(&priv->lock);
}

static int qcom_mpm_set_type(struct irq_data *d, unsigned int type)
{
	struct qcom_mpm_priv *priv = d->chip_data;
	int pin = d->hwirq;
	unsigned int index = pin / 32;
	unsigned int shift = pin % 32;

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_RISING:
		mpm_set_type(priv, !!(type & IRQ_TYPE_EDGE_RISING),
			     MPM_REG_RISING_EDGE, index, shift);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		mpm_set_type(priv, !!(type & IRQ_TYPE_EDGE_FALLING),
			     MPM_REG_FALLING_EDGE, index, shift);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		mpm_set_type(priv, !!(type & IRQ_TYPE_LEVEL_HIGH),
			     MPM_REG_POLARITY, index, shift);
		break;
	}

	if (!d->parent_data)
		return 0;

	if (type & IRQ_TYPE_EDGE_BOTH)
		type = IRQ_TYPE_EDGE_RISING;

	if (type & IRQ_TYPE_LEVEL_MASK)
		type = IRQ_TYPE_LEVEL_HIGH;

	return irq_chip_set_type_parent(d, type);
}

static struct irq_chip qcom_mpm_chip = {
	.name			= "mpm",
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_mask		= qcom_mpm_mask,
	.irq_unmask		= qcom_mpm_unmask,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.irq_set_type		= qcom_mpm_set_type,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
	.flags			= IRQCHIP_MASK_ON_SUSPEND |
				  IRQCHIP_SKIP_SET_WAKE,
};

static irq_hw_number_t get_parent_hwirq(struct qcom_mpm_priv *priv, int pin)
{
	const struct mpm_pin *gic_pins = priv->data->gic_pins;
	int i;

	for (i = 0; gic_pins[i].pin >= 0; i++) {
		int p = gic_pins[i].pin;

		if (p < 0)
			break;

		if (p == pin)
			return gic_pins[i].hwirq;
	}

	return MPM_NO_PARENT_IRQ;
}

static int qcom_mpm_alloc(struct irq_domain *domain, unsigned int virq,
			  unsigned int nr_irqs, void *data)
{
	struct qcom_mpm_priv *priv = domain->host_data;
	struct irq_fwspec *fwspec = data;
	struct irq_fwspec parent_fwspec;
	irq_hw_number_t parent_hwirq;
	irq_hw_number_t hwirq;
	unsigned int type;
	int  ret;

	ret = irq_domain_translate_twocell(domain, fwspec, &hwirq, &type);
	if (ret)
		return ret;

	ret = irq_domain_set_hwirq_and_chip(domain, virq, hwirq,
					    &qcom_mpm_chip, priv);
	if (ret)
		return ret;

	parent_hwirq = get_parent_hwirq(priv, hwirq);
	if (parent_hwirq == MPM_NO_PARENT_IRQ)
		return irq_domain_disconnect_hierarchy(domain->parent, virq);

	if (type & IRQ_TYPE_EDGE_BOTH)
		type = IRQ_TYPE_EDGE_RISING;

	if (type & IRQ_TYPE_LEVEL_MASK)
		type = IRQ_TYPE_LEVEL_HIGH;

	parent_fwspec.fwnode = domain->parent->fwnode;
	parent_fwspec.param_count = 3;
	parent_fwspec.param[0] = 0;
	parent_fwspec.param[1] = parent_hwirq;
	parent_fwspec.param[2] = type;

	return irq_domain_alloc_irqs_parent(domain, virq, nr_irqs,
					    &parent_fwspec);
}

static const struct irq_domain_ops qcom_mpm_ops = {
	.alloc		= qcom_mpm_alloc,
	.free		= irq_domain_free_irqs_common,
	.translate	= irq_domain_translate_twocell,
};

/* Triggered by RPM when system resumes from deep sleep */
static irqreturn_t qcom_mpm_handler(int irq, void *dev_id)
{
	struct qcom_mpm_priv *priv = dev_id;
	unsigned long enable, pending;
	int i, j;

	for (i = 0; i < priv->reg_stride; i++) {
		enable = qcom_mpm_read(priv, MPM_REG_ENABLE, i);
		pending = qcom_mpm_read(priv, MPM_REG_STATUS, i);
		pending &= enable;

		for_each_set_bit(j, &pending, 32) {
			unsigned int pin = 32 * i + j;
			struct irq_desc *desc =
					irq_resolve_mapping(priv->domain, pin);
			struct irq_data *d = &desc->irq_data;

			if (!irqd_is_level_type(d))
				irq_set_irqchip_state(d->irq,
						IRQCHIP_STATE_PENDING, true);

		}
	}

	return IRQ_HANDLED;
}

static int qcom_mpm_enter_sleep(struct qcom_mpm_priv *priv)
{
	int i, ret;

	for (i = 0; i < priv->reg_stride; i++)
		qcom_mpm_write(priv, MPM_REG_STATUS, i, 0);

	/* Notify RPM to write vMPM into HW */
	ret = mbox_send_message(priv->mbox_chan, NULL);
	if (ret < 0)
		return ret;

	return 0;
}

static int qcom_mpm_cpu_pm_callback(struct notifier_block *nb,
				    unsigned long action, void *data)
{
	struct qcom_mpm_priv *priv = container_of(nb, struct qcom_mpm_priv,
						  pm_nb);
	int ret = NOTIFY_OK;
	int cpus_in_pm;

	switch (action) {
	case CPU_PM_ENTER:
		cpus_in_pm = atomic_inc_return(&priv->cpus_in_pm);
		/*
		 * NOTE: comments for num_online_cpus() point out that it's
		 * only a snapshot so we need to be careful. It should be OK
		 * for us to use, though.  It's important for us not to miss
		 * if we're the last CPU going down so it would only be a
		 * problem if a CPU went offline right after we did the check
		 * AND that CPU was not idle AND that CPU was the last non-idle
		 * CPU. That can't happen. CPUs would have to come out of idle
		 * before the CPU could go offline.
		 */
		if (cpus_in_pm < num_online_cpus())
			return NOTIFY_OK;
		break;
	case CPU_PM_ENTER_FAILED:
	case CPU_PM_EXIT:
		atomic_dec(&priv->cpus_in_pm);
		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}

	/*
	 * It's likely we're on the last CPU. Grab the lock and write MPM for
	 * sleep. Grabbing the lock means that if we race with another CPU
	 * coming up we are still guaranteed to be safe.
	 */
	if (raw_spin_trylock(&priv->lock)) {
		if (qcom_mpm_enter_sleep(priv))
			ret = NOTIFY_BAD;
		raw_spin_unlock(&priv->lock);
	} else {
		/* Another CPU must be up */
		return NOTIFY_OK;
	}

	if (ret == NOTIFY_BAD) {
		/* Double-check if we're here because someone else is up */
		if (cpus_in_pm < num_online_cpus())
			ret = NOTIFY_OK;
		else
			/* We won't be called w/ CPU_PM_ENTER_FAILED */
			atomic_dec(&priv->cpus_in_pm);
	}

	return ret;
}

static int qcom_mpm_init(struct platform_device *pdev,
			 struct device_node *parent,
			 const struct mpm_data *data)
{
	struct irq_domain *parent_domain;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct qcom_mpm_priv *priv;
	unsigned int pin_num;
	int irq;
	int ret;

	if (!data)
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->data = data;
	pin_num = priv->data->pin_num;
	priv->reg_stride = DIV_ROUND_UP(pin_num, 32);

	raw_spin_lock_init(&priv->lock);

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (!priv->base)
		return PTR_ERR(priv->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	priv->mbox_client.dev = dev;
	priv->mbox_chan = mbox_request_channel(&priv->mbox_client, 0);
	if (IS_ERR(priv->mbox_chan)) {
		ret = PTR_ERR(priv->mbox_chan);
		dev_err(dev, "failed to acquire IPC channel: %d\n", ret);
		return ret;
	}

	parent_domain = irq_find_host(parent);
	if (!parent_domain) {
		dev_err(dev, "failed to find MPM parent domain\n");
		ret = -ENXIO;
		goto free_mbox;
	}

	priv->domain = irq_domain_create_hierarchy(parent_domain,
				IRQ_DOMAIN_FLAG_QCOM_MPM_WAKEUP, pin_num,
				of_node_to_fwnode(np), &qcom_mpm_ops, priv);
	if (!priv->domain) {
		dev_err(dev, "failed to create MPM domain\n");
		ret = -ENOMEM;
		goto free_mbox;
	}

	irq_domain_update_bus_token(priv->domain, DOMAIN_BUS_WAKEUP);

	ret = devm_request_irq(dev, irq, qcom_mpm_handler,
			       IRQF_TRIGGER_RISING | IRQF_NO_SUSPEND,
			       "qcom_mpm", priv);
	if (ret) {
		dev_err(dev, "failed to request irq: %d\n", ret);
		goto remove_domain;
	}

	priv->pm_nb.notifier_call = qcom_mpm_cpu_pm_callback;
	cpu_pm_register_notifier(&priv->pm_nb);

	dev_set_drvdata(dev, priv);

	return 0;

remove_domain:
	irq_domain_remove(priv->domain);
free_mbox:
	mbox_free_channel(priv->mbox_chan);
	return ret;
}

/*
 * The mapping between MPM_GIC pin and GIC SPI number on QCM2290.  It's taken
 * from downstream qcom-mpm-scuba.c with a little transform on the GIC
 * SPI numbers (the second column).  Due to the binding difference from
 * the downstream, where GIC SPI numbering starts from 32, we expect the
 * numbering starts from 0 here, and that's why we have the number minus 32
 * comparing to the downstream.
 */
const struct mpm_pin qcm2290_gic_pins[] = {
	{ 2, 275 },	/* tsens0_tsens_upper_lower_int */
	{ 5, 296 },	/* lpass_irq_out_sdc */
	{ 12, 422 },	/* b3_lfps_rxterm_irq */
	{ 24, 79 },	/* bi_px_lpi_1_aoss_mx */
	{ 86, 183 },	/* mpm_wake,spmi_m */
	{ 90, 260 },	/* eud_p0_dpse_int_mx */
	{ 91, 260 },	/* eud_p0_dmse_int_mx */
	{ -1 },
};

const struct mpm_data qcm2290_data = {
	.pin_num = 96,
	.gic_pins = qcm2290_gic_pins,
};

static int qcm2290_mpm_init(struct platform_device *pdev,
			    struct device_node *parent)
{
	return qcom_mpm_init(pdev, parent, &qcm2290_data);
}

IRQCHIP_PLATFORM_DRIVER_BEGIN(qcom_mpm)
IRQCHIP_MATCH("qcom,qcm2290-mpm", qcm2290_mpm_init)
IRQCHIP_PLATFORM_DRIVER_END(qcom_mpm)
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. MSM Power Manager");
MODULE_LICENSE("GPL v2");
