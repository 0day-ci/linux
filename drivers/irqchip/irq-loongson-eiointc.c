// SPDX-License-Identifier: GPL-2.0
/*
 * Loongson Extend I/O Interrupt Controller support
 *
 * Copyright (C) 2020-2021 Loongson Technology Corporation Limited
 */

#define pr_fmt(fmt) "eiointc: " fmt

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/syscore_ops.h>

#define EIOINTC_REG_NODEMAP	0x14a0
#define EIOINTC_REG_IPMAP	0x14c0
#define EIOINTC_REG_ENABLE	0x1600
#define EIOINTC_REG_BOUNCE	0x1680
#define EIOINTC_REG_ISR		0x1800
#define EIOINTC_REG_ROUTE	0x1c00

#define VEC_REG_COUNT		4
#define VEC_COUNT_PER_REG	64
#define VEC_COUNT		(VEC_REG_COUNT * VEC_COUNT_PER_REG)
#define VEC_REG_IDX(irq_id)	((irq_id) / VEC_COUNT_PER_REG)
#define VEC_REG_BIT(irq_id)     ((irq_id) % VEC_COUNT_PER_REG)
#define EIOINTC_DEF_ENABLE	0xffffffff

static int nr_pics;

struct eiointc_priv {
	u32			node;
	nodemask_t		node_map;
	cpumask_t		cpuspan_map;
	struct fwnode_handle	*domain_handle;
	struct irq_domain	*eiointc_domain;
};

struct eiointc_priv *eiointc_priv[2];

int eiointc_get_node(int id)
{
	return eiointc_priv[id]->node;
}

static void eiointc_set_irq_route(int pos, unsigned int cpu, unsigned int mnode, nodemask_t *node_map)
{
	int node, cpu_node, route_node;
	unsigned char coremap[MAX_NUMNODES];
	uint32_t pos_off, data, data_byte, data_mask;

	pos_off = pos & ~3;
	data_byte = pos & 3;
	data_mask = ~BIT_MASK(data_byte) & 0xf;

	memset(coremap, 0, sizeof(unsigned char) * MAX_NUMNODES);

	/* Calculate node and coremap of target irq */
	cpu_node = cpu_to_node(cpu);
	coremap[cpu_node] |= (1 << (topology_core_id(cpu)));

	for_each_online_node(node) {
		if (!node_isset(node, *node_map))
			continue;

		/* Node 0 is in charge of inter-node interrupt dispatch */
		route_node = (node == mnode) ? cpu_node : node;
		data = ((coremap[node] | (route_node << 4)) << (data_byte * 8));
		csr_any_send(EIOINTC_REG_ROUTE + pos_off, data, data_mask, node);
	}
}

static DEFINE_SPINLOCK(affinity_lock);

static int eiointc_set_irq_affinity(struct irq_data *d, const struct cpumask *affinity, bool force)
{
	unsigned int cpu;
	unsigned long flags;
	uint32_t vector, pos_off;
	struct cpumask intersect_affinity;
	struct eiointc_priv *priv = (struct eiointc_priv *)d->domain->host_data;

	if (!IS_ENABLED(CONFIG_SMP))
		return -EPERM;

	spin_lock_irqsave(&affinity_lock, flags);

	cpumask_and(&intersect_affinity, affinity, cpu_online_mask);
	cpumask_and(&intersect_affinity, &intersect_affinity, &priv->cpuspan_map);

	if (cpumask_empty(&intersect_affinity)) {
		spin_unlock_irqrestore(&affinity_lock, flags);
		return -EINVAL;
	}
	cpu = cpumask_first(&intersect_affinity);

	/*
	 * Control interrupt enable or disalbe through cpu 0
	 * which is reponsible for dispatching interrupts.
	 */
	if (!d->parent_data)
		vector = d->hwirq;
	else
		vector = d->parent_data->hwirq;

	pos_off = vector >> 5;

	csr_any_send(EIOINTC_REG_ENABLE + (pos_off << 2),
		     EIOINTC_DEF_ENABLE & (~((1 << (vector & 0x1F)))), 0x0, 0);
	eiointc_set_irq_route(vector, cpu, priv->node, &priv->node_map);
	csr_any_send(EIOINTC_REG_ENABLE + (pos_off << 2), EIOINTC_DEF_ENABLE, 0x0, 0);

	irq_data_update_effective_affinity(d, cpumask_of(cpu));

	spin_unlock_irqrestore(&affinity_lock, flags);

	return IRQ_SET_MASK_OK;
}

static int eiointc_index(int node)
{
	int i;

	for (i = 0; i < nr_pics; i++) {
		if (node_isset(node, eiointc_priv[i]->node_map))
			return i;
	}

	return -1;
}

static int eiointc_router_init(unsigned int cpu)
{
	int i, bit;
	uint32_t data;
	uint32_t node = cpu_to_node(cpu);
	uint32_t index = eiointc_index(node);

	if (index < 0) {
		pr_err("Error: invalid nodemap!\n");
		return -1;
	}

	if (cpu == cpumask_first(cpumask_of_node(node))) {
		eiointc_enable();

		for (i = 0; i < VEC_COUNT / 32; i++) {
			data = (((1 << (i * 2 + 1)) << 16) | (1 << (i * 2)));
			iocsr_writel(data, EIOINTC_REG_NODEMAP + i * 4);
		}

		for (i = 0; i < VEC_COUNT / 32 / 4; i++) {
			bit = BIT(1 + index); /* Route to IP[1 + index] */
			data = bit | (bit << 8) | (bit << 16) | (bit << 24);
			iocsr_writel(data, EIOINTC_REG_IPMAP + i * 4);
		}

		for (i = 0; i < VEC_COUNT / 4; i++) {
			/* Route to Node-0 Core-0 */
			if (index == 0)
				bit = BIT(cpu_logical_map(0));
			else
				bit = (eiointc_priv[index]->node << 4) | 1;

			data = bit | (bit << 8) | (bit << 16) | (bit << 24);
			iocsr_writel(data, EIOINTC_REG_ROUTE + i * 4);
		}

		for (i = 0; i < VEC_COUNT / 32; i++) {
			data = 0xffffffff;
			iocsr_writel(data, EIOINTC_REG_ENABLE + i * 4);
			iocsr_writel(data, EIOINTC_REG_BOUNCE + i * 4);
		}
	}

	return 0;
}

static void eiointc_irq_dispatch(struct irq_desc *desc)
{
	int i;
	u64 pending;
	bool handled = false;
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct eiointc_priv *priv = irq_desc_get_handler_data(desc);

	chained_irq_enter(chip, desc);

	for (i = 0; i < VEC_REG_COUNT; i++) {
		pending = iocsr_readq(EIOINTC_REG_ISR + (i << 3));
		iocsr_writeq(pending, EIOINTC_REG_ISR + (i << 3));
		while (pending) {
			int bit = __ffs(pending);
			int irq = bit + VEC_COUNT_PER_REG * i;

			generic_handle_domain_irq(priv->eiointc_domain, irq);
			pending &= ~BIT(bit);
			handled = true;
		}
	}

	if (!handled)
		spurious_interrupt();

	chained_irq_exit(chip, desc);
}

static void eiointc_ack_irq(struct irq_data *d)
{
}

static void eiointc_mask_irq(struct irq_data *d)
{
}

static void eiointc_unmask_irq(struct irq_data *d)
{
}

static struct irq_chip eiointc_irq_chip = {
	.name			= "EIOINTC",
	.irq_ack		= eiointc_ack_irq,
	.irq_mask		= eiointc_mask_irq,
	.irq_unmask		= eiointc_unmask_irq,
	.irq_set_affinity	= eiointc_set_irq_affinity,
};

static int eiointc_domain_alloc(struct irq_domain *domain, unsigned int virq,
				unsigned int nr_irqs, void *arg)
{
	int ret;
	unsigned int i, type;
	unsigned long hwirq = 0;
	struct eiointc *priv = domain->host_data;

	ret = irq_domain_translate_onecell(domain, arg, &hwirq, &type);
	if (ret)
		return ret;

	for (i = 0; i < nr_irqs; i++) {
		irq_domain_set_info(domain, virq + i, hwirq + i, &eiointc_irq_chip,
					priv, handle_edge_irq, NULL, NULL);
	}

	return 0;
}

static void eiointc_domain_free(struct irq_domain *domain, unsigned int virq,
				unsigned int nr_irqs)
{
	int i;

	for (i = 0; i < nr_irqs; i++) {
		struct irq_data *d = irq_domain_get_irq_data(domain, virq + i);

		irq_set_handler(virq + i, NULL);
		irq_domain_reset_irq_data(d);
	}
}

static const struct irq_domain_ops eiointc_domain_ops = {
	.translate	= irq_domain_translate_onecell,
	.alloc		= eiointc_domain_alloc,
	.free		= eiointc_domain_free,
};

static int eiointc_suspend(void)
{
	return 0;
}

static bool is_eiointc_irq(struct irq_data *irq_data)
{
	int i;
	struct irq_domain *parent;

	for (parent = irq_data->domain; parent; parent = parent->parent) {
		for (i = 0; i < nr_pics; i++) {
			if (parent == eiointc_priv[i]->eiointc_domain)
				return true;
		}
	}

	return false;
}

static void eiointc_resume(void)
{
	int i;
	struct irq_desc *desc;
	struct irq_data *irq_data;

	eiointc_router_init(0);

	for (i = 0; i < NR_IRQS; i++) {
		desc = irq_to_desc(i);
		if (desc && desc->handle_irq && desc->handle_irq != handle_bad_irq) {
			irq_data = &desc->irq_data;
			if (!is_eiointc_irq(irq_data))
				continue;

			eiointc_set_irq_affinity(irq_data, irq_data->common->affinity, 0);
		}
	}
}

static struct syscore_ops eiointc_syscore_ops = {
	.suspend = eiointc_suspend,
	.resume = eiointc_resume,
};

struct irq_domain *eiointc_acpi_init(struct irq_domain *parent,
				     struct acpi_madt_eio_pic *acpi_eiointc)
{
	int i, parent_irq;
	unsigned long node_map;
	struct eiointc_priv *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return NULL;

	priv->domain_handle = irq_domain_alloc_fwnode((phys_addr_t *)acpi_eiointc);
	if (!priv->domain_handle) {
		pr_err("Unable to allocate domain handle\n");
		goto out_free_priv;
	}

	priv->node = acpi_eiointc->node;
	node_map = acpi_eiointc->node_map ? : -1ULL;

	for_each_node(i) {
		if (node_map & (1ULL << i)) {
			node_set(i, priv->node_map);
			cpumask_or(&priv->cpuspan_map, &priv->cpuspan_map, cpumask_of_node(i));
		}
	}

	/* Setup IRQ domain */
	priv->eiointc_domain = irq_domain_create_linear(priv->domain_handle, VEC_COUNT,
					&eiointc_domain_ops, priv);
	if (!priv->eiointc_domain) {
		pr_err("loongson-eiointc: cannot add IRQ domain\n");
		goto out_free_priv;
	}

	eiointc_priv[nr_pics++] = priv;

	eiointc_router_init(0);

	parent_irq = irq_create_mapping(parent, acpi_eiointc->cascade);
	irq_set_chained_handler_and_data(parent_irq, eiointc_irq_dispatch, priv);

	register_syscore_ops(&eiointc_syscore_ops);
	cpuhp_setup_state_nocalls(CPUHP_AP_IRQ_LOONGARCH_STARTING,
				  "irqchip/loongarch/intc:starting",
				  eiointc_router_init, NULL);

	return irq_find_matching_fwnode(priv->domain_handle, DOMAIN_BUS_ANY);

out_free_priv:
	priv->domain_handle = NULL;
	kfree(priv);

	return NULL;
}
