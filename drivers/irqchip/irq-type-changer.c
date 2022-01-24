// SPDX-License-Identifier: GPL-2.0

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/of_irq.h>

struct changer {
	unsigned long count;
	struct {
		struct irq_fwspec fwspec;
		unsigned int type;
	} out[0];
};

static int changer_set_type(struct irq_data *data, unsigned int type)
{
	struct changer *ch = data->domain->host_data;
	struct irq_data *parent_data = data->parent_data;

	return parent_data->chip->irq_set_type(parent_data,
					       ch->out[data->hwirq].type);
}

static struct irq_chip changer_chip = {
	.name			= "type-changer",
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_type		= changer_set_type,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
	.irq_set_wake		= irq_chip_set_wake_parent,
};

static int changer_domain_translate(struct irq_domain *domain,
				    struct irq_fwspec *fwspec,
				    unsigned long *hwirq,
				    unsigned int *type)
{
	struct changer *ch = domain->host_data;

	if (fwspec->param_count != 2)
		return -EINVAL;
	if (fwspec->param[0] >= ch->count)
		return -ENXIO;

	*hwirq = fwspec->param[0];
	*type = fwspec->param[1] & IRQ_TYPE_SENSE_MASK;
	return 0;
}

static int changer_domain_alloc(struct irq_domain *domain, unsigned int virq,
				unsigned int nr_irqs, void *arg)
{
	struct changer *ch = domain->host_data;
	struct irq_fwspec *fwspec = arg;
	irq_hw_number_t hwirq;
	unsigned int type;
	int ret;

	if (WARN_ON(nr_irqs != 1))
		return -EINVAL;

	ret = changer_domain_translate(domain, fwspec, &hwirq, &type);
	if (ret)
		return ret;

	irq_domain_set_hwirq_and_chip(domain, virq, hwirq, &changer_chip, ch);

	return irq_domain_alloc_irqs_parent(domain, virq, 1,
					    &ch->out[hwirq].fwspec);
}

static const struct irq_domain_ops changer_domain_ops = {
	.translate	= changer_domain_translate,
	.alloc		= changer_domain_alloc,
	.free		= irq_domain_free_irqs_common,
};

static int __init changer_of_init(struct device_node *node,
				  struct device_node *parent)
{
	struct irq_domain *domain, *parent_domain;
	int count, i, ret;
	struct changer *ch;
	struct of_phandle_args pargs;
	irq_hw_number_t unused;

	if (!parent) {
		pr_err("%pOF: no parent node\n", node);
		return -EINVAL;
	}

	parent_domain = irq_find_host(parent);
	if (!parent_domain) {
		pr_err("%pOF: no parent domain\n", node);
		return -EINVAL;
	}

	if (WARN_ON(!parent_domain->ops->translate))
		return -EINVAL;

	count = of_irq_count(node);
	if (count < 1) {
		pr_err("%pOF: no interrupts defined\n", node);
		return -EINVAL;
	}

	ch = kzalloc(sizeof(*ch) + count * sizeof(ch->out[0]), GFP_KERNEL);
	if (!ch)
		return -ENOMEM;
	ch->count = count;

	for (i = 0; i < count; i++) {
		ret = of_irq_parse_one(node, i, &pargs);
		if (ret) {
			pr_err("%pOF: interrupt %d: error %d parsing\n",
			       node, i, ret);
			goto out_free;
		}
		of_phandle_args_to_fwspec(pargs.np, pargs.args,
					  pargs.args_count,
					  &ch->out[i].fwspec);
		ret = parent_domain->ops->translate(parent_domain,
						    &ch->out[i].fwspec,
						    &unused,
						    &ch->out[i].type);
		if (ret) {
			pr_err("%pOF: interrupt %d: error %d extracting type\n",
			       node, i, ret);
			goto out_free;
		}
		if (ch->out[i].type == IRQ_TYPE_NONE) {
			pr_err("%pOF: interrupt %d: no type\n", node, i);
			ret = -ENXIO;
			goto out_free;
		}
	}

	domain = irq_domain_create_hierarchy(parent_domain, 0, count,
					     of_node_to_fwnode(node),
					     &changer_domain_ops, ch);
	if (!domain) {
		ret = -ENOMEM;
		goto out_free;
	}

	return 0;

out_free:
	kfree(ch);
	return ret;
}

IRQCHIP_PLATFORM_DRIVER_BEGIN(changer)
IRQCHIP_MATCH("linux,irq-type-changer", changer_of_init)
IRQCHIP_PLATFORM_DRIVER_END(changer)
MODULE_AUTHOR("Nikita Yushchenko <nikita.yoush.cogentembedded.com>");
MODULE_DESCRIPTION("Virtual irqchip to support trigger type change in route");
MODULE_LICENSE("GPL v2");
